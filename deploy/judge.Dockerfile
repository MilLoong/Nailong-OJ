# 可选判题镜像：C/C++/Python/Java。构建：
#   docker build -t nloj-judge:bookworm -f deploy/judge.Dockerfile .
FROM debian:bookworm-slim

RUN apt-get update \
 && apt-get install -y --no-install-recommends \
        g++ \
        gcc \
        python3 \
        openjdk-17-jdk-headless \
 && rm -rf /var/lib/apt/lists/*

WORKDIR /work
