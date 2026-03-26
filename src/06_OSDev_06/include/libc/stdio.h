#pragma once

#include <libc/stdbool.h>
#include <libc/stdint.h>
#include <libc/stddef.h>

int putchar(int ic);
bool print(const char* data, size_t length);
int printf(const char* __restrict__ format, ...);