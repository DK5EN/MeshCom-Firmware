# AU web GUI concept -- Fable Verdict (2026-10-18)

Six finders (Sonnet/Opus medium), six Opus verifiers. Evidence per item in the scratchpad files vf-*.md.

## Confirmed findings (all incorporated into the concept, section 11 maps them)

1. updapply cannot answer (reboot before the HTTP header, web_functions.cpp:4548-4551 vs command_functions.cpp:219-237) -- high
2. updapply answers OKAY on a failed apply (web_nodefunctioncalls.cpp:64-66) -- high
3. Safeboot apply failure keeps the node in Safeboot for 180 s; /austatus redirects to HTML; /ota/state JSON exists (safeboot/main.cpp:656-674, 736-739, 841-860) -- high
4. Live heapok false-negative on classic ESP32: job pauses BLE before the gate (fw_update_net.cpp:1077-1082, 544); bench 57 KB with BLE vs 80-85 KB paused -- high
5. Priority G/H above E hides a staged image although apply has no heap gate -- high
6. C hides the error of a failed download (I must overlay C) -- medium
7. Error table incomplete and job-dependent (16 missing tokens) -- medium
8. heapGateOk in anonymous namespace, logs on refusal, installs the mbedTLS allocator -- medium
9. status ilen is the last check's, not the staged record's; stagelan never sets zlen -- medium
10. /callfunction upd* open to CSRF (MC_WEB_GUARD 0 default, web_guard.h:87) -- medium, cheap fix
11. /austatus would carry ACAO * (web_functions.cpp:916, 4326); X-MC is a request header -- low/wording
12. Web setters still set auto (web_setup.cpp:276-305) -- low now, required for D2
13. No night path: checks every 22-26 h from the last start, not window-gated; no check while isPhoneReady (fw_update.h:470-487, esp32_main.cpp:2646) -- high
14. AU_AUTO_INSTALL placement starves checks if put at handover; clean rule fwEffectiveMode 2->1 -- medium
15. BLE: advertiseOnDisconnect(true) restarts advertising from the host task (esp32_main.cpp:555) -- high
16. BLE: CHECK and DOWNLOAD pause/resume separately -> McApp reconnects between them -- high
17. BLE: 10-min watchdog shorter than a legal job (~36 min) -- high
18. BLE: S3 never pauses (AU_BLE_PAUSE 0), stack deinit/reinit unbenched on S3 -- medium
19. BLE: isPhoneReady can stay 1 after a forced deinit (terminate-timeout path) -- medium
20. BLE: lowheap under the guard not mapped to lowheap_ble_off -> no attempt used, NimBLE cycles -- medium
21. Guard clears the phone flag -> pending RM1 upd go / handover proceed -- low, contract change
22. Mode default must be MC_PLATFORM_DEFAULT(1,0) (COMMON block reaches nRF52) -- medium
23. Migration hook: sanitize_loaded_settings() (esp32_flash.cpp:221); JSON range stays 0..2 -- medium
24. Stored mode 2 on web-configured nodes (prod/dev select = auto today) -- high for D2 semantics
25. New CSS/JS/route must be ESP32-guarded (web_functions.cpp is the RAK server too) -- medium
26. No C++/JS contract test for /austatus -- high
27. New tests not registered (platformio.ini test_filter, regression.sh:217-222 explicit list) -- high
28. W2a bench recipe impossible (reinstall guard 'last', fw_update.h:365-377; stagelan INSTRUMENT-only) -- high
29. Existing tests that break on 'off' removal (test_command_setters 656/674/702/1390-1401, test_rm_format 502, test_config_json 776-808) -- medium
30. Wave ownership overlaps and unowned files -- medium-high
31. Image string scan unspecified -- low
32. GitHub API unauthenticated (60/h per IP), manual checks have no cooldown -- low

## Refuted claims (do not re-investigate)

- "notify default starts WiFi on nodes without an SSID" -- refuted: wifiSsidMissingBlocksStart keeps them off.
- "D2 (verifying) cannot be detected" -- refuted: the job stays BUSY through digest, read-back and BLE resume; D2 = busy && download && pct == 100.
- "pct from bytesDone/zlen is wrong" -- refuted: bytesDone counts compressed bytes against zlen.
- "a LAN attacker can make the node install foreign code" -- refuted: pinned CA roots (setCACert), digest from the same TLS channel, Safeboot esp_image_verify; trust rests on the GitHub repo owner.
- "a check due while a phone is connected is lost" -- refuted: the wrap latch fires it on the first tick after the phone drops; only a permanently connected phone prevents it.
- "saving settings in the load path corrupts NVS" -- refuted for today's code: g_flash_load_in_progress refuses the save (fail-closed); the sanitize hook saves after preferences.end().
