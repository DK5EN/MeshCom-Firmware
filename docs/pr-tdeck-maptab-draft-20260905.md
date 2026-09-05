# PR-Entwurf: T-Deck – Karte wird beim Tab-Wechsel über die Tab-Leiste doppelt aufgebaut (TD-14)

Ziel: `icssw-org/MeshCom-Firmware`, Branch `dev`. Betrifft nur `src/t-deck/`.

## Symptom

Wird auf dem T-Deck der Karten-Tab über die Tab-Leiste (Trackball-Klick auf den Kartenbutton)
gewählt, wird die SD-Karte zweimal hintereinander komplett aufgebaut: einmal für die Ansicht mit
sichtbarer Leiste (294x140 px), einmal nach dem Einklappen der Leiste (294x182 px). Jeder Aufbau
kostet 470-750 ms mit blockierter Hauptschleife, Cursor und Touch stehen dabei rund 1,3 s.

## Ursache

LVGL setzt auf der Buttonmatrix der Tabview `LV_OBJ_FLAG_EVENT_BUBBLE` (`lv_tabview.c:233`). Ein
Klick auf einen Tab-Button liefert `tabview_event_cb()` deshalb zweimal:

1. aus `cont_scroll_end_event_cb` (`lv_tabview.c:351`) innerhalb von `lv_tabview_set_act`, Target
   ist die Tabview, die Leiste ist noch sichtbar;
2. das eigene `LV_EVENT_VALUE_CHANGED` der Buttonmatrix, das zur Tabview hochblubbert, Target ist
   die Buttonmatrix, die Leiste ist inzwischen versteckt.

Der serielle Pfad `lv_tabview_set_act()` erzeugt nur Ereignis 1.

## Änderungen

### `src/t-deck/event_functions.cpp`, `tabview_event_cb()`

- Am Anfang des `VALUE_CHANGED`-Zweigs wird das hochgeblubberte Duplikat verworfen:
  `if (lv_event_get_target(e) != lv_event_get_current_target(e)) return;`
- Im Fall 3 (Karte) wird `tdeck_hide_tab_menu()` vor `sdmap_refresh()` gerufen, damit der einzige
  verbleibende Aufbau bereits den Viewport ohne Leiste misst (`sdmap_refresh()` ruft selbst
  `lv_obj_update_layout()`). Der generische Aufruf am Ende des Callbacks bleibt für die anderen
  Tabs erhalten.

### `src/t-deck/tdeck_sdmap.cpp`, `tdeck_sdmap.h`

- `sdmap_refresh()` bekommt einen Aufrufer-Tag `why` (Default `"?"`), der in der Logzeile
  `Karte zusammengesetzt ... from=<tag>` erscheint. Alle sechs Aufrufer sind getaggt
  (`tab`, `zoom`, `pan`, `recenter`, `setmap`, `beacon`, `boundary`).
- Dedupe-Schutz: Stimmen Kartenset, Zoom (nach Zoom-Fallback), Mittelpunkt (lat/lon) und
  Viewport-Größe mit dem letzten erfolgreichen Aufbau überein und existiert das zusammengesetzte
  Bild, wird der Aufbau übersprungen (`Karte unveraendert, Aufbau uebersprungen from=<tag>`). Jeder
  Fehlerpfad und `sdmap_set_active_set()` machen den Schlüssel ungültig, echte Änderungen
  (Set, Zoom, Pan, neue Position) treffen den Schlüssel nie.

### Weitere Aufrufer (nur Tag-Argument)

`src/t-deck/lv_obj_functions.cpp` (zoom, pan, recenter, setmap, beacon), `src/esp32/esp32_main.cpp`
(boundary).

## Nachweis

Bench-Szenario `tools/bench/tdeck_harness.py --scenario map_tab_pick` (Trackball auf den
Kartenbutton, Klick, Zählung der Aufbau-Zeilen) auf DK5EN-14, T-Deck Plus, 2026-09-05:

| Stand   | Aufbauten                       | Loop-Lücke lvgl |
| ------- | ------------------------------- | --------------- |
| vorher  | 2 (294x140 px, dann 294x182 px) | 1337 ms         |
| nachher | 1 (294x182 px)                  | 718 ms          |

Der verbleibende Aufbau ist die Kachelkosten selbst (separat als TD-09 Kachel-Cache geführt).
