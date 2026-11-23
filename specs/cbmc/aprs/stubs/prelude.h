// first include of the harness TU: esbmc's libc has no strnlen (src/mc_text.h and aprs_functions.cpp use it).
#pragma once
#include <stddef.h>
#include <string.h>
static inline size_t strnlen(const char *s, size_t n)
{
    size_t i = 0;
    while(i < n && s[i]) i++;
    return i;
}
