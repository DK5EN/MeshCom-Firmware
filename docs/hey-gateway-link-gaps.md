# Gateway-to-gateway links are missing from the server feed

Status: deferred backlog item `DEF-GWLINK`, 2026-10-11. Nothing implemented. Follows on from
[`hey-echo-upload-concept.md`](hey-echo-upload-concept.md) (shipped in 9d7eab21).

## 1. BLUF

Two server-connected gateways that hear each other over LoRa (example: DK5EN-98 and DL2JA-2,
3.9 km apart) never produce a link in the INTERLINK feed, so mcmap / meshmap.oevsv.at cannot draw
one. All three ways the radio contact reaches the server lose the link:

1. **Direct reception, either direction.** The feed no longer names the uploading gateway (since
   late Aug 2026), so a one-element path carries a measurement but no receiver.
2. **Relay by a gateway.** A server gateway that relays a frame sets `msg_server` (`S1`). Uploads of
   such frames never appear in the feed: inferred hub rule, see section 3.
3. **Own-HEY echo (9d7eab21).** DK5EN-98 uploads the echo of its HEY relayed by DL2JA-2, but that
   echo carries `S1` and is dropped like any other gateway relay.

Fixing it cleanly is hub-side. Two fork-only firmware workarounds exist (F1, F2 in section 5); both
are deferred.

## 2. The DL2JA-2 case (measured 2026-10-11)

DL2JA-2: gateway (`gw:1`), firmware 4.40a, Heltec V3, Marzling, NCNT 15-16. DK5EN-98: gateway,
firmware 9d7eab21 since ~11:44.

| Route                             | What reaches the feed                                          | Why no link                           |
| --------------------------------- | -------------------------------------------------------------- | ------------------------------------- |
| DL2JA-2 hears DK5EN-98 directly   | `"path":"DK5EN-98","rssi":"15,116,-13;"` (`1AE1E385`, NCNT 15) | One-element path, uploader not named  |
| DK5EN-98 hears DL2JA-2 directly   | `"path":"DL2JA-2","rssi":"9,119,-9;"` (`E9FB71A9`, NCNT 9)     | One-element path, uploader not named  |
| DL2JA-2 relays DK5EN-98's HEY     | nothing                                                        | Relay sets `S1`; `S1` uploads dropped |
| DK5EN-98 uploads that echo itself | nothing                                                        | Same frame, still `S1`                |

mcmap `link_quality_overview DK5EN-98`, 7 days, both directions: no DK5EN-98 <-> DL2JA-2 link.
Contrast: DL2JA-1 is not a gateway, its relay of `1AE1E385` arrives
(`"path":"DK5EN-98,DL2JA-1","rssi":"9,124,-17;8,122,-10;"`) and mcmap has DK5EN-98 -> DL2JA-1
(221 samples / 7 d).

## 3. Evidence for the `S1` drop

- Bench 2026-10-11 (DK5EN-1 with 9d7eab21 as gateway): of its own-HEY echo uploads, the 5 with `S0`
  (last relay DK5EN-14 or DK5EN-92, non-gateways) all appear in the feed; the 5 with `S1` (last
  relay DK5EN-98) none.
- DK5EN-1's console shows it uploading `TX-UDP 056 @ xB080FA1B ... S1 ... DF2SI-12,DK5EN-98>HG@...`
  at 11:35:39 (a foreign HEY relayed by DK5EN-98). The feed has six rows for `B080FA1B`, all
  one-element `"path":"DF2SI-12"`; the copy relayed by DK5EN-98 is absent. Same for `5858115D`:
  the `DB0FHR-12,DB0ED-99` copy is there, the `...,DK5EN-98` copy is not.
- In the INTERLINK log from ~10:20 to ~11:50 (156 k lines) no HEY path contains DK5EN-98 or DL2JA-2
  as a relay, although DK5EN-98 relays HEYs continuously.

Not proven: the hub code was not read; the rule is inferred from these frames. Firmware side for
reference: `msg_server` is bit 0x80 of byte 5 of the frame (`src/aprs_functions.cpp:181-183`), set
on relay by a server gateway (`src/lora_functions.cpp`, relay block, `aprsmsg.msg_server = true`).
The receiver anonymity is recorded in mcmap's
`docs/findings/data-gateway-link-blindspot.md` (verdict predates the hub change) and in the fork's
memory note "INTERLINK receiver anonymous since Aug".

## 4. Server-side fix (preferred)

- Name the uploading gateway again (append its call to `path`, as before late Aug 2026). Restores
  every direct reception as a link, network-wide, for all firmware versions.
- Publish HEY uploads with `msg_server` set (HEY dedup is already disabled on the hub). Restores
  gateways as relays and makes the own-HEY echo work across gateway neighbours.

Both are a question for the hub operator; no firmware change needed.

## 5. Firmware workarounds (fork only, deferred)

| Option | Change                                                                                                         | Effect                                                                                                                              | Risk                                                                                                                                                                                                                                                                  |
| ------ | -------------------------------------------------------------------------------------------------------------- | ----------------------------------------------------------------------------------------------------------------------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| F1     | The own-HEY echo upload (`OnRxDone`, own-echo branch) clears `msg_server` in the uploaded copy only. One line. | DK5EN-98 -> DL2JA-2 appears whenever DL2JA-2 relays a DK5EN-98 HEY and DK5EN-98 hears that relay. Needs only our gateway.           | Overrides the meaning "a gateway already uploaded this" for that one frame class. Harmless while hub HEY dedup is off. Rests on the inferred `S1` rule.                                                                                                               |
| F2     | Every gateway HEY upload appends the own call to the path of the uploaded copy (not the on-air frame).         | Every direct reception by our gateway becomes a named link (DL2JA-2 -> DK5EN-98 etc.). Needs only our gateway for its receive side. | Changes the path semantics the hub and mcmap see. If the hub later names uploaders again, the call would appear twice. Agree with the hub operator first (see memory note on a double append). mcmap pairs group `i` with `path[i] -> path[i+1]`, which F2 satisfies. |

Test plan for F1 when picked up: keep DK5EN-1 as gateway (its relays carry `S1`), flash F1 on
DK5EN-98, `--sendhey`, expect a feed row `"path":"DK5EN-98,DK5EN-1","rssi":"<DK5EN-1's group>;"`.
That one row confirms both the `S1` rule and the fix.

## 6. Trigger to revisit

- The hub operator declines both server-side changes, or
- mcmap's gateway graph is needed before the hub changes (ties in with `DEF-HN`).
