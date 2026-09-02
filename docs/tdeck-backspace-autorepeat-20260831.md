# TD-10 — T-Deck: Auto-Repeat für Backspace (Taste halten = mehrfach löschen)

**Status: IMPLEMENTIERT 2026-09-02** (erweitert von Backspace auf Backspace + Space +
Alphatasten). Umsetzung, Protokoll-Verifikation gegen den LilyGo-Quelltext und
Review-Fixes siehe
[`tdeck-keyrepeat-impl-plan-20260902.md`](tdeck-keyrepeat-impl-plan-20260902.md).
Backlog-Eintrag: `BACKLOG.md` §3.8p follow-ups, Zeile TD-10.

**Wunsch (Operator):** "Bei der Tastatur wäre es nett, die Zurück-Taste so zu belegen,
dass wenn man drauf bleibt, auch gleich mehrere Buchstaben gelöscht werden."

**Ergebnis der Prüfung:** Machbar, minimal-invasiv, nur `src/t-deck/tdeck_main.cpp`.
Die komplette Repeat-Mechanik existiert bereits in LVGL — es fehlt einzig die
Information "Taste wird noch gehalten". Die liefert der Raw-Mode der
Stock-Keyboard-Firmware.

## 1. Warum es heute nicht geht

Die T-Deck-Tastatur ist ein eigener ESP32-C3 (I2C-Slave `0x55`), der pro Tastendruck
genau **ein** Zeichen liefert — kein Release-Event, kein Hardware-Repeat (die
LilyGo-Firmware feuert nur auf Zustands*wechsel* der Matrix). `keypad_get_key()`
(`src/t-deck/tdeck_main.cpp:749`) pollt 1 Byte; `keypad_read()` meldet deshalb pro
Zeichen nur einen einzigen PRESSED-Zyklus an LVGL. Halten der Backspace-Taste ist für
den Haupt-ESP32 unsichtbar.

## 2. LVGL kann Repeat von Haus aus

Der Keypad-Treiber in `lib/lvgl/src/core/lv_indev.c:488-514` wiederholt jede
Nicht-Spezialtaste automatisch per `lv_group_send_data()` — nach `long_press_time`
(Default 400 ms), dann alle `long_press_repeat_time` (Default 100 ms; beides pro
`lv_indev_drv_t` einstellbar) — **solange der Treiber weiter `LV_INDEV_STATE_PR` mit
demselben Key meldet**. `0x08` ist `LV_KEY_BACKSPACE` und löscht in der Textarea
direkt. Es ist also keine eigene Repeat-Logik nötig, nur die Halte-Erkennung.

## 3. Der Schlüssel: Raw-Mode der Stock-Keyboard-Firmware

Die LilyGo-C3-Firmware (`Xinyuan-LilyGO/T-Deck`,
`examples/Keyboard_ESP32C3/Keyboard_ESP32C3.ino`) versteht I2C-Kommandos vom Host:

| Cmd    | Wirkung                                                                                              |
| ------ | ---------------------------------------------------------------------------------------------------- |
| `0x01` | Keyboard-Backlight-Duty — **nutzen wir bereits** (`setKeyboardBacklight()`, `tdeck_helpers.cpp:140`) |
| `0x03` | Raw-Mode an: jeder Request liefert 5 Bytes **Live-Matrixzustand** (Byte pro Spalte, Bit pro Zeile)   |
| `0x04` | zurück in den normalen Zeichen-Modus                                                                 |

Backspace liegt in der Matrix auf Spalte 4, Zeile 3 → Byte 4, Bit 3.

**Korrektur (2026-09-02):** Der ursprüngliche Schluss "Backlight (`0x01`) funktioniert
im Feld, also funktioniert auch Raw-Mode (`0x03`)" ist **falsch** und wurde bei der
Umsetzung verworfen. Ein Blick in die LilyGo-Historie zeigt zwei unabhängige
Firmware-Generationen: `0x01` (Backlight) existiert bereits seit dem Commit vom
2024-12-25, `0x03`/`0x04` (Raw-Mode) erst seit Commit `1eb6fb0e` vom 2025-06-11 — ein
gutes halbes Jahr später. Dass ein Gerät die Backlight-Taste bedienen kann, sagt also
nichts über Raw-Mode-Unterstützung aus; die beiden Kommandos sind nur formal
benachbart. Der Support-Status wird deshalb ausschließlich zur Laufzeit pro Boot
ermittelt (`kbd_raw_support`/`KbdRepeat.support` in `src/t-deck/kbd_repeat.h`, siehe
Umsetzungsplan §2/§3.2), nie aus der Backlight-Fähigkeit abgeleitet.

Bit-Polarität und Frame-Aufbau sind mittlerweile aus dem LilyGo-Quelltext verifiziert
(nicht mehr nur vermutet wie in §5 unten): `onRequest` im Raw-Mode schreibt fünf Bytes,
je eines pro Spalte, `val |= (lastValue[col][row] << row)` mit
`lastValue = digitalRead(row) == LOW` bei aktiv gezogener Spalte — **Bit gesetzt =
Taste gedrückt** (active-high im Frame). Details und die vollständige
Matrix-Übersetzung: Umsetzungsplan §2/§3.2.

## 4. Vorgeschlagener Ansatz (~40-60 Zeilen, nur `tdeck_main.cpp`)

Kommt ein `0x08` an: kurz in den Raw-Mode schalten und pro LVGL-Poll das
Backspace-Bit prüfen. Solange gesetzt → weiter `LV_INDEV_STATE_PR` melden (LVGL
wiederholt dann selbst). Bei Loslassen — oder Timeout ~5 s als Sicherheitsnetz —
`0x04` senden, Release melden, alles wie vorher. Der Raw-Mode ist nur während des
Haltens aktiv; der normale Zeichenpfad bleibt unangetastet.

**Absicherung für alte Keyboard-Firmware:** Direkt nach dem `0x08` muss der erste
Raw-Frame das Backspace-Bit als gedrückt zeigen (der Finger ist noch drauf). Zeigt er
das nicht (altes Keyboard ignoriert `0x03`, liefert Müll/0xFF-Padding), wird Raw-Mode
dauerhaft als "nicht unterstützt" markiert → Verhalten exakt wie heute, ein Zeichen
pro Druck. Das kalibriert nebenbei die Bit-Polarität.

## 5. Offene Punkte / Risiken (Stand 2026-09-02)

- ~~Bit-Polarität und Byte-Reihenfolge des Raw-Frames sind aus dem LilyGo-Quelltext
  nicht eindeutig (active-low?) — einmal am Bench-Gerät DK5EN-14 messen.~~ **Gelöst
  ohne Bench-Messung:** aus dem Quelltext verifiziert (active-high, Byte = Spalte, Bit
  = Zeile, siehe §3 oben und Umsetzungsplan §2). Die tatsächliche
  Bench-Bestätigung an DK5EN-14 steht noch aus (Umsetzungsplan §7); bis dahin gilt der
  Quelltext-Befund als Auslegung, nicht als Feldnachweis.
- Zeichen, die während des Raw-Fensters getippt würden, kommen erst nach dem
  Rückschalten an — akzeptiert, dokumentiert im Umsetzungsplan §3.5 ("Stale char after
  0x04").
- Repeat wirkt auf allen Tabs mit Textfeld gleich; auf Nicht-Text-Tabs ändert sich
  nichts. Bestätigt durch die Eligibility-Regeln im Umsetzungsplan §3.3 (Map-Tab-Tasten
  und SYM-Kombinationen setzen `bSPEC` und öffnen kein Repeat-Fenster).
- **Neu gelöst während der Umsetzung** (siehe Review-Verdikt,
  [`review-verdict-tdeck-keyrepeat-20260902.md`](review-verdict-tdeck-keyrepeat-20260902.md)):
  altes Keyboard liefert einen unentscheidbaren All-Zero-Frame statt der ursprünglich
  angenommenen `0xFF`-Auffüllung (K1); nichts schaltet nach einem ESP32-Reset zurück in
  den Zeichen-Modus (K2, jetzt beim Boot erzwungen); der Support-Status konnte nach
  einem `YES`-Verdikt fälschlich wieder auf `NO` zurückfallen (K3); ein Fokuswechsel
  während des Haltens lenkte die Wiederholung auf das falsche Feld um (K5).

## 6. Verworfene Alternativen

- **SYM-Kombination als "Zeile löschen"** (`lv_textarea_set_text("")`): trivial, kein
  Protokollrisiko, aber andere UX als gewünscht — Fallback, falls Raw-Mode im Feld
  Probleme macht.
- **Alternative Keyboard-Firmware** mit echtem Hardware-Repeat
  (z. B. `rgrizzell/lilygo-t-deck-keyboard`): scheidet aus, jeder Nutzer müsste den
  C3 separat flashen (interner Header, Gehäuse öffnen).

## Quellen

- LilyGo Keyboard-Firmware:
  <https://github.com/Xinyuan-LilyGO/T-Deck/blob/master/examples/Keyboard_ESP32C3/Keyboard_ESP32C3.ino>
- Keyboard-Interface-Übersicht: <https://deepwiki.com/Xinyuan-LilyGO/T-Deck/3.3-keyboard-interface>
- Community-Keyboard-Firmware: <https://github.com/rgrizzell/lilygo-t-deck-keyboard>
