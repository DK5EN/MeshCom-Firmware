/*
 * MeshCom fork: sizes the mbedTLS record buffers for the vendored TUs in this directory, see
 * ../README-mbedtls.md. MUST be the first include of every file here (ssl.h only defaults IN/OUT to
 * MBEDTLS_SSL_MAX_CONTENT_LEN when they are not defined yet; esp_config.h leaves them alone because
 * CONFIG_MBEDTLS_ASYMMETRIC_CONTENT_LEN is off in the arduino-esp32 2.0.17 SDK).
 *
 * Output record content 4096 (was 16384): MBEDTLS_SSL_OUT_BUFFER_LEN 16717 -> 4429 bytes per TLS session.
 * Input record content stays 16384: GitHub's CDN sends 16 KB records and does not honour max_fragment_length.
 */
#ifndef MC_MBEDTLS_OUT4K_H
#define MC_MBEDTLS_OUT4K_H

#if defined(MBEDTLS_SSL_IN_CONTENT_LEN) || defined(MBEDTLS_SSL_OUT_CONTENT_LEN)
#error "mc_mbedtls_out4k.h must be included before any mbedtls header"
#endif

#define MBEDTLS_SSL_IN_CONTENT_LEN 16384
#define MBEDTLS_SSL_OUT_CONTENT_LEN 4096

#endif /* MC_MBEDTLS_OUT4K_H */
