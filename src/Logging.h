// Copyright (c) multi-boot-ereader contributors. MIT licensed.
#pragma once

#include <Arduino.h>

// Serial logging helpers. Logging is compiled out entirely when
// MULTIBOOT_ENABLE_SERIAL_LOG is not defined, which keeps the selector small.
#ifdef MULTIBOOT_ENABLE_SERIAL_LOG
#define LOG_INFO(format, ...) Serial.printf("[info ] " format "\n", ##__VA_ARGS__)
#define LOG_WARN(format, ...) Serial.printf("[warn ] " format "\n", ##__VA_ARGS__)
#define LOG_ERROR(format, ...) Serial.printf("[error] " format "\n", ##__VA_ARGS__)
#else
#define LOG_INFO(format, ...) ((void)0)
#define LOG_WARN(format, ...) ((void)0)
#define LOG_ERROR(format, ...) ((void)0)
#endif
