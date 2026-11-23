# FORMAL-01 library reviews (2026-11-23)

**Three automated library reviews (`/code-review high`) of the shipped NimBLE, lwIP and mbedTLS sources, plus the
IDF 4.4 review. Every finding that is reachable in our firmware gets a fix or a stated reason; this table is the
status.**

- Review repos (local, outside the firmware tree): `~/WebDev/formal01-review/review-{nimble,lwip,mbedtls}`.
- Verification of each finding against our code: `../../campaign-formal-01-20261123.md`, sections "lwIP verification",
  "mbedTLS verification", "NimBLE verification", "IDF 4.4 review".
- Files: [nimble](nimble-review-findings.md), [lwip](lwip-review-findings.md), [mbedtls](mbedtls-review-findings.md).

Status values: **3b** / **3c** = patch or mitigation written in that wave (3b+3c+3d share one gate, pending), **no** =
not reachable in our configuration, **accept** = reachable, consequence accepted with reason.

## NimBLE-Arduino 2.2.3

| #   | finding                                         | reachable                    | status | fix                                                    |
| --- | ----------------------------------------------- | ---------------------------- | ------ | ------------------------------------------------------ |
| 1   | 512 B VLA on the 3072 B host stack per write    | yes, unpaired peer           | 3c     | src: characteristic max length, static onWrite item    |
| 2   | deinit frees objects after a failed stop (UAF)  | yes, update pause            | 3b     | NB-04 (rework: resume path must reuse the kept server) |
| 3   | old host task deletes the new one               | race, window closed today    | 3b     | NB-06                                                  |
| 4   | failed init not undone, sync wait without bound | yes, low-heap resume         | 3b     | NB-05 (rework: unwind only what this call brought up)  |
| 5   | setValue over-read after failed append          | yes                          | 3b     | NB-01 corrected                                        |
| 6   | NB-01 NULL store                                | yes                          | 3b     | NB-01 corrected                                        |
| 7   | 4 prepare writes hold all 4 msys buffers 30 s   | yes, unpaired peer, DoS 30 s | 3c     | cap prepare entries / timeout (build config or patch)  |
| 8   | stop counter never reset                        | yes                          | 3b     | NB-07                                                  |
| 9   | bond deleted before identity proof              | no (bonding off)             | no     | revisit if bonding is enabled                          |
| 10  | getPeerInfo index                               | no (1 connection)            | no     | revisit if MAX_CONNECTIONS > 1                         |

## lwIP / DHCP server / mDNS (IDF 4.4.7, prebuilt)

| #   | finding                                   | reachable                      | status | fix                                                        |
| --- | ----------------------------------------- | ------------------------------ | ------ | ---------------------------------------------------------- |
| 1   | ND6 Packet Too Big without floor          | no (IPv6 never enabled)        | no     |                                                            |
| 2   | DHCP option 26 MTU without floor          | yes, STA and ETH, every renew  | 3b     | `-Wl,--wrap` guard, `src/esp32/lwip_mtu_guard.cpp`         |
| 3   | mDNS NULL question list                   | safeboot only (`http` service) | 3c     | re-check the IDF 5.3 mdns copy first                       |
| 4   | DHCP server malloc unchecked              | yes, open softAP               | 3c     | dhcpserver override or heap reserve (AP password rejected) |
| 5   | DHCP lease exhaustion                     | yes, open softAP               | 3c     | as #4                                                      |
| 6   | mDNS A/AAAA over-read                     | yes, mDNS on by default        | 3c     | patched mdns object, low                                   |
| 7   | mDNS TXT parser heap overflow             | no (no mDNS queries in src)    | no     |                                                            |
| 8   | ND6 LLA option length 0                   | no (IPv6)                      | no     |                                                            |
| 9   | mDNS compression pointer 1-byte over-read | yes                            | 3c     | with #6                                                    |

## mbedTLS 2.28.7 (prebuilt)

| #   | finding                                      | reachable                   | status | fix                                                     |
| --- | -------------------------------------------- | --------------------------- | ------ | ------------------------------------------------------- |
| 1   | certificate dates never checked              | yes, update path            | 3b     | verify callback `src/fw_tls_time.h` (rework: stale RTC) |
| 2   | unbounded allocation while parsing the chain | yes, on-path attacker       | 3c     | capped calloc hook armed during an AU session           |
| 3   | S3 AES DMA bounce buffer leak                | indirect (heap pressure)    | accept | prebuilt, no call-site mitigation; heap gate before TLS |
| 4-8 | port-layer bignum / GCM / CTR                | no call site in our TLS use | no     | #6 (RSA > 4096) is a watch item for the root list       |

## IDF 4.4 / arduino-esp32 2.0.17 review

| #   | finding                                       | status | fix                                             |
| --- | --------------------------------------------- | ------ | ----------------------------------------------- |
| 1   | mDNS name decompression recursion (high, DoS) | 3c     | patched mdns object (depth cap)                 |
| 2   | DHCP server option parser over-read           | 3c     | with lwIP #4/#5                                 |
| 3   | lwIP DNS outside tcpip_thread                 | 3c     | tcpip_callback wrapper in our DNS callers       |
| 4   | HTTPClient header line without cap/deadline   | 3b     | AE-01                                           |
| 5   | WiFiUDP/WiFiClient alloc failure aborts       | 3b     | AE-02                                           |
| 6   | mbedTLS handshake on alloc failure            | accept | heap gate before the handshake already in place |
| 7   | TLS 1.0 / RSA kx / CBC allowed                | 3b     | minimum TLS 1.2 (suites unchanged)              |
| 8   | hostByName writes caller stack after timeout  | 3b     | AE-03                                           |
| 9   | WiFiClient::connected() misses FIN            | accept | web loop capped at 2 s                          |
| 10  | Content-Length via toInt                      | accept | clen/maxBytes checks                            |
