# RAM-Rückgewinn: Byte-Ringe statt Schlitzfelder

Stand 20.09.2026. Branch `neo-ram-reclaim`, abgezweigt von `fork-neo-test`
(5ed69ee2). Experimentell: `fork-neo-test` bleibt unangetastet, bis der
Dauerlauf auf DK5EN-98 ausgewertet ist.

## Worum es geht

Die Speicherkarte (`~/Desktop/meshcom-speicherkarte.html`) hatte gezeigt, dass
vier Frame-Ringe mit je 20 Schlitzen zu 246 bis 260 Byte den größten Posten
unseres statischen RAM stellen: 20 440 Byte auf der 20er-Klasse. Der
Dauerlauf (DK5EN-1, 12 h, 639 Frames) ergab eine mittlere Frame-Länge von 77
Byte bei 111 Byte Maximum. Die Schlitze waren also zu 70 % Reserve.

Die Idee, Rufzeichen gegen Symbole zu tauschen, wurde geprüft und verworfen:
Rufzeichen sind 42 % der Draht-Bytes, aber nur 12 % des reservierten
Schlitzes, und ein Schlitz schrumpft nicht mit seinem Inhalt. Die Reserve
selbst ist die Redundanz. Deshalb: die Frames dicht hintereinander ablegen.

## Was umgebaut wurde

**Option 1, Byte-Ringe** (`src/byte_fifo.h`, `src/byte_fifo.cpp`). Die drei
reinen FIFOs, Telefon-Daten, Telefon-Kommandos und UDP-Ausgang, liegen jetzt
als `[len][payload]`-Folge in einem Byte-Ring. Kein Kodieren, kein
Wörterbuch, die Nutzlast bleibt byte-genau die Draht-Form. Eigenschaften:

- Zwei Lesestellen: `tail` für den Verbraucher, `oldest` für den Verlauf,
  den die Web-Oberfläche als Nachrichtenseite liest. Ein Frame bleibt nach
  dem Senden liegen, bis der Platz gebraucht wird.
- Verdrängung wirft die ältesten Frames weg und zählt, wie viele davon
  ungelesen waren. Der UDP-Ring meldet das unter `--loradebug` als
  `RING_OVERFLOW buf=udp lost=N`; vorher war der Überlauf dort stumm.
- Ein Generationszähler auf `tail` lässt den UDP-Drain erkennen, ob sein
  Frame während des Sendens verdrängt wurde. Sonst hätte `pop()` den
  nächsten Frame getroffen.
- Auf nRF52 sperrt jede Operation selbst (`taskENTER_CRITICAL`); die Aufrufer
  haben ihre eigenen kritischen Abschnitte verloren. Auf ESP32 wie bisher
  ohne Sperre.
- Kein IRAM: keine Funktion trägt `IRAM_ATTR`, nur `memcpy` wird gezogen.

Der TX-Ring (`ringBuffer`) bleibt ein Schlitzfeld. Er braucht Zustand je
Schlitz und wahlfreien Zugriff für Retry und Priorität.

Größen je Speicherklasse (`configuration_global.h`):

| Klasse                    | Telefon | Kommandos | UDP   | vorher            |
| ------------------------- | ------- | --------- | ----- | ----------------- |
| klassischer ESP32         | 2 048   | 2 048     | 2 048 | 5 200/4 920/5 120 |
| ESP32-S3, RAK4631         | 3 072   | 2 048     | 3 072 | 5 200/4 920/5 120 |
| ENABLE_XML / SBUFFER      | 2 048   | 1 536     | 2 048 | 5 200/4 920/5 120 |
| ENABLE_TBEAM (Entwickler) | 1 024   | 1 024     | 1 024 | 2 600/2 460/2 560 |

2 048 Byte fassen rund 25 typische Frames, mehr als die 20 Schlitze vorher.

**Option 2, `web_header_collect`.** Der 1-kB-Sammelpuffer der
Web-Oberfläche sammelt jetzt direkt im `String`, der ihn ohnehin kopierte,
mit einmaligem `reserve()`. Das ist eine Verschiebung aus dem Linkerbild in
den Heap, kein Byte weniger zur Laufzeit. Sie entlastet die Linkregion, die
auf den klassischen ESP32 das Nadelöhr ist.

**Option 3, Display-Cache.** Zeilenkoordinaten als `int16_t` statt `int`
(halbiert `pageLine`/`pageLastLine`). Die Langtext-Seiten
`pageLastTextLong1/2` (1 350 Byte) gibt es nur noch auf Boards mit TFT oder
E-Paper; auf OLED-Boards waren sie immer leer, weil alle Schreibstellen in
deren `#if`-Blöcken liegen. `src/display_pages_cfg.h` hält die Vereinigung
der Guards.

**Option 4, Settings-Feldbreiten: verworfen.** Die vier Laufzeitfelder
`node_ip/dns/gw/subnet` (je 40 Byte) auf 20 zu kürzen hätte 80 Byte
gebracht. Der Host-Test `native_ble_settings_v1` hat es gestoppt:
`bleSettingsToV1()` kopiert `sizeof(out.node_ip)` = 40 Byte aus dem
Quellfeld, ein kürzeres Feld läse hinter sein Ende. Die übrigen Felder
sind entweder persistiert oder in ihrer Breite begründet. Der Struct ist
nicht die Reserve, für die er in der Speicherkarte aussah.

## Gemessen

Regionen aus der Linker-Map (`tools/resource_watch.py regions`), gleiche
Messung wie in der Speicherkarte. Vergleich gegen `fork-neo-test`.

| Board                  | DRAM vorher | DRAM jetzt | Delta   | IRAM |
| ---------------------- | ----------- | ---------- | ------- | ---- |
| ttgo_tbeam             | 103 012     | 91 060     | −11 952 | ±0   |
| ttgo-lora32-v21        | 102 964     | 91 028     | −11 936 | ±0   |
| E22_XML-DevKitC        | 106 764     | 94 324     | −12 440 | ±0   |
| heltec_wifi_lora_32_V3 | 173 992     | 164 104    | −9 888  | ±0   |

Zwölf Boards gebaut (drei nRF52, ein TFT, drei E-Paper, T-Deck, T-Deck Pro),
35 Host-Suiten grün, darunter der neue `native_byte_fifo` (13 Fälle) und der
umgebaute UDP-Twin (18 Fälle).

## Der Dauerlauf

DK5EN-98 (Heltec V3) per OTA geflasht, Konsole auf dem Raspberry Pi
mitgeschnitten. Auswertung mit `tools/reclaim_eval.py <log>`:

- Heap aus der STAT-Zeile (`heap=`). Ausgangslage vom Vorabend, Build
  19.09., acht Stunden: 132 352 → 132 208, kein Neustart.
- Neustarts (`[INIT]`), Uptime-Sprünge.
- `RING_OVERFLOW` mit `lost=`, Telefon-Ring `lost>0`.
- TX-Ring `ringmax`/`drop`, unverändert erwartet.

Was der Dauerlauf nicht prüft und noch offen ist: die BLE-Verbindung mit der
App (Kommando-Ring beim Verbinden, Nachrichten-Ring beim Empfang) und die
Nachrichtenseite der Web-Oberfläche. Beides von Hand.
