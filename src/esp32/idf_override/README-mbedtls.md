# mbedtls override: 4 KB TLS output record buffer (OTA-LOWHEAP-01)

## What

Build-time replacement of four objects inside `libmbedtls_2.a` of arduino-esp32 2.0.17 (framework package
`3.20017.*`, IDF v4.4.7, Mbed TLS 2.28.7) so that the TLS **output** record content is 4096 bytes instead of 16384.
Input stays 16384 (GitHub's CDN sends 16 KB records and ignores `max_fragment_length`).

| Buffer                       | Before  | After                                            |
| ---------------------------- | ------- | ------------------------------------------------ |
| `MBEDTLS_SSL_IN_BUFFER_LEN`  | 16717 B | 16717 B (unchanged)                              |
| `MBEDTLS_SSL_OUT_BUFFER_LEN` | 16717 B | 4429 B (4096 + 13 header + 320 payload overhead) |

Saving: 12288 B of internal heap per TLS session (one `calloc` of the out buffer in `mbedtls_ssl_setup`).

Files in `mbedtls/`:

- `ssl_tls.c`, `ssl_msg.c`, `ssl_cli.c`, `ssl_srv.c`: Mbed TLS 2.28.7 `library/` sources, verbatim except patch MB-01.
- `common.h`, `constant_time_internal.h`: the private headers these files include (the SDK ships no private
  headers). `common.h` carries MB-01 (one include of `mc_mbedtls_check.h`).
- `mc_mbedtls_out4k.h`: the shim. Defines `MBEDTLS_SSL_IN_CONTENT_LEN 16384` and `MBEDTLS_SSL_OUT_CONTENT_LEN 4096`
  before any mbedtls header (`esp_config.h` leaves both undefined because `CONFIG_MBEDTLS_ASYMMETRIC_CONTENT_LEN`
  is off; `ssl.h` then does not default them to `MBEDTLS_SSL_MAX_CONTENT_LEN`). `#error` if included late.
- `mc_mbedtls_check.h`: `#error` unless MAX/IN/OUT are 16384/16384/4096, asymmetric content length, variable
  buffer length and DTLS CID are off (the buffer formulas in `ssl_internal.h` assume that).

Every `.c` is guarded `defined(ESP_PLATFORM) && !defined(NATIVE_BUILD) && ESP_IDF_VERSION_MAJOR == 4` (safeboot is
IDF 5, native and nRF52 compile nothing). PlatformIO builds them with the rest of `src/esp32/**`; no
`platformio.ini` change is needed. The config file stays the SDK's `mbedtls/esp_config.h` (`-DMBEDTLS_CONFIG_FILE`
of the framework build); the shim only adds the two defines.

Link marker: `ssl_tls.c` defines `mc_mbedtls_out4k_marker` (= 4096) and `src/esp32/mc_tls/mc_ssl_client.cpp` reads it on
IDF 4. If the guard ever turns false on an IDF 4 build (header change, file dropped), the four files compile to nothing
and the link fails with an undefined reference instead of silently pulling the 16 KB archive members.

## Why these four files, and no hook

The macros `MBEDTLS_SSL_OUT_CONTENT_LEN`, `MBEDTLS_SSL_OUT_BUFFER_LEN`, `MBEDTLS_SSL_IN_*`, `MBEDTLS_SSL_MAX_CONTENT_LEN`,
`MBEDTLS_TLS_EXT_ADV_CONTENT_LEN` are used in exactly these library TUs (grep over all of Mbed TLS 2.28.7 `library/`
and `include/`; the ESP port uses them only in `port/dynamic/*`, which is off, and `esp_mem.c` uses the `CONFIG_`
constants for a placement decision only):

```
library/ssl_tls.c  library/ssl_msg.c  library/ssl_cli.c  library/ssl_srv.c
```

`ssl_srv.c` is not used for a handshake (client only) but `ssl_tls.c` references `mbedtls_ssl_handshake_server_step`,
so the archive member would be pulled and would carry the 16384 constants: a mixed 16K/4K TU set.

**The zeroize trap.** A `calloc` hook that returns a smaller buffer for the 16717 B request does not work:
`mbedtls_ssl_session_reset_int` (`ssl_tls.c` :3915) and `mbedtls_ssl_free` (:6748) zeroize `out_buf` with the compiled
`MBEDTLS_SSL_OUT_BUFFER_LEN` (`mbedtls_ssl_setup` :3829 allocates it with the same constant), which would write
12 KB past a smaller allocation. The size must be the same
compile-time constant in every TU that touches the buffer. That is why all four TUs are overridden together.

## Proof that the sources match the prebuilt objects

Done with the framework package `3.20017.241212+sha.dcc1105b` (installed copy at the time was the safeboot
framework 3.3.7; the 2.0.17 package was fetched from the PlatformIO registry into a scratch directory) and the
xtensa gcc 8.4.0 (esp-2021r2-patch5) of the toolchain package, with exactly the flags in the archive's DWARF producer
string (`-Os -mlongcalls -mfix-esp32-psram-cache-issue -mfix-esp32-psram-cache-strategy=memw -ggdb -std=gnu99
-ffunction-sections -fdata-sections -freorder-blocks -fstack-protector -fstrict-volatile-bitfields -fno-jump-tables
-fno-tree-switch-conversion`) and the include paths and defines of `platformio-build-esp32.py`.

- Sources: `espressif/mbedtls` 2b8e772 (review clone, branch `review`) is byte-identical to upstream Mbed TLS v2.28.7
  `library/ssl_{tls,msg,cli}.c`. The review clone has no `ssl_srv.c`; it is taken from upstream v2.28.7. The SDK
  headers (`ssl.h`, `ssl_internal.h`, `config.h`) are identical to the review clone.
- With the config left at 16384 (no shim), the compiled objects of `ssl_tls`, `ssl_msg`, `ssl_cli`, `ssl_srv`
  (and `ssl_ciphersuites`, `ssl_ticket`, `ssl_cookie`, `ssl_cache`) have the same global symbol set (116, 38, 1, 3
  symbols for the four), the same symbol sizes, an identical `objdump -dr` (code and relocations) and identical
  non-debug section sizes as the archive members. Only the DWARF directory strings differ.
- With the shim (4096), the four objects define the same global symbol set as the archive members. They differ
  from the 16K build only where the macros are used (`ssl_tls`: `mbedtls_ssl_setup`, `_session_reset_int`,
  `_write_certificate`, `_handshake`, `ssl_mfl_code_to_length`, ...; `ssl_msg`: 16 lines, `ssl_cli`: 6, `ssl_srv`: 529).
- `ld -r` of the four objects ahead of `libmbedtls_2.a libmbedcrypto.a libmbedx509.a`: no duplicate definitions; the only
  archive member pulled from `libmbedtls_2.a` is `ssl_ciphersuites.c.obj`. No archive `ssl_*` member that uses the
  macros is pulled.
- Compiled clean with `-Wall -Wextra -Wformat=2 -Wno-missing-field-initializers -Werror` (the `build_src_flags`).
- Size check: a TU with the shim reads `MBEDTLS_SSL_OUT_BUFFER_LEN = 0x114d = 4429`, `IN = 0x414d = 16717`,
  `MBEDTLS_SSL_PAYLOAD_OVERHEAD = 320`.

## Safety analysis

- **Partial writes.** `mbedtls_ssl_write` takes at most 4096 B per
  call and returns the count it took. The only TLS users in the firmware are `McTlsClient` (Auto Update, via
  `HTTPClient`), the INSTRUMENT probe (`WiFiClientSecure`, handshake only, no write) and `ESP32-audioI2S` `Audio.h`
  (HTTPS web radio, GET request of ~1 KB). `HTTPClient::sendHeader` (2.0.17, `HTTPClient.cpp:1231`) treats a short write as an error, so
  `mc_ssl_send` (`src/esp32/mc_tls/mc_ssl_client.cpp`) now loops until all bytes are out (before it returned the
  first partial count; the old 16 KB cap was never reached by a request header). The timeout restarts per chunk.
  The Arduino `WiFiClientSecure` `send_ssl_data` has no such loop: requests there must stay below 4096 B.
- **Handshake messages (client).** Largest ClientHello: 4 + 2 + 32 + 33 + 2 + 2 x 120 ciphersuites (`supported_ciphersuites`
  has 121 entries incl. terminator) + 2 + extensions (SNI, groups, point formats, sig_algs, renegotiation_info,
  EMS, ETM) is about 0.52 KB for `api.github.com`, below 0.75 KB with a 255 B host name. ClientKeyExchange: 70 B (ECDHE
  P-256), 262 B (RSA-2048 kx), at most about 1030 B for an 8192-bit RSA/DH key (`MBEDTLS_MPI_MAX_SIZE` 1024). Finished:
  16 B. Client Certificate message without own certificate: 7 B. All fit 4096 with room; encryption overhead goes into
  the 320 B payload slack beyond the content length.
- **Renegotiation.** Compiled in (`CONFIG_MBEDTLS_SSL_RENEGOTIATION`), runtime default off
  (`mbedtls_ssl_config_defaults`); `mc_ssl_client.cpp` never enables it. The messages would not be larger anyway.
- **`max_fragment_length`.** Not requested by any code here. `MBEDTLS_TLS_EXT_ADV_CONTENT_LEN` is now 4096, so
  `mbedtls_ssl_conf_max_frag_len` accepts at most the 4096 code (2^12). Do not request 8192/16384 there.
- **Input.** Untouched: a peer sending 16 KB records still works.
- **Compiler flag difference.** The archive was built with `-mfix-esp32-psram-cache-issue`. The app build adds it only
  for the PSRAM envs (`ttgo_tbeam*`); the vendored objects follow the env flags like every other file in `src/`, which
  is correct for boards without PSRAM.

## Re-sync

On a framework bump (`package.json` version other than `3.20017.*`) or an IDF change of `components/mbedtls`: rebuild
the four files from the matching Mbed TLS tag, re-apply MB-01 (first include, closing `#endif`, `common.h`), then
repeat the proof above: compile with the archive's DWARF producer flags and the 16384 config, `nm -S --defined-only -g`
and `objdump -dr` against the archive members. If the symbol sets or code differ, the override is no longer a drop-in
and must be re-checked or dropped. Mbed TLS 3.x (IDF 5) has a different layout; the guard keeps it out on purpose.
Re-check `grep -rn 'MBEDTLS_SSL_\(OUT\|IN\)_\(CONTENT\|BUFFER\)_LEN\|MBEDTLS_SSL_MAX_CONTENT_LEN'` over `library/` for a
new user of the macros.
