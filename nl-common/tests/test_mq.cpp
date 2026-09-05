// nl-common 判题队列：RabbitMQ 或进程内降级（不连 MySQL）。
#include "nloj/common/mq.h"

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <string>
#include <thread>

namespace {

int g_failed = 0;  // 任一条失败则置 1

void expect_true(const char* name, int ok) {
    if (ok) {
        std::cout << "[PASS] " << name << '\n';
    } else {
        std::cout << "[FAIL] " << name << '\n';
        g_failed = 1;
    }
}

void drain_judge_queue() {
    nloj::common::JudgeTaskMessage dump;
    while (nloj::common::try_pop_judge_task(dump)) {
        nloj::common::ack_judge_task(dump);
    }
}

void test_reject_invalid() {
    nloj::common::JudgeTaskMessage bad;
    bad.submission_id = 0;
    bad.problem_id = 1;
    bad.language = "CPP";
    expect_true("reject zero submission_id", !nloj::common::publish_judge_task(bad));

    bad.submission_id = 1;
    bad.problem_id = 0;
    expect_true("reject zero problem_id", !nloj::common::publish_judge_task(bad));

    bad.problem_id = 1;
    bad.language.clear();
    expect_true("reject empty language", !nloj::common::publish_judge_task(bad));
}

void test_try_pop_roundtrip() {
    drain_judge_queue();
    nloj::common::JudgeTaskMessage msg;
    msg.submission_id = 10001;
    msg.problem_id = 7;
    msg.language = "CPP";
    expect_true("publish ok", nloj::common::publish_judge_task(msg));

    nloj::common::JudgeTaskMessage out;
    expect_true("try_pop ok", nloj::common::try_pop_judge_task(out));
    expect_true("roundtrip submission_id", out.submission_id == 10001);
    expect_true("roundtrip problem_id", out.problem_id == 7);
    expect_true("roundtrip language", out.language == "CPP");
    nloj::common::ack_judge_task(out);

    nloj::common::JudgeTaskMessage empty;
    expect_true("queue empty after pop", !nloj::common::try_pop_judge_task(empty));
}

void test_wait_pop() {
    drain_judge_queue();
    std::thread producer([] {
        std::this_thread::sleep_for(std::chrono::milliseconds(80));
        nloj::common::JudgeTaskMessage msg;
        msg.submission_id = 20002;
        msg.problem_id = 8;
        msg.language = "CPP";
        nloj::common::publish_judge_task(msg);
    });

    nloj::common::JudgeTaskMessage out;
    nloj::common::wait_pop_judge_task(out);
    producer.join();
    expect_true("wait_pop submission_id", out.submission_id == 20002);
    expect_true("wait_pop problem_id", out.problem_id == 8);
    expect_true("wait_pop language", out.language == "CPP");
    nloj::common::ack_judge_task(out);
}

}  // namespace

int main() {
    test_reject_invalid();
    test_try_pop_roundtrip();
    test_wait_pop();
    if (nloj::common::mq_using_rabbit()) {
        std::cout << "mq backend: rabbitmq\n";
    } else {
        std::cout << "mq backend: in-process (RabbitMQ unavailable)\n";
    }
    if (g_failed) {
        std::cerr << "nl-common mq tests failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "nl-common mq tests passed\n";
    return EXIT_SUCCESS;
}
