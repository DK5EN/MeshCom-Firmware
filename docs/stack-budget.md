# Stack-Budget-Gate (`tools/stack_budget.py`)

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

## Grenze der Analyse (bitte lesen, bevor ein PASS als Beweis zaehlt)

Zwei Dinge sieht das Skript grundsaetzlich nicht:

- **Indirekte Calls** (`callx0/4/8/12`) -- das Ziel steht erst zur Laufzeit
  fest. Jeder indirekte Call auf dem berichteten Pfad wird als Warnung
  ausgegeben; die genannte Zahl ist dann eine **untere Schranke**, kein
  exakter Wert.
- **Interrupts.** ESP-IDF schaltet beim Interrupt-Dispatch auf einen eigenen
  ISR-Stack je CPU (`CONFIG_FREERTOS_ISR_STACKSIZE`, 2096 B); auf dem Stack der
  unterbrochenen Task landen nur der Exception-Frame und der Fenster-Spill, je
  Verschachtelungsebene einige hundert Byte. Der Beitrag ist also begrenzt,
  aber nicht null, und das Skript kennt ihn nicht -- das ist der Grund fuer
  den Margin (siehe unten), nicht ein Bug.

Ausserdem: Ein Zyklus im Call-Graph (Rekursion, direkt oder gegenseitig)
bricht den Walk an der Wiedereintrittsstelle ab und wird als lautes WARNING
im Bericht markiert, nicht still abgeschnitten -- eine rekursive Kette hat
keine statische obere Grenze, die berichtete Zahl ist dann per Definition zu
niedrig.

## Warum `--margin` (Default 512 Byte)

Der Margin wird vor dem PASS/FAIL-Vergleich vom Budget abgezogen
(`threshold = budget - margin`). Er deckt zwei kleinere, aber reale
Unschaerfen der Analyse selbst ab:

1. Ein `call*`, dessen Ziel `<funktion+0xNN>` ist (Sprung in die Mitte einer
   Funktion -- Register-Window-Spill/Fill-Trampolines und manche Tail-Calls
   erzeugen das), wird der Framegroesse dieser Funktion zugerechnet. Das ist
   exakt das, was der Compiler erzeugt hat, aber es ist eine Naeherung an
   der Stelle, wo mehrere Trampoline-Sprungziele in derselben Funktion
   liegen koennten.
2. Frame-Groessen aus `entry a1, N` sind auf die vom Compiler gewaehlte
   Rundung angewiesen; kleine Rundungsdifferenzen zwischen Toolchain-Versionen
   sollen das Gate nicht knapp umkippen lassen.

512 Byte sind bei einem 8-12 KB Stack-Budget eine kleine, aber spuerbare
Reserve (6-4 %) fuer diese beiden Punkte. **Der Margin ist keine Reserve fuer
ISR-Nesting** -- dessen Beitrag ist unbeschraenkt (mehrere ISR-Ebenen koennen
theoretisch verschachteln) und muss weiterhin von einem Menschen beurteilt
werden, nicht vom Gate automatisch mitgerechnet werden. Bei Bedarf mit
`--margin` explizit anpassen.

## Verwendung

```bash
uv run tools/stack_budget.py <elf> --root <symbol> [--root <symbol> ...] \
    --budget <bytes> [--margin <bytes>] [--objdump <pfad>]
```

Beispiel (Regressionsfall dieses Skripts, Loop-Stack vor der Erhoehung auf
12288 Byte):

```bash
uv run tools/stack_budget.py build/reclaim-heltecV3.elf \
    --root getExternUDP --budget 8192
# -> FAIL, exit 1 (Kette liegt ueber 8192 - 512 = 7680 Byte)

uv run tools/stack_budget.py build/reclaim-heltecV3.elf \
    --root getExternUDP --budget 12288
# -> PASS, exit 0
```

`--objdump` wird automatisch unter
`~/.platformio/packages/toolchain-xtensa-esp32/bin/xtensa-esp32-elf-objdump`
gesucht, danach im `$PATH`; bei Bedarf explizit angeben (z. B. fuer die
S3-Toolchain).

## Wo das Gate einzuhaengen ist

Der eigentliche CI-Einsatz -- Root-Symbol(e) und Budget je Board/Env
festlegen und den Aufruf in die Build-Pipeline haengen -- ist nicht Teil
dieses Skripts und bleibt der jeweiligen Umgebung ueberlassen (`platformio.ini`
o.ae. gehoert nicht zu diesem Dateiumfang).
