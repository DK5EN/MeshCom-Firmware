# TLM-04 — Extern-UDP `tele` reports the barometric altitude as `qfe`

**Status:** **FIXED** 2026-09-05 on `fork-main` (option 3 from §5): `"qfe"` in the `lora` `tele`
datagram now carries `aprspos.press` (APRS `/P=`), the `/F=` pressure altitude moved to a new key
`"pressure_alt"`, the datagram builder was extracted into `src/extern_tele_json.h` so the key
contract is native-testable (`test/test_extern_tele_json`, 3 cases, registered in `[env:native]`).
Doc note for the installed base in `docs/ext_udp_telemetry.md` §6. Operator ruling: this is a bug
fix, not a contract change -- the key name promised hPa all along. Backlog row `TLM-04`, §3.8y.
Original status before the fix: mechanism established by code reading at `695b6141`, blocked on
this decision.
**Severity:** Low. No crash, no mesh impact, no data lost on the air. A dashboard fed by Extern-UDP
shows a wrong pressure for every relayed node that runs a BME680 (or a BMX280 with `--setpress`).
**Class:** upstream defect. `git diff upstream/dev` over `src/extudp_functions.cpp`,
`src/loop_functions.cpp` and `src/bme680.cpp` shows no difference in the involved lines; the
behaviour is identical on `upstream/dev`.
**Reported:** 2026-09-04, operator chat thread. Gateway `Primär` (ESP32, 192.168.0.212) forwards a
`tele` datagram for `DM3KS-13` with `"qfe":191`; the reporter's dashboard renders it as
"190,0 hPa". Reporter: _"glaube das ging los als der BME280 kaputt ging und ich durch BME680
ersetzte"_, and _"Druck wird da falsch dargestellt, ist aber über HF korrekt"_.
**Branch:** `fork-main` @ `695b6141` · **Upstream merge-base:** `4e649eae`
**Related:** `TLM-01..03` (§3.8i, telemetry over LoRa/UDP), `UDP-01`/`UDP-02` (§3.8l, EXTUDP),
`docs/ext_udp_telemetry.md`, MCProxy `src/mcapp/storage/ingest.py:1425-1446` (consumer-side
workaround, in place since the telemetry verdict V4/V4a).

---

## 1. Verdict in one paragraph

The value 191 is not a pressure. It is the BME680's pressure altitude in metres against 1013.25 hPa
(`readAltitude(1013.25)`, i.e. altitude vs QNE -- neither a height above ground nor a Q-group). The APRS
position frame carries that altitude in the `/F=` field, the receiving gateway parses `/F=` into a
variable named `qfe`, and the Extern-UDP `tele` datagram for a relayed node copies that variable
into the JSON key `"qfe"`. The real station pressure travels in the same frame as `/P=`, is parsed
correctly into `aprspos.press`, and is then never written to the datagram. The same key `"qfe"`
in the datagram for the gateway's **own** sensor is a genuine hPa value. One key, two physical
quantities, selected by `src_type`.

## 2. The chain, line by line

| Step                    | Location                           | What happens                                                                                                                               |
| ----------------------- | ---------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------ |
| Sensor read (sender)    | `src/bme680.cpp:158-161`           | `node_press = bme.pressure / 100` (hPa). `node_press_alt = bme.readAltitude(1013.25)` (metres).                                            |
| Beacon call             | `src/esp32/esp32_main.cpp:3324`    | `sendPosition(..., node_press, ..., node_press_alt, node_press_asl)`. The parameter that receives `node_press_alt` is named `qfe` (`int`). |
| Frame build             | `src/loop_functions.cpp:4324-4326` | `if (qfe > 0) "/F=%i"` — the altitude. `/P=%.1f` is the pressure (`:4296`). `/Q=` is suppressed while `bBME680ON` (`:4332`).               |
| Parse (gateway)         | `src/aprs_functions.cpp:843`       | `/F=` → `aprspos.qfe` (`int`). `/P=` → `aprspos.press` (`:731`).                                                                           |
| Datagram, relayed node  | `src/extudp_functions.cpp:600`     | `ctJson["qfe"] = aprspos.qfe` — the altitude. `aprspos.press` is not emitted at all.                                                       |
| Datagram, gateway's own | `src/extudp_functions.cpp:580`     | `ctJson["qfe"] = meshcom_settings.node_press` — a real hPa value.                                                                          |

Arithmetic check on the field sample: `readAltitude(p) = 44330 · (1 − (p / 1013.25)^0.1903)`.
191 m corresponds to 990.5 hPa, which is a plausible QFE for a station at 225 m GPS altitude
on a day with QNH around 1017 hPa. The number is consistent with the mechanism.

The `/F=` field has carried `node_press_alt` since V4.25 (`93854150`). The `int` type of the
parameter and of `aprspos.qfe`, and the `"ALT:%5im / %5im"` display line in
`src/loop_functions.cpp:1574`, both confirm it was always an altitude; only the variable name
says otherwise.

## 3. Why it started with the BME680

With a BMX280 the altitude comes from `getPressALT()` in `src/bmx280.cpp:308`, which returns 0
until a reference pressure has been set with `--setpress`. At 0 the `/F=` field is not emitted, so
`aprspos.qfe` stays 0 at the gateway. At the same time the BMX280 path emits `/Q=` (QNH). The
BME680 path always produces a non-zero altitude, so `/F=` is always present, and it suppresses
`/Q=`. From the dashboard's point of view the sensor swap turned `qfe` from 0 into 191 and `qnh`
from a real value into 0 — exactly what the reporter's datagram shows.

## 4. Why it looks right "over RF"

Consumers on the BLE or serial path see the APRS text and read `/P=`. MCProxy's `ble_protocol.py`
maps `qfe` to `/P=` by key and documents the `/F=` trap (`:390-400`); its Extern-UDP ingest drops
`qfe` for `src_type == "lora"` by key, not by magnitude, because an altitude above 850 m reads as
a plausible pressure (`ingest.py:1425-1446`). The reporter's dashboard is a different consumer
that trusts the key name, as `MCProxy/doc/telemetry.md:47` ("Station pressure (hPa)") and the
MeshCom telemetry convention (`v1 = qfe`) both invite it to.

## 5. Options

1. **Firmware, minimal.** `src/extudp_functions.cpp:600`: emit `aprspos.press` instead of
   `aprspos.qfe`. One line. `"qfe"` then carries hPa in both `src_type` variants. Leave the
   `/F=` wire field alone — it is an altitude by design and other receivers may read it as such.
   Optionally add `"alt_baro": aprspos.qfe` so the altitude is not lost. Needs a native test in
   `test/` that builds a `lora` tele datagram from a parsed frame with `/P=` and `/F=` and asserts
   the key.
2. **Consumer side only.** Document the trap in `docs/ext_udp_telemetry.md` and tell the
   reporter to drop `qfe` for `src_type:"lora"` (as MCProxy does). No firmware change, no PR.
3. **Both.** Fix upstream, document the trap for the installed base, which will emit the old
   datagram for months.

Recommendation if the decision is to act: option 3. The fix is a one-liner with no RAM cost; the
doc note is needed regardless because fielded gateways will not update quickly.

## 6. Decision needed

Whether this goes to upstream at all. Arguments against: it is a consumer-visible contract change
(`"qfe"` for `lora` switches from metres to hPa), any dashboard that has learned to ignore or
re-interpret the field keeps working, and MCProxy is already correct. Arguments for: the key
name is a promise the datagram breaks, the fix is one line, and every new consumer will hit it.

Until decided: **BLOCKED**, Low priority, no implementation.

## 7. Repro without hardware

Feed the gateway a frame with `/P=990.5` and `/F=191` (bench: `--injectmsg` skips `OnRxDone`, so
use a second node or the serial `::` prefix; see `bench-serial-message-and-inject.md`) and read the
`tele` datagram on the Extern-UDP port. Expected today: `"qfe":191,"qnh":0`. Expected after
option 1: `"qfe":990.5`.

## 8. Fix as implemented (2026-09-05)

- `src/extern_tele_json.h` (new): `externTeleJsonNode()` / `externTeleJsonLora()` build the two
  `tele` shapes; `sendExtern()` in `src/extudp_functions.cpp` calls them instead of building the
  JSON inline. Same keys, same order, plus `pressure_alt` in the `lora` shape.
- `lora` shape: `qfe` ← `aprspos.press` (`/P=`), `pressure_alt` ← `aprspos.qfe` (`/F=`).
  `node` shape unchanged.
- `/F=` on the wire, `aprspos.qfe` and the `sendPosition()` parameter name are untouched; the
  misnomer stays in the variable names, the datagram no longer inherits it.
- Test `test/test_extern_tele_json`: the field sample (`/P=990.5`, `/F=191`) asserts
  `qfe == 990.5` and `pressure_alt == 191`; the `node` shape keeps hPa; buffer bound (JSN-01).
- Verified: `pio test -e native -f test_extern_tele_json` green, `E22-DevKitC` and
  `wiscore_rak4631` build green. No bench run; the argument order in `sendExtern()` itself is
  covered by code reading only, since `sendExtern()` is guarded out of the native build.
- Consumers: MCProxy `ingest.py` drops `qfe` for `src_type:"lora"` by key. That workaround can be
  narrowed to gateways below this firmware once it is fielded; nothing breaks meanwhile because the
  dropped value is now the correct one.
