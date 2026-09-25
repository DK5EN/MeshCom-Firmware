#!/usr/bin/env python3
# /// script
# requires-python = ">=3.11"
# dependencies = []
# ///
"""Testet ``tools/nm_symsum.py`` gegen einen mitgeschriebenen ``nm -C -S``-Ausschnitt
(unten, ``NM_SAMPLE``) -- kein echter Toolchain-``nm`` noetig, kein Build.

Die Fixture ist ein gekuerzter, aber realer Ausschnitt aus
``xtensa-esp32-elf-nm -C -S --size-sort`` auf einem E22-DevKitC-firmware.elf
(2026-09-25), erweitert um synthetische Zeilen fuer drei Faelle, die die echte
Ausgabe an diesem Tag nicht enthielt, aber vorkommen koennen:

  * eine nRF52-Funktions-lokale ``static struct mheardLine`` (demangled als
    ``<Funktion>()::mheardLine``) -- auf ESP32-Klassik gibt es das nicht, siehe
    src/mheard_functions.cpp / web_functions.cpp (NRF52_SERIES-Gate).
  * ein Symbol mit Groesse 0 (muss trotz passendem Namen nicht zaehlen).
  * eine ``U``-Zeile ohne Groessenfeld (nm druckt die bei undefinierten Symbolen
    ohne Hex-Groesse -- muss beim Parsen einfach uebersprungen werden).

Die restlichen "Stoerer" (``mheardFreshMs`` als Funktion, ``nbrLog``,
``stat_nbr_cancel``, ``bNBRSYM``) sind unveraendert aus der echten Ausgabe
kopiert: sie pruefen, dass Typ-Filter (nur b/B/d/D, keine Funktionen) und
Gruppen-Regex (kein "nbr*" auf gut Glueck) tatsaechlich greifen.

Usage::

    uv run test/test_nbrlog/test_nm_symsum.py
"""

from __future__ import annotations

import importlib.util
import io
import json
import sys
import tempfile
from contextlib import redirect_stdout
from pathlib import Path
from unittest import mock

TOOLS_DIR = Path(__file__).resolve().parents[2] / "tools"


def load_module():
    spec = importlib.util.spec_from_file_location("nm_symsum", TOOLS_DIR / "nm_symsum.py")
    mod = importlib.util.module_from_spec(spec)
    # dataclasses.dataclass() looks its owning module up via sys.modules at class
    # creation time -- without this, exec_module() on a module that isn't
    # registered yet raises AttributeError on the @dataclass in nm_symsum.py.
    sys.modules[spec.name] = mod
    spec.loader.exec_module(mod)
    return mod


NM_SAMPLE = """\
400f8db4 00000028 T mheardFreshMs(int, unsigned int)
3ffcb545 0000001e b mheard_send_idx
3ffcb564 00000001 B mheardWrite
3ffcc1e8 00000078 B mheardAlt
3ffcc170 00000078 B mheardEpoch
3ffcc350 0000012c B mheardCalls
3ffcc47c 00000258 B mheardRecords
3ffcb563 00000001 B mheardPathWrite
3ffcb630 000000a0 B mheardPathEpoch
3ffcb860 00000820 B mheardPathBuffer1
3ffcc6d4 00000534 B nbrMatrix
3ffccc08 00000004 B nbrLog
3ffcb144 00000004 B stat_nbr_cancel
3ffbe45d 00000001 D bNBRSYM
3ffcd078 00000050 B ringAlone
3ffcd0c8 00000050 B ringNeed
20004000 00000248 b sendMheard()::mheardLine
3ffcb000 00000000 B mheardZeroSized
         U free
"""

# Erwartete Gruppensummen fuer NM_SAMPLE (siehe Docstring fuer die Herleitung
# jeder ausgeschlossenen Zeile).
EXPECT_GROUPS = {
    "mheard": 30 + 1 + 120 + 120 + 300 + 600 + 584,  # 1755
    "mhpath": 1 + 160 + 2080,  # 2241
    "nbr": 1332,
    "rings": 80 + 80,  # 160
}
EXPECT_TOTAL = sum(EXPECT_GROUPS.values())  # 5488
EXPECT_UNMATCHED = 3  # nbrLog, stat_nbr_cancel, bNBRSYM


def main() -> int:
    failures: list[str] = []
    mod = load_module()

    # -- parse_nm_output: liest nur vollstaendige "addr size type name"-Zeilen --
    parsed = mod.parse_nm_output(NM_SAMPLE)
    names = {name for _size, _typ, name in parsed}
    if "free" in names:
        failures.append("parse_nm_output: eine U-Zeile ohne Groesse wurde nicht uebersprungen")
    if len(parsed) != NM_SAMPLE.count("\n") - 1:  # eine Zeile (U free) faellt raus
        failures.append(f"parse_nm_output: {len(parsed)} Zeilen geparst, erwartet {NM_SAMPLE.count(chr(10)) - 1}")

    # -- classify(): Gruppenzuordnung pro Name, unabhaengig von Typ/Groesse --
    cases = [
        ("mheardAlt", "mheard"),
        ("mheardCalls", "mheard"),
        ("mheardPathEpoch", "mhpath"),
        ("mheardPathBuffer1", "mhpath"),
        ("nbrMatrix", "nbr"),
        ("ringNeed", "rings"),
        ("ringAlone", "rings"),
        ("sendMheard()::mheardLine", "mheard"),
        ("showMHeard()::mheardLine", "mheard"),
        ("sub_page_mheard()::mheardLine", "mheard"),
    ]
    for name, expected in cases:
        got = mod.classify(name)
        if got != expected:
            failures.append(f"classify({name!r}) = {got!r}, erwartet {expected!r}")
    # classify() entscheidet rein nach Namen -- dass mheardFreshMs() (eine
    # Funktion) trotzdem "mheard" liefert, ist beabsichtigt: measure() filtert
    # den Typ (nur b/B/d/D) VOR dem classify()-Aufruf weg, siehe die
    # measure()-Pruefung unten (EXPECT_GROUPS zaehlt die 40 Byte nicht mit).
    for name in ["nbrLog", "stat_nbr_cancel", "bNBRSYM", "nbrreport_timer", "nbrsnap_timer",
                 "tg_post_nbrrelay_on()"]:
        got = mod.classify(name)
        if got is not None:
            failures.append(f"classify({name!r}) = {got!r}, erwartet None (kein Topologie-Symbol)")

    # -- measure(): Typ-Filter (nur b/B/d/D) und Groesse-0-Ausschluss ueber run_nm --
    with mock.patch.object(mod, "run_nm", return_value=NM_SAMPLE):
        result = mod.measure("E22-DevKitC", Path("/fake/E22-DevKitC.elf"), Path("/fake/nm"))
    if result.groups != EXPECT_GROUPS:
        failures.append(f"measure(): Gruppen {result.groups} != erwartet {EXPECT_GROUPS}")
    if result.total != EXPECT_TOTAL:
        failures.append(f"measure(): total {result.total} != erwartet {EXPECT_TOTAL}")
    if result.unmatched_count != EXPECT_UNMATCHED:
        failures.append(f"measure(): unmatched_count {result.unmatched_count} != erwartet {EXPECT_UNMATCHED}")

    # -- nm_for(): dieselbe Heuristik wie tools/neo/gate.sh --
    nm_cases = [
        ("heltec_wifi_lora_32_V3", mod.NM_S3),
        ("heltec_wifi_lora_32_V2", mod.NM_S3),  # bekannte Ungenauigkeit, siehe Docstring in nm_symsum.py
        ("t_deck_plus", mod.NM_S3),
        ("ttgo_tbeam_supreme", mod.NM_S3),
        ("wiscore_rak4631", mod.NM_ARM),
        ("t_echo", mod.NM_ARM),
        ("heltec_t114", mod.NM_ARM),
        ("E22-DevKitC", mod.NM_ESP32),
        ("E22_XML-DevKitC", mod.NM_ESP32),
        ("ttgo_tbeam", mod.NM_ESP32),
    ]
    for env, expected in nm_cases:
        got = mod.nm_for(env)
        if got != expected:
            failures.append(f"nm_for({env!r}) = {got}, erwartet {expected}")

    # -- family_for(): build.py-Familien --
    fam_cases = [
        ("E22_XML-DevKitC", "E22_XML"),
        ("wiscore_rak4631", "nRF52"),
        ("heltec_t114", "nRF52"),
        ("t_echo", "nRF52"),
        ("heltec_wifi_lora_32_V3", "S3"),
        ("t_deck_plus", "S3"),
        ("E22-DevKitC", "klassisch"),
        ("heltec_wifi_lora_32_V2", "klassisch"),
    ]
    for env, expected in fam_cases:
        got = mod.family_for(env)
        if got != expected:
            failures.append(f"family_for({env!r}) = {got!r}, erwartet {expected!r}")

    # -- collect_targets(): --elf-Mapping plus Verzeichnis-Scan (<env>.elf) --
    with tempfile.TemporaryDirectory() as td:
        tdir = Path(td)
        (tdir / "heltec_wifi_lora_32_V3.elf").write_bytes(b"")
        (tdir / "wiscore_rak4631.elf").write_bytes(b"")
        parser = mod.build_parser()
        args = parser.parse_args(["--elf", "custom=/tmp/custom.elf", str(tdir)])
        targets = mod.collect_targets(args)
        expected_targets = {
            "custom": Path("/tmp/custom.elf"),
            "heltec_wifi_lora_32_V3": tdir / "heltec_wifi_lora_32_V3.elf",
            "wiscore_rak4631": tdir / "wiscore_rak4631.elf",
        }
        if targets != expected_targets:
            failures.append(f"collect_targets(): {targets} != erwartet {expected_targets}")

    # -- main(): Ende-zu-Ende mit gemocktem run_nm, Text- und JSON-Ausgabe --
    with tempfile.TemporaryDirectory() as td:
        elf_path = Path(td) / "firmware.elf"
        elf_path.write_bytes(b"")
        with mock.patch.object(mod, "run_nm", return_value=NM_SAMPLE):
            buf = io.StringIO()
            with redirect_stdout(buf):
                rc = mod.main(["--elf", f"E22-DevKitC={elf_path}"])
            if rc != 0:
                failures.append(f"main(): Exitcode {rc} statt 0")
            out = buf.getvalue()
            if str(EXPECT_TOTAL) not in out:
                failures.append(f"main(): Gesamtsumme {EXPECT_TOTAL} nicht in der Tabelle:\n{out}")

            buf = io.StringIO()
            with redirect_stdout(buf):
                rc = mod.main(["--elf", f"E22-DevKitC={elf_path}", "--json"])
            if rc != 0:
                failures.append(f"main() --json: Exitcode {rc} statt 0")
            try:
                doc = json.loads(buf.getvalue())
            except json.JSONDecodeError as exc:
                failures.append(f"main() --json: keine gueltige JSON-Ausgabe ({exc})")
            else:
                env_doc = doc.get("envs", {}).get("E22-DevKitC")
                if env_doc is None:
                    failures.append(f"main() --json: kein Eintrag fuer E22-DevKitC:\n{doc}")
                elif env_doc["total"] != EXPECT_TOTAL or env_doc["family"] != "klassisch":
                    failures.append(f"main() --json: unerwarteter Eintrag {env_doc}")

    # -- main(): fehlendes ELF fuehrt zu rc=2, kein Traceback --
    buf = io.StringIO()
    with redirect_stdout(buf), redirect_stdout(buf):
        rc = mod.main(["--elf", "ghost=/does/not/exist.elf"])
    if rc != 2:
        failures.append(f"main() mit fehlendem ELF: Exitcode {rc} statt 2")

    # -- main(): keine Targets --
    err = io.StringIO()
    with redirect_stdout(io.StringIO()), mock.patch.object(sys, "stderr", err):
        rc = mod.main([])
    if rc != 2:
        failures.append(f"main() ohne Targets: Exitcode {rc} statt 2")

    if failures:
        print(f"FEHLGESCHLAGEN ({len(failures)}):")
        for f in failures:
            print(f"  - {f}")
        return 1
    print("test_nm_symsum: alle Pruefungen bestanden.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
