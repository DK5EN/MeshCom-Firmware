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

// The one reason-phrase table of the web server (FORMAL-02 wave 7b step 8a). Pure, no Arduino: native tests call it.
inline const char *webStatusText(uint16_t code)
{
    switch (code)
    {
    case 200: return "OK";
    case 400: return "Bad Request";
    case 401: return "Unauthorized";
    case 403: return "Forbidden";
    case 404: return "Not Found";
    case 405: return "Method Not Allowed";
    case 408: return "Request Timeout";
    case 422: return "Unprocessable Entity";
    case 431: return "Request Header Fields Too Large";
    case 500: return "Internal Server Error";
    case 503: return "Service Unavailable";
    default: return "Unknown";
    }
}

// Writes the head of a fixed-length response to web_client, every line CRLF:
//   HTTP/1.1 <status> <reason>, [Content-type:<type>], [extra verbatim: complete lines, each ending in CRLF],
//   Content-Length: <len>, [Cache-Control: <cache>], Connection: close, blank line.
// Nothing after the blank line: the caller writes the body. type / extra / cache may be NULL.
void webSendFixedHead(uint16_t status, const char *type, size_t len, const char *extra, const char *cache);
