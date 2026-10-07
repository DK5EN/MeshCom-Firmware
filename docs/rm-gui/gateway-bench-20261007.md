# Gateway bench run 20261007-083533 - analysis

Run dir: `tools/bench/runs/gwbench_20261007-083533` (not kept in git). Toolbox: `tools/bench/gwbench/`. Analyzer: `python3 tools/bench/gwbench/gwanalyze.py <run dir>` (`--trace rm:status@1>90`, `--selftest`).
Heltec = DK5EN-1 (.71), RAK = DK5EN-90 (.73), mock on the rpizero (.64).

Conventions: node log lines carry the host wall clock (local CEST, 1 s resolution; the node's own `[LOG]` stamp is UTC and ignored). Mock times come from
`mock.log` start + `udp-log.txt` offset (ms). Pi and Mac clocks agree to about 0.1 s (command DATA at the mock shows -0.1 .. +0.3 s against `t_send`). `t_send` is the
moment the driver's POST `/rmsend` returned. RM tags are shortened to 6 hex. No password or key was read or printed.

Pairing of `[RM];...` verdict lines with the frame copy they belong to is inferred by the analyzer (oldest unpaired non-duplicate command copy for that node, at most
10 s before the verdict; `ok/fail/cached` also match the counter). It reproduces both nodes' `/rmstatus` rej totals exactly (Heltec 18, RAK 18), which is the cross-check.

## 1. Why every RM command took about 2.2 s

- The first copy of every command and of every reply came over the server path: first command copy at the receiver = `srv` in 40 of 40 sent cases; first reply copy at
  the sender = `srv` in 37 of 37 answered cases. Never RF first.
- Server path is fast: command DATA reaches the mock a median 0.1 s after `t_send` (-0.1 .. 0.3), reply DATA median 0.4 s (0.2 .. 1.9) after `t_send`. The sender logs
  `[RM];reply;<dst>;ctr;<n>;verified;1` in the same or the next wall-clock second.
- The 2.2 s itself is the driver: `rm_send()` does `nap(2)` and then polls `/rmstatus`; the first poll that already holds the reply returns after 2.1 to 2.3 s (all 37
  answered cases report 2.1-2.3). It is a poll floor, not a transport latency; the true round trip is about 0.3-1 s (see the mock times).
- The RF copy needs far longer, and it is the Heltec TX queue that dominates (`[LOG] TX ... wait=` is the ring wait):
  - RAK as sender: own TX-LoRa of the command a median 9.5 s after `t_send` (max 27.7 s); first RF copy at the Heltec median 12.5 s (0.7-28.7 s), 8 of 20 inside 12 s.
  - Heltec as sender: own TX-LoRa of the command a median 110 s after `t_send` (6-173 s); first RF copy at the RAK median 113 s (15-179 s), 0 of 20 inside 12 s.
  - Reply RF TX: RAK median 9.0 s (1.4-23 s), Heltec median 304 s (128-418 s).
  - Example (case 9, 1>90 `status`): `08:36:28 TX-UDP xEA25A006` ... `08:36:43 [LOG] TX xEA25A006 wait=14535 q=4` ... RAK RF rx `08:36:44` (RSSI -45, S0, `DUP:n`).
- RF copies are not reliable either: command RF copy logged at the receiver in 17 of 20 (1>90) and 15 of 20 (90>1) cases; reply RF copy logged at the sender in
  11 of 18 and 16 of 20.
- Why the Heltec ring waits minutes is not established (see "Not established").

## 2. `name -` / `atxt -` write-back and the noanswer cases

What was sent (frame text, sender TX-UDP / receiver RX-UDP, 1>90):

```
08:38:49 RX-UDP 070 : xEA25A014 H04 S0 T0 M01 DK5EN-1>DK5EN-90:RM1 1791355128 name - 5b27ee..
08:38:51 [RM];ok;ctr;1791355128                         (flash save [SETST];save;ok;bytes=1560 at 08:38:51, was 1559)
08:38:51 TX-UDP 070 : x91A434DD ... DK5EN-90>DK5EN-1:RM1 1791355128 ok n=- 1a818a..
08:39:01 RX-UDP 068 : xEA25A015 H04 S0 T0 M01 DK5EN-1>DK5EN-90:RM1 1791355140 name 29612b..      <- the verify read
08:39:01 [RM];reject;rate
```

- Reads of an empty text print `-` (`rmfText()` in `src/rm_format.h`: empty -> `-`), and the write validator accepts `-` (`rmTextAllowed()`: `-` is an allowed character). So
  the driver's "same value" write of `-` stored the literal text `-`: the RAK's own `--info` at teardown prints `...ATXT: -` and `...NAME: -` (08:53:50; at start both were empty).
  The settings blob also grew by one byte per write (1559 -> 1560 at 08:38:51, 1560 -> 1561 at 08:40:38).
- Why the next read got no answer: `[RM];reject;rate`. `rmAccept()` stamps the rate limiter after `execute()` (the write includes the flash save), here about 2 s after the frame
  arrived (RX 08:38:49, ok 08:38:51). The next command was processed at 08:39:01, less than `RM_RATE_MS` (10 s) after the stamp. Same for the atxt pair:
  `[SETST];save;ok` at 08:40:38, next frame `[RM];reject;rate` at 08:40:48. A rate reject is silent on the air, so the driver saw `noanswer` (cases 22 and 26).
  Driver spacing was about 11.6 s send-to-send; reads after reads pass, reads after a write miss by less than a second.
- The rej deltas were not duplicates of the same frame. Every counted reject in those windows is a late RF copy of an older command (valid tag, counter below the mark = `replay`)
  or the rate reject. From the paired log:
  - case 21 (rej_d 1): `08:38:52 [RM];reject;replay` = RF copy of ctr 1791355034 (`pos`, sent 08:37:15).
  - case 22 (rej_d 3): `08:39:01 rate`, `08:39:26 replay` (ctr ..046 `sens`), `08:39:58 replay` (ctr ..058 `mh 0`).
  - case 26 (rej_d 6): `08:40:48 rate` + replay of ctr ..082, ..093, ..117, ..128, ..140 at 08:40:54, 08:41:13, 08:41:24, 08:41:40, 08:41:55.
  - case 50 (90>1, rej_d 2): `08:45:46 replay` (ctr ..520 `name`), `08:46:02 replay` (ctr ..532 `atxt`).
  - The `rej_d` windows start before `t_send` (`rm_send()` may wait on busy/cooldown before the POST that sets `t_send`), so a reject shortly before `t_send` can land in the next
    case. Case 34 (rej_d 1, executed once) has no reject inside its own window; the only candidate is the RF copy of the previous `sync` (`08:42:49 [RM];reject;rate`). That is an
    inference, not a log fact.
- `verify_restore` DIFFERENT: the compared reply was `""` (noanswer) against `ok n=-`; it did not compare two states. The state did change though (empty -> `-`).
- 90>1 `atxt MeshCom Freising (same)` (case 50), space in args is fine: the frame arrived intact and executed, but the reply was never sent:

```
08:45:45 RX-UDP 085 : x91A434F6 ... DK5EN-90>DK5EN-1:RM1 1791355544 atxt MeshCom Freising f8176e..
08:45:45 [RM];ok;ctr;1791355544
08:45:45 NEW-TXT 085 : xEA25A031 ... DK5EN-1>DK5EN-90:RM1 1791355544 ok a=MeshCom Freising f854ea..
08:45:45 [RING] full, depth 19 of 19
08:45:45 [MC-DBG] RING_DROP_NEW slot=0 prio=3 type=3A msg_id=EA25A031 (queue full, no lower prio to evict)
08:45:45 [BP];nack;QTA;dst;DK5EN-90;...;txt;QTA NOT SENT - RM1 1791355544 ok a=MeshCom Freising f854ea..
08:45:45 [RM];reply;send_failed;-2
```

`ok_d` 1, `rej_d` 2 (the two replay copies above), value unchanged, next read `ok a=MeshCom Freising`.

- Decision: driver artefact for the stored `-` and for the rate misses (placeholder write of a read-format token; pacing too close to the 10 s limit after a write). Two firmware
  findings behind it: (a) the read format cannot tell empty from `-` and the write path accepts `-`, so a generic "read, write back" loses emptiness; (b) the RM reply is lost
  without retry when the TX ring is full (case 50, command executed once, sender sees noanswer).

## 3. Negative phase

- 1>90: the three junk frames never left the Heltec. Its backpressure refused every one (user messages are refused in the QRT band):

```
08:47:52 [BP];refuse;depth;17;max;20  /  [BP];nack;QRT;dst;DK5EN-90;...;txt;QRT NOT SENT - RM1 1791355401 status 0123456789abcde0
08:48:23 ... depth;16 ... RM1 1791355402 status 0123456789abcde1
08:48:38 ... depth;15 ... RM1 1791355403 status 0123456789abcde2
```

No TX-UDP/TX-LoRa line exists for them and the RAK log contains no `abcde` text. So `rej_d 0 / 0` and `lock 0` are correct: the test sent nothing. It is void for 1>90, not
a pass; the driver does not notice a refused send. `neg:valid cmd while locked` 1>90 `ok` is therefore consistent (nobody was locked).

- 90>1: the target was not locked earlier. The earlier rejects were rate/replay only (neither counts toward the lockout; `rmCheck()`: replay and rate are not counted), and the
  driver cleared the table before the phase (`08:49:17 [RM];unlock`). Each junk frame was counted twice, once per path:

```
08:49:20 RX-UDP x91A434FC ... DK5EN-90>DK5EN-1:RM1 1791355710 status 0123456789abcde0   [RM];reject;tag   (server copy, strike 1)
08:49:38 [LOG] x91A434FC ... DUP:n   [RM];reject;tag                                      (RF copy,     strike 2)  -> x1 check: rej_d 2, lock 0
08:49:50 RX-UDP x91A434FD ... ..abcde1  [RM];reject;tag                                  (server copy, strike 3 -> lock set)
08:50:05 [LOG] x91A434FD ... DUP:n   [RM];reject;lockout
08:50:06 RX-UDP x91A434FE ... [RM];reject;lockout     08:50:15 [LOG] x91A434FE DUP:n [RM];reject;lockout
```

`neg:junk x3 lock`: rej_d 6, `lock 1`, `lockS 260` (5 min from the third strike at 08:49:50, read at 08:50:30). Strikes 1-3 came from two junk frames, not three.

- Why the RF copy is counted again: a server-ingress DM to the own call gets `RX_DEDUP_NEW` but no ring entry (all 47 Heltec and all 49 RAK server-ingress DMs/RM frames to
  the own call show `RX_DEDUP_NEW` and no `RX_DEDUP_ADD` for that msgid), so the RF copy is again `DUP:n`; the second DM dedup layer (`dmDedupCheck`, keyed on call + `{NNN`) is only inside the `{NNN` branch
  (`src/esp32/udp_frame_esp32.cpp` about line 455-464) and RM frames carry no `{NNN`. Plain DMs are saved by that layer (`[DMDUP]`), RM frames are not.
- `neg:valid cmd while locked` 90>1 = `noanswer`, `ok_d 0`: matches the design. `rmCheck()` returns `RM_REJ_LOCKOUT` for any frame of a locked sender before the tag is even
  looked at; ADR Amendments ("Lockout per sender"): per sender callsign, silent, `lockout` does not count and does not extend the lock. Log: the valid `status` (ctr 1791355830) got
  `08:50:32 [RM];reject;lockout` (server copy) and `08:50:37 [RM];reject;lockout` (RF copy), no reply. The lock was released by `08:51:48 [RM];unlock`; `neg:sync after unlock` ok.
- So for a locked sender a valid frame must get nothing (silent `lockout`). 1>90 `ok` is consistent only because that sender was never locked; the executed `status` there proves
  nothing about the lockout.

## 4. Path evidence (frame by frame; `--trace` reproduces them)

Format: wall clock (local), node, event. Mock times in ms. `+x.x` = seconds after `t_send`.

### 1>90 `status` (case 9, ctr 1791354987, command xEA25A006, reply x91A434CF)

```
08:36:28      DK5EN-1  NEW / TX-UDP xEA25A006 DK5EN-1>DK5EN-90 RM1 1791354987 status 993e4f..     (t_send 08:36:28.596)
08:36:28.707  mock     DATA in from .71 xEA25A006 (+0.1)        08:36:28.709 GATE out to .73
08:36:28      DK5EN-90 RX-UDP xEA25A006 S0, RX_DEDUP_NEW
08:36:29      DK5EN-90 [RM];ok;ctr;1791354987   -> NEW/TX-UDP x91A434CF reply ... ok v=4.40a up=28 ... ad373f..
08:36:29.514  mock     DATA in from .73 x91A434CF (+0.9)        08:36:29.515 GATE out to .71
08:36:29      DK5EN-1  RX-UDP x91A434CF S0   [RM];reply;DK5EN-90;ctr;1791354987;verified;1
08:36:37      DK5EN-90 [LOG] TX x91A434CF wait=8409 q=2 -> TX-LoRa (reply RF; no RF copy logged at the Heltec)
08:36:43      DK5EN-1  [LOG] TX xEA25A006 wait=14535 q=4 -> TX-LoRa (command RF)
08:36:44      DK5EN-90 [LOG] xEA25A006 S0 RSSI -45 DUP:n -> [RM];reject;replay   (+15.4; ctr ..999 `radio` accepted 08:36:41 meanwhile)
```

First copy server, second copy (RF) not a duplicate for the ring (`DUP:n`) and counted as a reject (`replay`); no re-execution.

### 1>90 `mh 0` (case 15, ctr 1791355058)

```
08:37:39      DK5EN-1  TX-UDP xEA25A00D ... mh 0 2c7fc5..        08:37:39.261 mock DATA in .71 (+0.1), GATE out to .73 .262
08:37:40      DK5EN-90 [RM];ok;ctr;1791355058 -> x91A434D6 reply (`ok 3 - DK5EN-98 0 DK5EN-1 1 DK5EN-92 2`)
08:37:40.143  mock DATA in .73 x91A434D6 (+1.0)  ->  08:37:40 DK5EN-1 [RM];reply;...;verified;1
08:37:49      DK5EN-90 [LOG] TX x91A434D6 wait=9434 -> TX-LoRa
08:39:58      DK5EN-90 [LOG] xEA25A00D H03 S1 DK5EN-1,DK5EN-98>DK5EN-90 RSSI -67 DUP:n -> [RM];reject;replay   (first and only RF copy logged at the RAK, +139 s)
```

The Heltec's own TX-LoRa of this command happened at about +127 s (ring wait); the RAK logged no direct copy of it, only the DK5EN-98 relay (S1) 12 s later.

### 1>90 `txq` (case 17, ctr 1791355082)

```
08:38:02      DK5EN-1  TX-UDP xEA25A010 ... txq b24200..       08:38:02.803 mock DATA in .71 (-0.0), GATE .73
08:38:03      DK5EN-90 [RM];ok;ctr;1791355082 -> x91A434D9 reply `ok q=8/20 bp=quiet tx=8 rt=0 dr=0 u=0`
08:38:03.631  mock DATA in .73 (+0.8)  ->  08:38:03 DK5EN-1 [RM];reply;...;verified;1
08:38:20      DK5EN-90 [LOG] TX x91A434D9 wait=16797 -> TX-LoRa;  08:38:21 DK5EN-1 RF rx x91A434D9 S0 RSSI -52 DUP:n, GWU/RLY, TX-UDP back to the mock (08:38:21.645, echoed to the RAK as RX-UDP DUP)
```

Command RF copy at the RAK: `08:40:54 [RM];reject;replay` (ctr ..082, +172 s).

### 90>1 `status` (case 34, ctr 1791355372, command x91A434E5, reply xEA25A01E)

```
08:42:52      DK5EN-90 NEW / [RM];send;DK5EN-1;ctr;1791355372 (t_send 08:42:52.856)    08:42:53 TX-UDP x91A434E5
08:42:53      DK5EN-1  RX-UDP x91A434E5 S0   [RM];ok;ctr;1791355372 -> NEW/TX-UDP xEA25A01E reply `ok v=4.40a up=34 bat=0 heap=113 ...`
08:42:53.018  mock DATA in .73 x91A434E5 (+0.2) / GATE .71 .019;   08:42:53.213 mock DATA in .71 xEA25A01E (+0.4) / GATE .73
08:42:53      DK5EN-90 RX-UDP xEA25A01E S0 -> [RM];reply;DK5EN-1;ctr;1791355372;verified;1
08:43:03      DK5EN-90 [LOG] TX x91A434E5 wait=10294 q=1 -> TX-LoRa
08:43:04      DK5EN-1  [LOG] x91A434E5 S0 RSSI -54 DUP:n -> [RM];cached;ctr;1791355372  -> a second reply frame xEA25A01F (NEW + TX-UDP), no execution, no reject
08:43:08 / 08:43:14   DK5EN-1 RF copies via DK5EN-98 (S1, RSSI -47) and DK5EN-92 (S0, RSSI -32): DUP:d
```

### 90>1 `mh 0` (case 40, ctr 1791355440)

```
08:44:01      DK5EN-90 TX-UDP + TX-LoRa x91A434EC (wait=324)     08:44:01.394 mock DATA in .73 (+0.1), GATE .71
08:44:01      DK5EN-1  RX-UDP x91A434EC S0  [RM];ok;ctr;1791355440 -> reply xEA25A027;  08:44:01.514 mock DATA in .71 (+0.3)
08:44:01      DK5EN-90 RX-UDP xEA25A027 S0 -> [RM];reply;...;verified;1
08:44:02      DK5EN-1  [LOG] x91A434EC RSSI -54 DUP:n -> [RM];cached;ctr;1791355440 -> second reply xEA25A028 (UDP)
08:44:11 / 08:44:35  DK5EN-1 RF copies via DK5EN-98 (S1) and DK5EN-92 (S0): DUP:d
```

Here the RAK's RF TX was fast (queue wait 324 ms), so the RF copy followed the server copy by about 1 s and became `cached` (no reject, one extra reply frame).

### 90>1 `txq` (case 42, ctr 1791355463)

```
08:44:24      DK5EN-90 NEW x91A434EF txq 1ef2f5..;  08:44:24.359 mock DATA in .73 (+0.2)
08:44:24      DK5EN-1  RX-UDP x91A434EF [RM];ok;ctr;1791355463 -> reply xEA25A02A `ok q=30/20 bp=qrs tx=2 rt=0 dr=0 u=0`;  08:44:24.554 mock DATA in .71 (+0.4)
08:44:24      DK5EN-90 RX-UDP xEA25A02A -> [RM];reply;...;verified;1
08:44:38      DK5EN-90 [LOG] TX x91A434EF wait=14702 -> TX-LoRa
08:44:35      DK5EN-1  [RM];ok;ctr;1791355474 (next command, case 44)
08:44:39      DK5EN-1  RF rx x91A434EF S0 RSSI -54 DUP:n -> [RM];reject;replay
```

The RF copy came 15 s after the server copy, after the next command (ctr ..474) had been accepted. The cache holds only the last accepted ctr + tag, so ..463 is `replay`
(counted in `rej`, not toward the lockout) instead of `cached`. The reply text `q=30/20` is the Heltec's own `txq` (depth 30 against a max of 20), not explained.

Summary over all 40 sent cases (verdict per second copy, via RF): ring duplicate (`DUP:d`) 34, `cached` 7 (no reject, extra reply), `replay` 23 (counted in `rej`, not toward lockout),
`rate` 2 (late RF copy of a `sync`, counted in `rej`). First copies: `ok` 34, `fail` 2 (`sens` unsupported), `sync` 2, `rate` 2 (the two post-write cases of item 2). No counter was
executed twice (`[RM];ok|fail` per ctr: none >1 on either node).

## 5. Counts over the whole run

- RM cases sent: 20 per direction (including the first `sync`); 2 per direction refused by the sender policy before any send (`maxhop (temp)`, `maxhop (restore)`: `err cmd`, `retry` 2 / 0).
- `ok_d == 1 and rej_d == 0`: 16 of 19 non-sync cases in each direction. Sync cases: `ok_d 0`, `rej_d 0` by design.
- Deviating (3 per direction):
  1>90: case 21 `name -` (ok_d 1, rej_d 1 = late RF copy of the `pos` command, executed once), case 22 `name` (noanswer, rate), case 26 `atxt` (noanswer, rate).
  90>1: case 34 `status` (rej_d 1, probably the late `sync` copy, see item 2; executed once), case 42 `txq` (rej_d 1 = late RF copy of the previous command, executed once), case 50 `atxt (same)`
  (executed once, reply lost, ring full).
- Executions: RAK 18 ctrs (17 case commands + the neg `status`), Heltec 19; none executed twice. Two of the three rate rejects at the RAK (08:39:01, 08:40:48) are the two commands that never ran; the third is the RF copy of the first `sync`.
- Replies with a verified tag at the sender: 40 (Heltec as sender 20, RAK as sender 20), 0 unverified: 37 of the 40 case replies (3 noanswer: cases 22, 26, 50), plus the neg
  `status` 1>90 and the two `sync after unlock`.
- Rejects in total: Heltec rate 2, replay 8, tag 3, lockout 5 = 18 (`rej=18`, matches `/rmstatus`); RAK rate 3, replay 15 = 18.

## 6. Plain traffic

- `plain:dm` 1>90 (`gwb085153-dm1`) and `plain:grp` 1>TEST (`gwb085219-grp1`): never sent. `08:51:54 [BP];refuse;depth;14;max;20` and `[BP];nack;QRT;dst;DK5EN-90;...;txt;QRT NOT SENT - gwb085153-dm1`; `08:52:19 ... QRT NOT SENT - gwb085219-grp1`. No NEW/TX line at the Heltec, nothing at the RAK. Driver event says `sent`.
- `plain:dm` 90>1 (`gwb085244-dm90{258`, x91A43502): RAK `08:52:44` NEW + TX-UDP; `08:52:51` TX-LoRa. Heltec: server copy `08:52:44` (`RX_DEDUP_NEW`, accepted, `NEW-ACK xEA25A03C ... :ack258` sent at once over UDP, RF at 08:52:55 after `wait=10508`);
  RF copy `08:52:51` `DUP:n` -> `[DMDUP] from DK5EN-90 nnn:258` (suppressed); relayed RF copies via DK5EN-98 (08:53:02, S1) and DK5EN-98,DK5EN-92 (08:53:12) `DUP:d`, each followed by `[REACK-LIMIT] dup from DK5EN-90 nnn:258`.
  Accepted once (one copy passed both dedup layers); the log has no display marker, so "shown" is not observable.
- ACK back at the RAK: `08:52:45 [UDP-MSGID] ack_msg_id:91A43502 ACK...02` (server, +1 s), `08:52:56 [ACK-MSGID] ack_msg_id:91A43502` (RF) and `[RETX] DM-ACK for retid:19 stop retransmit msg-id:91A43502`; later RF ack copies (08:53:01, 08:53:06) are `DUP:d`.
- `plain:grp` 90>TEST (`gwb085309-grp90{259`): RAK NEW + TX-UDP 08:53:09, TX-LoRa 08:53:14. Heltec: server copy `08:53:09` (`NEW`), RF copies `08:53:15`, `08:53:20`, `08:53:24` all `DUP:d`. Received once.

## 7. Foreign traffic

- Mock DATA datagrams in: 163 (Heltec 88, RAK 75), GATE out 163 (to Heltec 75, to RAK 88), KEEP 70. All 163 DATA frames have an own origin (DK5EN-1 / DK5EN-90); no foreign-origin frame was uplinked to the mock and none was gated down.
- `[LOG]` RF lines with S1 and a source path not starting with DK5EN-1/-90: Heltec 6, RAK 6 (4 distinct frames each, two of them seen twice with an extra relay hop): `x55A8F198` DK5DM-1 ... DK5EN-98>9, `x55A8F19B` DK5DM-1,DD7MH-55,DL2JA-2 >20, `x6AC5D656` OE1XAR-33,DK5EN-98>* (time beacon), `xA1C1E25A` DO7FJK-99,DK5EN-98>262.
  Example: `[LOG] 110 : x55A8F198 H00 S1 T0 M01 DK5DM-1,DD7MH-55,DB0HOB-12,DB0ED-99,DK5EN-98>9:Guten Morgen ringsherum der Antenne.73 de Marco  ... RSSI:-66 SNR:7 DUP:n`.
  These come from the production network via DK5EN-98 (not from the mock); S1 is set by that gateway. Own-origin frames relayed by DK5EN-98 also carry S1 (149 such RF lines: Heltec 83, RAK 66), i.e. the bench RM frames were heard and re-radiated by the production gateway; whether it uplinked them to the real server is not visible in these logs.
- Transmitted by a bench node: one foreign frame, Heltec mesh relay `08:47:17 TX-LoRa 113 : x55A8F19B H01 S1 ... DK5DM-1,DD7MH-55,DL2JA-2,DK5EN-1>20:Guten Morgen aus Ampfing JN68EG ...` (`[LOG] TX ... src=r wait=239964 q=18`, ring wait 240 s, 2 dBm). RAK: none (mesh off, `RLY ... q=nomesh` x3).

## 8. Anything unexpected

- No `[BOOT]`, no `## dropped`, no watchdog/WDT, no panic/backtrace in either log. `tx_fail;0` in all 19 RAK ETH status lines; ETH and WiFi link lines all `up`, no downs/renews/resets.
- RAK: one `[ETH];stall;udp_tx;ms;126;task;mcloop` (08:38:46); `[INSTR-LOOP]` gaps median 284 ms, max 2703 ms (webserver_loop), 1876/1846 ms (eth_udp_tx); `OnRxError` x4; one `ONRXDONE_SLOW ms=176`; `RING_DROP_STALE` x2 (age 181 s, ids 91A434CD and 91A434D1; D1 is the node's own HEY, so not RM frames).
- Heltec: `OnRxError` x3; `ONRXDONE_SLOW` x91 (54-130 ms, median 65); TX ring: `RING_DROP_STALE` x6 (age about 181 s), `RING_DROP_PRIO` x3, `RING_DROP_NEW` x1, `[RING] full, depth 19 of 19`;
  14 `[BP]` lines: notices `QRS` 08:37:27 (depth 6), `QRT` 08:44:24 (depth 16), `QTA` 08:45:45 (depth 19), 5 `refuse` + 5 `nack QRT` (3 junk frames, 1 DM, 1 group message), 1 `nack QTA` (RM reply of case 50); `[LOG] STAT ringmax` 10/20, 13/20, 19/20, 15/20 and
  `drop=0/0/0/1/6`, `0/0/1/1/0`, `0/0/0/0/1` per 5 min. Own TX waits (`[LOG] TX ... wait=`, prio 3 own frames): Heltec n=42 mean 203 s, max 418 s (plus 1 relayed frame 240 s, 1 prio-1 ACK 10.5 s); RAK n=46 mean 10.9 s, max 27.9 s. The Heltec wrote 71 ring entries (`RING_WRITE`) and radiated 46 frames in 18 min.
- `/rmstatus` at the end of the run, RAK `ok 18 rej 18`, Heltec `ok 19 rej 18`; matches the log verdict counts exactly.

## 9. Read-only GETs after the run (09:03 local)

```
RAK  http://192.168.68.73/rmstatus : {"on":0,"pw":1,"ok":18,"rej":18,"lock":0,"lockS":0,"hwm":1791355743,...}
Heltec http://192.168.68.71/rmstatus: {"on":1,"pw":1,"ok":19,"rej":18,"lock":0,"lockS":0,"hwm":1791355660,...}
```

- RAK start page `GET /`: `<tr><td>APRS text</td><td>-</td></tr>` (the page prints the stored string unchanged, `web_functions.cpp:3917`), so the text is now the literal `-`; before the run it was empty. RM is `off` again as before the run (`Remote management (RM1): off (ok=18 rej=18)`).
- The start page has no name field. The RAK name is taken from its own `--info` in `DK5EN-90.log` at teardown (`08:53:50 ...NAME: -`, `...ATXT: -`); before the run (`info_DK5EN-90.txt`, 08:35:49) both were empty. Name `-` is also implied by the accepted write (`rm_exec_write.cpp` replies `err failed` unless stored == args; reply was `ok n=-`, `[RM];ok`).
- Heltec start page: `APRS text MeshCom Freising`; `--info` at teardown `08:53:44 ...NAME: Martin`, `...ATXT: MeshCom Freising` = unchanged.
- The RAK's name and text therefore differ from before the run (empty -> `-`) and have not been restored by the run. Remotely an empty value cannot be written back (`-` is accepted as text, `none` is refused by
  `rmTextAllowed()` / `writeText()`); `src/rm_exec_write.cpp` notes that the console `--atxt none` maps to an empty comment. I did not check the console way for the name.

## Not established

- Why the Heltec TX ring holds 14-19 of 20 entries and waits minutes (mean 203 s) while the RAK waits about 11 s. Contributors visible in the log: gateway mode (every RF-heard frame is uplinked and every
  bounced server frame arrives), cached-reply extra frames (6 on the Heltec), relays (8 `RLY q=tx`, mesh on), channel utilisation 13-26 %, `RX_TIMEOUT_DEFERRED` waits; no measurement separates them.
- Why the Heltec does not log an RF copy of some RAK frames (reply RF copy logged in 11 of 18 cases), not explained (RAK TX-LoRa line exists for each).
- Whether a server-path ACK alone stops the DM retransmit: here `[RETX] ... stop retransmit` appears only with the RF ack at 08:52:56, 11 s after the `[UDP-MSGID]` ack.
- Why the Heltec `txq` reports depth 30 against max 20 (case 42 reply `q=30/20`).
- Exact accept-to-check interval of the rate rejects: wall clock has 1 s resolution and `[RM]` lines have no millisecond stamp; the `<10 s` follows from the verdict itself.
- Verdict-to-frame pairing is inferred (see top); `rf:?` marks in the table are copies without any matching verdict line (e.g. case 28).
- Mock-side behaviour of the real server (the mock gates a frame to every other gateway, including back to the origin of an RF-heard frame) is the mock's, not evidence for production.

Follow-up: finding RM-DUP (dual-path copy counted twice) is fixed in the firmware, see `extended-commands-impl-plan.md` section 10.
