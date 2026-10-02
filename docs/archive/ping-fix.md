# Ping feature: root cause and fix (T-Beam 1W report)

Date: 2026-09-05
Branch: fork-main
Symptom reported: on a LilyGo T-Beam 1W, `--ping start` logs "[PING]...send Ping to <call>" but the neighbor node never receives anything over LoRa.

## Verdict

The ping code contains no board-specific path. The T-Beam 1W is not broken; the node is in
track mode, and `sendPing()` returns silently in track mode after the scheduler has already
printed its success line. Track mode is the natural state for a GPS tracker board, which is why
the bug looks like a T-Beam problem.

## Call chain

| Step | File and line                        | What happens                                                  |
| ---- | ------------------------------------ | ------------------------------------------------------------- |
| 1    | `src/esp32/esp32_main.cpp:3976-3995` | Scheduler fires every `node_pingtime` s while `pingcount > 0` |
| 2    | `src/esp32/esp32_main.cpp:3989`      | Prints "[PING]...send Ping to <call> <count>" unconditionally |
| 3    | `src/esp32/esp32_main.cpp:3993`      | Calls `sendPing()`                                            |
| 4    | `src/loop_functions.cpp:3290`        | `if(bDisplayTrack) return;` with no log, no display, no TX    |
| 5    | `src/esp32/esp32_main.cpp:3995`      | `node_pingcount--` regardless, so five "sent" lines then stop |

The nRF52 scheduler (`src/nrf52/nrf52_main.cpp:2557-2572`) has the same structure.

## How track mode gets switched on

- `--track on` (persisted as `node_sset & 0x0020`, survives reboot)
- Web setup page, track switch
- Triple click on the user button (`src/onebutton_functions.cpp:219`), which also turns GPS on

## Confirm on the affected node

1. `--info` prints a line `...Track: on/off`. If it says on, that is the cause.
2. `--track off`, then `--ping start`. With `--info on` the node should now print `NEW-PING`
   followed by `TX-APRS:` for the frame.
3. If track is already off and `NEW-PING` is still missing, the fault is elsewhere. No silent
   drop path exists in `addTxRingEntry()` or `doTX()`, so ordinary text messages would then fail
   as well.

## Secondary defects found on the way

- Interval clamp mismatch. `--pingtime` (`src/command_functions.cpp:3671`) accepts 15 s and
  above, but the scheduler only runs for `node_pingtime > 29`. Values 15 to 29 are accepted,
  saved, and never fire, with no log line at all.
- Counter burns down without a send. The decrement lives in the caller, not behind a successful
  enqueue, so the silent return still consumes all attempts.

## Proposed fix (minimal, upstream-safe)

### 1. Move the track check ahead of the log line, in both schedulers

`src/esp32/esp32_main.cpp` (and the mirror block in `src/nrf52/nrf52_main.cpp`):

```cpp
if((int32_t)(millis() - (resendPing + meshcom_settings.node_pingtime * 1000)) > 0)
{
    resendPing = millis();

    if(bDisplayTrack)
    {
        printfdeb("[PING]...suppressed, track mode on (--track off to enable)\n");
    }
    else
    {
        if(bPingSend)
        {
            printfdeb("[PONG]...fail from %s\n", meshcom_settings.node_pingcall);
            PongFail(meshcom_settings.node_pingcall);
        }

        printfdeb("[PING]...send Ping to %s <%i>\n", meshcom_settings.node_pingcall, meshcom_settings.node_pingcount);
        sendPing(meshcom_settings.node_pingcall);
        meshcom_settings.node_pingcount--;
    }
}
```

The early return inside `sendPing()` stays as a backstop.

### 2. Align the interval clamp with the scheduler

`src/command_functions.cpp:3671`: change `< 15` to `< 30`, and update the help text at
line 871 accordingly. Alternatively lower the scheduler threshold from `> 29` to `>= 15` in
both mains; pick one, not both.

### 3. Optional: refuse `--ping start` in track mode

In the `ping start` handler (`src/command_functions.cpp:3681`) print a hint and leave
`node_pingcount` at 0 when `bDisplayTrack` is set, so the user learns about the conflict at
command time rather than 60 s later.

## Regression test

Bench on any node with `--track on`: before the fix, five "[PING]...send Ping" lines and no
`NEW-PING`; after the fix, five "[PING]...suppressed" lines. With `--track off` both builds
must print `NEW-PING` and `TX-APRS`.

## PR description (German, for upstream DEV)

Das Ping-Feature sendet im Track-Modus nichts, obwohl das Log "[PING]...send Ping" ausgibt.
`sendPing()` in `loop_functions.cpp` bricht bei gesetztem `bDisplayTrack` ohne Meldung ab,
der Scheduler in `esp32_main.cpp` und `nrf52_main.cpp` hat die Logzeile aber schon geschrieben
und `node_pingcount` dekrementiert. Betroffen sind vor allem Tracker-Boards wie der T-Beam 1W,
bei denen der Track-Modus per Dreifachklick oder `--track on` aktiv ist. Der Fix zieht die
Prüfung vor die Logzeile und gibt eine klare Meldung aus. Zusätzlich wird der Wertebereich von
`--pingtime` (bisher ab 15 s) an die Scheduler-Schwelle (ab 30 s) angeglichen, da Werte von
15 bis 29 s stumm ignoriert wurden.

## --info output vom betroffenen node

--MeshCom 4.35s (build: Sep 5 2026 / 15:16:42)
...UPDATE: 2026-09-05 16:13:52
...Call: <DM3KS-12> ...ID F6CBBEC0 ...NODE 51 <TBEAM_1W> ...UTC-OFF 2.000000 [GPS]
...BATT 8.32 V ...BATT 96 % ...MAXV 8.400 V
...TIME 23115 ms
...Flash-Version 20260724
...NOMSGALL off ...MESH on ...BUTTON (17) off ...SOFTSER off ... SOFTSERREAD off
...PASSWD <>
...DEBUG man ...DEBUG en
...DEBUG off ...LORADEBUG on ...GPSDEBUG off/0 ...SOFTSERDEBUG off
...WXDEBUG off ...BLEDEBUG off
...DisplayInfo on ...DisplayCont off ...DisplyLog on ...contrast 255
...EXTUDP on ...EXT IP 192.168.0.44 ...NOPMOTHER off
...BTCODE sagichnicht
...POWER (FLASH): 22 dBm
...APRSMC: APRSMC
...ATXT: iGate Idar-Oberst. Quad
...NAME: Sascha
...BLE : short
...DISPLAY off
...CTRY EU8
...FREQ 433.1750 MHz TXPWR 22 dBm RXBOOST on
...MAXHOP text 4 / pos 2
GC-2:9 GC-3:20 GC-4:26255 GC-5:26298 GC-6:232
...PING CALL DB0SD-22 Time:40 Max:5 Count:0

...BATTERY PIN 4 factor 1.0000

...Webserver on / Webpwd <> / Gateway on
...NETConsole on (port 2323)
...WIFI-AP off
...SSID <DM3KS> / PASSWORD <***>
...NETWORK Mode:WiFi
...hasIpAddress: yes
...IP address : 192.168.0.212
...SUBNET-MASK : 255.255.255.0
...HAMNET ONLY : true
...GW server : OE
...GW address : 192.168.0.1
...DNS address : 192.168.0.19
...UDP-HBeat : 23119
