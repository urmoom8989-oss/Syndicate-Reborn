#pragma once
#include <stdio.h>
#include <string>

#define LOG_ERROR 1
#define LOG_WARN 2
#define LOG_INFO 3
#define LOG_TRACE 4

#define LOG(LogLevel, Message, ...) printf(Message "\n", ##__VA_ARGS__)
