# Review findings: mbedTLS 2.28.7 / ESP-IDF 4.4.7 port, TLS client path

**Date:** 2026-11-23
**Source:** `/code-review high` run on branch `review` (diff `main...HEAD`, no `origin` remote)
**Status:** Reviewer output, **not yet independently verified**. Findings 1-3 should be confirmed against the source
before acting on them.

## Scope

- TLS client handshake path, X.509 parsing, ESP-IDF port layer (bignum, AES, GCM, SHA).
- Checked against `config/sdkconfig.esp32` and `config/sdkconfig.esp32s3`.
- The upstream TLS parsers read (ServerHello and extensions, ServerKeyExchange, CertificateRequest, NewSessionTicket,
  record decryption) showed nothing beyond the already known CVEs (CVE-2025-27810, CVE-2026-50581).
- Reminder from `README.md`: the library ships prebuilt, so fixes must be mitigations in our code, config changes, or
  a replacement object.

## Summary

| #   | Severity  | Area                       | Finding                                                   | Reachable from network        |
| --- | --------- | -------------------------- | --------------------------------------------------------- | ----------------------------- |
| 1   | important | config / `x509.c`          | Expired and not-yet-valid certificates are accepted       | Yes                           |
| 2   | important | `x509_crt.c`, `ssl_tls.c`  | Unbounded allocation while parsing the certificate chain  | Yes                           |
| 3   | important | `port/aes/dma/esp_aes.c`   | DMA bounce buffer leak on partial allocation failure (S3) | Indirectly, via heap pressure |
| 4   | nit       | `port/esp_bignum.c`        | Possible use-after-free in overlong hardware multiply     | No call site found            |
| 5   | nit       | `port/esp_bignum.c`        | Wrong sign when output aliases a ±1 operand               | No                            |
| 6   | nit       | `port/esp_bignum.c`        | RSA/DHE above 4096 bits fails (no software fallback)      | Yes, as a kill switch         |
| 7   | nit       | `port/aes/esp_aes_gcm.c`   | Re-keying a GCM context keeps the old hash subkey H       | No                            |
| 8   | nit       | `port/aes/block/esp_aes.c` | AES-CTR does not bounds-check the stream offset           | No                            |

## Findings

### 1. Expired certificates are accepted (important)

- **Where:** `config/sdkconfig.esp32:1440`, `config/sdkconfig.esp32s3:1653`; effect in `code/library/x509.c:1056`.
- **Defect:** `CONFIG_MBEDTLS_HAVE_TIME_DATE` is off on both targets. That compiles the stub
  `mbedtls_x509_time_is_past()` / `is_future()`, which always return 0, so `x509_crt_verify_chain` never sets
  `BADCERT_EXPIRED` or `BADCERT_FUTURE`.
- **Scenario:** A MITM presents a leaf whose private key leaked after it expired (or an expired intermediate under one
  of the pinned roots). Verification passes, and the attacker serves a malicious firmware update over "verified"
  HTTPS.
- **Fix:** Enable `CONFIG_MBEDTLS_HAVE_TIME_DATE` and gate TLS on SNTP having synced. If the clock cannot be trusted,
  at least enforce a build-time floor date. Since the library is prebuilt, a mitigation in our code is a custom
  `f_vrfy` verify callback that checks `valid_from` / `valid_to` against a synced clock or floor date.

### 2. Heap exhaustion while parsing the certificate chain, before verification (important)

- **Where:** `code/library/x509_crt.c:692` (SAN), `asn1parse.c:346` (EKU), `x509.c:485` / `x509.c:502` (names),
  `ssl_tls.c:2410` (`ssl_parse_certificate_chain`).
- **Defect:** `x509_get_subject_alt_name` allocates a 16-byte node per 2-byte GeneralName; EKU and names do the same.
  The chain parser accepts up to about 128 certificates of at least 128 bytes each, each with a full
  `mbedtls_x509_crt`, pk context and raw copy. Nothing caps any of this, and it all happens before any signature
  check.
- **Scenario:** A MITM answers the ClientHello with a ~16 KB Certificate message holding one certificate whose SAN has
  ~5000 empty `[2]` dNSName entries. That is ~5000 x (16 B + heap header), roughly 100-120 KB of internal RAM
  (`MBEDTLS_INTERNAL_MEM_ALLOC` forces internal memory). Free heap is exhausted during parsing, and other firmware
  tasks (WiFi, LoRa, web) fail allocations or abort.
- **Fix:** Cap SAN/EKU/RDN entries per certificate (e.g. 64) and certificates per chain (e.g.
  `MBEDTLS_X509_MAX_INTERMEDIATE_CA + 2`), rejecting the message beyond that. With the prebuilt library, a possible
  mitigation is a capped allocator via the memory hooks for the duration of the handshake.

### 3. ESP32-S3 AES DMA bounce buffer leak (important)

- **Where:** `code/idf/mbedtls/port/aes/dma/esp_aes.c:266` (`esp_aes_process_dma_ext_ram`).
- **Defect:** When both input and output need a bounce copy, `input_buf` is allocated first. If the `output_buf`
  malloc then fails, the function returns -1 without freeing `input_buf`.
- **Scenario:** S3 with PSRAM, cipher input outside DMA-capable memory and an unaligned PSRAM output buffer, under
  attacker-induced heap pressure (e.g. via finding 2). Each failed AES call leaks up to 1600 B of internal DMA RAM;
  repeated reconnects slowly drain internal heap until the device fails permanently.
- **Fix:** Set `ret = -1` and `goto cleanup` (which frees `input_buf`) instead of returning directly.

### 4. Possible use-after-free in hardware big-number multiply (nit)

- **Where:** `code/idf/mbedtls/port/esp_bignum.c:504`, read at line 514 (`mpi_mult_mpi_overlong`).
- **Defect:** `Ypp` is built with `.p` pointing into `Y->p`, then `mbedtls_mpi_mul_mpi(Z, X, &Ypp)` first grows `Z`. If
  `Z` is the same number as `Y` and must grow, the realloc frees the buffer `Ypp` still reads from.
- **Scenario:** `mbedtls_mpi_mul_mpi(A, B, A)` with `A` > 2048 bits and a product > 4096 bits. The hardware then loads
  `Ypp` from freed heap: wrong result or heap read-after-free. No aliasing call site was found in the TLS client path.
- **Fix:** Copy `Ypp` / `Yp` into a temporary mpi when `Z` aliases `X` or `Y`, or compute into a local and copy to `Z`
  at the end.

### 5. Hardware multiply can return the wrong sign (nit)

- **Where:** `code/idf/mbedtls/port/esp_bignum.c:409` and `:414` (`mbedtls_mpi_mul_mpi` shortcut for 1-bit operands).
- **Defect:** It does `mbedtls_mpi_copy(Z, Y)` then `Z->s *= X->s`. If `Z` is `X`, the copy has already overwritten
  `X->s`, so the sign of a ±1 operand is lost (same at line 414 when `Z` is `Y`). The `x_bits == 0` shortcut also
  ignores the return value of `mbedtls_mpi_lset`.
- **Scenario:** `X = -1`, `mbedtls_mpi_mul_mpi(X, X, Y)` with `Y = 7` returns +7 instead of -7. Upstream software
  bignum returns -7.
- **Fix:** Save `X->s` and `Y->s` into locals before the copy; check the `mbedtls_mpi_lset` return value.

### 6. TLS fails against keys above 4096 bits (nit)

- **Where:** `code/idf/mbedtls/port/esp_bignum.c:293` (`esp_mpi_exp_mod`).
- **Defect:** Returns `MBEDTLS_ERR_MPI_NOT_ACCEPTABLE` above `SOC_RSA_MAX_BIT_LEN`, and
  `CONFIG_MBEDTLS_LARGE_KEY_SOFTWARE_MPI` is off, so `MBEDTLS_MPI_EXP_MOD_ALT` has no software fallback.
- **Scenario:** The server or CDN presents an RSA-8192 certificate, or DHE with a 6144-bit prime. Signature check or key
  exchange fails, and every update download fails. Acts as a server-side kill switch rather than a memory bug.
- **Fix:** Enable `CONFIG_MBEDTLS_LARGE_KEY_SOFTWARE_MPI`, or document the limit and pin only roots/servers with keys
  of 4096 bits or less.

### 7. Re-keying a GCM context keeps the old hash subkey (nit)

- **Where:** `code/idf/mbedtls/port/aes/esp_aes_gcm.c:276` (`esp_aes_gcm_setkey`), see also line 392.
- **Defect:** `setkey` updates the key but does not reset `gcm_state`; `esp_aes_gcm_starts` only derives H while
  `gcm_state == ESP_AES_GCM_STATE_INIT`. Tags are then computed with H from the previous key.
- **Scenario:** `setkey(K1)`, starts/finish, then `setkey(K2)` on the same context: GHASH still uses E_K1(0). Tags do
  not verify against standard implementations, or forgeries are possible for anyone who knows K1. TLS creates a fresh
  cipher context per transform, so this is not reachable from the handshake.
- **Fix:** Set `ctx->gcm_state = ESP_AES_GCM_STATE_INIT` in `setkey`.

### 8. ESP32 hardware AES-CTR does not check the stream offset (nit)

- **Where:** `code/idf/mbedtls/port/aes/block/esp_aes.c:398` (`esp_aes_crypt_ctr`).
- **Defect:** Uses `n = *nc_off` unchecked and indexes `stream_block[n]`. Upstream `mbedtls_aes_crypt_ctr` rejects
  `n > 15` with `MBEDTLS_ERR_AES_BAD_INPUT_DATA`; `esp_aes_crypt_ofb` already checks.
- **Scenario:** A caller passing a stale or corrupted `nc_off` of e.g. 40 reads `stream_block[40..]` out of bounds
  while interrupts are off: keystream bytes leak or the device faults. The TLS record path always passes 0.
- **Fix:** Add `if (n > 15) return MBEDTLS_ERR_AES_BAD_INPUT_DATA;` before acquiring the hardware.

## Open next steps

1. Independently verify findings 1-3 against the source (and the `#if`s for our config).
2. Decide on mitigations given the prebuilt library: config changes (findings 1, 6), verify callback or floor date
   (finding 1), capped allocator during the handshake (finding 2), replacement object for port-layer fixes (3-5, 7, 8).
3. Write patches against this exact version for whatever is chosen.
