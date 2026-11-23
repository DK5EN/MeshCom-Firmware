# idf_override: patched copies of prebuilt IDF components

Build-time replacement of objects inside the arduino-esp32 2.0.17 prebuilt libraries (FORMAL-01 wave 3c).
Compiled only for IDF 4.x ESP32 app builds; every file is guarded with
`defined(ESP_PLATFORM) && !defined(NATIVE_BUILD) && ESP_IDF_VERSION_MAJOR == 4`, so safeboot (IDF 5), native
and nRF52 never see it.

## mdns.c

- Source: ESP-IDF v4.4.7 `components/mdns/mdns.c`, Apache-2.0 header kept. It matches the object in
  `libmdns.a` of arduino-esp32 `3.20017.241212+sha.dcc1105b`: all 43 global symbols equal, and the DWARF
  line numbers of `mdns_init` (4783), `mdns_parse_packet` (3066), `mdns_query_aaaa` (5603) and
  `_mdns_server` (25) are identical to the unpatched file.
- Headers: `mdns.h` comes from the SDK; `mdns_private.h` and `mdns_networking.h` are verbatim copies of the IDF 4.4.7 private_include files (the SDK ships no private headers).
- Why it replaces the archive member: this object defines every symbol the firmware (ESPmDNS.cpp) uses
  from `mdns.c.obj`, so the linker never pulls the archive member (no duplicate definitions). It must not
  define the six `mdns_networking_lwip.c.obj` symbols; it does not.
- Patches (search `MESHCOM-PATCH`):

| ID    | Finding                                                    | Change                                                                     |
| ----- | ---------------------------------------------------------- | -------------------------------------------------------------------------- |
| MD-01 | IDF #1 name decompression recursion, GHSA-7fpr-vjw5-2c86   | `_mdns_read_fqdn_depth` with depth cap 8; `_mdns_read_fqdn` is a wrapper   |
| MD-02 | lwIP #9 compression pointer as last byte, 1-byte over-read | bounds check before reading the second pointer byte                        |
| MD-03 | lwIP #3 repeated PTR answer, NULL question list            | `_mdns_remove_parsed_question` returns when the list is empty              |
| MD-04 | lwIP #7 TXT parser heap overflow                           | advance by 1 only when `=` is present; stop at the counted number of items |
| MD-05 | lwIP #6 A/AAAA answers read past the buffer                | record handled only if rdata length is at least 4 / 16                     |

- Compiler diagnostics: none disabled.

## Re-sync

On a framework bump (`package.json` version other than `3.20017.*`) check the new `libmdns.a` against the
matching IDF tag: `nm --defined-only` of the member and `nm -l` line numbers. If the IDF mdns changed,
re-vendor, re-apply MD-01..05, or drop the override when upstream fixed them. The Arduino 3.x core
(IDF 5) is not covered by the guard on purpose.
