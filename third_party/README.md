# third_party

本地放第三方预编译依赖。当前包含 MySQL Connector/C 6.1.11（win64），供 CMake 查找 `mysql.h` / `libmysql`。

zip 包不入库；若目录缺失，可从 MySQL 官网下载 Connector/C 解压到本目录，或设置环境变量 `MYSQL_DIR`。
