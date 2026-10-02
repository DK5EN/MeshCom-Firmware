# G2 H6 -- heltec-93, UDP-1990 inbound, final image e64ce346 + INSTRUMENT_ENABLED (2026-09-17 23:38)

Recipe (the one that works; two earlier attempts this evening flashed the
T-Deck by port autodetect and a third built without the define):

    PLATFORMIO_BUILD_FLAGS="-DINSTRUMENT_ENABLED=1" pio run -e heltec_wifi_lora_32_V3 -t upload --upload-port /dev/cu.usbserial-0001
    node: --srvip 192.168.68.58, --udplog on, --gateway on   (serial, after WiFi join)
    mac:  python3 tools/mock/meshcom_server.py --host 0.0.0.0 --port 1990 --replay test/golden/corpus/udp1990 --gap 1.0 --capture-dir <here> --no-redistribute --linger 15 --replay-timeout 90
    node log: serial recording -> test/golden/normalize.py -> udp-rx-log.txt (raw kept as serial-raw.txt)
    python3 test/golden/compare_udp.py test/golden/hw/G1/heltec-93/udp test/golden/hw/G2/heltec-93/udp

## Result

    server side  A 36 corpus datagrams, B 36   identical
    node side    A 30 classified datagrams, B 31

The 30 corpus classifications are identical to G1 line by line (diff of the
`[GW];rx` lines minus BEAT: only one ADDED trailing line). The 31st is a
`CET len 55` that arrived AFTER a reboot, from the real OE server (the
`--srvip` override is RAM-only and gone after a reset). So on the surface H6
measures, the final image behaves as G1 did. **Done as scoped** (operator
decision: instrument image, H6 cannot validate the shipping build).

## Finding H6-01: the corpus's malformed tail now resets the WiFi radio, and the run ends in a software reboot

New against G1, in `serial-raw.txt` lines 138-186:

    [UDP];rx;ip;192.168.68.58;port;1990;len;91;head;47415445     <- corpus tail, a malformed GATE
    [WIFI];event;disconnected;reason;8;ms;92374                   <- reason 8 = the STA left (own disconnect)
    [WIFI-DBG] resetMeshComUDP: WiFi disconnected, flags reset for reconnect
    E (95196) wifi:timeout when WiFi un-init, type=4
    ... reconnect, got_ip at 100778 ...
    [WIFI];event;disconnected;reason;8;ms;104565
    [BOOT] RESET_REASON=3 SW                                       <- esp_restart()

This is the DR-20 path (W6: `getMeshComUDP()` now acts on a failed handler
status and calls `resetMeshComUDP()`), which EXPECTED-DIFF.md lists as "fault
path, not comparable at H6, U1 twin only". At G1 the same datagrams were
classified OTHER and ignored. What EXPECTED-DIFF did not predict: the recovery
takes the whole WiFi radio down (`WiFi.disconnect(true,true)` + `WIFI_OFF`),
the un-init times out, a second leave follows and the node reboots. The
restart site is not pinned (candidates: `rebootAuto` from a settings
command, none of the radio-init sites). Any host that can reach UDP 1990 with
a datagram the handler rejects can do this to a gateway. Recorded as open
defect `H6-01` in BACKLOG; needs a repro with the reset site instrumented
before a fix.

Gateway was ON with the real server for ~30 s after the reboot (one relayed
frame `GWI ... from=OE1XAR-33`, TX at 1 dBm). The node is back on stock
`e64ce346`, gateway off, maxhop 2.
