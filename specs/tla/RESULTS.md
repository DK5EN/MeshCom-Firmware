# TLC results

Written by `specs/tla/run_tlc.sh --all`. Do not edit by hand.

- Date: 2026-11-27T02:55Z
- Commit: cdfc76ea plus uncommitted changes in specs/tla
- Jar sha256: 936a262061c914694dfd669a543be24573c45d5aa0ff20a8b96b23d01e050e88
- Configurations: 56, unexpected: 0

| spec | cfg | checks | expected | result | verdict | distinct states | time |
|---|---|---|---|---|---|---|---|
| bytefifo.tla | bytefifo_fix_udp_dup.cfg | NoDup | holds | holds | ok | 18691 | 1s |
| bytefifo.tla | bytefifo_fix_udp_loss.cfg | NoLoss | holds | holds | ok | 18691 | 0s |
| bytefifo.tla | bytefifo_mut_genwrap.cfg | PopOnlyPeeked | violates PopOnlyPeeked | violates PopOnlyPeeked | ok | 1167 | 0s |
| bytefifo.tla | bytefifo_mut_lostcount.cfg | Counters | violates Counters | violates Counters | ok | 46 | 1s |
| bytefifo.tla | bytefifo_mut_noevictgen.cfg | NoTornIter | violates NoTornIter | violates NoTornIter | ok | 337 | 1s |
| bytefifo.tla | bytefifo_mut_nopopgen.cfg | PopOnlyPeeked | violates PopOnlyPeeked | violates PopOnlyPeeked | ok | 657 | 1s |
| bytefifo.tla | bytefifo_mut_notailbump.cfg | NoLoss | violates NoLoss | violates NoLoss | ok | 506 | 0s |
| bytefifo.tla | bytefifo_mut_peekunlocked.cfg | NoTornFrame | violates NoTornFrame | violates NoTornFrame | ok | 2083 | 1s |
| bytefifo.tla | bytefifo_was_udp_dup.cfg | NoDup | violates NoDup | violates NoDup | ok | 6244 | 1s |
| bytefifo.tla | bytefifo_was_udp_loss.cfg | NoLoss | violates NoLoss | violates NoLoss | ok | 2593 | 1s |
| bytefifo.tla | bytefifo.cfg | Counters Accounting PopOnlyPeeked NoTornFrame NoTornIter NoDup NoLoss Delivery | holds | holds | ok | 18691 | 1s |
| extqueue.tla | extqueue_fix_esp32_b5.cfg | NoTorn NoDup NoSilentDrop | holds | holds | ok | 403 | 0s |
| extqueue.tla | extqueue_fix_nrf52.cfg | NoTorn NoDup NoSilentDrop | holds | holds | ok | 695 | 1s |
| extqueue.tla | extqueue_mut_checklate_notorn.cfg | NoTorn | violates NoTorn | violates NoTorn | ok | 380 | 0s |
| extqueue.tla | extqueue_mut_noclear_nodup.cfg | NoDup | violates NoDup | violates NoDup | ok | 299 | 0s |
| extqueue.tla | extqueue_was_esp32_b2.cfg | NoTorn NoDup NoSilentDrop | holds | holds | ok | 154 | 1s |
| extqueue.tla | extqueue_was_esp32_b5_nosilentdrop.cfg | NoSilentDrop | violates NoSilentDrop | violates NoSilentDrop | ok | 409 | 0s |
| extqueue.tla | extqueue_was_esp32_b5_notorn.cfg | NoTorn NoDup | holds | holds | ok | 444 | 0s |
| extqueue.tla | extqueue_was_nrf52_nodup.cfg | NoDup | violates NoDup | violates NoDup | ok | 837 | 1s |
| extqueue.tla | extqueue_was_nrf52_nosilentdrop.cfg | NoSilentDrop | violates NoSilentDrop | violates NoSilentDrop | ok | 815 | 1s |
| extqueue.tla | extqueue_was_nrf52_notorn.cfg | NoTorn | violates NoTorn | violates NoTorn | ok | 409 | 0s |
| netconsole.tla | netconsole_mut_aba.cfg | NoWrongTear | violates NoWrongTear | violates NoWrongTear | ok | 1530282 | 4s |
| netconsole.tla | netconsole_mut_flagsurvive.cfg | SessFlag | violates SessFlag | violates SessFlag | ok | 4757 | 0s |
| netconsole.tla | netconsole_mut_noclose.cfg | OneSession | violates OneSession | violates OneSession | ok | 787502 | 3s |
| netconsole.tla | netconsole_mut_nodeadline.cfg | SlotFree | violates SlotFree | violates SlotFree | ok | 210726 | 4s |
| netconsole.tla | netconsole_mut_nofdcheck.cfg | NoWrongTear | violates NoWrongTear | violates NoWrongTear | ok | 2018381 | 5s |
| netconsole.tla | netconsole_mut_noreset.cfg | NoUnauthCmd | violates NoUnauthCmd | violates NoUnauthCmd | ok | 127008 | 1s |
| netconsole.tla | netconsole_mut_preemptactive.cfg | NoKickMarked | violates NoKickMarked | violates NoKickMarked | ok | 1685 | 1s |
| netconsole.tla | netconsole_mut_stalefd.cfg | NoDoubleClose | violates NoDoubleClose | violates NoDoubleClose | ok | 35057 | 1s |
| netconsole.tla | netconsole_was_deadinstall.cfg | NoDeadInstall | violates NoDeadInstall | violates NoDeadInstall | ok | 128696 | 1s |
| netconsole.tla | netconsole_was_foreignshut.cfg | NoForeignShut | violates NoForeignShut | violates NoForeignShut | ok | 29390 | 1s |
| netconsole.tla | netconsole_was_kickmarked.cfg | NoKickMarked | violates NoKickMarked | violates NoKickMarked | ok | 13754 | 1s |
| netconsole.tla | netconsole_was_slotlockout.cfg | NoSilentBlock | violates NoSilentBlock | violates NoSilentBlock | ok | 203 | 1s |
| netconsole.tla | netconsole.cfg | OneSession NoUnauthCmd SessionAuthed NoWrongTear NoDoubleClose SessFlag NoSilentBlock NoKickMarked NoForeignShut NoDeadInstall SlotFree | holds | holds | ok | 1362217 | 49s |
| rxflags.tla | rxflags_mut_clearlate_nodeaf.cfg | NoDeaf | violates NoDeaf | violates NoDeaf | ok | 285 | 0s |
| rxflags.tla | rxflags_mut_noirqguard_norxintx.cfg | NoRxInTx | violates NoRxInTx | violates NoRxInTx | ok | 388 | 1s |
| rxflags.tla | rxflags_mut_norecovery_nodeaf.cfg | NoDeaf | violates NoDeaf | violates NoDeaf | ok | 264 | 0s |
| rxflags.tla | rxflags_mut_noreenable_listening.cfg | Listening | violates Listening | violates Listening | ok | 118 | 0s |
| rxflags.tla | rxflags_today_listening.cfg | Listening TxArmed | holds | holds | ok | 1049 | 1s |
| rxflags.tla | rxflags_today_nodeaf.cfg | NoDeaf | holds | holds | ok | 1049 | 1s |
| rxflags.tla | rxflags_today_norxintx.cfg | NoRxInTx | holds | holds | ok | 1049 | 0s |
| rxflags.tla | rxflags_today_stall_norxintx.cfg | NoRxInTx | violates NoRxInTx | violates NoRxInTx | ok | 477 | 1s |
| txring.tla | txring_esp32.cfg | NoSharedSlot NoSplice SlotConsistent InflightOwned QueuedInSlot EvictBounded NoStuckInflight NoSilentLoss AllEnqueued | holds | holds | ok | 18842 | 2s |
| txring.tla | txring_lenfirst_cs.cfg | NoSplice SlotConsistent | holds | holds | ok | 29480 | 1s |
| txring.tla | txring_mut_ackinflight.cfg | NoSplice | violates NoSplice | violates NoSplice | ok | 2950 | 1s |
| txring.tla | txring_mut_evictinflight.cfg | NoSplice | violates NoSplice | violates NoSplice | ok | 3539 | 0s |
| txring.tla | txring_mut_forget.cfg | NoStuckInflight | violates NoStuckInflight | violates NoStuckInflight | ok | 1039 | 1s |
| txring.tla | txring_mut_lenfirst.cfg | NoSplice | violates NoSplice | violates NoSplice | ok | 173 | 1s |
| txring.tla | txring_mut_nocs.cfg | NoSharedSlot | violates NoSharedSlot | violates NoSharedSlot | ok | 122 | 1s |
| txring.tla | txring_nocs_lenlast.cfg | NoSplice SlotConsistent | holds | holds | ok | 1781 | 0s |
| txring.tla | txring_nrf52_big.cfg | NoSharedSlot NoSplice SlotConsistent InflightOwned QueuedInSlot EvictBounded NoStuckInflight NoSilentLoss AllEnqueued | holds | holds | ok | 510585 | 71s |
| txring.tla | txring_nrf52.cfg | NoSharedSlot NoSplice SlotConsistent InflightOwned QueuedInSlot EvictBounded NoStuckInflight NoSilentLoss AllEnqueued | holds | holds | ok | 17852 | 2s |
| txring.tla | txring_reach_dropnew.cfg | ReachDropNew | violates ReachDropNew | violates ReachDropNew | ok | 7746 | 1s |
| txring.tla | txring_reach_evict.cfg | ReachEvict | violates ReachEvict | violates ReachEvict | ok | 530 | 1s |
| txring.tla | txring_reach_expire.cfg | ReachExpire | violates ReachExpire | violates ReachExpire | ok | 179 | 0s |
| txring.tla | txring_sweepinflight_dead.cfg | NoExpireInflight NoSplice | holds | holds | ok | 20796 | 0s |
