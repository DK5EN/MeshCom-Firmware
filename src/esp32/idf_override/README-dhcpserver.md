# dhcpserver.c override (FORMAL-01 wave 3c)

## What

`dhcpserver.c` is a vendored copy of ESP-IDF v4.4.7 `components/lwip/apps/dhcpserver/dhcpserver.c`
(arduino-esp32 2.0.17, framework package `3.20017.*`) with the patches below. The prebuilt
`liblwip.a` contains the same file as member `dhcpserver.c.obj`. Our object defines all 11 symbols
of that member (`dhcp_search_ip_on_mac`, `dhcps_coarse_tmr`, `dhcps_dns_getserver`,
`dhcps_dns_setserver`, `dhcps_option_info`, `dhcps_pbuf_alloc`, `dhcps_set_new_lease_cb`,
`dhcps_set_option_info`, `dhcps_start`, `dhcps_stop`, `node_remove_from_list`), so the linker never
pulls the archive member. Users of the symbols (`libesp_netif.a`, `liblwip.a(timeouts.c.obj)`) bind
to ours. The softAP stays open (operator decision); nothing here adds a password.

The whole file is guarded by `ESP32 && !NATIVE_BUILD && ESP_IDF_VERSION_MAJOR == 4`: safeboot (IDF 5),
native and nRF52 do not compile it.

## Source and version match

Source: branch `review` of the review clone, path `code/idf/lwip/apps/dhcpserver/dhcpserver.c`
(1377 lines). The prebuilt member exports exactly the same 11 globals and the same file-local
statics (`s_dhcps_mask`, `dhcps_dns`, `dhcps_offer`, `plist`, `renew`, `magic_cookie`, `handle_dhcp`,
`create_msg`, `add_offer_options`).

## Patches (`/* MESHCOM-PATCH DS-0x */`)

| ID    | Finding                                  | Change                                                                                                                                                                                                                                     |
| ----- | ---------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ |
| DS-01 | lwIP #4 (`mem_malloc` unchecked)         | `parse_msg`: NULL checks for the lease and the list node; on failure free what was taken, clear `client_address`, drop the request.                                                                                                        |
| DS-02 | IDF #2 (GHSA-g764-gwc3-75m5, option OOB) | `parse_options` walks options with `dhcps_opt_step()`: an option is read only if code, length byte and all value bytes lie inside the packet; per-option minimum value lengths (type 1, requested IP 4). Pad (0) is a single byte now.     |
| DS-03 | lwIP #5 (pool exhaustion by random MACs) | New leases are unconfirmed (30 s) until ACK promotes them to the full lease; known-MAC packets no longer refresh the timer. A new MAC at `min(MAX_STATION_NUM, DHCPS_MAX_LEASE)` leases evicts first (`kill_oldest_dhcps_pool` rewritten). |
| DS-04 | hardening found while reading            | `handle_dhcp`: `s16_t` length overflow and two-pbuf copy replaced by length checks (240..1500 bytes) and `pbuf_copy_partial()`.                                                                                                            |

DS-03 eviction order: oldest unconfirmed lease, else the lease with the least remaining time (so a
new station is never refused, but a flood of full DISCOVER+REQUEST handshakes can still push out
older leases; the client then simply asks again).

## Compiler pragmas

None.

## Host test (not wired, platformio.ini is not part of this wave)

`dhcps_opt_step()` is a pure function, compiled also with `-DMESHCOM_DHCPS_HOST_TEST` (the rest of the
file stays out). A native Unity test can `#include "../../src/esp32/idf_override/dhcpserver.c"` after
defining that macro and check: option ending exactly at `avail`; length byte at `avail-1`; value one
byte too long; pad/end handling; `pos >= avail`. It needs an `[env:native_dhcps_opt]` block modelled on
the other `[env:native*]` envs (`platform = native`, `test_filter`, `build_flags = -DMESHCOM_DHCPS_HOST_TEST`).

## Re-sync (framework or IDF bump)

1. `ar t`/`nm` the new `liblwip.a`: member name, exported symbols, and users must still match the 11
   symbols above. Any new export means this override must be updated, or the archive member gets
   pulled and the link fails with duplicate symbols.
2. Diff the new upstream `dhcpserver.c` against the vendored one, re-apply DS-01..DS-04.
3. Prove it in the map file: `liblwip.a(dhcpserver.c.obj)` absent, our object present.
4. Drop the override once upstream carries the fixes (IDF 5 builds do not use it).
