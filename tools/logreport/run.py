#!/usr/bin/env python3
"""logreport runner: multi-node MeshCom log capture -> measured data -> self-contained HTML reports.

Usage (from the repo root):

    python3 -I tools/logreport/run.py --logs logs/20261101-24h-log \\
        --run docs/log-reports/2026-11-01-24h [--work <dir>] [--steps normalize,extract,render] \\
        [--name "DB0AIS 24 Stnuden.log=db0ais"]

Steps:
  normalize  every *.log in --logs -> <work>/norm/<key>.log (CR stripped). Key: --name override, else
             `meshcom_<key>_...log`, else the first word of the file name, lower-case.
  (config)   optional <run>/logreport.json: node-key overrides ("names"), per-topic extra arguments
             ("topics", e.g. clock profiles, annotations), mesh groups ("groups": {name: [keys]}; logs
             of different meshes must not be correlated) with their own topic args ("group_topics").
             Group "main" (all ungrouped logs) writes <run>/data/<q>/, group g writes <run>/data/g/<q>/.
  extract    runs each topic extractor (tools/logreport/<topic>/run.py --norm --out) into
             <work>/<topic>/ and copies its results.json (and findings.md if produced) into
             <run>/data/<datadir>/. Master data (mcmap) is an agent/MCP step: put master.tsv,
             distances.tsv, coverage.txt into <run>/data/master/ before rendering
             (tools/logreport/master/ merges the saved tile JSONs).
  render     runs every <run>/generators/*.py as
             `python3 -I <gen> --data <run>/data --kit tools/logreport --out <run>/<gen-stem>.html`.

The work dir (default: <logs>/.logreport-work) holds the large normalised logs and intermediates and
stays out of git (logs/ is gitignored). <run>/data holds only the small results that the reports cite.
Judgement text (findings.md per topic, CORE-FINDINGS.md, the generators) is written per run by the
agent workflow in tools/logreport/README.md.
"""

from __future__ import annotations

import argparse
import json
import re
import shutil
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent

#: topic package -> data sub-directory the generators read (question numbers of the standard
#: question set: F2 TX pool, F3/F4 decay, F5 time signal, F6 manipulations).
TOPICS: dict[str, str] = {
    "txpool": "q2",
    "decay": "q34",
    "timesignal": "q5",
    "manipulations": "q6",
    "health": "health",
}


def node_key(path: Path, overrides: dict[str, str]) -> str:
    if path.name in overrides:
        return overrides[path.name]
    m = re.match(r"meshcom_([A-Za-z0-9]+)_", path.name)
    if m:
        return m.group(1).lower()
    m = re.match(r"([A-Za-z0-9]{3,7})-(\d{1,2})[_ .]", path.name)  # e.g. HB9VQQ-4_loradebug_... -> hb9vqq4
    if m:
        return (m.group(1) + m.group(2)).lower()
    return re.split(r"[\s_.-]+", path.name, maxsplit=1)[0].lower()


#: logger prefix "YYYY-MM-DD HH:MM:SS.mmm" followed by ONE space (tools/meshlogger.py over TCP 2323);
#: the topics expect the two-space form written by the serial logger, so normalize canonicalises it.
_ONE_SPACE_PREFIX = re.compile(rb"^(\d{4}-\d\d-\d\d \d\d:\d\d:\d\d\.\d{3}) (?! )")


def normalize(logs: Path, work: Path, overrides: dict[str, str]) -> list[Path]:
    """CR stripped, logger prefix canonicalised to two spaces. Skips files whose copy is newer."""
    norm = work / "norm"
    norm.mkdir(parents=True, exist_ok=True)
    out: list[Path] = []
    for src in sorted(logs.glob("*.log")):
        dst = norm / f"{node_key(src, overrides)}.log"
        if dst in out:
            raise SystemExit(f"node key collision for {src.name} -> {dst.name}; use --name")
        out.append(dst)
        if dst.exists() and dst.stat().st_mtime >= src.stat().st_mtime:
            print(f"normalize {src.name} -> {dst.relative_to(work)} (up to date)")
            continue
        with src.open("rb") as fi, dst.open("wb") as fo:
            for line in fi:
                fo.write(_ONE_SPACE_PREFIX.sub(rb"\1  ", line.replace(b"\r", b"")))
        print(f"normalize {src.name} -> {dst.relative_to(work)}")
    if not out:
        raise SystemExit(f"no *.log files in {logs}")
    return out


def run_config(run: Path) -> dict:
    """<run>/logreport.json: {"names": {file: key}, "topics": {topic: [extra args]}}. Optional."""
    cfg = run / "logreport.json"
    return json.loads(cfg.read_text()) if cfg.exists() else {}


def topic_args(run: Path, args: list[str]) -> list[str]:
    """Extra topic args; an arg naming an existing file relative to the run dir becomes absolute."""
    return [str(run / a) if (run / a).is_file() else a for a in args]


def groups(work: Path, run: Path) -> dict[str, list[Path]]:
    """Mesh groups: logreport.json "groups" {name: [node keys]}; every other log is in group "main".
    Topics correlate all wall-clock logs of one call, so separate meshes must run separately."""
    cfg = run_config(run).get("groups", {})
    logs = sorted((work / "norm").glob("*.log"))
    named = {k for keys in cfg.values() for k in keys}
    out: dict[str, list[Path]] = {"main": [p for p in logs if p.stem not in named]}
    for g, keys in cfg.items():
        out[g] = [p for p in logs if p.stem in keys]
    return {g: ps for g, ps in out.items() if ps}


def extract(work: Path, run: Path, master: Path | None, only: set[str] | None = None) -> None:
    cfg = run_config(run)
    for g, paths in groups(work, run).items():
        if only and g not in only:
            continue
        gdir = work / "groups" / g
        gnorm = gdir / "norm"
        if gnorm.exists():
            shutil.rmtree(gnorm)
        gnorm.mkdir(parents=True)
        for p in paths:
            (gnorm / p.name).symlink_to(p.resolve())
        data = run / "data" if g == "main" else run / "data" / g
        extra = cfg.get("topics", {}) if g == "main" else cfg.get("group_topics", {}).get(g, {})
        print(f"group {g}: {', '.join(p.stem for p in paths)}")
        for topic, sub in TOPICS.items():
            script = HERE / topic / "run.py"
            if not script.exists():
                print(f"extract {topic}: missing {script.relative_to(HERE.parent.parent)}, skipped")
                continue
            out = gdir / topic
            out.mkdir(parents=True, exist_ok=True)
            cmd = [sys.executable, "-I", str(script), "--norm", str(gnorm), "--out", str(out)]
            if master is not None and topic in ("timesignal", "manipulations", "health"):
                cmd += ["--master", str(master)]
            cmd += topic_args(run, extra.get(topic, []))
            print("extract", g, topic, "...", flush=True)
            subprocess.run(cmd, check=True)
            dest = data / sub
            dest.mkdir(parents=True, exist_ok=True)
            for name in ("results.json", "findings.md"):
                if (out / name).exists() and not (name == "findings.md" and (dest / name).exists()):
                    shutil.copy2(out / name, dest / name)


def render(run: Path) -> None:
    gens = sorted((run / "generators").glob("*.py"))
    if not gens:
        raise SystemExit(f"no generators in {run / 'generators'}")
    for gen in gens:
        out = run / f"{gen.stem}.html"
        cmd = [sys.executable, "-I", str(gen), "--data", str(run / "data"), "--kit", str(HERE),
               "--out", str(out)]
        subprocess.run(cmd, check=True)
        print(f"render {gen.name} -> {out}")


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--logs", type=Path, help="directory with the raw *.log captures")
    ap.add_argument("--run", type=Path, required=True, help="report directory, e.g. docs/log-reports/<id>")
    ap.add_argument("--work", type=Path, help="work dir for normalised logs (default <logs>/.logreport-work)")
    ap.add_argument("--steps", default="normalize,extract,render")
    ap.add_argument("--name", action="append", default=[], help="'<file name>=<key>' node key override")
    ap.add_argument("--group", action="append", default=[], help="extract only these mesh groups")
    a = ap.parse_args(argv)
    steps = {s.strip() for s in a.steps.split(",") if s.strip()}
    overrides = dict(run_config(a.run).get("names", {}))
    overrides.update(dict(n.split("=", 1) for n in a.name))
    if steps & {"normalize", "extract"} and a.logs is None and a.work is None:
        ap.error("--logs (or --work) is required for normalize/extract")
    work = a.work or (a.logs / ".logreport-work" if a.logs is not None else None)
    if "normalize" in steps:
        normalize(a.logs, work, overrides)
    if "extract" in steps:
        master = a.run / "data" / "master"
        extract(work, a.run, master if master.exists() else None, set(a.group) or None)
    if "render" in steps:
        render(a.run)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
