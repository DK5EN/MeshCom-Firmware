# Stack-Budget-Gate (`tools/stack_budget.py`)

## Kontext: was dieses Gate absichert

`ARDUINO_LOOP_STACK_SIZE=12288` steckt bereits in fork-main
(`platformio.ini:574`, Commit `c754d01f`) -- der Flag ist keine Vorschlag
dieses Dokuments, sondern der Ist-Zustand. Grund war die Extern-UDP-Kette:
`esp32loop -> ... -> sendExtern -> decodeAPRS -> printfdeb -> MeshSerial/lwIP`
passte nicht in den arduino-esp32-Default von 8192 B. Die flache
`[esp32]`-Basis vererbt den Flag an alle Board-Envs, die `extends = esp32`
schreiben; sechs Envs erben NICHT davon und tragen die Zeile in ihrer
eigenen `variants/*/platformio.ini` selbst: `t5_epaper`, `t_deck_pro`,
`vision-master-e290`, `wireless-paper`, `vision-master-e213` und
`vision-master-e213-preview` (Letzteres, weil es `build_flags` komplett
ueberschreibt). `esp32-external-radio` erbt ueber `env:t_deck_pro` mit.

Dieses Skript ist das Gate, das beweist, dass 12288 B tatsaechlich reicht --
nicht nur fuer den einen damals gemessenen Fall, sondern bei jedem
Nachbau. Es ist tree-unabhaengig aus fork-neo-test uebernommen; der
Algorithmus wurde nicht veraendert.

fork-main hat 14 `native*`-Host-Test-Envs (`grep -c '^\[env:native'
platformio.ini`), nicht die 35 aus dem neo-Zweig -- die Zahl ist hier ohne
Bedeutung fuer das Gate selbst, aber eine haeufige Verwechslungsquelle beim
Uebertragen von Text zwischen den Zweigen.

## Was es misst

Fuer einen oder mehrere `--root`-Symbole aus einem gelinkten ELF folgt das
Skript dem direkten Call-Graph (`call0`/`call4`/`call8`/`call12`) und
addiert je Funktion die Framegroesse aus dem Windowed-ABI-Prolog
(`entry a1, N`), den xtensa-gcc fuer jede nicht-Blattfunktion erzeugt. Das
erfasst auch Bibliothekscode (lwIP, esp-idf, ...), nicht nur Dateien mit
einer `.su`-Sidecar-Datei. Ausgegeben wird pro Root die tiefste gefundene
Kette mit jeder Framegroesse, die Summe, das Budget und PASS/FAIL. Der
Exitcode ist ungleich null, sobald eine Kette `budget - margin`
ueberschreitet.

Root-Namen duerfen gemangelt oder demangelt angegeben werden (`getExtern`
findet `_Z9getExternPhi` ueber den demangelten Namen). Ein unbekannter oder
mehrdeutiger Root ist ein harter Fehler mit Fehlermeldung (bei Mehrdeutigkeit
die passenden Symbole, bei Unbekanntem die naechstliegenden Namen) -- nie ein
stiller 0-Byte-Treffer, denn genau das waere der schlimmste Fehlerfall fuer
ein Gate: ein Tippfehler im Root-Namen wuerde sonst als "PASS" durchlaufen.

Fuer fork-main relevant sind zwei Root-Symbole: `esp32loop` (die Haupt-Loop,
Einstieg fuer jede eingehende/ausgehende Nachricht) und `getExternUDP` (der
Extern-UDP-Empfangspfad, der Anlass fuer die Erhoehung auf 12288 B war).

## Die Namensfalle: `ARDUINO_LOOP_STACK_SIZE` vs. `CONFIG_ARDUINO_LOOP_STACK_SIZE`

Der Flag heisst `ARDUINO_LOOP_STACK_SIZE`, NICHT
`CONFIG_ARDUINO_LOOP_STACK_SIZE`. Letzteres definiert die `sdkconfig.h` des
Frameworks selbst auf 8192 und wird nach der Kommandozeile eingebunden,
gewinnt also -- still, ohne Warnung, ohne dass `-Werror` (das nur auf
`build_src_flags` steht) etwas dazu sagt. `cores/esp32/main.cpp` prueft
`ARDUINO_LOOP_STACK_SIZE` zuerst; nur dieser Name greift. Wer den Flag mit
dem `CONFIG_`-Praefix schreibt, bekommt anstandslos 8192 B zurueck und
erfaehrt es erst, wenn dieses Gate (oder das Geraet im Feld) darauf faellt.

## Grenze der Analyse (bitte lesen, bevor ein PASS als Beweis zaehlt)

Zwei Dinge sieht das Skript grundsaetzlich nicht:

- **Indirekte Calls** (`callx0/4/8/12`) -- das Ziel steht erst zur Laufzeit
  fest. Jeder indirekte Call auf dem berichteten Pfad wird als Warnung
  ausgegeben; die genannte Zahl ist dann eine **untere Schranke**, kein
  exakter Wert. Auf fork-main betrifft das beide Root-Ketten: sowohl
  `esp32loop` als auch `getExternUDP` laufen ueber `printfdeb`, `lwip_sendto`
  und mehrere lwIP-/Diag-Funktionen, die als indirekte Calls markiert sind --
  der reale Bericht listet diese Symbole namentlich in einer WARNING-Zeile,
  nicht nur als Fussnote hier.
- **Interrupts.** ESP-IDF schaltet beim Interrupt-Dispatch auf einen eigenen
  ISR-Stack je CPU (`CONFIG_FREERTOS_ISR_STACKSIZE`); auf dem Stack der
  unterbrochenen Task landen nur der Exception-Frame und der Fenster-Spill,
  je Verschachtelungsebene einige hundert Byte. Der Beitrag ist also
  begrenzt, aber nicht null, und das Skript kennt ihn nicht -- das ist der
  Grund fuer den Margin (siehe unten), nicht ein Bug.

Ausserdem: Ein Zyklus im Call-Graph (Rekursion, direkt oder gegenseitig)
bricht den Walk an der Wiedereintrittsstelle ab und wird als lautes WARNING
im Bericht markiert, nicht still abgeschnitten -- eine rekursive Kette hat
keine statische obere Grenze, die berichtete Zahl ist dann per Definition zu
niedrig.

Kurz: ein PASS dieses Gates ist ein notwendiger, kein hinreichender Beweis,
dass der Stack reicht. Es beweist, dass der direkt sichtbare Aufrufpfad
passt -- nicht, dass kein indirekter Call oder keine ISR-Verschachtelung den
tatsaechlichen Verbrauch ueber das Budget treibt.

## Warum `--margin` (Default 512 Byte)

Der Margin wird vor dem PASS/FAIL-Vergleich vom Budget abgezogen
(`threshold = budget - margin`). Bei Budget 12288 B ist der Schwellwert also
11776 B. Er deckt zwei kleinere, aber reale Unschaerfen der Analyse selbst
ab:

1. Ein `call*`, dessen Ziel `<funktion+0xNN>` ist (Sprung in die Mitte einer
   Funktion -- Register-Window-Spill/Fill-Trampolines und manche Tail-Calls
   erzeugen das), wird der Framegroesse dieser Funktion zugerechnet. Das ist
   exakt das, was der Compiler erzeugt hat, aber es ist eine Naeherung an
   der Stelle, wo mehrere Trampoline-Sprungziele in derselben Funktion
   liegen koennten.
2. Frame-Groessen aus `entry a1, N` sind auf die vom Compiler gewaehlte
   Rundung angewiesen; kleine Rundungsdifferenzen zwischen Toolchain-Versionen
   sollen das Gate nicht knapp umkippen lassen.

**Der Margin ist keine Reserve fuer ISR-Nesting** -- dessen Beitrag ist
unbeschraenkt (mehrere ISR-Ebenen koennen theoretisch verschachteln) und
muss weiterhin von einem Menschen beurteilt werden, nicht vom Gate
automatisch mitgerechnet werden. Bei Bedarf mit `--margin` explizit
anpassen.

## Verwendung

```bash
uv run tools/stack_budget.py <elf> --root <symbol> [--root <symbol> ...] \
    --budget <bytes> [--margin <bytes>] [--objdump <pfad>]
```

Der Aufruf fuer fork-main (Heltec V3 als Referenz-Env, ELF liegt nach einem
`pio run -e heltec_wifi_lora_32_V3` unter `.pio/build/`):

```bash
uv run tools/stack_budget.py .pio/build/heltec_wifi_lora_32_V3/firmware.elf \
    --root esp32loop --root getExternUDP --budget 12288
```

Gemessen auf `fork-main` am Heltec-V3-ELF, Schwellwert 11776 B, Exitcode 0:

| Kette          | vor P2 (`45e411d4`) | nach P2 (`efc9681e`) | Delta |
| -------------- | ------------------- | -------------------- | ----- |
| `esp32loop`    | 10160 B             | 8960 B               | -1200 |
| `getExternUDP` | 8464 B              | 7264 B               | -1200 |

Beide Ketten bestehen in beiden Zustaenden -- P1 allein reichte dafuer schon.
Die -1200 B sind exakt `EXTERN_MSG_JSON_BUF` (700) + 500, also die beiden
Puffer aus `sendExtern()` und sonst nichts. Vor P1, mit 8192 B Stack, lagen
beide Werte darueber: das war der Absturz.

Beide Ketten tragen die oben erwaehnte Indirect-Call-WARNING; die genannten
Zahlen sind damit untere Schranken, keine exakten Werte.

`--objdump` wird automatisch unter
`~/.platformio/packages/toolchain-xtensa-esp32/bin/xtensa-esp32-elf-objdump`
gesucht, danach im `$PATH`; bei Bedarf explizit angeben (z. B. fuer die
S3-Toolchain).

## Negativtests (Gate beweist sich selbst)

Ein Gate, das nie FAIL sagt, ist kein Gate. Zwei Proben:

```bash
uv run tools/stack_budget.py .pio/build/heltec_wifi_lora_32_V3/firmware.elf \
    --root esp32loop --budget 100
# -> verdict: FAIL, Exitcode 1 (threshold rutscht mit Margin 512 ins Negative)

uv run tools/stack_budget.py .pio/build/heltec_wifi_lora_32_V3/firmware.elf \
    --root thisSymbolDoesNotExistAnywhere --budget 12288
# -> "error: root symbol ... not found ...", Exitcode 1 -- harter Fehler,
#    kein stiller 0-Byte-Pass
```

## Wo das Gate einzuhaengen ist

Der eigentliche CI-Einsatz -- Root-Symbol(e) und Budget je Board/Env
festlegen und den Aufruf in die Build-Pipeline haengen -- ist nicht Teil
dieses Skripts und bleibt der jeweiligen Umgebung ueberlassen (`platformio.ini`
o.ae. gehoert nicht zu diesem Dateiumfang).
