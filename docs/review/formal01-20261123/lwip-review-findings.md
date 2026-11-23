# Review findings: lwIP, DHCP server, mDNS

Review date: 2026-11-23. Branch: `review` (commit `06b389d`). Diff reviewed: `git diff main...HEAD`
(the repo has no `origin/HEAD`). Tool: Claude Code `/code-review high`.

## Bottom line

Nine findings: seven important, two minor. Four of them let an attacker on the network hang or
crash the device:

- An attacker-chosen tiny MTU hangs the network thread (`tcpip_thread`). Two ways in: an ICMPv6
  "Packet Too Big" message (IPv6) and a rogue DHCP server's MTU option (IPv4).
- An mDNS packet that repeats one answer crashes the device when at least one mDNS service is
  registered.
- A client on the open softAP can crash the DHCP server when memory is low, or use up every DHCP
  address.

## Verification status

- The reviewer found these by reading code paths only. Nothing was compiled, run or tested.
- No finding has been checked independently yet. Treat every entry as "found by review, not yet
  confirmed".
- No CLAUDE.md applies to the vendored code, so the review raised no conventions findings.

## Summary table

| #   | Severity  | File and line                                       | Problem                                                       |
| --- | --------- | --------------------------------------------------- | ------------------------------------------------------------- |
| 1   | important | `code/lwip/src/core/ipv6/nd6.c:928`                 | ICMPv6 Packet Too Big without an MTU floor: endless loop      |
| 2   | important | `code/lwip/src/core/ipv4/dhcp.c:803`                | DHCP MTU option (26) without a floor: endless loop            |
| 3   | important | `code/idf/mdns/mdns.c:3272`                         | Repeated PTR answer dereferences a NULL question list: crash  |
| 4   | important | `code/idf/lwip/apps/dhcpserver/dhcpserver.c:906`    | `mem_malloc` result never checked for NULL: crash on low heap |
| 5   | important | `code/idf/lwip/apps/dhcpserver/dhcpserver.c:1319`   | DHCP address pool can be used up by random client MACs        |
| 6   | important | `code/idf/mdns/mdns.c:3442` (and 3492)              | AAAA and A answers read past the receive buffer               |
| 7   | important | `code/idf/mdns/mdns.c:3012`                         | TXT parser loses its place: heap overflow                     |
| 8   | minor     | `code/lwip/src/core/ipv6/nd6.c:362` (404, 465, 843) | Link-layer address option of length 0: 6-byte over-read       |
| 9   | minor     | `code/idf/mdns/mdns.c:338`                          | Name-compression pointer as last byte: 1-byte over-read       |

## Findings in detail

### 1. ICMPv6 Packet Too Big lowers the path MTU without a floor

- **Where:** `code/lwip/src/core/ipv6/nd6.c:928`
- **Severity:** important. Remote hang of the network thread, then watchdog reset.
- **Defect:** an ICMPv6 "Packet Too Big" message can lower the path MTU to any value. A value of
  48 to 55 leaves the IPv6 fragmenter with a fragment size of zero, so it loops forever in
  `tcpip_thread`.
- **How it is triggered:**
  1. A LAN host pings the device over IPv6. This creates a destination-cache entry for that host.
  2. The host sends an ICMPv6 type 2 (Packet Too Big) with `mtu=48` and an inner IPv6 header whose
     destination is its own address.
  3. It pings again with a payload over 8 bytes, so the reply's `tot_len` (more than 48) is larger
     than `pmtu`.
  4. `ip6_output_if` calls `ip6_frag`: `nfb = (48-48) & 0xFFF8 = 0`, `cop = 0`, and `left` never
     shrinks. `tcpip_thread` spins sending empty fragments until the task watchdog resets the
     device.
- **Fix:** before storing `pmtu`, ignore values below `IP6_MIN_MTU_LENGTH` (1280) or raise them to
  it, as RFC 8201 requires.

### 2. DHCP MTU option is accepted without a floor

- **Where:** `code/lwip/src/core/ipv4/dhcp.c:803`
- **Severity:** important. Remote hang, repeated on every reconnect.
- **Defect:** the DHCP MTU option (26), which ESP added, is accepted whenever it is below the
  current MTU, with no lower bound. A value of 13 to 27 makes `ip4_frag` compute `nfb = 0`, and it
  then loops forever in `tcpip_thread`.
- **How it is triggered:**
  1. A rogue DHCP server on the station's LAN (or one that answers before the real server) sends an
     ACK with option 26 = `0x0018` (24). `netif->mtu` becomes 24.
  2. The next UDP datagram to the MeshCom server (port 1990) has `tot_len` greater than 24, so
     `ip4_output_if` calls `ip4_frag`.
  3. There `nfb = (24-20)/8 = 0`, `fragsize = 0` and `left` never shrinks: an endless loop of
     20-byte fragments, then a watchdog reset. The same happens after every reconnect.
- **Fix:** apply the option only when the value is at least 576 (at minimum 68, the RFC 791 floor).
  Also guard `ip4_frag` against `(netif->mtu - IP_HLEN) < 8`.

### 3. Repeated PTR answer in an mDNS query crashes the mDNS task

- **Where:** `code/idf/mdns/mdns.c:3272`
- **Severity:** important. Remote crash and reboot. Needs at least one registered mDNS service.
- **Defect:** in a discovery query whose PTR answer is repeated, `_mdns_remove_parsed_question`
  dereferences a NULL question list. The first answer removes the only service-discovery PTR
  question, and the next identical answer is handled with `parsed_packet->questions == NULL`.
- **How it is triggered:** a LAN host sends one mDNS packet from UDP 5353 with flags 0. It holds one
  question, `_services._dns-sd._udp.local` PTR, and two identical answers:
  `_services._dns-sd._udp.local` PTR to `_<ourservice>._<proto>.local`. With one registered
  service, the first answer frees the only question. The second calls
  `_mdns_question_matches(NULL, ...)`, which reads `question->type`. The mDNS task crashes
  (LoadProhibited) and the device reboots. With N services, N+1 answers do the same.
- **Fix:** return early from `_mdns_remove_parsed_question` when `parsed_packet->questions` is
  NULL, or guard the call the way the other branches do.

### 4. DHCP server does not check memory allocation

- **Where:** `code/idf/lwip/apps/dhcpserver/dhcpserver.c:906` (same flaw at line 912)
- **Severity:** important. Crash of the network thread from the open softAP.
- **Defect:** `parse_msg` calls `mem_malloc` for a new lease and for its list node, then clears and
  uses the result without a NULL check. Every new client MAC allocates, so on low heap one more
  DISCOVER crashes the device inside `tcpip_thread`.
- **How it is triggered:** a client on the open softAP drains the heap (many TCP connections to
  ports 80 or 2323, mDNS queries and so on), then sends a DHCPDISCOVER with a new client MAC
  (`chaddr`). `mem_malloc(sizeof(struct dhcps_pool))` returns NULL and `memset(NULL, 0, ...)`
  faults, which crashes `tcpip_thread`. The list-node allocation at line 912 has the same flaw.
- **Fix:** check both allocations, free the first if the second fails, and return 0 (drop the
  request).

### 5. DHCP address pool can be used up

- **Where:** `code/idf/lwip/apps/dhcpserver/dhcpserver.c:1319`
- **Severity:** important. Denial of service: real clients cannot join the AP.
- **Defect:** every message with an unknown client MAC gets a lease of `DHCPS_LEASE_TIME`
  (minutes). The station limit `MAX_STATION_NUM` is enforced only lazily: `kill_oldest_dhcps_pool`
  frees one entry per 1-second timer tick and never looks at the head of the list.
- **How it is triggered:** a client on the open softAP sends DHCPDISCOVERs with random client MACs
  at more than 100 per second. The address range (up to `DHCPS_MAX_LEASE` entries) fills in under a
  second, and each entry stays until its lease runs out. The timer frees one per second and the
  attacker refills it at once. Real clients reach the pool check with an address beyond `end_ip`,
  get NAKs and cannot join the AP.
- **Fix:** enforce `MAX_STATION_NUM` in `parse_msg` before inserting (or evict at once), give
  offers that were never acknowledged a short timeout, and include the list head in
  `kill_oldest_dhcps_pool`.

### 6. mDNS AAAA and A answers read past the receive buffer

- **Where:** `code/idf/mdns/mdns.c:3442` (AAAA) and `code/idf/mdns/mdns.c:3492` (A)
- **Severity:** important. Remote heap over-read. Can cause a false host-name conflict or a fault.
- **Defect:** AAAA and A answers copy 16 or 4 bytes from `data_ptr` without checking that
  `data_len` is large enough. This happens before any check of whether the record is ours or
  matches a search.
- **How it is triggered:** a LAN host sends an mDNS response whose last record is `x.local` AAAA,
  class 1, rdlength 0. `content == data + len` passes the bounds check, and
  `memcpy(ip6.addr, data_ptr, 16)` reads 16 bytes beyond the packet into adjacent heap. Those bytes
  feed `_mdns_check_aaaa_collision`, which can falsely detect a conflict and rename the host, or
  fault if the buffer ends at an unmapped boundary.
- **Fix:** skip the record unless `data_len >= MDNS_ANSWER_AAAA_SIZE` (or at least 4 for A records
  at line 3492).

### 7. mDNS TXT parser loses its place: heap overflow

- **Where:** `code/idf/mdns/mdns.c:3012`
- **Severity:** important. Heap overflow, but only while the firmware runs an active mDNS query.
  Responder-only use does not reach it.
- **Defect:** `_mdns_result_txt_create` gets lost on a TXT item with no `=`: `name_len == partLen`,
  so `i += name_len + 1` skips the next length byte. The parse can then produce more items than
  `_mdns_txt_items_count_get` counted, and `txt[txt_num]` and `txt_value_len[txt_num]` are written
  past their allocated size.
- **How it is triggered:** the device has an active `mdns_query` (PTR or TXT search). A LAN host
  answers with TXT rdata `[01 'a' 0A 01 'b' 00 01 'c' 00 01 'd' 00]`. The counting pass sees 2
  items, but the parsing pass yields `a`, `b`, `c`, `d` and writes `txt[2]` and `txt[3]` (heap
  pointers to key strings) past the 16-byte allocation, which corrupts the heap.
- **Fix:** advance by `name_len`, plus 1 only when an `=` is present (`name_len < partLen`), and
  stop when `txt_num >= num_items`.

### 8. IPv6 neighbor discovery: link-layer address option of length 0

- **Where:** `code/lwip/src/core/ipv6/nd6.c:362`, with the same pattern at lines 404, 465 and 843
- **Severity:** minor. Over-read of up to 6 bytes; IPv6 traffic can go to a garbage MAC.
- **Defect:** the Neighbor Advertisement, Neighbor Solicitation and Redirect handlers check the
  link-layer address option only with `p->len >= hdr + (length << 3)`. An option length of 0
  passes, then `MEMCPY` copies `hwaddr_len` (6) bytes from offset +2, up to 6 bytes past the
  received data.
- **How it is triggered:** a LAN host sends a solicited Neighbor Advertisement (Override flag set)
  for an address in the neighbor cache, whose payload ends right after a 2-byte option header with
  type 2, length 0. The size check passes, and
  `MEMCPY(neighbor_cache[i].lladdr, lladdr_opt->addr, 6)` reads 6 bytes beyond `p->len`. Adjacent
  buffer bytes become the neighbor's MAC.
- **Fix:** also require `lladdr_opt->length >= 1` (and the matching option type) before using it,
  at all four places.

### 9. mDNS name reader: compression pointer as the last byte

- **Where:** `code/idf/mdns/mdns.c:338`
- **Severity:** minor. 1-byte over-read. Close to the known recursion issue in this function, but a
  separate flaw.
- **Defect:** when the last byte of a packet is a compression-pointer byte (0xC0 or higher),
  `_mdns_read_fqdn` reads the second pointer byte `start[index++]` without checking it against
  `packet_end`.
- **How it is triggered:** a LAN host sends an mDNS packet whose last byte is 0xC0 at a name
  position. The loop condition passes (`start + index < packet_end`), `len = 0xC0`, and
  `start[index++]` reads 1 byte beyond the packet. The address formed from it is then used for the
  recursive name read.
- **Fix:** return NULL if `start + index >= packet_end` before reading the second pointer byte.

## Open next steps

1. Confirm findings 1 to 4 independently against the source (ideally reproduce 1 to 3 with crafted
   packets on a test device).
2. Decide the fix route: the code is vendored lwIP and ESP-IDF, so fixes mean patching upstream
   sources (local patch set, or report upstream), or only a written report.
3. Every fix gets a regression test that fails before and passes after.
