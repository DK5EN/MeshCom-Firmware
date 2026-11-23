#pragma once
#include <stdint.h>
#include <stddef.h>
struct ModelState { unsigned sha_calls, hmac_calls; size_t msg_len; uint8_t msg[160]; uint8_t mac[32]; };
extern ModelState g_m;
