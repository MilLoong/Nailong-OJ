-- 已有 nloj_db 补题型 / SPJ 字段（新库直接用 schema.sql 即可）
USE nloj_db;

ALTER TABLE `problem`
    ADD COLUMN `problem_type` VARCHAR(16) NOT NULL DEFAULT 'STANDARD'
        COMMENT '题型：STANDARD | INTERACTIVE | COMMUNICATION' AFTER `visible`,
    ADD COLUMN `judge_mode` VARCHAR(16) NOT NULL DEFAULT 'EXACT'
        COMMENT '比对：EXACT | SPJ' AFTER `problem_type`,
    ADD COLUMN `extra_code` MEDIUMTEXT NULL
        COMMENT 'SPJ checker / 交互器 / 通信管理器（C++，不对用户展示）' AFTER `judge_mode`;
