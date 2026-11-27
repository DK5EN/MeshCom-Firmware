// Request body reader glue (FORMAL-02 wave 7b step 7, docs/web-server-seams-20261127.md section 6.5).
// The pure accumulator is web_body.h; this is the I/O loop around it, shared by web_read_body()
// (web_functions.cpp) and rmReadBody() (web_rm_handlers.cpp).
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "web_body.h"

// Reads up to content_length bytes of the request body from web_client into buf (cap bytes, NUL-terminated).
// Returns the bytes read (> 0) or WB_NOLEN / WB_SIZE / WB_SHORT (web_body.h). idle_ms: no byte for that long;
// total_ms: absolute deadline of the whole body; both inclusive. Feeds the task watchdog on every pass.
int webReadBody(char *buf, size_t cap, long content_length, uint32_t idle_ms, uint32_t total_ms);
