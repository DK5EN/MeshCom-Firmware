# `auto_join` and `send_repeat_time` — what they are, where they came from, what they could still teach us

Date: 2026-09-13. Tree: `dry-unification` @ 1221f85b.

## BLUF

Both fields are leftovers from the **WisBlock-API** library by Bernd Giesecke (RAKwireless), which
the MeshCom nRF52 port was bootstrapped from in 2022/2023. In that library they steer a **LoRaWAN**
sensor node: `auto_join` decides whether the node performs the LoRaWAN OTAA join on its own at boot,
`send_repeat_time` is the period (in ms) of the automatic uplink timer. MeshCom is not LoRaWAN, so
neither field does anything today: the only code that reads them (`api_timer_*`) has **no caller**,
and the one other use is a commented-out block. They survive only because the nRF52 settings struct
_was_ the on-disk and BLE wire format, so removing a member moved every offset behind it. The W3
keyed store lifts that constraint; the fields can then be dropped as two `X()` rows.

Three concepts behind them are worth understanding for our own work, independent of LoRaWAN:

1. an **event-driven main loop** that sleeps on a semaphore and wakes with a reason bit mask;
2. a **single periodic-send scheduler** instead of one `millis()` timer per beacon type;
3. an **attended/autonomous mode switch** that limits BLE advertising to a short window.

## 1. Where they come from

WisBlock-API (github: beegee-tokyo/WisBlock-API, later WisBlock-API-V2) is a framework for RAK4631
/ RAK11310 / RAK11200 nodes. It provides flash-backed settings, an AT command set, BLE
configuration and a sleep/wake loop. Its settings struct `s_lorawan_settings` holds DevEUI,
AppEUI, AppKey, session keys, ADR, data rate, `send_repeat_time`, `auto_join`, plus P2P radio
parameters. MeshCom renamed it to `s_meshcom_settings`, kept the `valid_mark_1/2` markers and the
first field `node_device_eui`, deleted the LoRaWAN keys, and appended its own fields. What stayed:

| Remnant in `src/nrf52/`                                       | Original purpose                                  | State in MeshCom                                                                                 |
| ------------------------------------------------------------- | ------------------------------------------------- | ------------------------------------------------------------------------------------------------ |
| `node_device_eui[8]` (`WisBlock-API.h:185`)                   | OTAA DevEUI                                       | reused: `"MC"` + 6 MAC bytes, shown in the BLE name                                              |
| `send_repeat_time` (`WisBlock-API.h:217`)                     | uplink timer period, ms                           | dead, always 0                                                                                   |
| `auto_join` (`WisBlock-API.h:219`)                            | join at boot without user command                 | dead, always false                                                                               |
| `g_lpwan_has_joined`, `g_join_result`, `otaaDevAddr` externs  | join state, assigned DevAddr                      | declared, never defined or used                                                                  |
| `LORA_JOIN_FIN`, `STATUS`, ... event bits (`WisBlock-API.h`)  | wake reasons for the loop task                    | `BLE_DATA`/`BLE_CONFIG` still set, nobody consumes them                                          |
| `api_timer_init/start/stop/restart` (`api_functions.cpp:337`) | drive the periodic uplink timer                   | compiled, zero callers                                                                           |
| `g_task_sem`, `api_wait_wake()`                               | loop sleeps until an event gives the semaphore    | semaphore created, loop never blocks on it (`nrf52_main.cpp:890` is a 10-tick take in `setup()`) |
| `nrf52_ble.cpp:183`                                           | if `auto_join`: advertise 60 s only, else forever | commented out, always forever                                                                    |

## 2. LoRaWAN OTAA in five sentences

A LoRaWAN device may not send data until it has a **session** with a network server. With
**OTAA** (Over-The-Air Activation) the device sends a _join request_ signed with its factory keys
(DevEUI, JoinEUI/AppEUI, AppKey); the network answers with a _join accept_ that assigns a short
device address (DevAddr) and derives the two session keys (NwkSKey for MAC integrity, AppSKey for
payload encryption). Until the accept arrives, `g_lpwan_has_joined` is false and every uplink is
refused by the stack. The alternative, **ABP**, has the session keys pre-programmed and skips the
handshake. Joining costs airtime and a random back-off, so a battery node wants to join once and
keep the session; a node that is being commissioned by hand wants to wait for the operator.

## 3. `auto_join` (bool, default false)

**Original meaning.** After `init_lorawan()` the library checks `auto_join`. True: it calls the join
immediately and retries `join_trials` times; the node is _autonomous_ and needs no phone or terminal.
False: the node waits until the operator sends `AT+JOIN=1:...` over serial or taps "Join" in the
BLE app; the node is _attended_.

**Set by.** `AT+JOIN=<join>:<auto_join>:<reconnect_tries>:<join_interval>` in the original AT set,
or the BLE settings characteristic (the phone app writes the whole struct back).

**Side effects in the original.** Two things hang off the flag besides the join itself:

- BLE advertising runs only 60 s after boot when `auto_join` is true (nobody is expected to
  connect), forever when false (somebody is commissioning). That is the block commented out at
  `nrf52_ble.cpp:183`.
- `api_timer_restart()` re-arms the periodic timer only if `send_repeat_time != 0 && auto_join`
  (still visible at `api_functions.cpp:429`): unattended sending is only wanted on an unattended
  node.

## 4. `send_repeat_time` (uint32, ms, default 0 = off; library default was 120 000 = 2 min)

**Original meaning.** Period of the _automatic uplink_. The flow is a small state machine, and this
is the part worth reading twice:

```
FreeRTOS timer (send_repeat_time ms)
  -> periodic_wakeup()                    WisBlock-API.cpp:34
       -> api_wake_loop(STATUS)           api_functions.cpp:283
            g_task_event_type |= STATUS
            xSemaphoreGive(g_task_sem)
loop():
  api_wait_wake()                         blocks on g_task_sem, CPU sleeps
  if (g_task_event_type & STATUS) {       clear bit, read sensors,
      g_task_event_type &= N_STATUS;      build payload, lmh_send() }
  if (g_task_event_type & LORA_DATA) ...  downlink arrived
  if (g_task_event_type & BLE_CONFIG) ... phone wrote settings
```

Every wake has a _reason_. The loop does not poll anything; LoRa RX, BLE, AT parser and the timer
all go through `api_wake_loop(bit)`. `send_repeat_time == 0` means "never send on a timer, only on
external events" (e.g. a door sensor on an interrupt pin).

**Set by.** `AT+SENDINT=<seconds>` (older builds `AT+SENDFREQ`), stored as ms. Changing it calls
`api_timer_restart(new_time)`.

**In MeshCom.** The semaphore exists and `nrf52_ble.cpp:268/358` still give it, but `loop()` in
`nrf52_main.cpp` never takes it; it polls with `millis()` timers like the ESP32 side. So the timer,
its callback and the four `api_timer_*` functions are compiled and orphaned. Every fleet export
shows `send_repeat_time=0`, `auto_join=0` (e.g. `docs/bench/w3-baseline/rak90-config-20260912.json`).

## 5. Why they are still in the tree

- The nRF52 settings were persisted as a **raw memory image** of the struct and the BLE settings
  characteristic ships `sizeof(s_meshcom_settings)` bytes with `setFixedLen` (`nrf52_ble.cpp:296`).
  Both fields sit _in the middle_ of the struct, between `node_opwd` and `node_hamnet_only`, so
  deleting them would have shifted ~200 fields for every RAK in the field and broken the app's
  struct mirror. That is exactly the "struct is the format" defect that W3 removes.
- Under the W3 keyed store they are two `X()` rows in `CFG_FIELD_LIST_PLATFORM`
  (`config_json.h:358-359`) with their member names as keys. Unknown keys are ignored on read, so
  dropping the rows later is safe on the nRF52 side. The 16-character key `send_repeat_time` would
  exceed the 15-character NVS limit only if it ever went to ESP32; it will not.
- The BLE characteristic still serves today's byte layout from the frozen snapshot until the app is
  updated, so the _members_ stay until that cutover even after the _rows_ go.

## 6. What is transferable to our project

### 6a. Event-driven loop with wake reasons (the real gem)

The WisBlock loop sleeps on a semaphore and is handed a bit mask that says _why_ it woke. Our nRF52
loop polls. Consequences we already pay for: busy CPU on battery RAKs, CONC-17-style "apply the BLE
write from the loop task" workarounds that would fall out naturally from a `BLE_CONFIG` event, and
no single place to say "the radio handed us a frame". A MeshCom node cannot sleep as deeply as a
LoRaWAN sensor because it must stay in RX for the mesh, so the power win is smaller than RAK's
marketing numbers, but the _structure_ (every producer calls `api_wake_loop(REASON)`, the loop
dispatches on the mask) is worth adopting when the nRF52 main loop is next reworked. The plumbing
is already compiled in; `api_wait_wake()` just has to replace the polling tail of `loop()`, and
`OnRxDone` in the LORA task has to `api_wake_loop(LORA_DATA)` instead of the loop scanning the
ring.

### 6b. One periodic scheduler instead of N timers

`send_repeat_time` is _the_ send interval; the app decides on each STATUS tick what to send. We have
`node_postime` (position beacon, minutes, floor 5), `node_ptime` (telemetry), heartbeat, WX, each
with its own `millis()` arithmetic in different files. The audit's DRY list already points at this
duplication. A single tick with per-type due-times is the WisBlock shape; it also gives one place to
enforce the airtime budget before anything periodic goes out.

### 6c. Attended vs autonomous mode (the `auto_join` idea without the join)

The flag really encodes "is a human expected to connect to this node?". The BLE-advertising
timeout that hangs off it is directly useful for battery RAK nodes: advertise for a window after
boot or after a button press, then stop, and re-arm on the button. Today every RAK advertises
forever. This is a small, self-contained fork feature (`--ble timeout <s>` or a `node_sset` bit)
and needs no protocol change.

### 6d. Join as a gate before the first transmission

LoRaWAN refuses to transmit without a session. MeshCom has one comparable gate, the `XX0XXX`
default-callsign TX block (TX-01), and none for "am I reachable / registered". The DM
store-and-forward concept's _presence_ announcement plays the role of the join accept (a peer learns
you are reachable) without the crypto. The key-derivation half of OTAA has no analogue as long as
MeshCom has no per-node keys; if authenticated DMs ever come, note that NimBLE's mbedtls on
arduino-esp32 lacks CMAC, so the primitive would have to come from elsewhere.

### 6e. The cautionary tale

Two fields nobody used for three years survived because the struct was the wire and disk format.
Any future "just add a field to the struct" on nRF52 has the same cost. Under W3, add an `X()` row
and a default, never a positional member with meaning.

## 7. Recommendation

- Keep both fields untouched until the W3 keyed store is the only reader and the app no longer
  mirrors the struct; then delete the two `X()` rows and the members together with the orphaned
  `api_timer_*`, `g_lpwan_has_joined`, `g_join_result`, `otaaDevAddr` and `LORA_JOIN_FIN`.
- Do not create ESP32 equivalents.
- Open a backlog item for 6a (event-driven nRF52 loop) and a small one for 6c (BLE advertising
  window); 6b is already covered by the DRY audit's scheduler unification.
