**Status: ARCHIVIERT 2026-09-01 — Befunde eingearbeitet in BP-10 (`3aecc90f`).**
H1, H2, H3 und M1-M5, M7, M8 sind behoben (siehe `docs/BACKLOG.md`, Zeile
BP-10, und `docs/CHANGELOG-stability.md`, Eintrag 166). M6 ist bewusst nur
erfasst, nicht behoben (ausserhalb des Umfangs, siehe BP-10-Commit-Message).
D1 (Doku-Nachzug BACKLOG/CHANGELOG/RESUME/Runbook) ist mit dieser
Dokumentations-Welle (2026-09-01) erledigt. D2-D5, D7 und D8 betreffen
`docs/backpressure-flow-control.md` und bleiben dort offen — diese Datei liegt
ausserhalb des Dateisatzes dieser Welle.

Ursprungsplan: `bp-l1-l4-impl-plan-20260901.md` im selben Verzeichnis.

---

# BP-07/08/09 — Fable Verdict (2026-09-01)

Diff: `458af2b1..4f97f7f0` plus zwei uncommittete Nachzuege (Web-JS, Kommentar).
7 Finder, alle Befunde unten sind vom Orchestrator an der Quelle nachgeprueft.

## HIGH

### H1 — BP-09 versteckt die BP-07-Quittung auf beiden T-Decks

- **File:** `src/t-deck/event_functions.cpp:717ff`, `src/t-deck-pro/ui_deckpro.cpp:1805ff`
- **Nachgeprueft:** `tdeck_add_system_message()` (`lv_obj_functions.cpp:4277`) ruft
  `msg_focus_and_alert(false)`; der Tabwechsel dort haengt an `bWithAudio`
  (`:4189`) und passiert fuer Systemblasen absichtlich nicht.
- **Szenario:** Operator tippt bei QRT-Band. Quittung landet als Systemblase auf
  Tab 0. BP-09 unterdrueckt den Tabwechsel. Operator bleibt auf dem Eingabe-Tab
  und sieht NICHTS. Vor BP-09 schaltete der Handler unbedingt um.
- **Fix:** Tabwechsel bzw. `scr_mgr_switch()` wieder unbedingt; nur das Leeren
  des Eingabefelds an `BP_SEND_OK` binden.

### H2 — `addTxRingEntry()` liefert -1 aus drei Gruenden, alle als QTA gelesen

- **File:** `src/loop_functions.cpp`, Verwurfzweig nach `addTxRingEntry()`
- **Nachgeprueft:** `txring_functions.cpp:401` (TX-01, unkonfiguriertes
  Rufzeichen), `:423` (len==0 / >255), erst danach RING_DROP_NEW.
- **Szenario:** Fabrikfrischer Knoten (`XX0XXX`). Jede getippte Nachricht:
  `onSend(depth=0, dropped=true)` zwingt `BP_QRT`, meldet "QTA - message
  discarded, TX buffer full" bei LEEREM Ring, haengt "QTA NOT SENT - ..." an,
  Drain-Poll schliesst sofort mit QRV. Pro Nachricht ein falscher Zyklus mit
  falscher Begruendung. Teilweise Vorzustand (onSend bekam w<0 auch vorher),
  neu ist der sichtbare Text pro Nachricht.
- **Fix:** Nur als Rueckstau behandeln, wenn der Ring wirklich unter Druck steht
  (`txRingDepth() >= bp_state.refuseThreshold()`); sonst `BP_SEND_INVALID`, kein
  Zustandsautomat, keine Quittung.

### H3 — `[BP];nack;` loggt den ROHEN Text, nicht den bereinigten

- **File:** `src/loop_functions.cpp`, `bpEmitNack()`
- **Nachgeprueft:** Das `Serial.printf` benutzt `msg_text`; `bpNackCompose()`
  laeuft erst danach und fuellt `body`.
- **Szenario:** EXTUDP `{"msg":"a\nb"}` -> echtes LF im Text -> die Marker-Zeile
  bricht auf. Ein Text, der `[MC-DBG] RING_STATUS ...` enthaelt, faelscht einen
  Marker. `tools/serial_monitor.py` (RING_ZOMBIE) und `tools/loganalyse.sh`
  fehlinterpretieren das.
- **Fix:** `bpNackCompose()` vor das Logging ziehen und `body` loggen.

## MEDIUM

- **M1 Stack:** `char body[140]` in `bpEmitNack()` auf dem nRF52-4-KB-Loop-Stack,
  auf dem `getExtern -> sendMessage`-Pfad (N-22: Watermark 0 gemessen). Derselbe
  Commit hat `c_json` unter Berufung auf N-22 nach BSS geschoben. -> `static`
  auf NRF52_SERIES, wie im Rest der Datei.
- **M2 Rad neu erfunden + Transportdivergenz:** `charset_utf8_safe_truncate()`
  (`src/charset_filter.h:81`) existiert, ist in `native_aprs` gelinkt und laeuft
  VORWAERTS ueber Lead-Bytes. Die handgeschriebene Rueckwaertsschleife loescht
  bei verirrten Folgebytes den ganzen Text. Zusaetzlich: nur der BLE-Pfad laeuft
  durch `charset_filter_apply(PLAIN)`, EXTUDP nicht -> ungueltiges UTF-8
  ueberlebt im JSON und die Transporte senden verschiedene Bytes (E3 verletzt).
- **M3 Ellipse:** `truncated` wird vor dem Puffer-Clamp entschieden; ein durch
  `room` erzwungener Schnitt bekommt kein "...".
- **M4 QRV erreicht den Abgewiesenen nicht:** `bpEmitNack()` umgeht die
  `bp_episode_origin`/`bp_episode_dst`-Latch. Wer mitten in einer fremden
  Episode abgewiesen wird, erfaehrt den Verlust, aber nie die Entwarnung.
- **M5 Wiedereintritt:** `bpRoute()` setzt `bp_episode_origin` NACH dem Senden
  zurueck. `addMessage()` dreht 100 ms `lv_task_handler()`, kann einen
  Tastendruck zustellen und `sendMessage()` verschachteln; der aeussere Aufruf
  ueberschreibt dann die frisch gelatchte Herkunft. Vorzustand, neu erreichbar.
  -> Herkunft in Locals sichern, Globals VOR dem Senden zuruecksetzen.
- **M6 Blockade (nur erfasst, nicht gefixt):** `addMessage()` 100 ms Spin,
  `TDeck_pro_lora_disp()` `delay(100)` — jetzt pro abgewiesener Nachricht statt
  ~2x pro Episode. Aenderung an diesen Senken ist ausserhalb des Umfangs;
  Benchlauf soll die reale Groessenordnung zeigen.
- **M7 Leeres Ziel:** die bewusste `bpPeekDst`-Regel "leeres `{}` -> `*`" ging
  mit der Loeschung verloren; `{}Hallo` erzeugt jetzt `dst=""`.
- **M8 msg_id 0:** `bpNextMsgId()` kann beim 49,7-Tage-Rollover genau einmal 0
  liefern (`bp_last_msg_id == 0xFFFFFFFF`, +1 wrappt).

## Dokumentation (eigener Commit)

- D1 Beide Dokumente fuehren L1-L4 und Kapitel 9 noch als "geplant"; BACKLOG,
  CHANGELOG-stability, RESUME, automation-runner-runbook unberuehrt.
- D2 `backpressure-flow-control.md:549` nennt App-Entwicklern das FALSCHE
  Praefix (`QRT - not sent` statt `QRT NOT SENT - `) und die falsche Kuerzung.
- D3 Kapitel 9 behauptet pauschal "keine msg_id verbraucht" — gilt nur fuer den
  Refuse-Pfad; auf dem QTA-Pfad sind `node_msgid++` und `save_settings()` durch.
- D4 Die `bLORADEBUG`-Gatterung des `txt;`-Feldes steht in keinem Dokument.
- D5 Der E4-Kommentar zaehlt die unterdrueckten Pfade auf, nennt aber die
  T-Deck-Displayaufrufe nicht — liest sich wie ein Versehen.
- D7 `loop_functions.cpp` ist in KEINEM nativen `build_src_filter`. Kein Test
  fasst die Verdrahtung an; der Benchlauf ist der einzige Ende-zu-Ende-Nachweis.
- D8 Die BP-08-Commit-Message schliesst "keine Nebenlaeufigkeit" aus zwei
  geprueften Zeilen; `OnRxDone` (`lora_functions.cpp:1346`) ruft aus dem
  Timer-Task ebenfalls `addTxRingEntry()`. Vorzustand, aber die Aussage war zu
  breit.

## Widerlegt / nicht weiterverfolgen

- `bpNackCompose()` Speichersicherheit: handgeprueft, `written <= out_len-1`
  immer, `text[take]` immer in Grenzen, kein Underflow. Nur M2/M3 bleiben.
- Erfolgspfad nach der Umstellung: gleiche Frames, gleiche Reihenfolge —
  `bp_origin_dst`, `{mcp}`-Zweig, `user_msg_status` gegen den `{NNN}`-Suffix,
  Gateway-Baken gegen `addNodeData()` alle sauber.
- Kein Nack kann auf die Luft: alle fuenf `bpDeliver`-Arme verfolgt,
  `msg_app_offline` immer gesetzt, BLE-Frame <=186 gegen 251 Byte Grenze.
- ArduinoJson expandiert Nicht-ASCII NICHT; die 1:1-Byte-Annahme haelt. 400
  Byte reichen (Worst Case 279/289). `if(json_len == 0)` ist allerdings eine
  Scheinsicherung — ArduinoJson kuerzt still und liefert `bufferSize`.
- `BpSendResult` im "falschen" Header: Altitude-Geschmack, kein Fehler. Nicht
  angefasst, weil ein Header-Wechsel die Upstream-Rebasierung verteuert.
