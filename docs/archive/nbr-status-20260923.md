# Neighbour matrix: status 2026-09-23 20:20 and what to check after 20:01

## BLUF

- Both nodes have run build `Sep 23 2026 / 19:59:40` since 20:01 CEST. That build contains two fixes: the
  case-B hold is a one-time deadline (`301d9b08`), and text frames feed only "I heard the last
  hop" (`8487ea2a`).
- By construction the matrix can now only hold **hop 1** (heard by me over the air) and **hop 2**
  (heard by one of my hop-1 neighbours, taken from a POS or HEY path). Server traffic injected by
  gateways can no longer create rows.
- First check at 20:17 passed: 9 rows (3 hop 1, 6 hop 2), none server-borne, 0 text edges; the
  server sent only text. One checker run over the night answers everything below:
  `tools/nbrhopcheck.py`, see check 1.
- Evaluate **from 2026-09-23 20:01**. The reboot cut the case-B soak (started 19:24) after about
  40 min, so both fixes are judged together from this point on.

## Where we stand

| Item         | State                                                                                                                                                    |
| ------------ | -------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Branch       | `feature-neighbour-matrix`, HEAD `518c1786` (docs) on `8487ea2a` (text rule) on `301d9b08` (case-B deadline). Not pushed.                                |
| DK5EN-98     | Heltec V3, 192.168.68.68, gateway + mesh. `--nbrrelay on` (real cancels), `--nbrsym on`, `--nbrreport on` (HN every 15 min).                             |
| DK5EN-1      | Bench Heltec, 192.168.68.71, 2 dBm, gateway on, `--nbrrelay count`. Receives the HN reports.                                                             |
| Pi capture   | `rpizero.local:~/meshlog/dk5en-98/`, flags nbrdebug + loradebug + txcapture. **The 48 h run ends 2026-09-24 about 09:28**, restart it for a longer soak. |
| Leaf capture | Laptop, `~/meshlog/dk5en-1/2026-09-22.log` (appended, still running, `pgrep -f serial_capture`). Opening that USB port reboots the leaf.                 |
| Campaign doc | `docs/nbr-stage2-campaign.md` (waves 0-6), log contract `docs/nbr-logformat.md`.                                                                         |
| Old reports  | Moved to `docs/archive/` (field runs 1-3, stage-2 summary), indexed in `docs/archive/README.md`.                                                         |

### Why the table was polluted, and what changed

A gateway with mesh on puts server frames on LoRa with the path `<server path>,<gateway>`. The
pair before the gateway was never an RF reception, but the matrix counted it as "the gateway heard
X". In 34 h the server pushed 511 frames to DK5EN-98, **all text**, no POS, no HEY. 79 edges came
only from text, and every one ended at an injecting gateway (DL2JA-2 57, DK5EN-1 22). The on-air
server bit cannot tell injection from a normal gateway relay, so the frame type is the only usable
marker.

Since `8487ea2a` a text frame only records "I heard its last hop". It no longer adds path edges,
creates no row for earlier path tokens, and writes no `CUT` line. POS and HEY are unchanged. HEY
signal groups and HN reports never create rows, so rows now come from exactly two places:

- the last hop of any frame: **hop 1**
- the second-to-last token of a POS or HEY path, heard by that last hop: **hop 2**

## What to check after 20:01

Fetch the logs and run the commands below. They work in zsh and bash; the run crosses midnight,
so add the second day's file once it exists.

```sh
mkdir -p ~/nbrcheck && cd ~/nbrcheck
scp rpizero.local:'~/meshlog/dk5en-98/2026-09-2[34].log' .
R=~/WebDev/MeshCom-Firmware-DEV-Main
F=(2026-09-23.log 2026-09-24.log)
since() { awk '$1" "$2 >= "2026-09-23 20:01"' "${F[@]}"; }

# 1  table holds only hop 1 / hop 2, no server callsign
python3 $R/tools/nbrhopcheck.py --since 2026-09-23T20:01 "${F[@]}"
# 2  text edges
since | grep -cE '\[NBR\]\|EDGE\|[^|]*\|[^|]*\|[^|]*\|T\|'
# 3  text still counts as a direct reception
since | grep -cE '\[NBR\]\|ME\|[^|]*\|[^|]*\|T\|'
# 4  frame types the server pushes down
since | grep -oE 'RX-UDP +[0-9]+ \S' | awk '{print $3}' | sort | uniq -c
# 7  ring drops
since | grep -oE 'RING_DROP_[A-Z]+' | sort | uniq -c
# 8  relay wait in the ring, section "eigene Relays (TX src=r) je Typ, Wartezeit im Ring"
python3 $R/tools/nbrrelay.py --since 2026-09-23T20:01 --own DK5EN-98 "${F[@]}"
# 9  cancels
since | grep -c '\[NBR\]|CANCEL|'
# 10 HN reports: logged, transmitted, received at the leaf
since | grep -c '\[NBR\]|RPTTX|'
since | grep -c 'TX-LoRa .*>HN@'
awk '$1" "$2 >= "2026-09-23 20:01"' ~/meshlog/dk5en-1/2026-09-22.log | grep -c 'RPTSUM|[^|]*|DK5EN-98|'
# 11 reboots
since | grep -cE 'rst:0x|Guru|\[BOOT\]'
```

Result at 20:20 (19 min of data): 1 PASS, 2 = 0, 3 = 14, 4 = only `:`, 7 = none, 9 = 1,
10 = 1/1/1, 11 = 0.

| #   | Check                            | Pass                                                                                                                   | If it fails                                                                                              |
| --- | -------------------------------- | ---------------------------------------------------------------------------------------------------------------------- | -------------------------------------------------------------------------------------------------------- |
| 1   | Table shows only hop 1 and hop 2 | `PASS`, every snapshot `Befunde 0`                                                                                     | `FAIL: kein direkter Hoerer` = orphan row; `<-- SERVER` = a server-borne callsign has a row              |
| 2   | No text edges any more           | `0`                                                                                                                    | old firmware running: check the build string on the web page                                             |
| 3   | Text still counts as direct      | greater than 0                                                                                                         | the ME step for text is broken                                                                           |
| 4   | Server sends only text           | only `:`                                                                                                               | `!` or `@`: the server now pushes POS/HEY, the text rule no longer suffices, and nothing on air marks it |
| 5   | Direct neighbours unchanged      | web page `http://192.168.68.68/?page=neighbours`: DB0ED-99, DK5EN-1, DL2UD-1, DL2JA-2, DL2JA-1 are D after a few hours | a former D that stays I had text as its only evidence, which contradicts the 34 h analysis               |
| 6   | No server-borne callsign         | none of OE1XAR-33, DO2QG-1, DM3KS-12, DL1GFM-7, HB9JAY-6, HB9VQQ-1, DF2AP-99, HB9HDI-5 on the page                     | see check 4                                                                                              |
| 7   | Case-B relays no longer dropped  | about 0 (149 in 9.3 h before `301d9b08`)                                                                               | the ring still clogs: look up the `NEED` line of each dropped msg_id                                     |
| 8   | Case-B wait capped               | POS and HEY p90 and max around 60-70 s (09:20-20:00 today: POS p90 431 s / max 966 s, HEY p90 129 s / max 181 s)       | the deadline or the short search is not effective                                                        |
| 9   | Cancels still happen             | well above 0; the day before the fix cancelled 62 % of case-B relays                                                   | cancelling broke; compare with the web page line "Relay mode: on -- ... cancelled"                       |
| 10  | HN reports go out and arrive     | all three counts equal, about 4 per hour                                                                               | RPTTX without TX-LoRa = dropped in the ring (wave-5 defect); sent but not received = RF or leaf problem  |
| 11  | No reboot                        | 0, and `reconnects` in `~/meshlog/dk5en-98/status.txt` on the Pi stays at 7                                            | read the lines around the hit                                                                            |

### Result 2026-09-24 07:05 (11 h after the flash): all 11 checks pass

| #   | Result                                                                                                                                                         |
| --- | -------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| 1   | PASS, 44 of 44 snapshots with 0 findings. Last snapshot: 20 rows, 5 hop 1, 15 hop 2.                                                                           |
| 2   | 0 text edges                                                                                                                                                   |
| 3   | 697 text ME lines                                                                                                                                              |
| 4   | server sent 238 frames, all text                                                                                                                               |
| 5   | DL2JA-2, DK5EN-1, DL2UD-1, DB0ED-99, DL2JA-1 are D                                                                                                             |
| 6   | none of the server callsigns in the table                                                                                                                      |
| 7   | 0 ring drops (was 149 in 9.3 h)                                                                                                                                |
| 8   | own relays: POS p90 42 s / max 65 s, HEY p90 42 s / max 72 s (was POS max 966 s)                                                                               |
| 9   | 208 cancels, 0 REFUSE; 661 case-B and 81 case-A relay decisions                                                                                                |
| 10  | 44 RPTTX = 44 on air, 41 at the leaf. All 3 misses: the leaf was transmitting its own relay of the same DB0FHR-12 HEY at that moment (half duplex), not a loss |
| 11  | 0 reboots, logger reconnects still 7                                                                                                                           |

Observations, not failures:

- **DL2JA-2 stays "Super" with #X 8**, contrary to the expectation below. The 8 are now real hop-2
  nodes that only DL2JA-2 hears (e.g. OE2XZR-12, OE7XWT-12, DG3MNF-4). For OE7XWT-12 the RF path is
  proven: DL2JA-2 appends its own signal report to the relayed HEY (`R0;17,119,-20`).
- **The table is full**: 20 foreign rows of 20, 16 evictions since 20:01, all among hop-2 rows.
  `NBR_MAX_ROWS` (21) is the limit now, not pollution.
- **Fewer cancels than on the first `on` day**: 208 of 661 case-B relays (31 %, day one 62 %),
  own relays 48/h (day one 22/h, count night 72/h). That is the price of the 60 s cap: a relay
  has less time to overhear a covering repeat before it goes out. Day one's higher share came
  with minutes of waiting and 149 drops.

### Expected changes that are not failures

- ~~DL2JA-2 loses its "Super" role~~ (wrong, see the result above). Its 8 exclusive nodes in the
  18:00 screenshot were mostly server-borne rows (OE1XAR-33, DO2QG-1, DM3KS-12, DL1GFM-7).
- **The "Hears me" column has fewer entries.** The echo of my own text (`DK5EN-98,X`) no longer
  counts as "X hears me", because another gateway can inject my uploaded text the same way. POS,
  HEY and the HN reports still provide it.
- **Fewer `CUT` lines**: text no longer uses the 2-hop window.
- **Right after the reboot, a direct neighbour can show as I (hop 2) for a while**, until the node
  itself is heard again (DL2JA-2 at 20:13 and 20:17). Judge check 5 after a few hours, not minutes.

### Comparing with older captures

Captures before 20:01 still contain text `EDGE` lines. Filter them before comparing old and new
runs, since none of the scripts has a switch for it:

```sh
grep -vE '\[NBR\]\|EDGE\|[^|]*\|[^|]*\|[^|]*\|T\|' old.log > old-without-text.log
```

## Remaining risk

The rule assumes the server keeps sending only text to gateways. The gateway firmware still
accepts POS and HEY from the server (`src/esp32/udp_frame_esp32.cpp`), and nothing on air would
mark them. Check 4 is the tripwire for that.

## Server-borne callsigns seen in the 34 h before the fix

These appeared only in text and never in a POS or HEY heard over RF. `tools/nbrhopcheck.py`
carries this list, and also derives new ones from each capture (server copies at DK5EN-98 that
never appear in an RF POS or HEY).

OE1XAR-33 (server `{CET}` time beacon), DO2QG-1, DO5NE-1, DM3KS-12, OE1XAR-62, DC4FRT-3, DL2XL-5,
HB9VQQ-1, DL1UDO-12, DF2AP-99, DO2QG-99, DK6IX-12, DK3ACH-12, DG3RAP-12, DO5DMF-8, DK4DO-1,
DH1FR-1, DJ9DQ-1, DO7FJK-99, DL1MX-12, DL1HRU-7, DL9CL-9, DO5DMF-99, DJ2AX-99, DK6OK-12,
IS0QLX-11, DO1TFS-99, OE1KFR-1, DO7FJK-12, TG5ALY-8, DL9UW-01, IW2NKC-10, IU5SNJ-12, OE6BYD-12,
DF4ND-99, DF1BO-12, DC8CE-12, DL6WAB-1, OE5ANI-13, DK1JZ-12, OE6DJG-2, F6JON-13, DB2GS-1,
IS0HXK-8, DD3AT-99, OE4AZU-10, DC2JR-2, DH1FR-2, DL4AWI-7, DL2XL-15, IU5SNJ-21, DK8JP-12,
IW1QQG-64, DL8NDG-7, DL1GFM-7, OE5BKO-9, DM2FK-86, DL1EEN-12, IQ8KL-15, DL1RI-15, DL2YED-99,
DL4MGC-12, DD9FH-1, DB1CDD-1, DK9ZZ-12, DL1ABR-12, DM5SR-13, plus HB9JAY-6 and HB9HDI-5 (23.09. evening).
