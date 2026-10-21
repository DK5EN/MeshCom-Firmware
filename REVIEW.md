# REVIEW.md -- Review-Leitfaden MeshCom-Firmware (Fork DK5EN)

Stand: 2026-10-21, Branch `fork-dev`. Abgeleitet aus dem Fork-Entwicklungszyklus
(`CLAUDE.md`, `docs/BACKLOG.md` §1.5/§2.5/§2.7, `docs/code-quality-2.0.md` Part B/D,
`docs/wire-compat.md`, `docs/architecture/11-wire-format.md`, `tools/regression.sh`,
`test/golden/`), aus den 62 eigenen Upstream-PRs (#709 .. #1224, davon 58 gemerged) und aus
unseren Reviews fremder Upstream-PRs (#1114, #1164 .. #1175, #1196, #1219/#1223/#1227, #1220).

Gilt für drei Situationen:

1. **Self-Review** eines eigenen PR-Branches vor `gh pr create` (Autor).
2. **Verdict** einer Welle auf `fork-dev` (zweite Sitzung oder Agent, Stage 1-3).
3. **Review fremder PRs** gegen `icssw-org/MeshCom-Firmware:dev` (Kommentar upstream).

Jede Regel hat eine ID (`S-` Scope, `K-` Kompatibilität, `P-` Plattform, `C-` Code, `T-` Test,
`U-` Upstream-Review, `V-` Verdict). Ein Review-Kommentar zitiert die ID; das "Warum" steht hier.
Die Detailregeln zu Code-Mustern bleiben in `docs/codequality-rules.md` (1.0) und
`docs/code-quality-2.0.md` (C01..C28, P01..P16). Diese Datei wiederholt nur, was ein Reviewer
in fünf Minuten braucht.

Sprache: PR-Body und Commit-Body deutsch (sie werden die PR-Beschreibung), Code-Kommentare
englisch, `release-notes.md` englisch. Upstream-Kommentare an deutschsprachige Autoren deutsch,
sonst englisch, nie gemischt.

---

## 0. Leitsätze

1. **Die Flotte zuerst.** Im Feld laufen 4.35a bis 4.40a nebeneinander (Stand 10/2026:
   278 Nodes 4.35a-o, 537 auf 4.35p, 1478 ab 4.35s), viele ohne Update-Pfad. Jede Änderung an
   Frame, Settings-Bit oder Kommando wird zuerst gegen "ein alter Node empfängt das" und "dieser
   Node empfängt ein altes Frame" geprüft, dann gegen "läuft auf meinem Tisch".
2. **Klein und chirurgisch.** Upstream merged binnen Minuten bis Stunden ohne zweites Review,
   hat keine Test-CI und revertet ganze PRs ohne Begründung im Thread. Ein PR, der drei Dinge
   tut, verliert alle drei, wenn eines bricht (#1196, #1209, #1227).
3. **Beweis vor Behauptung.** "Getestet" heißt: Env-Name, Board, Build-String aus `--info`,
   Golden-Diff. Ein Satz ohne diese vier Angaben ist eine Behauptung (P02).
4. **Twin-Pflicht.** Was ESP32 und nRF52 doppelt haben, wird doppelt geändert, oder der Diff
   erklärt, warum nicht (C06).
5. **Unser Gate ist das einzige Netz.** `meshcom-ci.yml` baut nur auf Tag-Push, ohne Tests.
   Nichts geht als PR raus, was `tools/regression.sh --stage 1,2` nicht grün gesehen hat.
6. **Unser Self-Review ist das Review.** Von 62 eigenen PRs hatten etwa 12 überhaupt einen
   Kommentar; der Maintainer verlangte genau einmal eine funktionale Änderung (#1149).

---

## 1. Ablauf und Rollen

```
fork-dev (Welle)  -->  Verdict  -->  pr/<topic> von upstream/dev  -->  PR (deutsch)
      ^                                                                  |
      |                              Kurt (oe1kbc) reviewt und merged (Merge-Commit)
      +------------------ /sync-upstream (merge, nie rebase) <-----------+
```

| Stufe           | Wer                       | Prüft                                 | Ergebnis                    |
| --------------- | ------------------------- | ------------------------------------- | --------------------------- |
| Self-Review     | Autor                     | Abschnitt 2, 3, 5 Tier 1              | Diff bereinigt, Body fertig |
| Verdict         | zweite Sitzung oder Agent | Tier 1-3, Gate-Lauf, Refuted-Tabelle  | `docs/*-verdict-*.md`       |
| Upstream-Review | Maintainer                | Build, Feldverträglichkeit            | Merge oder Änderungswunsch  |
| Post-Merge      | Autor                     | Revert-Scan, Sync, stille Hunks (P08) | `fork-dev` wieder auf Stand |

- DK5EN hat Schreibrecht upstream, merged aber **nie** selbst (`docs/BACKLOG.md` §2.7).
- Upstream merged als Merge-Commit (`6b2fdfa`, `4f9f17eb`, `9669ec99`), nicht als Squash. Der
  PR-Branch bleibt trotzdem ein Commit (S-06), damit ein Revert eine Einheit trifft.
- Upstream nutzt keine Inline-Reviews und keine Review-Objekte. Alles Feedback sind
  Issue-Kommentare; Datei:Zeile steht im Text.
- Ein Review ohne Gate-Lauf ist ein Lesebericht, kein Verdict (Abschnitt 9).

---

## 2. PR-Zuschnitt (S-Regeln)

| ID   | Regel                                                                                                                                                                                                                                                                                                                                                      |
| ---- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| S-01 | **Ein Thema pro PR, ein Finding pro Commit.** Fix und Feature getrennt. Sammel-PRs (#1102 "82 Änderungen", #1115 102 Dateien) nur mit nummerierter Tabelle je Änderung, interner ID (`BP-11`, `ETH-05`) und einem Commit je Zeile, "damit sie sich einzeln zurücknehmen lassen" (#1207).                                                                 |
| S-02 | **Branch von `upstream/dev`, nicht von `fork-dev`.** `git checkout -b pr/<topic> upstream/dev`, dann nur Firmware-Dateien (`src/`, `variants/`, `include/`, die Nicht-native-Hunks von `platformio.ini`). Keine `docs/`, `tools/`, Bench-Skripte, Debug-Marker, Screenshots.                                                                              |
| S-03 | **Tests nur als Zaun.** Ein `test/test_*` plus `[env:native_*]` geht mit, wenn er genau die geänderte Einheit absichert und ohne Fork-Tooling läuft (#1224: 20 Host-Tests, `native_web_txbuf`). Golden-Korpora, `extra_scripts`, `-Werror`-Policy und `--port "$UPLOAD_PORT"` bleiben im Fork (vier Kopplungen, `scout-conventions.md`).                 |
| S-04 | **Minimaldiff.** Kein Reformat, keine Umbenennung, kein Refactor als Beifang. `git diff --stat upstream/dev` und `git diff -w --stat upstream/dev` zeigen dieselben Hunks. `git checkout fork-dev -- src` kopiert ganze Dateien und revertet ungesyncte Upstream-Commits: den staged Diff nach Identifiern des Upstream-Deltas grepen.                        |
| S-05 | **Body deutsch, nach Vorlage (Anhang C).** Je Datei/Funktion: Was, Warum, Wie geprüft. Pflichtblöcke: "Was sich auf dem Draht ändert", "Alte Nodes / Downgrade", "Nicht in diesem PR", "Nicht auf Hardware geprüft". Env-Zahlen aus dem Gate-Lauf, nicht aus dem Gedächtnis (P14: ein PR zitierte 12 Envs, die seine `platformio.ini` nicht mehr enthielt). |
| S-06 | **Eine Squash-Einheit.** Der PR-Branch ist ein Commit; Titel conventional-commit, höchstens 70 Zeichen, Issue in Klammern (`(#1217)`). Nach dem Merge `/sync-upstream` (merge), Worktree und Branch löschen.                                                                                                                                              |
| S-07 | **Fetch unmittelbar vor `gh pr create`.** `gh pr list -R icssw-org/MeshCom-Firmware --state open` und den aktiven Maintainer-Branch (`oe1kbc_v4.xx`) prüfen. #1118 wurde auf einer Basis geöffnet, die der Maintainer in derselben Minute änderte; der Merge-from-dev verlor drei `variants/`-Hunks ohne Konfliktmarker.                                   |
| S-08 | **Beschreibung gleich Diff.** Dateizahl, Variantenliste und Verhaltensaussagen im Body werden gegen `git diff --stat` geprüft (#1118: "vier Varianten" im Text, zwei im Diff; #1175: "30 s Mindestabstand" existierte nicht; #1227: Kommentar in `charge_led_mode.h` umgeschrieben, damit ein Bit "frei" aussieht).                                         |
| S-09 | **Keine Binaries mit Quelländerungen** (`safeboot*.bin`, `otadata.bin`) im selben PR, außer der PR ist der Safeboot-Release (#1209 mischte Server-Feature, esptool und Blobs).                                                                                                                                                                                 |

---

## 3. Flottenkompatibilität und Historie (K-Regeln)

Referenz: `docs/architecture/11-wire-format.md` (eingefrorene Beschreibung, 4.35p/4.35t),
`docs/wire-compat.md` (Persistenz; der nRF52-Blob-Teil ist seit dem keyed Store überholt),
`docs/ack-wer-hat-quittiert.md` §5, `docs/hey-storm-2026-10-17.md`,
`docs/userstory-hashtag-filter.md` §6-7. Die Fragen, die der PR-Body beantwortet:

> Was passiert, wenn ein 4.35-Node dieses Frame empfängt oder weiterleitet?
> Was passiert, wenn dieser Node ein 4.35-Frame empfängt?
> Welche Leser außerhalb der Firmware gibt es (App, MCProxy, mcmap, Server, aprs.fi)?
> Was tut ein Node nach dem Update, ohne dass jemand ein Kommando tippt?

### 3.1 On-Air

| ID   | Regel                                                                                                                                                                                                                                                                                                                                                                                                    |
| ---- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| K-01 | **LoRa-Frame-Layout ist eingefroren** (`11-wire-format.md` §1.1): Typbyte, `msg_id` LE, Flag/Hop-Nibble, Pfad `>`-terminiert (max 120), Payload `0x00`, HW-Id, MOD, FCS als Byte-Summe BE, Trailer (FW, LASTHW, FW-Sub, `0x7E`). Decoder tolerieren fehlenden Trailer, Encoder schreiben ihn immer. Max 255, unter 16 Byte verworfen. Sub-Version `0x7E` ist reserviert und wird nicht "repariert" (DR-27). |
| K-02 | **Neue Information nur als optionales Tag am Payload-Ende** (`/R=`, `/U=`, `/I=`, `#name`, #1142). Unbekannte Tags werden ignoriert, nie `split('/')`, 100-Byte-Budget für den Kommentarschwanz. Kein neues Header-Byte, kein neuer Typ ohne Fallback für Empfänger, die ihn nicht kennen.                                                                                                                 |
| K-03 | **Flag-Day-Änderungen sind ausgeschlossen.** Ein Zielfeld mit `#` verwerfen Nodes unter 4.36 komplett und relayen es nicht: "Alte Nodes sind nicht nur taub, sie sind auch Relay-Löcher." Gleiches gilt für jedes Zeichen außerhalb der Callsign-Regex im Ziel.                                                                                                                                               |
| K-04 | **Versions-Cutoffs nur mit Zahl.** `aprs_functions.cpp:533` verwirft Quellen unter 4.35a. Ein Cutoff bei 4.35p träfe 35 % des Netzes und etwa 380 Gateways. Reihenfolge: Verhaltens-Guard zuerst, Versions-Guard ein Release später, davor nur zählen.                                                                                                                                                      |
| K-05 | **`msg_id` ist `((gw_id & 0x3FFFFF) << 10) \| (counter & 0x3FF)`, Zähler 0..999** (nicht 1023: der ACK-Suffix `{NNN` ist `%03i`). Jede Stelle, die `node_msgid` erhöht, klemmt auf 999 und sichert über `save_msgid()`.                                                                                                                                                                                  |
| K-06 | **ACK-Toleranz.** Alte Nodes relayen ACKs auf 12 Byte gekürzt. Ein Parser verwirft nie wegen kaputtem Anhang, nutzt Byte 10/11 nie als Verwerfungsgrund (0,4 % valider Feld-ACKs weichen ab), prüft Länge vor jedem Zugriff, lässt `isPlausibleAckFrame()` (Byte 5) unverändert. Rollout: Firmware-Toleranz zuerst, dann App, dann Sender.                                                                 |
| K-07 | **Neue Notices als Text, der durch alte Parser fällt** (`:stoNNN`: kein `:ack`, kein `:rej`, kein `{`). Preis: Chat-Rauschen auf alten Nodes, im Body benennen. Bench-Fall gegen ein 4.35t-Ziel und einen 4.35t-Sender gehört dazu.                                                                                                                                                                       |
| K-08 | **Airtime-Argument für jedes neue On-Air-Frame:** Frames/h je Node im Default und im schlechtesten Pfad (GPS-variabel, Sofortsenden), Kadenz-Untergrenze, Relay-Vervielfachung. #1219 brachte +2 Frames/min ohne Mindestintervall und wurde revertet; #1148 ließ den Hop-Zähler auf 15 laufen und erzeugte Schleifen über 7 Relays.                                                                        |
| K-09 | **Region und Frequenz.** Eine Sendeweg-Änderung prüft `lora_setchip_aprs()` (nRF52 fest 433.775 MHz, ignoriert Region: ein 868er RAK sendet durch sein 868er Frontend auf 433) und den `track_freq`-Sentinel `999` in Region 5 (ESP32: `setFrequency()` scheitert, Ring-Slot wird bei jedem `doTX()` wiederholt). Ein entferntes Opt-in-Gate (`bDisplayTrack` in `doTX()`, #1227) ist ein Blocker.          |
| K-10 | **Keine Reparatur an Mustern, die die Flotte teilt** (Byte-Summe statt CRC, `0x7E`, Trailer-Lücke, `t032`-Rundung). Dokumentieren, nicht ändern; eine Änderung bräuchte ein Koordinationsfenster der ganzen Flotte.                                                                                                                                                                                       |
| K-11 | **Architekturgrenzen gelten vor dem Code.** Gateways dekrementieren beim Einspeisen vom Server bewusst nicht (#742 abgelehnt). Die Firmware öffnet keine eigene TCP-Verbindung zu APRS-IS; MeshCom-Verkehr erreicht APRS-IS über den Server (#1227). Sonderpin-Varianten werden vom Team nicht getragen (#1220). "Keine Architekturänderung ohne Rücksprache mit dem Firmware-Team."                       |

### 3.2 Settings, Bits, Persistenz

| ID   | Regel                                                                                                                                                                                                                                                                                                                                                                                                                                     |
| ---- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| K-12 | **Ein `node_sset*`-Bit ist nie "frei", nur weil der aktuelle Baum es nicht liest.** `git log -S "0x2000" -- src` über die ganze Historie: `0x2000` war `--checkmesh` (v4.35p, Mai bis Juni 2026), `0x0010` war `bGPSDEBUG` (V4.28 bis v4.35p, 2,5 Jahre), `node_sset3` `0x0001` war `bMHONLY`. Nichts löscht gesetzte Bits. Ein Default-off-Feature auf einem recycelten Bit ist nach dem Update **an** (#1219, #1227, #1196). Entweder das Bit beim Boot einmal löschen, oder ein neues Feld. |
| K-13 | **Masken sind 16 Bit breit geschrieben, nicht gedacht.** `0x7FEF` löschte `NOPMOTHER` (`0x8000`) mit; 16-Bit-Masken wischen Bit 15 von `node_sset2` (#1207 §1.2/§1.3). Jede neue Maske nennt im Body jedes Bit, das sie berührt.                                                                                                                                                                                                                       |
| K-14 | **Member-Namen sind der Vertrag** (`src/meshcom_settings.h`, X-Macro; NVS-Key und keyed Store auf beiden Plattformen). Reihenfolge ist frei. Umbenennung oder Tippfehler im Key ist ein stiller Rückfall auf den Default beim nächsten Boot, ohne Warnung. NVS-Key höchstens 15 Zeichen.                                                                                                                                                                 |
| K-15 | **Neues Feld = vier Stellen:** Member mit Default, Schema-Zeile (`config_json.h`), Store-Default gleich Struct-Default, `native_settings_members_esp32/_nrf52` und `settings_schema_lint` grün. Sensor-Treiber inkludieren `meshcom_settings.h` nicht, sondern bekommen Werte per Init und Get/Set (Feedback tkuschel auf #1201, offen).                                                                                                                  |
| K-16 | **`FLASH_STRUCT_VERSION` ist die Flotten-Wipe-Taste.** Nur bei echter Layout-Generation, mit Migrationsnotiz, nie "weil das Datum neu ist" (08/2026: ein Datums-Bump löschte Rufzeichen, WLAN und Sensoren jedes updatenden Nodes). `FLASH_VERSION` ist informativ. Keyed Stores brauchen für neue Felder keinen Bump (`node_tz`, `node_rmstrict`). Ein Wipe im Upgrade-Test (RAK-90) ist ein FAIL.                                                     |
| K-17 | **`src/nrf52/ble_settings_v1.h` ist eingefroren** (`static_assert(sizeof == 2000)`, Marker `0xAA 0x55`, kein Versionsfeld, "length is the only negotiation"). Jede Änderung dort ist ein Wire-Bruch zur App und braucht einen v2-Typ, nie einen Patch an v1. Die `offsetof`-Asserts werden nie an einen geänderten Struct "angepasst".                                                                                                                |
| K-18 | **Kommandos sind API.** `--<cmd>` sprechen App, Web-GUI, Netzkonsole, RM1 und Skripte. Kein Umbenennen, keine geänderte Antwort, keine geänderte Argumentform (`--setinet` zu `--setinet on` in #1209 brach die argumentlose Form) ohne `console_golden.py --compare` vor/nach; `help_parity_lint` und `command_ladder_lint` grün.                                                                                                                       |

### 3.3 BLE, UDP, EXTUDP, Versionen

| ID   | Regel                                                                                                                                                                                                                                                                                                                                                            |
| ---- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| K-19 | **BLE-JSON-Budget 244 Byte** (`BLE_JSON_PAYLOAD_MAX`). `SN` steht bei 228/244, `I` bei 239/244; neue Settings-Keys gehen nach `SN1`, und auch dort rechnet der Body typisch und schlechtesten Fall vor (#1227: sechs Keys, 71 auf 189 Byte typisch, 284 im Worst Case). `bleJsonFrameFailSoft()` wirft von hinten stumm ab, also steht Entbehrliches zuletzt. |
| K-20 | **BLE-Frames sind Vertrag mit der App:** `0x40` Text, `0x44` JSON (2 Pad), `0x91` legacy; ACK 13 Byte; Zeitstempel BE. `MH`: 13 alte Felder in alter Reihenfolge, dann 7 angehängte. `PLT` bleibt numerisch (`PLT:33`), nie `"POS"`: MCProxy und App lesen es als Zahl.                                                                                       |
| K-21 | **Server-UDP 1990** (`GATE`/`BEAT`/`CONF`, `KEEP`/`DATA`/`STOR` mit `%-4.4s%-1.1s`-Version) und **EXTUDP 1799** (feste Key-Sets in/out, `src_type` wählt die Sensorfelder) sind Schemata mit Lesern (Server, MCProxy, Logger). Neue Keys hinten; Konsument im Body benannt; `compare_udp.py`/`compare_extudp.py` gelaufen.                                     |
| K-22 | **Serverwahl ist Tabelle, nicht Kette.** `GW_SERVERS[]` (`src/gw_servers.cpp`, 16 Unity-Fälle je Zelle) ersetzt die `else if`-Ketten, die in #1209-#1212 auseinanderliefen und den OE-Default verloren. Ein Diff an der Serverwahl nennt jede Zelle (Plattform x Land x Hamnet x statisch) und lässt `test_gw_servers_golden.py` jede Abweichung benennen.         |
| K-23 | **Versionsbytes.** `SOURCE_VERSION`/`SOURCE_VERSION_SUB` landen als `%-4.4s%-1.1s` in `node_fwversion[8]`, `KEEP`, `DATA` und im LoRa-Trailer (Werte 1..34 verworfen). Tags sind `v<VER>.MM.DD`, nie `.N`-Suffix (`fwParseTag()` kennt es nicht, Auto Update sieht das Release nicht). Keine Version, keine Id aus `millis()` (C12).                             |

### 3.4 Das Golden Set

Kein einzelner Datensatz, sondern vier Schichten mit je eigenem Werkzeug. Die Lints laufen im
Host-Gate (`test/golden/selftest.sh`), die Hardware-Captures vergleicht ein Mensch am Bench.

| Schicht                | Inhalt                                                                                                                                          | Vergleich                                                                      | Fail heißt                                                                                                    |
| ---------------------- | ----------------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------ | ------------------------------------------------------------------------------------------------------------- |
| Statische Lints        | `help_parity`, `command_ladder`, `toggle_table`, `settings_schema`, `settings_persist`, `persist_readback`, `nano_printf`, `variant_macros`, `radio_units`, `twin_stub`, `drift_matrix` | `sh test/golden/selftest.sh`                                                   | Quelle ist von Registry/Tabelle abgedriftet (Kommando ohne Help, Setting ohne Schema, `%lld` auf nRF52)        |
| Aufgelöste Konfig      | `native/variant-ini-effective.json`, `variant-macros-effective.txt`                                                                             | regeneriert der Orchestrator nach Lesen des Diffs (`--generate`)               | neues Env oder `test_filter` ohne Regeneration                                                                |
| Frozen Decoder/Encoder | `test/test_aprs_corpus/corpus.txt` + `golden.txt`; `gw_servers_legacy.json` (einmalig aus `fe8f10d0`, nie aus neuem Code)                       | `pio test -e native_aprs`; `test_gw_servers_golden.py` mit `EXPECTED_DELTAS`   | Verhaltensänderung: der Diff von `golden.txt` **ist** die Änderung und gehört in den PR-Body                   |
| Twin-Dumps             | `native/u1-*`, `u2-*` ESP32 vs nRF52                                                                                                            | `twin_diff.py --check`                                                         | ein Block kippt agree/differ; auch ein absichtlich behobener Drift muss durch Regenerieren bestätigt werden   |
| Hardware-Captures      | `hw/G0`, `G2` je Board: BLE-Frames, UDP 1990, EXTUDP, Konsole (307 Kommandos, USB und 2323), T-Deck-UI                                          | `ble_golden.py --compare`, `console_golden.py --compare`, `EXPECTED-DIFF.md`   | "Any extra diff is a failure, including a 'better' one." Erwartete Diffs stehen **vor** dem Lauf in der Datei |

Regeln:

- Ein PR, der eine Zeile in Encoder, Decoder, Parser, Serverwahl oder Kommando-Ladder ändert,
  nennt im Body die Schicht, die er gelaufen hat, und ob der Diff leer war.
- `APRS_GOLDEN_UPDATE=1` wird nie nebenbei gesetzt. Golden-Vektoren aus aktuellem Output heißen
  "oracle: current-behaviour"; nur Captures, Spec-Vektoren und Differential-Tests finden
  Altfehler, der Rest sind Regressionszäune.
- Die Kommandokorpora sind gegenüber HEAD veraltet (etwa 370 Zeilen); wer sie regeneriert, macht
  daraus einen eigenen Lauf mit eigenem Verdict, nicht einen Beifang.

---

## 4. Plattform-Parität ESP32 / nRF52 und Varianten (P-Regeln)

| ID   | Regel                                                                                                                                                                                                                                                                                                                                                                                                   |
| ---- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| P-01 | **Twin-Liste (C06).** Ein Diff auf einer Seite eines Paares ist eine Review-Frage an die andere: `getMeshComUDPpacket` <-> `NrfETH::getUDP`, `sendMeshComUDP` <-> `sendUDP`, `checkSerialCommand` (esp32_main) <-> (nrf52_main), `startNetwork` <-> `startETH`, `read_batt`/`battDetectUpdate` <-> `batt_function_old.cpp`, Gateway-Block `esp32loop` <-> `nrf52loop`, `udp_frame_esp32.cpp` <-> `udp_frame_nrf52.cpp`. |
| P-02 | **Bauen auf Boards, die der Autor nicht hat.** Mindestens `wiscore_rak4631` plus `heltec_wifi_lora_32_V3` plus `ttgo_tbeam`; bei Web/Display zusätzlich `t_deck`, `t_deck_pro`. #1196 brach alle ESP32 ohne PMU, weil `setChargeLedOn()` außerhalb des AXP-Guards lag; #1201 brach `ttgo_tbeam`, weil ein `#include` im `ENABLE_MSGSTORE`-Guard saß.                                                       |
| P-03 | **Link-Regionen lesen, nicht die `RAM:`-Zeile.** `ttgo_tbeam*` hatte 20 Byte IRAM frei, `E22_XML-DevKitC` ist DRAM-knapp; PlatformIO zeigt auf PSRAM-Boards `RAM: 8.7 %` bei `iram0_0_seg` 99,98 %. Ein `strtok_r` (122 B libc auf der IRAM-Liste) revertete #1114. Vor jedem PR mit neuem libc-Aufruf: `firmware.map` nach `iram0_0_seg`/`dram0_0_seg` auf den knappen Envs; `strptime()` (2265 B) ist das größte Risiko. |
| P-04 | **Guards (C07).** Jedes neue `#if` testet `defined()`; 28 `BOARD_*`-Makros sind nackte Produktnamen, `#if BOARD_X` rechnet mit einem String. Das Makro wird von einem Env gesetzt (`pio project metadata`); keine Option-`#define` in einer `.cpp`; `#if`/`#endif`-Zählung je Datei gleich; Boards ohne das neue Flag kompilieren zu demselben Code wie vorher (#1166).                                        |
| P-05 | **nRF52-Kontext (C10/C13).** Alles, was von `OnRxDone` erreichbar ist: lock-guarded, print-frei, flash-write-frei. Große Locals auf dem Loop-Task-Pfad sind `static` unter `NRF52_SERIES`. `%lld` existiert auf nRF52 nicht (`nano_printf_lint`). Statische IPs auf nRF52 bleiben statisch (ETH-05).                                                                                                      |
| P-06 | **Varianten vollständig.** Eine Änderung an `variants/*/platformio.ini` trifft jede Variante derselben Klasse (#1118: `upload_protocol` fehlte in `wireless-paper`, `vision-master-e213`, `-e290`; das entfernte `--port` lässt esptool bei mehreren Boards den falschen Port wählen). Neue Variante: `DISPLAY`/`RELEASE_ENVS` in `pages_flasher.py`, `mv`-Zeile und `artifacts:` in `meshcom-ci.yml`, die Zählungen 27/66. |
| P-07 | **Board-Konstanten und Flash-Budget.** `MAX_RING`, `MAX_DEDUP_RING`, Partitionstabellen variieren je Board (`configuration_global.h`). RAK sitzt bei 81-96 % Flash (`BOARD_FLASH_MAX_PCT=95` im Gate); jede Web-Seite, jeder Font, jeder String steht in der Flash-Delta-Tabelle des Body.                                                                                                                 |
| P-08 | **Gates symmetrisch (C25).** Was ein Node mit IP aber ohne Gateway braucht (NTP, DHCP-Renew, Link-Poll) liegt außerhalb von `if (bGATEWAY)`; ESP32- und nRF52-Gates um dasselbe Feature stimmen überein. Neue CSS/JS/Routen in `web_functions.cpp` sind ESP32-geguarded, die Datei ist auch der RAK-Server.                                                                                              |

---

## 5. Code-Checkliste (C-Regeln, aus `code-quality-2.0.md` Part D)

**Tier 1, jeder Diff, etwa fünf Minuten.** Befehl ausführen, Treffer lesen (Anhang A).

| ID   | Check                                                                                                                                                                                       | Muster  |
| ---- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ------- |
| C-01 | Länge kommt vom Produzenten, nicht aus `sizeof(dst)` oder `measureJson`: jedes neue `memcpy`, `serializeJson`, `snprintf`. Validiert wird gegen den Puffer, nicht gegen die Payload (BND-03). | C01     |
| C-02 | Keine `uint8_t`-Längenarithmetik, kein `strncat(..., sizeof(dst)-1)`, kein `unsigned long` gegen `millis()`.                                                                                | C02     |
| C-03 | Jedes neue Feld in BLE/UDP/JSON: Worst-Case-Bytes gegen den konsumierenden Clamp stehen im Body.                                                                                              | C03/C04 |
| C-04 | Blockierer im Diff: `delay(`, `scanNetworks`, `hostByName`, `SD.`, `WiFi.getMode`, `esp_wifi_get_`, `i2s_write`. Jeder Treffer nennt Task und Schranke. `delay(40)` auf dem LoRa-RX-Pfad war ein Upstream-Fund (UP-02). | C08 |
| C-05 | Zeit: `(uint32_t)(now - t) >= X`; keine `millis() + N`-Deadlines; kein Altern auf `getUnixClock()`; keine Id aus `millis()`.                                                                | C12     |
| C-06 | Reihenfolge: Seiteneffekte (Echo, Uplink, Ring-Insert, Dedup-Insert, Reboot vor HTTP-Antwort) nach der Gating-Entscheidung; eine Funktion, die ablehnen kann, gibt ein Ergebnis zurück, der Aufrufer liest es. | C22 |
| C-07 | Logging: Formatstring immer Literal (`Serial.printf(temp)` mit Funktext war CRITICAL); kein `printf("%s", ...)` auf dem Hot-Path; kein roher RF-Text in einem `[TAG];`-Marker.               | C14     |
| C-08 | Tests: neuer Test lief rot gegen den alten Code, die rote Menge steht im Test-Kommentar; `build_src_filter` des Env enthält die Einheit; der Test ist in `platformio.ini` `test_filter` oder `regression.sh` registriert. | C28 |
| C-09 | Web-Ausgabe: alles aus dem Mesh (Call, Pfad, Payload, `node_tz`) läuft durch den Escape-Helfer vor `innerHTML` (WEB-03a); hand-gebautes JSON escaped; `maxlength` und Serverlimit stimmen überein (#1201). | C27 |
| C-10 | Hardware-Erkennung nur auf positivem Beweis (`begin()`-Rückgabe, Id-Register), nie auf Abwesenheit eines Fehlers. Ein gespeichertes Setting wird beim Boot auch angewendet (#1196: `--charge led on` war nach Power-Cycle wirkungslos). | C17 |

**Tier 2, wenn der Diff Settings, Ringe, Zustandsautomaten oder nRF52-Pfade berührt.**

| ID   | Check                                                                                                                                                                                                        | Muster |
| ---- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ | ------ |
| C-11 | Settings: K-12 bis K-17 abgehakt; GUI-Handler laufen über `commandAction()`; kein GUI-only-Zustand; kein `FLASH_STRUCT_VERSION`-Bump ohne Migrationsnotiz.                                                    | C19    |
| C-12 | Ring: Invarianten in `test/test_txring` halten; keine In-Band-Sentinel; Voll-Erkennung bleibt `MAX_RING-1` (ETH-03, `loop_functions.cpp`); Index-Distanz ist keine Tiefe.                                     | C15    |
| C-13 | Kritische Abschnitte: nichts, was verzögert, allokiert, loggt oder mit dem Radio spricht, in `taskENTER_CRITICAL`; Ressourcen-Guards sind Mutexe, nie binäre Semaphore; kein `portMAX_DELAY`; Prioritäten unter `configMAX_PRIORITIES`. | C11 |
| C-14 | Zustandsautomat: ein gelatchter Emitter trägt keine Per-Message-Payload; jeder Close-Pfad setzt Routing-Zustand zurück; Schwellen auf Ringtiefe gegen eine Feld-`RING_STATUS`-Verteilung geprüft.              | C22    |
| C-15 | Watchdog: vor neuem WDT-Feed oder -Scope ein Inventar der blockierenden Arbeit auf dem Task; Feed pro Zeile beim Rendern großer Seiten (#1224), nicht pro Seite; ein Fehlerpfad meldet nie OK.                 | C09    |
| C-16 | Kopie statt Aufruf: eine Funktion, die "etwa 25 Zeilen" einer anderen kopiert, lässt genau die Zweige aus, die später fehlen (#1219: `bGateway_done`, EXTUDP, KISS). Gemeinsamer Helfer oder Begründung.      | C16    |
| C-17 | Lebensdauer: `delete` ohne `NULL`-Setzen, hängende Captures, Model-Trim ohne View-Gegenstück; `--kiss off`, das den Port nie schließt (#1114 F3).                                                              | C21    |

---

## 6. Tests und Gates (T-Regeln)

| ID   | Regel                                                                                                                                                                                                                                                                                                        |
| ---- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ |
| T-01 | **Definition of Done je Fix** (`docs/BACKLOG.md` §2.5): Finding gegen den aktuellen Baum re-verifiziert; Fehler vorgeführt, nicht argumentiert; Regressionstest rot-vorher/grün-nachher oder die Vorführung im Commit-Body; Host-Gate grün; jedes betroffene Env baut, beide MCU-Familien bei geteilter Datei; RAM/Flash-Delta notiert; `Refs:` auf die Finding-ID. |
| T-02 | **Host-Gate vor jedem Push:** `tools/regression.sh --stage 1,2` (alle `[env:native*]`, 77 Envs Stand 10/2026, `test/golden/selftest.sh`, ruff, pytest, node/jsdom-Suiten, nRF52-Link-Check). Nie bares `pio test`, es flasht angeschlossene Hardware. Ein `pio`-Prozess zur Zeit. Zahlen aus dem Lauf, nie aus `test-suite-map.md`. |
| T-03 | **Gate-Breite = Release-Breite (P09).** Vor Release oder großem PR: volle native Liste, ein Env je Build-Filter, beide Safeboot-Envs, `-Werror` auf den Flash-Targets; `IGNORED\|skipped`-Zahl darf nicht wachsen. #1114 war auf 7 Standard-Envs grün und scheiterte am Linker von vier anderen.              |
| T-04 | **Bench-Nachweis (Stage 3) für Radio, Ringe, Netz, Settings, Display, Sleep:** mindestens ein ESP32 und ein nRF52 mit dem PR-Commit geflasht; `--info`-Build-String stimmt; Reboot-Erkennung über Uptime (`ms=` im ersten Marker), nicht über `rst:`-Banner; Capture mit offenem Port mindestens 4 Minuten. |
| T-05 | **Der Default-Pfad wird im Feld getestet, nicht nur gebaut.** "code+QA only" für einen Pfad, der nach dem Update auf RF sendet, ist ein Blocker (#1227). Ein ehrlicher Block "Nicht auf Hardware geprüft" (#1135, #1148, #1213) ist Pflicht und wurde zweimal von Dritten im Feld nachgeholt (Stego-Lab).      |
| T-06 | **Soak** für Ringe, Heap, WDT, Netz: Fenster und Build in einer Tabelle, Metriken gegen den Vorlauf (Resets, Log-Lücken über 120 s, Heap-Quartile erst/letzt, TX-Fehler, Ring-Drops, Kanalnutzung, CRC, NBR-CHECK, `hop check`), Block "Was das nicht beweist". Implizite Schwelle: 0 Resets, 0 Lücken, kein Heap-Trend. |
| T-07 | **Feldnachweis** (DK5EN-98, DB0TAW-14 u. a.): Rohlog im Korpus mit md5, bevor die erste Zahl geschrieben wird; jede Kopfzahl mit Herleitung oder Einzeiler; Teil-Buckets fallen weg; feed-sichtbar vs. gemessen gekennzeichnet (P13/P16).                                                                         |
| T-08 | **Bench-Regeln sind Review-Kriterien:** nur Gruppe `TEST` oder DM an eigene Nodes, nie `*`; kein fremdes Rufzeichen; höchstens 2 dBm; `identity_guard.py` vor dem Lauf; `lsof` vor dem Öffnen; DK5EN-98 ist Produktion, nie Bench. Ein Capture mit eigenem `*`-Frame ist ungültig (`verify_captures.py`).      |
| T-09 | **Release-Gate zusätzlich:** Idle-Heap auf dem T-Beam-Image gegen `tools/heap_baseline.json` (mehr als 3072 B unter Baseline ist FAIL), Field-Command-Scan beider T-Deck-Images (`mute=2 stat=1 udplog=1 injectraw=0`: ein Guard-Wechsel lässt Kommandos ohne Compiler-Meldung verschwinden), 66 Assets, Safeboot-Diff gegen HEAD leer. |

---

## 7. Review fremder Upstream-PRs (U-Regeln)

Upstream-Fakten, aus denen die Regeln folgen: Kurt merged in Minuten bis Stunden, ohne zweites
Review und ohne CI. #1219 wurde nach 4,6 h gemerged, unser Review kam 11 h später, der Revert
4 Minuten danach. #1114 wurde am selben Tag gemerged, an dem unser Bericht "cannot be merged
as-is" sagte, und eine Stunde später revertet. Ein Review nach dem Merge ist ein Revert-Antrag.

| ID   | Regel                                                                                                                                                                                                                                                                                                                                                                                             |
| ---- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| U-01 | **Früh und sichtbar.** Erster Kommentar innerhalb des Tages, notfalls als Platzhalter: "Review läuft, bitte noch nicht mergen." Jedes neue On-Air-Verhalten braucht eine Nacht zwischen Review und Merge.                                                                                                                                                                                           |
| U-02 | **Bauen, bevor geschrieben wird.** PR gegen den aktuellen `dev` auf Boards bauen, die der Autor nicht hat (P-02, P-03). Compiler-Ausgabe mit dem Befehl davor zitieren (`pio run -e heltec_wifi_lora_32_V3`, dann die `error:`-Zeilen). Sagen, welche Envs sauber bauen. Was nicht gebaut oder nicht auf Hardware geprüft wurde, steht als Satz dabei ("im Code nachvollzogen, nicht gebaut").   |
| U-03 | **Ein Kommentar je Runde, festes Skelett:** (1) Verdict oder Anerkennung zuerst ("korrekt und minimal", "careful piece of work"); (2) nummerierte Findings, je Symptom, Ursache mit Datei/Funktion/Zeile, Folge, Fix-Vorschlag als Code oder Einzeiler, Testwunsch konkret; (3) Block "Kleinere Anmerkungen (kein Blocker)" und "Nicht für diesen PR"; (4) Merge-Aussage ("Good to merge from my side" / "Currently not accepted for merge, because ..."). |
| U-04 | **Fragenkatalog, den jeder fremde PR durchläuft:** Bit-Historie (K-12), Masken (K-13), BLE-Budget (K-19), Default-off bleibt off nach Update, Airtime-Argument (K-08), Region/Frequenz (K-09), Architekturgrenze (K-11), Kopie vs. Originalpfad (C-16), Beschreibung gleich Diff (S-08), umgeschriebene Kommentare, Feldtest des Default-Pfads (T-05), Mixed Concerns (S-01), Binaries (S-09). |
| U-05 | **Historie mit Hash belegen.** Ein Bit, ein Cutoff, ein "war schon immer so" zitiert den Commit (`d2911166`, `62cfc0a6`, `6911821e`). `git log -S` und `git bisect` sind Review-Werkzeuge, nicht Beifang.                                                                                                                                                                                       |
| U-06 | **Zahlen statt Adjektive.** Bytes und Prozent vom Budget, Frames/h, Zeilenwachstum (147 auf 1094), IRAM-Arithmetik ("122 + 2 - 20 = 104, Byte für Byte die Zahl aus der Fehlermeldung").                                                                                                                                                                                                       |
| U-07 | **Altfehler sind nicht dieser PR.** Was der PR nicht berührt, kommt in einen eigenen Block "nur zur Info", gated den Merge nicht, bekommt eine Defect-ID im Fork.                                                                                                                                                                                                                                 |
| U-08 | **Zurücknehmen auf Beweis.** Widerlegt der Autor ein Finding mit Build-Tabelle oder Hardware-Log (#1164: toter `#else`-Zweig seit `80511056`), wird es im Thread zurückgezogen ("I withdraw that point") und der Rest neu verifiziert.                                                                                                                                                              |
| U-09 | **Nach der Fix-Runde erneut gegen `dev` verifizieren,** nicht gegen den Stand des ersten Reviews: semantische Konflikte mit bewegtem Upstream (#1114: `sendMessage()` gab inzwischen `BpSendResult` zurück) und die vollen Envs inklusive der knappen (P-03).                                                                                                                                     |
| U-10 | **Interner Review-Bericht** (`docs/archive/pr<NNN>-review-<datum>.md`): Kopf (PR, Head-Hash, Basis, Diff-Größe, Methode), Executive Summary mit Empfehlung, Build-Tabelle je Env mit RAM/Flash-Delta, Regel-Audit nach Kategorie, Findings F1..Fn mit Verifikationsmarke ("verified end-to-end", "code-certain"), "Refuted claims (do not re-report)", "Verified-correct highlights", Fix-Priorität. Beim Posten interne Prozesswörter entfernen ("Fable", "per standing preference"). |
| U-11 | **Reverts ohne Grund sind unser Thema.** Wird ein fremder PR revertet, den wir reviewt haben, schreiben wir den Grund in den Revert-Thread (Link auf Analyse), damit der Autor nicht fragen muss ("Could you share what issue it caused?", #867).                                                                                                                                                    |

---

## 8. Was Upstream von uns erwartet (Lehren aus 62 eigenen PRs)

| Lehre                                                                                                                                                                                                                           | Beleg                                   |
| ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | --------------------------------------- |
| Routing-"Fixes" erst mit dem Team klären: Gateways dekrementieren beim Server-Einspeisen bewusst nicht. "Bitte um Kontakt mit der Entwicklung."                                                                                    | #742 abgelehnt in 37 min                |
| Feldgewohnheiten nicht wegnormalisieren: Rufzeichen ohne SSID bleiben erlaubt, nur `-01`/`-1`-Formen werden kanonisiert. Rework in 11 min, Merge 21 min später.                                                                   | #1149, oe1kbc                           |
| Sensor-Treiber bleiben datenstruktur-unabhängig (Werte per Init, Get/Set). Der Hinweis blieb unbeantwortet und ist offen.                                                                                                         | #1201, tkuschel                         |
| Basis ist immer `dev`, nie ein `oe1kbc_*`-Arbeitsbranch.                                                                                                                                                                         | #709/#710, #776, #782, #802             |
| Ein Merge-from-dev kann unsere Hunks ohne Konfliktmarker verlieren; nach dem Merge den eigenen Diff erneut grepen.                                                                                                               | #1118 (`upload_protocol` in 3 Varianten) |
| Maintainer pushen direkt auf `dev` und mergen eigene PRs in Sekunden (#1209: 7 Dateien, kein Body, 15 s). Unser Upstream-Sync-Verdict reviewt Kurts Hunks wie fremde PRs: "zwei der vier Map-Hunks landen ohne Konfliktmarker, von Hand prüfen." | `2026-08-29-upstream-sync-verdict.md`   |
| Reverts kommen als Ein-Klick-PR ohne Begründung (#807, #871, #1105-#1107, #1128, #1223). Nach jedem Sync `git log --grep='^Revert'` und je revertiertem Symbol grepen; keine Fork-Reparatur für Features, die Upstream entfernt hat. | P08                                     |
| Große PRs werden trotzdem gemerged (#1115 in 82 min). Das ist kein Freibrief: je größer, desto mehr hängt am eigenen Verdict.                                                                                                     | #1102, #1115, #1135, #1207              |

---

## 9. Verdict-Format (V-Regeln)

| ID   | Regel                                                                                                                                                                                                                                                                                                   |
| ---- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| V-01 | **Kopf:** Scope (Diff-Range, Tag, HEAD-Hash), Methode (Finder/Verifier-Zahl und Modelle, "Report only: no source was changed"), Abdeckung und "Not covered" ausdrücklich, Recall-Vorbehalt.                                                                                                             |
| V-02 | **Finding:** ID, **File** (Datei:Zeile, Zeilen driften binnen Stunden, also zusätzlich Funktion), **Severity** nach Widerlegung, nicht die des Finders, **Failure scenario** mit Eingabe und Zustand, **Fix**, optional **Rule** (`BND-03`, `C06`) und **Behaviour change**.                                 |
| V-03 | **Pflichttabellen:** "Corrections to the findings doc" (Doc said / Corrected), "Refuted claims (do not re-investigate)", "Suggested order". Positive Ergebnisse, die aktiv angegriffen wurden, stehen als "Verified-correct highlights".                                                                     |
| V-04 | **Marken:** VERIFIED (Orchestrator selbst hergeleitet), CONFIRMED (Finder mit Datei:Zeile, stichprobengeprüft), REFUTED (aufgezeichnet, damit es nicht erneut gefunden wird). Ein Label wie RESOLVED meint "Defekt behoben", nichts anderes (DRY-20).                                                     |
| V-05 | **Jeder Agentenfund wird einmal selbst gegrept**, bevor er ins Verdict geht (P04). Finder haben Recht auf der Zeile und Unrecht im Mechanismus (setup vs. loop, PSRAM vs. interner Heap); die Architekturfrage (Redraw-Vertrag, Blocking-Inventar, Task-Map) kommt vor der Checkliste (P15).                   |
| V-06 | **Kein Zahlenwert, der nicht aus diesem Lauf stammt.** Gate-Zahlen aus `tools/bench/runs/regression-<ts>/`; Kopfzahlen mit Einzeiler; zurückgezogene Sätze überleben keine Zusammenfassung (P06/P13).                                                                                                      |
| V-07 | **Verdict-Wort am Ende:** APPROVED / approved with conditions / REWORK (Advisor), PASS / FAIL (Gate, Soak). Bedingungen sind Defect-IDs, keine Prosa.                                                                                                                                                       |
| V-08 | **Kein Schließen durch den Implementierer.** Ein Finding gilt als behoben, wenn der Reviewer es gegen den aktuellen Baum re-verifiziert hat (f6: 16 von 19 "fixed" hielten, die Ausfälle waren Selbst-Adjudikation). Ausnahmen brauchen eine gemessene Prämisse.                                             |

---

## 10. Severity und Merge-Entscheidung

| Stufe (Upstream-Wort) | Audit-Wort | Bedeutung                                                                                                                                                                                  | Folge                                                        |
| --------------------- | ---------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ | ------------------------------------------------------------ |
| **Blocker**           | CRITICAL   | Speicherkorruption, Build- oder Link-Bruch auf einem Release-Target, Wire- oder Settings-Bruch gegen alte Nodes, ungewolltes Senden nach Update, Datenverlust in NVS/Store, Architekturbruch | kein Merge; Fix und erneutes Gate                             |
| **Blocker**           | HIGH       | wahrscheinlicher Crash oder Fehlverhalten unter realen Bedingungen (Twin-Drift, Blockierer im Loop, fehlende Grenzprüfung an Mesh-Eingaben, Budget-Überlauf)                               | Fix vor Merge, oder im Body begründet verschoben mit Defect-ID |
| **kein Blocker**      | MEDIUM     | latenter Fehler, der Zuverlässigkeit senkt (Rollover-Form, Logging auf Hot-Path, fehlender Test, Kopie statt Helfer)                                                                        | darf mit, Defect-ID im Catalogue                              |
| **nur zur Info**      | LOW        | Stil, Benennung, Kommentar, Altfehler außerhalb des PR                                                                                                                                      | nie Grund für Änderungswunsch upstream                        |

Ein Finding ohne Datei:Zeile, Mechanismus und Nachweis (grep, Test oder Capture) ist eine
Vermutung und wird als solche markiert. Sicherheitsfunde bleiben offen, bis der Operator
entscheidet.

---

## 11. Prozess-Anti-Patterns, die uns getroffen haben (aus Part B)

| ID  | Muster                                                            | Gegenmittel im Review                                                                   |
| --- | ----------------------------------------------------------------- | --------------------------------------------------------------------------------------- |
| P01 | Sichere Behauptung zuerst, trennendes Experiment später            | n und Ergebnis je Arm, zweites Board anderer Klasse                                     |
| P02 | "Verifiziert", das keines war                                     | Env, Board, Build-String, Golden-Diff im Body                                           |
| P04 | Scout- oder Agentenbericht ohne Stichprobe übernommen              | jeder Fund einmal selbst gegrept, bevor er in ein Verdict geht                          |
| P06 | Veraltete Zeilennummern, Zähler, zurückgezogene Sätze              | IDs statt Zeilen zitieren; Zahlen aus dem Gate-Lauf, der dem Tag unmittelbar vorausgeht |
| P08 | Upstream-Sync-Kollisionen, Revert-Zyklen                          | `merge-tree` vor dem Plan; `git log --grep=Revert`; stille Hunks grepen                 |
| P10 | Brief oder Intake-Prämisse falsch                                 | jede Behauptung "X ist falsch in F" hat ihr `grep -n` im Brief                          |
| P11 | Skript-Edits ohne Treffer-Assertion                               | `s.count(old) == 1`; `#if`/`#endif` zählen; Vier-Env-Build nach jedem Skript-Edit       |
| P14 | Release-Hygiene (Pre-Fix-Safeboot ausgeliefert, Env-Zahl erfunden) | T-09; `grep -c "^\[env:native" platformio.ini` gleich der Zahl im PR-Text              |
| P15 | Review-Scope enger als die Fehlerklasse                            | Scope folgt dem Call-Graph des Events, nicht dem Verzeichnis; "Refuted claims"-Tabelle  |

---

## Anhang A: Schnellbefehle

```sh
# Diff-Hygiene (S-04, S-08)
git fetch upstream dev
git diff --stat upstream/dev; git diff -w --stat upstream/dev
git diff upstream/dev --name-only | grep -vE '^(src|variants|include|platformio.ini|test/)' # muss leer sein

# Tier 1 (C-01 .. C-10)
git diff upstream/dev -- src | grep -nE "^\+.*(delay\(|scanNetworks|hostByName|SD\.|WiFi\.getMode|esp_wifi_get_|i2s_write)"
git diff upstream/dev -- src | grep -nE "^\+.*(sprintf|strcpy|strcat|strtok)\("
git diff upstream/dev -- src | grep -nE "^\+.*millis\(\) *\+"
git diff upstream/dev -- src | grep -nE "^\+#if " | grep -v defined
for f in $(git diff --name-only upstream/dev -- src); do echo "$f $(grep -c '^#if' $f) $(grep -c '^#endif' $f)"; done

# Bit-Historie (K-12, K-13)
git log --oneline -S "0x2000" -- src/configuration_global.h src/command_functions.cpp | head
git diff upstream/dev -- src | grep -nE "^\+.*node_sset[0-9]?.*0x[0-9A-Fa-f]+"

# Twin-Drift (P-01)
git diff upstream/dev --name-only -- src/esp32 src/nrf52

# Builds (P-02, P-03)
for e in wiscore_rak4631 heltec_wifi_lora_32_V3 ttgo_tbeam E22_XML-DevKitC; do pio run -e $e || echo "FAIL $e"; done
grep -A3 "iram0_0_seg\|dram0_0_seg" .pio/build/ttgo_tbeam/firmware.map | head

# Gates (T-02)
tools/regression.sh --stage 1,2
grep -c "^\[env:native" platformio.ini

# Golden (K-08 .. K-22)
pio test -e native_aprs
python3 tools/bench/console_golden.py --compare before/cmd-usb.txt after/cmd-usb.txt
python3 test/golden/compare_udp.py --self-test; python3 test/golden/compare_extudp.py --self-test

# Upstream vor PR (S-07, P08)
git merge-tree --write-tree HEAD upstream/dev
git log --oneline --grep='^Revert' <alt>..upstream/dev
gh pr list -R icssw-org/MeshCom-Firmware --state open
```

## Anhang B: Referenzen

- `CLAUDE.md`: Branch-Modell, Gates, Bench-Regeln
- `docs/BACKLOG.md` §1.5 Arbeitsregeln, §2.5 Definition of Done, §2.7 Upstream-PR-Mechanik
- `docs/code-quality-2.0.md`: C01..C28, P01..P16, Part D Checkliste
- `docs/codequality-rules.md`: MEM/BND/RACE/ISR/STAB/STK-Regeln 1.0, Audit-Checkliste
- `docs/architecture/11-wire-format.md`: Frame-, UDP-, EXTUDP-, BLE-Referenz, Vektoren
- `docs/wire-compat.md`: Endianness, CONF, `FLASH_STRUCT_VERSION` (nRF52-Blob-Teil überholt)
- `docs/settings-registers.md`: Key/Member-Zuordnung; `src/meshcom_settings.h`: Schema
- `docs/ack-wer-hat-quittiert.md` §5, `docs/hey-storm-2026-10-17.md`, `docs/userstory-hashtag-filter.md`: Flottenregeln
- `docs/test-suite-map.md`: Suite-Inventar (Zahlen veraltet, Gate zählt); `tools/regression.sh`: die drei Stages
- `docs/architecture/08-defect-catalogue.md`: Defect-IDs
- `docs/archive/pr1114-kiss-review-20260901.md`: Muster eines internen PR-Reviews
- `docs/archive/2026-08-29-upstream-sync-verdict.md`: Muster eines Upstream-Sync-Verdicts
- `.claude/commands/code-audit.md`, `.claude/commands/release-firmware.md`

## Anhang C: PR-Body-Vorlage (deutsch)

```markdown
## Zusammenfassung
Ein Absatz: Symptom im Feld (Quelle, Datum, Rufzeichen), Ursache in einem Satz. `Fixes #NNNN`.

## Ursache
1. **<Mechanismus>.** Datei, Funktion, warum es passiert. Commit-Hash, seit wann.

## Änderungen
- `src/<datei>.cpp`, `<funktion>()`: was, warum. (ein Eintrag je Datei/Funktion)

## Was sich auf dem Draht ändert
Nichts. / Frame X bekommt Tag Y am Ende; alte Decoder ignorieren es (geprüft gegen 4.35t).

## Alte Nodes / Downgrade
Was ein 4.35-Node mit dem Frame tut; was dieser Node mit einem 4.35-Frame tut; was ein Node
nach dem Update ohne Kommando tut (Bits, Defaults); Downgrade-Verhalten.

## Test
- Host: `tools/regression.sh --stage 1,2`, N Envs, M Fälle (aus dem Lauf).
- Neuer Test `test/test_<x>`: rot gegen <alter Hash>, grün danach.
- Build: wiscore_rak4631, heltec_wifi_lora_32_V3, ttgo_tbeam (+ Flash/RAM-Delta).
- Bench: DK5EN-90 (RAK), DK5EN-92 (T-Beam), Build-String `<tag>`, Capture <datei>.
- Golden: <Schicht>, Diff leer / Diff erklärt oben.

## Nicht auf Hardware geprüft
- <Pfad>, Grund.

## Nicht in diesem PR
- <Thema>, eigener PR / Fork-ID.

## Hinweise für den Reviewer
- <wo zuerst hinschauen, Twin-Datei, Budget-Rechnung>.
```
