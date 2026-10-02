# ETH-02b on hardware, 2026-09-18 21:22 -- late DHCP lease starts the services

Operator pulled the Ethernet cable, `--reboot` over serial (soft reset,
`RESETREAS=0x00000004`), boot without a lease (`[ETH];event;link;down;ip;0`),
cable back at ~75 s, then in `eth02b-late-lease.txt`:

    [ETH];event;dhcp_acquire_retry;link;1;ms;77582
    [ETH];event;got_ip;192.168.68.66;ms;77708
    [ETH];event;dhcp_acquired_late;ms;77708

`curl http://192.168.68.66/` answered 200 seven seconds after the lease
(21:23:54, 1.45 s) and again at +2m45s; the closing `--info` reads
`hasIpAddress: yes`. Before the fix the web server stayed dead until the
15-minute `web_timer` and `--info` said `no` (handover-rak-h8-20260918.md).
Link stats at the end: `got_ip_n;1 ... resets;1`. The harness report's
"CRASH for wait" is bookkeeping: the requested reboot's banner landed after
the 6 s probe window.
