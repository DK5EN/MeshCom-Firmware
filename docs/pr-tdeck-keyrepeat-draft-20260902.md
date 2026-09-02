# PR-Entwurf: T-Deck-Tastatur — Auto-Repeat für Backspace, Leertaste und Alphatasten (TD-10)

> **Status: ENTWURF** (Stand 2026-09-02, Basis `v4.35p_prio` @ `16c0733f`,
> `src/t-deck/` zu diesem Zeitpunkt byte-identisch zu `upstream/dev` @ `6a613547`).
> Zielbranch: icssw-org DEV. Umsetzung, Protokoll-Herleitung und Review-Fixes:
> [`tdeck-keyrepeat-impl-plan-20260902.md`](tdeck-keyrepeat-impl-plan-20260902.md),
> [`review-verdict-tdeck-keyrepeat-20260902.md`](review-verdict-tdeck-keyrepeat-20260902.md).

## Was

Zwei Dateien, ausschließlich T-Deck / T-Deck Plus (`src/t-deck/` wird auf keinem
anderen Board kompiliert):

- **`src/t-deck/kbd_repeat.h` (neu).** Header-only Zustandsautomat, kein
  Arduino-Bezug, läuft unverändert im nativen Testtarget: `struct KbdRepeat`
  (20 B auf dem 32-Bit-Ziel), `kbdRawFrameValid()` (Frame-Plausibilität),
  `kbdExpectedCell()` (bildet ein zugestelltes Zeichen auf die physische
  Matrixzelle `(col,row)` zurück, anhand einer aus dem LilyGo-Quelltext
  transkribierten Matrixtabelle), `kbdRepeatArm()`/`kbdRepeatHold()`/
  `kbdRepeatClear()`.
- **`src/t-deck/tdeck_main.cpp`.** `setup()`: ein `kbdRawMode(false)` direkt nach
  `checkKb()`, damit die C3 nach jedem Boot sicher im Zeichen-Modus startet.
  `keypad_read()`: neuer Zweig am Anfang der Funktion, der einen laufenden
  Halte-Zustand pollt (I2C-5-Byte-Read, Fokus- und Sperr-Check, Timeout) und ihn
  bei Bedarf beendet; am bestehenden Tastenpfad eine Eligibility-Prüfung direkt
  vor `last_key = act_key`, die bei einer erlaubten Taste ein Halte-Fenster
  öffnet. Zwei neue statische Helfer `kbdRawMode()`/`kbdRawRead()` (gleiches
  Muster wie `setKeyboardBacklight()`). Zwei neue `Serial.printf`-Logzeilen
  (`[KBD];rawprobe;…` und `[KBD];repeat;…`, unbedingt, wie das bestehende
  `[KEY];…`).

## Warum

Der Operator wollte am 2026-08-31, dass Halten der Rücktaste mehrere Zeichen
löscht ("Bei der Tastatur wäre es nett, die Zurück-Taste so zu belegen, dass
wenn man drauf bleibt, auch gleich mehrere Buchstaben gelöscht werden."), am
2026-09-02 erweitert auf Leertaste und alle Alphatasten (inkl. ihrer
Shift-/SYM-/Zahlen-Remaps).

Die komplette Wiederholungs-Mechanik existiert bereits in LVGL: der
Keypad-Treiber (`lib/lvgl/src/core/lv_indev.c:471-513`) wiederholt jede
Nicht-Spezialtaste automatisch, solange der Treiber weiter `PRESSED` mit
demselben `data->key` meldet — es fehlte einzig die Information "Taste wird
noch gehalten". Die T-Deck-Tastatur ist ein eigener ESP32-C3
(I2C-Slave `0x55`), der im normalen Zeichen-Modus pro Tastendruck genau ein
Byte liefert, kein Release-Event. Die Stock-Keyboard-Firmware
(`Xinyuan-LilyGO/T-Deck`, `examples/Keyboard_ESP32C3/Keyboard_ESP32C3.ino`,
Quelltext gelesen und gegen die Matrixtabellen `keyboard[col][row]` /
`keyboard_symbol[col][row]` verifiziert) kennt seit Commit `1eb6fb0e`
(2025-06-11) einen Raw-Modus (`0x03` an/`0x04` aus): jeder Request liefert dann
fünf Bytes Live-Matrixzustand, ein Byte pro Spalte, ein Bit pro Zeile,
Bit gesetzt = Taste gedrückt. Diese fünf Bytes reichen, um "noch gehalten" pro
LVGL-Poll zu prüfen; die Wiederholung selbst übernimmt LVGL unverändert.

## Verhalten bei alter Tastatur-Firmware (unverändert)

Eine Tastatur ohne Raw-Mode-Unterstützung verhält sich exakt wie heute — ein
Zeichen pro Tastendruck, kein Repeat. `Wire.requestFrom(0x55, 5)` liefert auf
dem ESP32-Arduino-Core immer entweder 5 Bytes oder 0, nie eine kurze Antwort.
Eine Zeichen-Modus-Tastatur hat ihr eines anstehendes Byte für die
vorangehende 1-Byte-Abfrage bereits geschrieben, und ihr I2C-TX-FIFO wird bei
jedem STOP zurückgesetzt — die 5-Byte-Raw-Anfrage kommt deshalb als
`00 00 00 00 00` zurück, nicht mit `0xFF` aufgefüllt wie ursprünglich
angenommen. Ein solcher All-Zero-Frame gilt als **unentscheidbar**, nicht als
Fehlversuch: der Support-Status bleibt für den ganzen Boot `UNKNOWN`, wird nie
als `NO` markiert, und das Fenster wird nie geöffnet (ein All-Zero-Frame kann
das erwartete Bit nie tragen). Einziger messbarer Effekt: eine zusätzliche
I2C-Transaktion (~1 ms) pro berechtigtem Tastendruck. Die Log-Zeile
`[KBD];rawprobe;…;support;0` erscheint dabei bis zu `KBD_RAW_PROBE_MAX + 2`
(= 5) mal pro Boot, dann nicht mehr.

## Kosten

- **20 Bytes statisch** (`sizeof(struct KbdRepeat)` auf dem 32-Bit-Ziel, ein
  statisches Objekt in `keypad_read()`), gemessen im nativen Test.
- Betrifft ausschließlich T-Deck / T-Deck Plus; kein Einfluss auf Flash/RAM
  irgendeines anderen Boards, da `src/t-deck/` dort nicht kompiliert wird.
- Laufzeit: eine zusätzliche I2C-Transaktion pro berechtigtem Tastendruck
  (Fensteröffnung) plus eine pro LVGL-Poll während eines aktiven Haltens —
  läuft im bestehenden `lv_task_handler()`-Kontext auf dem Hauptloop, dieselbe
  Stelle wie die heutige 1-Byte-Abfrage.

## Tests

39 native Testfälle, `test/test_kbd_repeat/test_main.cpp`,
`pio test -e native -f test_kbd_repeat`: Frame-Plausibilität,
Zellen-Rücktransformation (`kbdExpectedCell`, inkl. Groß-/Kleinschreibung und
SYM-Tabelle), Arm/Halten/Timeout inkl. `millis()`-Rollover, die
Degradations-Fälle (All-Zero unentscheidbar, `0xFF`-Fehlversuch zählt, drei
Fehlversuche → `NO`, ein Erfolg → `YES` und setzt den Fehlversuchszähler
zurück, ein `YES`-Verdikt wird durch spätere Fehlversuche nie wieder
zurückgenommen), `kbdRepeatClear()`.

## Offene Punkte

**Bench-Nachweis durch Operator ausstehend.** Die native Testsuite deckt die
reine Zustandslogik ab; der Nachweis am realen Gerät (Halten löscht wirklich
mehrfach, Leertaste/Alphatasten wiederholen, Enter wiederholt nicht,
Tastatursperre bleibt wirksam, normales Tippen bleibt unauffällig) steht laut
Umsetzungsplan §7 noch aus (DK5EN-14, `/dev/cu.usbmodem1101`). Bis dahin ist
unbekannt, ob DK5EN-14s Tastatur-Firmware den Raw-Modus überhaupt unterstützt;
falls nicht, verhält sich der Knoten nachweislich wie heute (siehe oben).

## PR-Schnitt

Nur `src/t-deck/tdeck_main.cpp` und `src/t-deck/kbd_repeat.h`. Der native Test
(`test/test_kbd_repeat/`) und die zugehörige `platformio.ini`-Änderung
(`[env:native]` `test_filter`) bleiben im Fork, ebenso diese Dokumentation.
