// esbmclibc on this host does not declare strnlen(); remote_cmd.cpp uses it. Harness-local: forward to the real
// <string.h> and add the declaration (the body is in hmac_model.cpp).
#pragma once
#include_next <string.h>
#ifdef __cplusplus
extern "C" {
#endif
size_t strnlen(const char *s, size_t n);
#ifdef __cplusplus
}
#endif
