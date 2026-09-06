# Two follow-up bugs found while bench-testing issue #962 (deepsleep)

Repo: `/Users/martinwerner/WebDev/MeshCom-Firmware-DEV-Main` (MeshCom Firmware fork, branch
`fork-main`). Found 2026-09-06 while bench-verifying the `--deepsleep` fix for issue #962 on real
hardware (T-Beam, RAK4631, Heltec V3, T-Deck Plus). Background/design doc for the parent work:
`docs/issue-962-deepsleep-verdict.md`. The deepsleep fix itself is committed and bench-verified
clean on all four boards above (commits `e242a4cf`, `37537b11`, `828a2567`); these two issues are
separate, **not required** for that fix to be correct, and were never fixed as part of it.

Both were found by dispatching an independent advisor (Fable) to investigate live bench symptoms,
then independently re-verifying every claim against the actual ESP-IDF headers and source before
acting — do the same before touching either of these: read the cited lines yourself, don't take
this document's word for it.

## Status 2026-09-06

- Issue 1 (CDC-02): **fixed** in `b57daf44`. Ring sized before the first `begin()`, once-flag
  for the second `begin()` on T5-ePaper/T-Deck Pro (a case this doc missed). Regression bench:
  `tools/bench/tdeck_cdc_portopen.py`. The 50-iteration bench run is still open (no T-Deck Plus
  on USB at fix time); BACKLOG row `CDC-02`.
- Issue 2 (DS-02): **fixed blind** in `f7801a1c` (`gpio_hold_dis(PIN_LORA_NSS)` before the radio
  init in `esp32setup()`, compile-verified on both envs). Still unverified on hardware; BACKLOG
  row `DS-02` carries the field-test ask.

---

## Issue 1: HWCDC ring-buffer race crashes native-USB ESP32-S3 boards on port-open reset

**Severity: real, reproducible crash. Not related to deepsleep — pre-existing, from an earlier,
unrelated same-day commit (`f3fddc91`, "CDC-01").**

### Symptom

On ESP32-S3 boards with native USB (T-Deck, T-Deck Plus — anything using `HWCDC`/USB-Serial-JTAG
instead of a CP2102/CH9102 USB-UART bridge chip), simply **opening the serial port** — which
resets these boards unconditionally (`rst:0x15 USB_UART_CHIP_RESET`, documented board behavior,
nothing to do with this bug) — can trigger:

```
assert failed: xRingbufferReceiveUpToFromISR ringbuf.c:1269 (pxRingbuffer)
```

followed by a crash-loop (2-3 reboots, each logging
`esp_core_dump_flash: Not enough space to save core dump!` — cosmetic, the 4 KB coredump partition
can never hold a real ELF dump, ignore that part), before finally booting clean with
`RESET_REASON=4 PANIC`. Reproduced twice on a T-Deck Plus (`DK5EN-14`, env `t_deck_plus`) purely
from a host script (re)opening `/dev/cu.usbmodem*` — no `--deepsleep` involved either time.

A secondary, likely-related symptom seen alongside the crash-loop: SD card and the keyboard's I2C
controller both failed to initialize (`SDCard: ERROR`, `Keyboard: ERROR`,
`[E][Wire.cpp:513] requestFrom(): i2cRead returned Error -1`) and **stayed failed across every
subsequent soft reset** until a full USB unplug/replug power cycle. This may be a separate,
hardware-level effect (an SD card or I2C peripheral interrupted mid-transaction by a rapid
reset burst can wedge until VCC is cycled — nothing in the boot sequence recovers from this;
`checkKb()`/`Wire.begin()`/`SD.begin()` all just do a single blind attempt, no bus-clear or retry)
rather than something this specific race causes directly. Worth re-testing after Issue 1 below is
fixed to see if it still occurs, since the race being gone removes the rapid-reset-burst trigger.

### Root cause

`xRingbufferReceiveUpToFromISR` is framework code, not this repo's — the only caller is inside
`hw_cdc_isr_handler()` in the installed Arduino-ESP32 core:
`~/.platformio/packages/framework-arduinoespressif32@3.20014.231204/cores/esp32/HWCDC.cpp:92`,
on the `SERIAL_IN_EMPTY` interrupt (fires when the host has just drained the TX FIFO). It calls
`xRingbufferReceiveUpToFromISR(tx_ring_buf, ...)` with **no NULL check**; IDF's ring buffer code
asserts on a NULL `pxRingbuffer` at `ringbuf.c:1269` — exactly the message seen.

This repo creates the race window itself. `src/net_console.cpp` (`MeshSerialClass::begin()`,
around line 225-241) and `src/esp32/esp32_main.cpp` (around line 641-649, the
`DISABLE_NET_CONSOLE` path) both do this, in this order:

```cpp
s_hwSerial.begin(baud);          // HWCDC::begin() — already creates a 256 B tx_ring_buf
                                  // and enables the SERIAL_IN_EMPTY ISR before returning
...
s_hwSerial.setTxBufferSize(4096);
s_hwSerial.setTxTimeoutMs(0);
```

`HWCDC::begin()` (`HWCDC.cpp:171-197`) already does, internally, `if (tx_ring_buf == NULL)
setTxBufferSize(256)` **and then enables the ISR** before returning. Then this repo's own
`setTxBufferSize(4096)` call runs _after_ the ISR is already live:

```cpp
size_t HWCDC::setTxBufferSize(size_t tx_queue_len){   // HWCDC.cpp:228-241
    if(tx_ring_buf){
        vRingbufferDelete(tx_ring_buf);
        tx_ring_buf = NULL;               // <-- ISR can fire here, tx_ring_buf is NULL
    }
    ...
    tx_ring_buf = xRingbufferCreate(tx_queue_len, RINGBUF_TYPE_BYTEBUF);
    ...
}
```

Between the `vRingbufferDelete`/`tx_ring_buf = NULL` and the `xRingbufferCreate` a few lines later,
`tx_ring_buf` is NULL while the `SERIAL_IN_EMPTY` interrupt is still enabled and can fire — and it
reliably does exactly when a host is actively polling the CDC IN endpoint at that moment, which is
precisely what happens on a port-open (the host starts polling immediately; the device's own
`begin()`→`setTxBufferSize()` sequence runs microseconds later during boot). That is why the crash
is always `rst:0x15` (the reset that happens because a host opened the port) and never a
sleep-related reset, and why it happens before any application code (including `--deepsleep`) has
run.

### Fix

Reorder: call `setTxBufferSize(4096)` **before** `begin()`, not after. `HWCDC::begin()` explicitly
honours a pre-set buffer (its own comment: "TX Buffer default has 256 bytes if not preset",
`HWCDC.cpp:182-186` — it only calls `setTxBufferSize(256)` itself `if (tx_ring_buf == NULL)`), and
at that point in boot the ISR does not exist yet, so there is no race window at all.

Two call sites to fix, both in this repo:

- `src/net_console.cpp` — `MeshSerialClass::begin()`, currently:

  ```cpp
  void MeshSerialClass::begin(unsigned long baud)
  {
      s_hwSerial.begin(baud);
  #if ARDUINO_USB_CDC_ON_BOOT && ARDUINO_USB_MODE
      s_hwSerial.setTxBufferSize(4096);
      s_hwSerial.setTxTimeoutMs(0);
  #endif
  }
  ```

  Move the `#if` block's `setTxBufferSize(4096)` call to before `s_hwSerial.begin(baud)`.
  `setTxTimeoutMs(0)` has no ordering dependency, can stay where it is or move too, doesn't matter.

- `src/esp32/esp32_main.cpp`, around line 641-649 (the `DISABLE_NET_CONSOLE` path, same pattern
  applied directly to `Serial` instead of the `net_console` wrapper):
  ```cpp
  Serial.begin(MONITOR_SPEED);
  Serial.setTimeout(50);
  #if ARDUINO_USB_CDC_ON_BOOT && ARDUINO_USB_MODE && defined(DISABLE_NET_CONSOLE)
      Serial.setTxBufferSize(4096);
      Serial.setTxTimeoutMs(0);
  #endif
  ```
  Same fix: move `setTxBufferSize(4096)` before `Serial.begin(MONITOR_SPEED)`.

### Verification plan

1. Apply the reorder at both sites.
2. Build `t_deck` and `t_deck_plus` (the only two envs with native USB CDC that exercise this
   path — `ARDUINO_USB_CDC_ON_BOOT && ARDUINO_USB_MODE` needs to actually be set for these two
   envs; check `variants/t_deck/platformio.ini` / `variants/t_deck_plus/platformio.ini` build
   flags to confirm, and check whether any other ESP32-S3 board also sets those flags — if so it's
   in scope too).
3. Flash a T-Deck or T-Deck Plus, then rapidly open/close its serial port from a host script
   ~30-50 times in a tight loop (a plain `pyserial` open+close, no delay needed between attempts —
   that's what triggered it originally). The assert must not reappear. Before the fix, it was
   observed on roughly 2 out of a handful of attempts, so a single clean run is not enough
   evidence — do the full 30-50 iteration loop.
4. Re-check whether the SD/keyboard wedge-until-power-cycle symptom still occurs during that same
   loop. If it's gone too, it was downstream of this race (the crash-loop's rapid resets were the
   trigger). If it still happens even without any crash, it's a separate, hardware-level issue with
   no known firmware fix (see "Symptom" above) — document it as such rather than chasing it further.
5. Full 32-env build matrix (`pio run -e <env>` for every env in `platformio.ini`'s `default_envs`)
   must stay green — this touches two files shared across every ESP32 board's boot path, not just
   the two S3-native-USB ones (the changed code is inside `#if` guards that only compile for
   `ARDUINO_USB_CDC_ON_BOOT && ARDUINO_USB_MODE`, so it should be a no-op for CP2102/CH9102 boards,
   but verify rather than assume).

---

## Issue 2: Wireless Paper / Vision Master E213 likely have the same "gpio hold never released" bug — unverified, no bench hardware

**Severity: potentially high (radio permanently broken after first deep sleep) but UNCONFIRMED —
no Wireless Paper or Vision Master E213 unit was available to bench-test. This is a plausible bug
found by code inspection, not a reproduced one. Verify on real hardware before trusting the
analysis below.**

### Background

Commit `828a2567` (same repo, same day) fixed a confirmed bug on T-Deck/T-Deck Plus and
T-Beam-1W: `gpio_hold_en()` on a GPIO **survives the wake-up reset** from deep sleep (per the
ESP-IDF `driver/gpio.h` documentation: "It can be used to retain the pin state through a core
reset and system reset triggered by watchdog time-out or Deep-sleep events") and is **only**
cleared by an explicit `gpio_hold_dis()` call or a full power-down — not by simply waking up and
rebooting. Any board that calls `gpio_hold_en()` before sleeping and then never calls
`gpio_hold_dis()` on boot will have that pin permanently stuck at its held level after the first
sleep/wake cycle, until a physical power cycle.

The working template for doing this correctly already exists in this tree:
`src/t5-epaper/t5epaper_main.cpp:579-581`, in a function called at boot
(`idf_setup()`, confirm exact caller before editing anything):

```cpp
gpio_hold_dis((gpio_num_t)BOARD_TOUCH_RST);
gpio_hold_dis((gpio_num_t)BOARD_LORA_RST);
gpio_deep_sleep_hold_dis();
```

called _before_ those pins are reconfigured/re-driven for normal operation.

### The suspected bug

`src/Platforms/WirelessPaper/power_controls.cpp` and
`src/Platforms/VisionMasterE213/power_controls.cpp` (nearly identical files, one per board) each
have a `prepareToSleep()` function that does, among other things:

```cpp
// WirelessPaper/power_controls.cpp:88-90 (VisionMasterE213/power_controls.cpp:83-85 is the same)
pinMode(PIN_LORA_NSS, OUTPUT);
digitalWrite(PIN_LORA_NSS, HIGH);
gpio_hold_en((gpio_num_t) PIN_LORA_NSS);    // "stay where you're told"
```

**Searched the entire `src/` tree: there is no `gpio_hold_dis()` call anywhere for
`PIN_LORA_NSS`, on either board, anywhere in the boot path.** (The only `gpio_hold_dis()` calls
in the whole repo are the three in `t5-epaper/t5epaper_main.cpp` quoted above — that's a
different board, `BOARD_T5_EPAPER`, which isn't even in `platformio.ini`'s `default_envs` list,
i.e. not built by default.)

If the ESP-IDF hold-survives-wake-reset behavior applies here the same way it did for
`TDECK_POWERON`/`RADIO_LDO_EN`, then after the **first** `--deepsleep` + wake cycle,
`PIN_LORA_NSS`'s output would be permanently force-locked HIGH. This pin is the SX1262's SPI
chip-select, driven via bit-banged software SPI (see `loraToSleep()`, same file,
lines 45-66) — **every** subsequent radio SPI transaction needs to pull it LOW to select the chip.
Concretely, `loraToSleep()` itself would break on the _second_ sleep:

```cpp
void loraToSleep() {
    digitalWrite(PIN_LORA_NSS, HIGH);   // no-op if already held high — harmless
    ...
    digitalWrite(PIN_LORA_NSS, LOW);    // <-- silently ignored if the pin is held from
                                         //     the PREVIOUS sleep cycle: NSS never goes low,
                                         //     the SX1262 never sees a valid chip-select,
                                         //     the "enter sleep" command is never received
    shiftOut(...); shiftOut(...);
    digitalWrite(PIN_LORA_NSS, HIGH);
}
```

and the same failure would apply to whatever the main (non-sleep) LoRa TX/RX SPI driver code
does with this pin elsewhere (not investigated as part of this discovery — find it and check).

Net effect, if this is real: on Wireless Paper and Vision Master E213, LoRa communication would
work fine up through the first `--deepsleep`, but **after waking from that first sleep, the radio
would never work again** (can't be selected over SPI) until a full USB/battery power cycle. Given
these boards' whole selling point is battery-powered field use with frequent sleep cycles, this
would be a serious defect if confirmed — and would explain a battery-powered Wireless Paper
appearing to "go deaf" on LoRa after its first nap, which is the kind of symptom that's easy to
misattribute to something else (bad antenna, out of range, etc.) in the field.

### Why this wasn't caught before

The doc this work descends from (`docs/issue-962-deepsleep-verdict.md`, §2.2) describes the
Wireless Paper / Vision Master E213 sleep path as "already fixed by PR 1050" and treats it as a
correct reference template for the rest of the deepsleep fix (which is accurate for _entering_
sleep — the radio-off/display-clear/wake-arm sequence on the way down is fine). The bench
verification table in that same doc (§4.5) checks "sleep current", "button wake", "OLED dark",
and "1053 regression" — it does not have a test row for "does the radio still work after waking
from a second sleep", which is exactly the gap this bug would hide in.

### What to do

1. **Get a Wireless Paper or Vision Master E213 unit on the bench.** This cannot be verified by
   static analysis alone — the whole point is confirming actual SPI/radio behavior across two
   consecutive sleep cycles.
2. Confirm the exact ESP32 chip family these boards use (`BOARD_WIRELESS_PAPER` / `BOARD_E213` —
   check `variants/wireless-paper/configuration.h` and `variants/vision-master-e213/configuration.h`)
   is ESP32-S3, and confirm `PIN_LORA_NSS`'s actual GPIO number for each board, to check whether
   it's within the S3's RTC/LP-GPIO range (0-21) or not — this determines whether a bare
   `gpio_hold_en()` is even sufficient to survive real Deep-sleep, or whether (like
   `TDECK_TFT_BACKLIGHT`/GPIO42 in the T-Deck fix) it also needs a paired
   `gpio_deep_sleep_hold_en()` call that isn't there either. Don't assume; check.
3. Bench test: flash either board, do `--deepsleep`, wake it (button), confirm the radio still
   receives/transmits (e.g. `--mheard` against a peer node broadcasting, or `--sendpos` observed on
   a peer's `--mheard`). If it fails, do `--deepsleep` → wake **again** and see if it's already
   broken after the _first_ cycle as predicted, to nail down exactly when it breaks.
4. If confirmed, fix by mirroring `t5epaper_main.cpp`'s pattern: add
   `gpio_hold_dis((gpio_num_t) PIN_LORA_NSS); gpio_deep_sleep_hold_dis();` (the second call only
   needed if step 2 shows it's actually required) to whatever function runs at boot before the
   radio is first used on each board — find the WirelessPaper/VisionMasterE213 equivalent of
   T-Deck's `initTDeck()` (likely in `src/esp32/esp32_main.cpp` or a board-specific init file under
   `src/Platforms/`) and add the release there, before the first `pinMode(PIN_LORA_NSS, ...)` of a
   normal boot.
5. Full 32-env build matrix must stay green after the fix, same as every other change in this
   family of bugs.

---

## General notes for whoever picks this up

- Both issues were found using the same method: an independent advisor investigation followed by
  the orchestrator re-verifying every claim directly against source/documentation before acting.
  Don't skip the re-verification step just because this document exists — re-check the file:line
  references above are still accurate (the tree may have moved on) before writing any fix.
- Bench boards available as of 2026-09-06: T-Beam v1.2 (`DK5EN-92`), RAK4631 (`DK5EN-90`),
  Heltec V3 (`DK5EN-93`), T-Deck Plus (`DK5EN-14`) — see project memory `bench-fleet-ports` for
  ports/identification. **No Wireless Paper or Vision Master E213 unit is on the bench**, which is
  the whole reason Issue 2 is unconfirmed.
- T-Deck Plus's native USB-Serial/JTAG resets on every port-open — expect this, don't read it as a
  new fault, and don't hammer the port with rapid opens without the Issue 1 fix in place (that's
  literally what triggers the crash).
