# TD-10 — T-Deck: Auto-Repeat für Backspace (Taste halten = mehrfach löschen)

**Status: ZURÜCKGESTELLT** (Operator-Entscheid 2026-08-31). Analyse abgeschlossen,
Umsetzung auf Abruf. Backlog-Eintrag: `BACKLOG.md` §3.8p follow-ups, Zeile TD-10.

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

Backspace liegt in der Matrix auf Spalte 4, Zeile 3 → Byte 4, Bit 3. Da die
Backlight-Steuerung (`0x01`) auf den Geräten im Feld funktioniert, haben die
Tastaturen dieselbe Firmware-Generation — Raw-Mode-Support ist damit für die Flotte
plausibel, wird aber pro Gerät zur Laufzeit verifiziert (siehe §4).

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

## 5. Offene Punkte / Risiken

- Bit-Polarität und Byte-Reihenfolge des Raw-Frames sind aus dem LilyGo-Quelltext
  nicht eindeutig (active-low?) — einmal am Bench-Gerät DK5EN-14 messen.
- Zeichen, die während des Raw-Fensters getippt würden, kämen erst nach dem
  Rückschalten an — praktisch irrelevant, das Fenster existiert nur, solange der
  Daumen auf Backspace liegt.
- Repeat wirkt auf allen Tabs mit Textfeld gleich; auf Nicht-Text-Tabs ändert sich
  nichts.

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
