/*
 * MeshCom fork: compile-time check that the configuration the vendored TUs see is the one this directory was
 * verified against (README-mbedtls.md). Included by common.h right after the mbedTLS config file.
 */
#ifndef MC_MBEDTLS_CHECK_H
#define MC_MBEDTLS_CHECK_H

#if defined(CONFIG_MBEDTLS_ASYMMETRIC_CONTENT_LEN)
#error "SDK enables asymmetric content length: IN/OUT sizes no longer come from MAX; re-verify README-mbedtls.md"
#endif
#if !defined(MBEDTLS_SSL_MAX_CONTENT_LEN) || MBEDTLS_SSL_MAX_CONTENT_LEN != 16384
#error "MBEDTLS_SSL_MAX_CONTENT_LEN is not 16384: the SDK config changed, re-verify README-mbedtls.md"
#endif
#if !defined(MBEDTLS_SSL_IN_CONTENT_LEN) || MBEDTLS_SSL_IN_CONTENT_LEN != 16384
#error "MBEDTLS_SSL_IN_CONTENT_LEN must be 16384 (mc_mbedtls_out4k.h not included first?)"
#endif
#if !defined(MBEDTLS_SSL_OUT_CONTENT_LEN) || MBEDTLS_SSL_OUT_CONTENT_LEN != 4096
#error "MBEDTLS_SSL_OUT_CONTENT_LEN must be 4096 (mc_mbedtls_out4k.h not included first?)"
#endif
#if defined(MBEDTLS_SSL_VARIABLE_BUFFER_LENGTH) || defined(MBEDTLS_SSL_DTLS_CONNECTION_ID)
#error "SDK config changed (variable buffer length or DTLS CID): buffer length formulas differ, re-verify README-mbedtls.md"
#endif

#endif /* MC_MBEDTLS_CHECK_H */
