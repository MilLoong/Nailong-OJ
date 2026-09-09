# NLOJ API 文档

> 基础路径：`http://localhost:8080`  
> 版本前缀：`/api/v1`  
> 机器可读契约：[openapi.yaml](./openapi.yaml)  
> 可视化调试：服务起来后打开 [http://127.0.0.1:8080/api/docs](http://127.0.0.1:8080/api/docs)（Swagger UI）

---

## 1. 通用约定

### 1.1 统一响应格式

```json
{
  "code": 0,
  "message": "ok",
  "data": { }
}
```

| 字段 | 类型 | 说明 |
|------|------|------|
| code | int | 0 表示成功，非 0 为业务错误 |
| message | string | 提示信息 |
| data | object / array / null | 业务数据 |

### 1.2 鉴权

需要登录的接口在 Header 携带：

```
Authorization: Bearer <token>
```

### 1.3 分页参数（Query）

| 参数 | 类型 | 默认 | 说明 |
|------|------|------|------|
| pageNum | long | 1 | 页码，从 1 开始 |
| pageSize | long | 20 | 每页条数，最大 100 |

分页响应 `data` 结构：

```json
{
  "pageNum": 1,
  "pageSize": 20,
  "total": 100,
  "records": []
}
```

### 1.4 错误码

| code | 含义 |
|------|------|
| 0 | 成功 |
| 40000 | 请求参数错误 |
| 40100 | 未登录 |
| 40101 | 无权限 |
| 40300 | 禁止访问 |
| 40400 | 资源不存在 |
| 50000 | 系统内部异常 |
| 50001 | 操作失败 |

---

## 2. 用户模块

### 2.1 注册

```
POST /api/v1/auth/register
```

**请求体**

```json
{
  "username": "alice",
  "password": "123456"
}
```

| 字段 | 类型 | 必填 | 约束 |
|------|------|------|------|
| username | string | 是 | 3-32 字符，字母数字下划线，唯一 |
| password | string | 是 | 6-64 字符 |

**成功响应**

```json
{
  "code": 0,
  "message": "ok",
  "data": 10001
}
```

`data` 为新用户 ID。

**错误示例**

```json
{
  "code": 40000,
  "message": "用户名已存在",
  "data": null
}
```

---

### 2.2 登录

```
POST /api/v1/auth/login
```

**请求体**

```json
{
  "username": "alice",
  "password": "123456"
}
```

**成功响应**

```json
{
  "code": 0,
  "message": "ok",
  "data": {
    "token": "eyJhbGciOiJIUzI1NiIs...",
    "userId": 10001,
    "username": "alice",
    "role": "user"
  }
}
```

**错误示例**（账号不存在或密码错误，避免枚举用户）

```json
{
  "code": 40100,
  "message": "用户名或密码错误",
  "data": null
}
```

---

### 2.3 获取当前用户

```
GET /api/v1/users/me
```

**需要登录**

**成功响应**

```json
{
  "code": 0,
  "message": "ok",
  "data": {
    "id": 10001,
    "username": "alice",
    "role": "user",
    "createTime": "2026-08-06T12:00:00"
  }
}
```

---

## 3. 题目模块

### 3.1 题目列表（分页）

```
GET /api/v1/problems?pageNum=1&pageSize=20&difficulty=EASY&keyword=A
```

**Query 参数**

| 参数 | 类型 | 必填 | 说明 |
|------|------|------|------|
| pageNum | long | 否 | 页码 |
| pageSize | long | 否 | 每页条数 |
| difficulty | string | 否 | EASY / MEDIUM / HARD |
| keyword | string | 否 | 标题关键词 |
| lastId | long | 否 | keyset 游标：上一页最后一条 id；传了按 `id < lastId` 取最新一页（忽略 pageNum 深翻页语义，适合加载更多/无限滚动） |

**成功响应**

```json
{
  "code": 0,
  "message": "ok",
  "data": {
    "pageNum": 1,
    "pageSize": 20,
    "total": 1,
    "records": [
      {
        "id": 1,
        "title": "A + B",
        "difficulty": "EASY",
        "timeLimit": 1000,
        "memoryLimit": 262144,
        "visible": 1,
        "createTime": "2026-08-06T12:00:00"
      }
    ]
  }
}
```

> 列表接口不返回完整题面，减少 payload。

---

### 3.2 题目详情

```
GET /api/v1/problems/{id}
```

**路径参数**

| 参数 | 说明 |
|------|------|
| id | 题目 ID |

**成功响应**

```json
{
  "code": 0,
  "message": "ok",
  "data": {
    "id": 1,
    "title": "A + B",
    "difficulty": "EASY",
    "description": "给定两个整数 A 和 B，输出它们的和。",
    "timeLimit": 1000,
    "memoryLimit": 262144,
    "visible": 1,
    "createTime": "2026-08-06T12:00:00",
    "samples": [
      { "id": 1, "input": "1 2", "output": "3" },
      { "id": 2, "input": "100 200", "output": "300" }
    ]
  }
}
```

`samples` 仅包含 `is_sample=1` 的用例。

---

### 3.3 创建题目（管理员）

```
POST /api/v1/problems
```

**需要 admin 角色**

**请求体**

```json
{
  "title": "A + B",
  "difficulty": "EASY",
  "description": "题面 Markdown...",
  "timeLimit": 1000,
  "memoryLimit": 262144,
  "visible": 1,
  "problemType": "STANDARD",
  "judgeMode": "EXACT",
  "extraCode": ""
}
```

| 字段 | 说明 |
|------|------|
| problemType | 可选，默认 `STANDARD`。`INTERACTIVE` / `COMMUNICATION` 必须带 `extraCode`（交互器 / 管理器，C++） |
| judgeMode | 可选，默认 `EXACT`。`SPJ` 必须带 `extraCode`（checker：`./checker in.txt user_out.txt ans.txt`，退出 0=AC） |
| extraCode | 不对用户展示；详情接口不返回 |

交互 / 通信的用户程序只接受 `CPP` / `C`。通信题一份 `code` 用 `===NLOJ_FILE:alice===` / `===NLOJ_FILE:bob===` 拆两段。

**成功响应**

```json
{
  "code": 0,
  "message": "ok",
  "data": 1
}
```

`data` 为新题目 ID。

---

### 3.4 更新题目（管理员）

```
PUT /api/v1/problems/{id}
```

**需要 admin 角色**

**请求体**：同创建接口。

**成功响应**

```json
{
  "code": 0,
  "message": "ok",
  "data": null
}
```

---

## 4. 提交 / 判题模块

### 4.1 提交代码

```
POST /api/v1/submissions
```

**需要登录**

**请求体**

```json
{
  "problemId": 1,
  "language": "CPP",
  "code": "#include <iostream>\nint main() { int a,b; std::cin>>a>>b; std::cout<<a+b; }"
}
```

| 字段 | 类型 | 必填 | 说明 |
|------|------|------|------|
| problemId | long | 是 | 题目 ID |
| language | string | 是 | CPP / C / PYTHON / JAVA |
| code | string | 是 | 源代码 |

Java 必须 `public class Main`。Python 跑 `python3 main.py`。

**成功响应**

```json
{
  "code": 0,
  "message": "ok",
  "data": {
    "submissionId": 50001
  }
}
```

**调用说明**

提交成功后立刻返回 `submissionId`，不用等评测结束。过一会儿再调「查询提交详情」看 `status`。

---

### 4.2 查询提交详情

```
GET /api/v1/submissions/{id}
```

**需要登录**（仅本人或 admin 可查看）

**成功响应 — 判题中**

```json
{
  "code": 0,
  "message": "ok",
  "data": {
    "id": 50001,
    "userId": 10001,
    "problemId": 1,
    "language": "CPP",
    "code": "#include <iostream> ...",
    "status": "JUDGING",
    "timeUsed": null,
    "memoryUsed": null,
    "judgeInfo": null,
    "createTime": "2026-08-06T12:05:00"
  }
}
```

**成功响应 — 已通过**

```json
{
  "code": 0,
  "message": "ok",
  "data": {
    "id": 50001,
    "userId": 10001,
    "problemId": 1,
    "language": "CPP",
    "code": "...",
    "status": "AC",
    "timeUsed": 12,
    "memoryUsed": 15360,
    "judgeInfo": "All 3 test cases passed",
    "createTime": "2026-08-06T12:05:00"
  }
}
```

**常见 status**

| status | 说明 |
|--------|------|
| PENDING | 等待判题 |
| JUDGING | 判题中 |
| AC | 通过 |
| WA | 答案错误 |
| TLE | 超时 |
| MLE | 内存超限 |
| RE | 运行时错误 |
| CE | 编译错误 |
| SYSTEM_ERROR | 沙箱或基础设施异常 |

---

### 4.3 我的提交列表

```
GET /api/v1/submissions?pageNum=1&pageSize=20&problemId=1&status=AC
```

**需要登录**

**Query 参数**

| 参数 | 类型 | 必填 | 说明 |
|------|------|------|------|
| pageNum | long | 否 | 页码 |
| pageSize | long | 否 | 每页条数 |
| problemId | long | 否 | 按题目筛选 |
| status | string | 否 | 按状态筛选 |
| lastId | long | 否 | keyset 游标：上一页最后一条 id；传了按 `id < lastId` 取最新一页（忽略 pageNum 深翻页语义，适合加载更多/无限滚动） |

**成功响应**

```json
{
  "code": 0,
  "message": "ok",
  "data": {
    "pageNum": 1,
    "pageSize": 20,
    "total": 2,
    "records": [
      {
        "id": 50001,
        "userId": 10001,
        "problemId": 1,
        "language": "CPP",
        "status": "AC",
        "timeUsed": 12,
        "memoryUsed": 15360,
        "judgeInfo": "All 3 test cases passed",
        "createTime": "2026-08-06T12:05:00"
      }
    ]
  }
}
```

> 列表接口可不返回完整 `code` 字段，减少数据量。

---

## 5. 健康检查

```
GET /api/v1/health
```

**成功响应**

```json
{
  "code": 0,
  "message": "ok",
  "data": {
    "status": "UP",
    "mysql": "UP",
    "redis": "UP",
    "rabbitmq": "UP",
    "cache": {
      "total": 0,
      "l1Hit": 0,
      "redisHit": 0,
      "mysqlLoad": 0,
      "hitRate": 0
    },
    "embedWorker": 1,
    "judgeNodes": []
  }
}
```
