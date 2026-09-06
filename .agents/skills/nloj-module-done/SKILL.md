---
name: nloj-module-done
description: >-
  NLOJ 领域模块做完后的收尾顺序。用户说模块完了、下一步、勾 Phase、
  写完 list/create 等业务函数时使用。强制：先测 → 改 README →
  把改动写成面试题（docs/interview.md）→ 才提下一模块；
  不要跳过测试直接开写 submit/judge。
---

# NLOJ 模块做完怎么收尾

领域函数（如 `nl-problem` 的 list/get/create/update）写完后，**禁止**直接说「接下来写 submit」。按下面顺序做完再往下。

## 固定顺序

1. **测试**（对照 `nloj-cpp-test`）
   - 有 DB 的领域：`nl-*/tests/test_*_db.cpp`，CMake `add_executable` + `add_test`
   - 纯逻辑：不连 MySQL 的 `test_*`（如 crypto）
   - 编译并跑通对应 `nloj_*_test.exe`；全绿才进入下一步
2. **README**
   - 把 Phase B/C 里对应条目改成 `[x]`，必要时补一句（测了什么）
3. **面试题同步（每次改动都要做，不只模块收尾）**
   - 把本次改动涉及的面试点写进 `docs/interview.md`，格式：项目现状 → 一句话答法 → 会被追问的点 → 诚实短板
   - 代码行为变了就同步改旧答案（计时/内存、Redis 降级、JWT 密钥、CI 测试等），禁止留与代码矛盾的说法
   - 硬规则：**别把「待改进」写成「已实现」**；被追问会翻车的点要主动标出来
4. **（仅当用户明确要求）** 再 `git commit` 或开下一模块

## 下一模块何时说

只有 1、2、3 做完（或用户明确跳过测试/面试题）后，才建议例如「下一步 `nl-submit`」。

## 和现有 skill 的关系

- 写法：`nloj-cpp-style` / `cpp-style-zh`
- 骨架：`nloj-domain-skeleton`（先注释后实现）
- 测试怎么写：`nloj-cpp-test`
- 面试题写到哪：`docs/interview.md`（步骤 3 每次改动后同步）
- **本 skill**：模块完成后的门禁顺序
