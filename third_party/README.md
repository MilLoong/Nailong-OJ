# third_party

本地放第三方预编译依赖。当前包含 MySQL Connector/C 6.1.11（win64），供 CMake 查找 `mysql.h` / `libmysql`。

zip 包不入库；若目录缺失，可从 MySQL 官网下载 Connector/C 解压到本目录，或设置环境变量 `MYSQL_DIR`。

`rabbitmq-c`（v0.14.0）同样不入库：CMake 在缺目录时会 `git clone` 到 `third_party/rabbitmq-c`。

`cpp-httplib` / `nlohmann/json` 单头文件也不入库；CMake 在缺失时用 curl 下载到 `third_party/cpp-httplib` 与 `third_party/nlohmann`。
