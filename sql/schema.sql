-- NLOJ Database Schema
-- MySQL 8.0+
-- Charset: utf8mb4

CREATE DATABASE IF NOT EXISTS nloj_db
    DEFAULT CHARACTER SET utf8mb4
    DEFAULT COLLATE utf8mb4_unicode_ci;

USE nloj_db;

-- ---------------------------------------------------------------------------
-- user: 用户表
-- ---------------------------------------------------------------------------
CREATE TABLE IF NOT EXISTS `user` (
    `id`            BIGINT       NOT NULL AUTO_INCREMENT COMMENT '主键',
    `username`      VARCHAR(32)  NOT NULL COMMENT '用户名',
    `password_hash` VARCHAR(256) NOT NULL COMMENT '密码哈希（PBKDF2-HMAC-SHA256）',
    `role`          VARCHAR(16)  NOT NULL DEFAULT 'user' COMMENT '角色：user | admin',
    `create_time`   DATETIME     NOT NULL DEFAULT CURRENT_TIMESTAMP COMMENT '创建时间',
    `update_time`   DATETIME     NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP COMMENT '更新时间',
    `deleted`       TINYINT      NOT NULL DEFAULT 0 COMMENT '逻辑删除：0 未删 1 已删',
    PRIMARY KEY (`id`),
    UNIQUE KEY `uk_username` (`username`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='用户表';

-- ---------------------------------------------------------------------------
-- problem: 题目表
-- ---------------------------------------------------------------------------
CREATE TABLE IF NOT EXISTS `problem` (
    `id`           BIGINT       NOT NULL AUTO_INCREMENT COMMENT '主键',
    `title`        VARCHAR(256) NOT NULL COMMENT '题目标题',
    `difficulty`   VARCHAR(16)  NOT NULL COMMENT '难度：EASY | MEDIUM | HARD',
    `description`  MEDIUMTEXT   NOT NULL COMMENT '题面（Markdown）',
    `time_limit`   INT          NOT NULL DEFAULT 1000 COMMENT '时间限制（ms）',
    `memory_limit` INT          NOT NULL DEFAULT 262144 COMMENT '内存限制（KB，默认 256MB）',
    `visible`      TINYINT      NOT NULL DEFAULT 1 COMMENT '是否可见：1 可见 0 隐藏',
    `problem_type` VARCHAR(16)  NOT NULL DEFAULT 'STANDARD' COMMENT '题型：STANDARD | INTERACTIVE | COMMUNICATION',
    `judge_mode`   VARCHAR(16)  NOT NULL DEFAULT 'EXACT' COMMENT '比对：EXACT | SPJ',
    `extra_code`   MEDIUMTEXT   NULL COMMENT 'SPJ checker / 交互器 / 通信管理器（C++，不对用户展示）',
    `create_time`  DATETIME     NOT NULL DEFAULT CURRENT_TIMESTAMP COMMENT '创建时间',
    `update_time`  DATETIME     NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP COMMENT '更新时间',
    `deleted`      TINYINT      NOT NULL DEFAULT 0 COMMENT '逻辑删除',
    PRIMARY KEY (`id`),
    KEY `idx_difficulty` (`difficulty`),
    KEY `idx_visible_create_time` (`visible`, `create_time`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='题目表';

-- ---------------------------------------------------------------------------
-- problem_case: 题目测试用例
-- ---------------------------------------------------------------------------
CREATE TABLE IF NOT EXISTS `problem_case` (
    `id`          BIGINT       NOT NULL AUTO_INCREMENT COMMENT '主键',
    `problem_id`  BIGINT       NOT NULL COMMENT '题目 ID',
    `input`       MEDIUMTEXT   NOT NULL COMMENT '输入',
    `output`      MEDIUMTEXT   NOT NULL COMMENT '期望输出',
    `is_sample`   TINYINT      NOT NULL DEFAULT 0 COMMENT '是否样例：1 是 0 否（样例可对用户展示）',
    `sort_order`  INT          NOT NULL DEFAULT 0 COMMENT '排序',
    `create_time` DATETIME     NOT NULL DEFAULT CURRENT_TIMESTAMP COMMENT '创建时间',
    `deleted`     TINYINT      NOT NULL DEFAULT 0 COMMENT '逻辑删除',
    PRIMARY KEY (`id`),
    KEY `idx_problem_id` (`problem_id`),
    KEY `idx_problem_sample` (`problem_id`, `is_sample`),
    CONSTRAINT `fk_case_problem` FOREIGN KEY (`problem_id`) REFERENCES `problem` (`id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='题目测试用例';

-- ---------------------------------------------------------------------------
-- submission: 提交记录
-- 状态机：PENDING -> JUDGING -> AC | WA | TLE | MLE | RE | CE | SYSTEM_ERROR
-- ---------------------------------------------------------------------------
CREATE TABLE IF NOT EXISTS `submission` (
    `id`          BIGINT       NOT NULL AUTO_INCREMENT COMMENT '主键',
    `user_id`     BIGINT       NOT NULL COMMENT '提交用户 ID',
    `problem_id`  BIGINT       NOT NULL COMMENT '题目 ID',
    `language`    VARCHAR(16)  NOT NULL COMMENT '语言：CPP | C | PYTHON | JAVA',
    `code`        MEDIUMTEXT   NOT NULL COMMENT '提交代码',
    `status`      VARCHAR(16)  NOT NULL DEFAULT 'PENDING' COMMENT '判题状态',
    `time_used`   INT          NULL COMMENT '耗时（ms）',
    `memory_used` INT          NULL COMMENT '内存（KB）',
    `judge_info`  VARCHAR(512) NULL COMMENT '判题详情（如 WA 在第几个用例失败）',
    `create_time` DATETIME     NOT NULL DEFAULT CURRENT_TIMESTAMP COMMENT '提交时间',
    `update_time` DATETIME     NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP COMMENT '更新时间',
    PRIMARY KEY (`id`),
    KEY `idx_user_create_time` (`user_id`, `create_time`),
    KEY `idx_problem_status` (`problem_id`, `status`),
    KEY `idx_status_create_time` (`status`, `create_time`),
    CONSTRAINT `fk_submission_user` FOREIGN KEY (`user_id`) REFERENCES `user` (`id`),
    CONSTRAINT `fk_submission_problem` FOREIGN KEY (`problem_id`) REFERENCES `problem` (`id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='提交记录';

-- ---------------------------------------------------------------------------
-- 示例数据（可选，开发调试用）
-- ---------------------------------------------------------------------------
INSERT INTO `user` (`username`, `password_hash`, `role`)
VALUES ('admin', 'placeholder_pbkdf2_hash_replace_me', 'admin');

INSERT INTO `problem` (`title`, `difficulty`, `description`, `time_limit`, `memory_limit`, `visible`)
VALUES (
    'A + B',
    'EASY',
    '给定两个整数 A 和 B，输出它们的和。\n\n## 输入\n一行两个整数 A、B\n\n## 输出\n一行一个整数',
    1000,
    262144,
    1
);

INSERT INTO `problem_case` (`problem_id`, `input`, `output`, `is_sample`, `sort_order`)
VALUES
    (1, '1 2', '3', 1, 1),
    (1, '100 200', '300', 1, 2),
    (1, '-1 1', '0', 0, 3);
