// AppLink (docs/applink-api-contract.md section 2) -- portable SHA-1 and base64 for the RFC 6455
// handshake. Arduino-free, no allocation; builds on the host, nRF52 and ESP32 alike.
#pragma once
#include <stddef.h>
#include <stdint.h>

void sha1(const uint8_t *data, size_t len, uint8_t out[20]);

// RFC 4648 base64 with padding; NUL-terminates. Returns the characters written (without NUL),
// 0 if out is too small (out[0] is then NUL when out_size > 0). Empty input gives "" and 0.
size_t base64Encode(const uint8_t *in, size_t len, char *out, size_t out_size);

// RFC 6455 Sec-WebSocket-Accept: base64(sha1(client_key + GUID)); 28 chars + NUL.
// false if client_key is NULL, empty or longer than 60 characters.
bool wsAcceptKey(const char *client_key, char out[29]);
