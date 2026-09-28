# PR-Entwurf: T-Deck: Ton laesst sich mit 4.35t nicht abschalten

Eingereicht 2026-09-13 als [PR #1141](https://github.com/icssw-org/MeshCom-Firmware/pull/1141) (Branch
`pr-tdeck-mute-20260913`, zwei Commits auf `dev` `1cb2d9e6`).

Ziel: `icssw-org/MeshCom-Firmware`, Branch `dev`. Eine Feldmeldung gegen 4.35t, zwei Commits,
nur `src/`. Bugreport: `docs/bugreport-tdeck-mute-4.35t.md`.

## 1. "Sound on" und `--mute on/off` sind in jedem 4.35t-Image wirkungslos

### Symptom

T-Deck und T-Deck Plus spielen bei jeder eingehenden Nachricht einen CW-Ton ("r") und beim
Boot die Startkennung ueber den I2S-Lautsprecher. Der Schalter "Sound on" in den Einstellungen
aendert daran nichts, der serielle Befehl `--mute on` antwortet nicht, obwohl `--help` ihn
anbietet. Einzig SYM+M schaltet den Ton stumm, nach dem naechsten Reset ist er wieder da.
Betroffen: upstream v4.35t und v4.35t.09.12, `t_deck` und `t_deck_plus`.

### Ursache

Der Handler fuer `--mute on/off` liegt in `src/command_functions.cpp` innerhalb von
`#if INSTRUMENT_ENABLED` (v4.35t: Guard ab Zeile 4786, T-Deck-Block 4945 bis 5242). Das Flag
ist per Default 0 (`src/instrument.h`), kein Build-Environment setzt es; in jedem Release-Image
fehlt der Handler. Der GUI-Schalter `btn_soundon` (`src/t-deck/event_functions.cpp`) ruft seit
4.35s `commandAction("--mute on")` bzw. `"--mute off"`, damit Zustand und Speicherung denselben
Weg gehen -- und laeuft damit ins Leere: `node_mute` bleibt false, der Ton spielt weiter.
Dasselbe gilt fuer `--persistflash`, `--persistsd`, `--immediatesave` und `--persiststat`.

Nachweis am Image: `strings t_deck_plus.bin | grep -c 'AUDIO\];mute;'` liefert 0 auf upstream
v4.35t, v4.35t.09.12 (`t_deck` und `t_deck_plus`); der Kontrollmarker `[AUDIO]..muted` aus dem
ungeguardeten Audio-Code ist in jedem Image vorhanden.

### Aenderung (Commit 1)

- `src/command_functions.cpp`: die fuenf Feldbefehle `--mute on/off`, `--persistflash on/off`,
  `--persistsd on/off`, `--immediatesave on/off` und `--persiststat` stehen in einem eigenen
  Block `#if defined(BOARD_T_DECK) || defined(BOARD_T_DECK_PLUS)` vor `#if INSTRUMENT_ENABLED`,
  direkt hinter den Felddiagnosen `--udplog`/`--udpstat`/`--wifistat`. Die Handler sind
  unveraendert verschoben, die else-Kette bleibt intakt. Der innere T-Deck-Guard um
  `loadPosPersistence()` in `--persistsd` entfaellt, weil der ganze Block jetzt T-Deck-geguardet
  ist. Der Kommentar ueber der Hilfezeile beschreibt den neuen Ort. `--playtone`, `--tft`,
  `--screencrc` und die UI-Hooks bleiben im Bench-Block.

## 2. SYM+M stummt, speichert aber nicht

### Ursache

`keypad_read()` in `src/t-deck/tdeck_main.cpp` ruft fuer SYM+M `audio_set_mute()` direkt. Die
Funktion setzt `node_mute` und schaltet die Hardware, speichert aber nicht; nach dem naechsten
Reset steht der alte Wert wieder im Flash.

### Aenderung (Commit 2)

- `src/t-deck/tdeck_main.cpp`, SYM+M-Zweig: ruft `commandAction("--mute off")` bei gesetztem
  `node_mute`, sonst `commandAction("--mute on")`. Damit gehen Tastenkombination, GUI-Schalter
  und serieller Befehl durch denselben Handler: `node_mute` setzen, `audio_set_mute()`,
  `save_settings()`. Der `ENABLE_AUDIO`-Zweig entfaellt; beide T-Deck-Varianten definieren das
  Flag, und der Handler selbst ist nur T-Deck-geguardet.

## Nachweis

Build: `t_deck`, `t_deck_plus`, `heltec_wifi_lora_32_V3`, `wiscore_rak4631` sauber.

String-Scan der gebauten Images (`strings firmware.bin | grep -c ...`):

| Image                    | `[AUDIO];mute;` | `[PERSIST];stat;` | Hilfe `mute on/off` | `injectraw` |
| ------------------------ | --------------: | ----------------: | ------------------: | ----------: |
| t_deck_plus (vorher)     |               0 |                 0 |                   1 |           0 |
| t_deck_plus (nachher)    |               2 |                 1 |                   1 |           0 |
| t_deck (nachher)         |               2 |                 1 |                   1 |           0 |
| heltec_wifi_lora_32_V3   |               0 |                 0 |                   0 |           0 |
| wiscore_rak4631 (`.elf`) |               0 |                 0 |                   0 |           0 |

Der Bench-Block bleibt draussen (`injectraw` 0), die Feldbefehle sind nur auf den T-Deck-Envs
drin.

T-Deck Plus (DK5EN-14), Fix-Image ueber USB geflasht, seriell:

```
--persiststat   -> [PERSIST];stat;flash;0;sd;0;immediate;0;mute;1
--mute off      -> [AUDIO];mute;0
--persiststat   -> [PERSIST];stat;flash;0;sd;0;immediate;0;mute;0
--reboot        -> Boot mit [AUDIO];play;cw;start (Startkennung spielt)
--persiststat   -> [PERSIST];stat;flash;0;sd;0;immediate;0;mute;0   (gespeichert)
--mute on       -> [AUDIO];mute;1
--reboot        -> Boot ohne [AUDIO];play;cw;start
--persiststat   -> [PERSIST];stat;flash;0;sd;0;immediate;0;mute;1   (gespeichert)
```

Auf dem 4.35t-Image antwortet derselbe Knoten auf `--mute on` und `--persiststat` nicht.

SYM+M (Commit 2) nur compile-verifiziert; der Handtest steht aus.
