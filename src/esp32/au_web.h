#pragma once
#include <stddef.h>

// GET /austatus (web_functions.cpp sub_au_status): the JSON body of the Info page row, written to buf (cap bytes).
// Returns the length, 0 when auStatusJson could not write (the caller answers 500 {"ok":false}). ESP32 only.
size_t auWebStatusJson(char *buf, size_t cap);
