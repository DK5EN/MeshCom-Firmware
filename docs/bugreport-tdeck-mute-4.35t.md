# Bugreport: T-Deck / T-Deck Plus, Ton lässt sich mit 4.35t nicht abschalten

Datum: 2026-09-13
Melder: Feldbericht (T-Deck Plus, 4.35t), analysiert von DK5EN
Betroffen: upstream v4.35t, upstream v4.35t.09.12, Fork v4.35t.09.12.2 (t_deck und t_deck_plus)

## Kurzfassung

Der Schalter "Sound on" in den T-Deck-Einstellungen und der serielle Befehl `--mute on/off` sind in
jedem ausgelieferten 4.35t-Image wirkungslos. Der Handler für `--mute` liegt in
`src/command_functions.cpp` innerhalb von `#if INSTRUMENT_ENABLED`. Dieses Flag ist im Release-Build
0, der Handler wird also nicht kompiliert. Der GUI-Schalter ruft seit HL-03 genau diesen Befehl auf und
läuft damit ins Leere. Einzig SYM+M schaltet den Ton stumm, speichert das aber nicht; nach dem
nächsten Reset ist der Ton wieder da.

## Welcher Ton

Beide Töne kommen aus `src/esp32/esp32_audio.cpp` über den I2S-Lautsprecher als CW-Ersatz, wenn keine
MP3-Datei auf der SD-Karte liegt:

| Auslöser                                 | Ton                     | Codepfad                                                           |
| ---------------------------------------- | ----------------------- | ------------------------------------------------------------------ |
| jede neu angezeigte eingehende Nachricht | CW-Zeichen "r" (`.-.`)  | `lv_obj_functions.cpp` msg_focus_and_alert → audio_play_file_or_cw |
| Boot                                     | CW-Startkennung `-.-.-` | `tdeck_main.cpp` startAudio → audio_play_file_or_cw                |

Der gemeldete "nervige" Ton ist der Nachrichtenton.

## Ursache

1. HL-03 (2026-08-30): Der GUI-Schalter `btn_soundon` in `src/t-deck/event_functions.cpp` rief vorher
   `audio_set_mute()` direkt und speicherte dann den alten Wert. Seitdem ruft er
   `commandAction("--mute on")` bzw. `commandAction("--mute off")`, damit Zustand und Persistenz
   denselben Weg gehen.
2. Firmware-only-Schnitt (2026-09-01, `2e874921`): Der Bench-Block in `src/command_functions.cpp` wurde
   in `#if INSTRUMENT_ENABLED` eingeschlossen. Der komplette T-Deck-Befehlsblock liegt innerhalb dieses
   Guards. In v4.35t: Guard Zeile 4786 bis 5362, T-Deck-Block Zeile 4945 bis 5242.
3. INS-01 (2026-09-06, `b0d10b59`): `--udplog`, `--udpstat`, `--wifistat`, `--ethstat` wurden vor den
   Guard gezogen. Die fünf T-Deck-Feldbefehle blieben drin: `--mute on/off`, `--persistflash`,
   `--persistsd`, `--immediatesave`, `--persiststat`. Ebenfalls betroffen: `--playtone`.
4. `INSTRUMENT_ENABLED` ist per Default 0 (`src/instrument.h`), kein Build-Environment setzt es. In jedem
   Release-Image fehlen die Handler. Der Hilfetext wirbt trotzdem für `--mute on/off`, weil diese Zeile
   nur mit BOARD_T_DECK/BOARD_T_DECK_PLUS geguarded ist.

Folge: `commandAction("--mute on")` findet keinen Handler, `node_mute` bleibt false, der Ton spielt
weiter.

## Bedingungen

- Board T-Deck oder T-Deck Plus (ENABLE_AUDIO gesetzt, Lautsprecher vorhanden).
- Release-Build ohne `-D INSTRUMENT_ENABLED=1`. Das sind alle veröffentlichten Images.
- Keine MP3 für den Nachrichtenton auf der SD (sonst spielt die MP3 statt des CW-Tons, aber ebenfalls
  ohne Abschaltmöglichkeit).
- `node_mute` im Flash ist false. Wer vor 4.35s mit dem alten direkten GUI-Pfad einmal true gespeichert
  hat, hört nichts und sieht den Fehler nicht.

## Nachvollziehen

### String-Scan der Release-Images

Der Marker `[AUDIO];mute;` stammt aus dem `--mute`-Handler. `[AUDIO]..muted` stammt aus dem
ungeguardeten Audio-Code und muss immer vorhanden sein. `[UDP];log` ist der INS-01-Kontrollmarker.

```
strings t_deck_plus.bin | grep -c 'AUDIO\];mute;'
strings t_deck_plus.bin | grep -c 'AUDIO\]\.\.muted'
```

| Image                                   | `[AUDIO];mute;` | `[AUDIO]..muted` | Hilfe `mute on/off` | `[UDP];log` |
| --------------------------------------- | --------------: | ---------------: | ------------------: | ----------: |
| upstream v4.35t, t_deck_plus.bin        |               0 |                1 |                   1 |           1 |
| upstream v4.35t.09.12, t_deck_plus.bin  |               0 |                1 |                   1 |           1 |
| upstream v4.35t.09.12, t_deck.bin       |               0 |                1 |                   1 |           - |
| Fork v4.35t.09.12.2, t_deck_plus.bin    |               0 |                1 |                   1 |           - |
| lokaler Build t_deck_plus, INSTRUMENT 0 |               0 |                1 |                   1 |           - |

Quelle der Images: GitHub-Releases icssw-org/MeshCom-Firmware (v4.35t vom 2026-09-10, v4.35t.09.12
vom 2026-09-12) und DK5EN/MeshCom-Firmware (v4.35t.09.12.2).

### Am Gerät

1. Seriell `--mute on` senden. Erwartet: `[AUDIO];mute;1`. Beobachtet: keine Antwort.
2. Seriell `--persiststat` senden. Erwartet: eine Statuszeile. Beobachtet: keine Antwort.
3. In den Einstellungen "Sound on" ausschalten, dann eine Nachricht an den Knoten schicken. Der
   CW-Ton kommt trotzdem.
4. SYM+M drücken: Ton ist stumm. Reset auslösen: Ton ist wieder da.
5. Gegenprobe: mit `-D INSTRUMENT_ENABLED=1` bauen. Der String ist im Image, Schalter und `--mute`
   funktionieren.

### Im Quelltext (v4.35t, `6edc7499`)

```
git show v4.35t:src/command_functions.cpp | awk 'NR>=4700 && NR<=5400 && /^[ \t]*#(if|endif|else|elif)/ {print NR": "$0}'
```

Zeigt `#if INSTRUMENT_ENABLED` bei 4786 ohne schließendes `#endif` vor 5362; der T-Deck-Block mit
`--mute` (4945 bis 5242) liegt dazwischen.

## Vorschlag zur Behebung

Die fünf Feldbefehle `--mute on/off`, `--persistflash`, `--persistsd`, `--immediatesave` und
`--persiststat` in einen eigenen, nur mit BOARD_T_DECK/BOARD_T_DECK_PLUS geguardeten Block vor
`#if INSTRUMENT_ENABLED` verschieben, nach dem Muster von INS-01. `--playtone`, `--tft`, `--screencrc`
und die übrigen UI-Hooks bleiben im Bench-Block. Vor dem Release das gebaute Image auf
`[AUDIO];mute;` scannen.

Optional zusätzlich: SYM+M über denselben Befehlspfad führen, damit die Tastenkombination ebenfalls
speichert.
