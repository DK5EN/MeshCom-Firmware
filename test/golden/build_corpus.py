#!/usr/bin/env python3
"""Assemble the golden corpora from the fixtures already in this repository.

Test plan P0.4. Four corpora, all generated -- never hand-edited, so a fixture
that changes upstream propagates by re-running this script:

    corpus/lora/       raw LoRa frames, one per .hex file
    corpus/udp1990/    server -> node datagrams: GATE-wrapped frames, a BEAT,
                       a CONF with all five TLVs, and the malformed cases
    corpus/extudp/     inbound EXTUDP JSON, one datagram per .json file
    corpus/commands/   the ladder order, its shadowed/duplicate pairs, and a
                       driveable command script
    corpus/ble/        the write corpus for tools/bench/ble_golden.py

Sources: `test/test_aprs_corpus/corpus.txt` (on-air captures),
`test/support/gwflood_frames.txt` (generated bench frames),
`test/test_getextern/test_getextern.cpp` (the inbound EXTUDP set) and
`src/command_functions.cpp` via `extract_commands.py`.

**Callsign and destination rewriting is not cosmetic.** A `GATE` datagram makes
a gateway radiate the frame verbatim, so every callsign in a source path would
go on the air under a licence we do not hold. The on-air corpus is full of
foreign calls -- DL2JA, OE1XAR, DK4YU, IV3OEP -- so every one is rewritten to
`DK5EN-<ssid>`, keeping the SSID so a frame stays traceable to its original,
and the FCS is recomputed (`mc_frame.rewrite_path`). Broadcast destinations
are rewritten to group `9999` in the `udp1990` corpus only: bench traffic never
goes to `*`. The `lora` corpus keeps `*`, because it is injected with
`--injectraw` while mesh and gateway are off and nothing is transmitted.

`corpus_lint.py` enforces both rules afterwards; this script is not trusted to
have done it right.

    python3 test/golden/build_corpus.py                  # write test/golden/corpus/
    python3 test/golden/build_corpus.py --out /tmp/c     # somewhere else
    python3 test/golden/build_corpus.py --self-test

No third-party dependencies: stdlib + type hints only.
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path
from typing import Dict, Iterable, List, Tuple

sys.path.insert(0, str(Path(__file__).resolve().parent))

import extract_commands  # noqa: E402
import mc_frame  # noqa: E402

REPO_ROOT = Path(__file__).resolve().parents[2]
APRS_CORPUS = REPO_ROOT / "test" / "test_aprs_corpus" / "corpus.txt"
GWFLOOD_FRAMES = REPO_ROOT / "test" / "support" / "gwflood_frames.txt"
GETEXTERN_SRC = REPO_ROOT / "test" / "test_getextern" / "test_getextern.cpp"
DEFAULT_OUT = REPO_ROOT / "test" / "golden" / "corpus"

OWN_BASE = "DK5EN"
BENCH_GROUP = b"9999"      # bench rule: group 9 or 9999, never "*"
BENCH_DM = "DK5EN-1"       # a direct destination that is ours


# --------------------------------------------------------------- rewriting


def own_callsign(call: str) -> str:
    """Map any callsign onto ours, keeping the SSID so frames stay traceable."""
    if call.upper().startswith(OWN_BASE + "-"):
        return call
    _, _, ssid = call.partition("-")
    return f"{OWN_BASE}-{ssid}" if ssid else OWN_BASE


def rewrite_frame(raw: bytes, *, broadcast_to_group: bool) -> bytes:
    """Make one frame safe to put on the air, FCS included."""
    frame = mc_frame.parse(raw)
    if frame is None:              # the binary ack carries no callsign
        return raw
    frame.path = [own_callsign(c) for c in frame.path]
    if broadcast_to_group and frame.dest == b"*":
        frame.dest = BENCH_GROUP
    elif frame.dest and frame.dest not in (b"*",) and not frame.dest.isdigit():
        # A direct destination is a real station that would receive this.
        dest = frame.dest.decode("ascii", errors="replace")
        head = dest.split(",")[0]
        frame.dest = own_callsign(head).encode("ascii")
    return frame.encode()


# --------------------------------------------------------------- sources


def read_frame_file(path: Path) -> List[Tuple[str, bytes]]:
    """`<name> <hex>` lines, '#' comments, as both frame fixtures are written."""
    out: List[Tuple[str, bytes]] = []
    for line in path.read_text().splitlines():
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        name, hexstr = line.split()
        out.append((name, bytes.fromhex(hexstr)))
    return out


_RAW_STR_RE = re.compile(r'R"\(([^)]*)\)"')


def read_extudp_set(path: Path) -> List[str]:
    """The complete inbound JSON literals from the native EXTUDP test.

    Only the ones that parse as a complete JSON object are kept: the test file
    also contains fragments that it concatenates at runtime to build oversize
    and embedded-NUL cases, and half an object is not a datagram.
    """
    import json

    seen: List[str] = []
    for m in _RAW_STR_RE.finditer(path.read_text(errors="replace")):
        literal = m.group(1)
        try:
            obj = json.loads(literal)
        except ValueError:
            continue
        if not isinstance(obj, dict):
            continue
        if literal not in seen:
            seen.append(literal)
    return seen


def rewrite_extudp(literal: str) -> str:
    """Point every inbound message at a bench destination.

    `dst` decides what the node transmits: a foreign callsign there is a DM to
    a real station, and `*` is a broadcast the bench rule forbids.
    """
    import json

    obj = json.loads(literal)
    dst = obj.get("dst")
    if isinstance(dst, str):
        if dst == "*":
            obj["dst"] = BENCH_GROUP.decode()
        elif not dst.isdigit() and not dst.upper().startswith(OWN_BASE):
            obj["dst"] = BENCH_DM
    return json.dumps(obj, separators=(",", ":"), ensure_ascii=False)


# --------------------------------------------------------------- datagrams


def build_conf_datagram() -> bytes:
    """CONF with all five TLVs in the only order the firmware parser accepts.

    The parser is offset arithmetic, not a TLV scanner (doc 11 §2.2): the
    callsign TLV must be first, the shortname must follow it, and the three
    coordinate TLVs must be contiguous -- omitting the shortname misaligns the
    coordinate reads by two bytes and they are silently dropped.
    """
    sys.path.insert(0, str(REPO_ROOT / "tools" / "mock"))
    import meshcom_server as mock

    return mock.build_conf_datagram(BENCH_DM, "BNCH", 481234567, 117654321, 520)


def malformed_datagrams(good_frame: bytes) -> List[Tuple[str, bytes, str | None]]:
    """The rejection cases the receive path is supposed to survive.

    Third element is a lint exemption reason, or None when the entry is a
    well-formed GATE the callsign lint can and must read.
    """
    base = b"GATE" + good_frame
    odd = base if len(base) % 2 else base + b"\x5a"
    return [
        # Shorter than the 4-byte indicator.
        ("m01-short-indicator", b"GA", "no indicator, nothing to parse"),
        # Indicator the node does not know: dropped before the frame is read.
        ("m02-unknown-indicator", b"XXXX" + good_frame,
         "indicator is not GATE/DATA, the frame behind it is never reached"),
        # Trailing zero run past MAX_ZEROS = 6, pair-aligned: the one shape the
        # firmware check actually catches (doc 11 §2.2).
        ("m03-trailing-zeros", base + b"\x00" * 12, None),
        # The same run followed by a non-zero pair, which resets the firmware's
        # counter -- real nodes do NOT reject this, the mock does. The zeros go
        # after the frame, never spliced into it: splicing them into the source
        # path corrupts a callsign and the entry then trips the callsign lint
        # for a reason that has nothing to do with what it is testing.
        ("m04-zeros-then-nonzero", base + b"\x00" * 12 + b"\x5a\x5a", None),
        # Odd total length: the nRF52 zero-scan reads buf[i+1] past the
        # datagram here (audit defect 7 / OPT-D7).
        ("m05-odd-length", odd, None),
        # GATE with no frame behind the indicator.
        ("m06-gate-empty", b"GATE", "no frame, nothing to radiate"),
        # Over the node's 255-byte read cap.
        ("m07-oversize", base + b"\x41" * max(0, 300 - len(base)), None),
    ]


# --------------------------------------------------------------- commands

# Held back from the automatic script, by exact command token and with the
# reason. A substring rule does not work here and is actively dangerous in
# both directions: "ota" matches `rotate` (display rotation, harmless) and
# "sf" matches `txsf`, while the genuinely destructive `cleanflash` contains
# none of the obvious words.
_MANUAL_ONLY: Dict[str, str] = {
    # (a) destroys or halts. A halting command is as destructive to a capture
    #     as an erasing one: --deepsleep at command 38 of 449 put Heltec-93 to
    #     sleep and the remaining 411 went into a node that was not listening.
    #     The capture looked complete and only 38 blocks meant anything.
    "cleanflash": "wipes settings on next boot",
    "reboot": "reboots the node mid-capture",
    "deepsleep": "halts the node; everything after it in a script is lost",
    # Found 2026-09-11 by reading the Heltec-93 console capture: `--setctry 1`
    # is a *valid* country (UK), so it is accepted -- it reconfigures the radio
    # to 439.9125 MHz and prints "Auto. Reboot after 15 sec.". In the capture
    # that lands about 600 lines from the end of 2124, so the tail is
    # post-reboot state at a different frequency, with reboot timing that
    # varies from run to run, and the node is left on UK afterwards. Same class
    # as --deepsleep: the command succeeds and the capture is the casualty.
    "setctry": "valid values reconfigure the radio and reboot after 15 s",
    "spiffs": "erases the filesystem (`spiffs reset`)",
    "ota-update": "reflashes the node",
    "flashpoke": "writes out-of-range radio values to flash (TM-32 hook)",
    # (b) writes a credential. Driving `--setpwd abc` set the WiFi password to
    #     "abc" and the node could not associate afterwards (reason 15,
    #     4-way handshake timeout); `--webpwd abc` then locked the HTTP restore
    #     out with a 401. Recovery took the vault copy of the password over
    #     serial, which is exactly the situation to avoid.
    "setpwd": "sets the WiFi password; a wrong value strands the node",
    "webpwd": "sets the web password; locks the HTTP restore out",
    "passwd": "changes the BLE PIN; a wrong value locks the bench out",
    # (c) changes node identity, which every capture is keyed on.
    "setcall": "changes the node callsign",
    # (d) cuts or reconfigures the transport the capture and the restore run
    #     over. The HTTP restore needs the web server, an IP, and the right
    #     WiFi credentials; the 2323 half of the golden needs the same link.
    "webserver": "turns the web server off; the HTTP restore needs it",
    # Found 2026-09-11: `--netconsole off` killed the 2323 console DURING the
    # 2323 capture. Every later reconnect was refused, the driver burned its
    # retry budget per command, and the run was killed by its timeout with
    # nothing written. A command that disables the transport it arrived on is
    # the worst case of this class.
    "netconsole": "turns the 2323 console off; the 2323 capture runs over it",
    "wifi": "`--wifi off` drops the link the restore runs over",
    "wifitxpower": "a low value cripples the radio and the node stops associating",
    "wifiap": "switches the node into AP mode",
    "wifidrop": "drops the WiFi link the capture runs over",
    "ethdrop": "drops the Ethernet link the capture runs over",
    "setssid": "changes the WiFi network the node joins",
    "setowndns": "changes DNS; also the duplicate-branch defect (OPT-D2)",
    "setowngw": "changes the default gateway",
    "setownip": "changes the static IP the 2323 capture connects to",
    "setownms": "changes the netmask",
    "setownntp": "changes the NTP server",
    "extudpip": "redirects the EXTUDP feed away from the listener",
    # (e) KEYS THE TRANSMITTER on a shared amateur-radio network. Added
    #     2026-09-16, before the first D2-V run: every category above is about
    #     protecting the node or the capture, and none of them looks outside
    #     the bench at all. These commands do -- the frames land in other
    #     operators' nodes and their dedup rings, and a run of this script
    #     would have put a burst of them on 433.175 MHz.
    #
    #     This is not theoretical: `--pingcall <call>` was driven by hand on
    #     DK5EN-90 on 2026-09-16 and immediately produced a PONG back from
    #     DK5EN-93 over the air. The name suggests it only stores a callsign;
    #     it pings.
    #
    #     Held back rather than made safe, because there is no value that
    #     makes them safe: the emission IS the command. They belong on an
    #     RF-isolated node or a dummy load, driven deliberately -- see the
    #     standing bench rule that test traffic goes to group 9/9999 or a
    #     direct contact and never to `*`.
    "sendhey": "originates a hey frame onto the shared mesh",
    "hey": "originates a hey frame onto the shared mesh",
    "sendpos": "originates a position frame onto the shared mesh",
    "posshot": "forces an immediate position beacon",
    "sendtele": "originates a telemetry frame onto the shared mesh",
    "sendtrack": "originates a track frame onto the shared mesh",
    "ping": "`--ping start` originates repeated pings over the air",
    "pingcall": "arms the recurring ping timer (sets node_pingtime when it was 0), so the node keeps transmitting afterwards -- the PONG seen from DK5EN-93 on 2026-09-16 was that timer, not a one-shot",
    "loratx": "raw LoRa transmit test",
    "injectraw": "injected frames can be relayed back out onto the air",
    "injectmsg": "injected frames can be relayed back out onto the air",
    "injectpos": "injected frames can be relayed back out onto the air",
    # Added 2026-09-16 after an advisor pass caught them still in the driven
    # script -- and the first G0 run had already driven all three. None of
    # them originates a single frame, which is why they were missed: they
    # change how much the node transmits, and they PERSIST, so a run aborted
    # between the `on` and the `off` leaves the node that way across reboots.
    "track": "SmartBeaconing: pushes one station's beacon cadence to ~10 s on a channel that carries about one packet per 8 s (src/track_warning.h), and persists",
    "mesh": "`--mesh on` makes the node relay every frame it hears, and persists",
    "gateway": "`--gateway on` radiates what the server pushes down, and persists",
    # Not emitters, but they poison the payload of everything the node does
    # transmit: none of the three range-checks its argument, and the script's
    # `abc` case parks the node at 0.0 N / 0.0 E -- persisted, then beaconed
    # onto the live mesh and into mcmap at the next position interval.
    "setlat": "no range check; the script's non-numeric case persists 0.0 as the node's latitude",
    "setlon": "no range check; the script's non-numeric case persists 0.0 as the node's longitude",
    "setalt": "no range check; the script's non-numeric case persists 0 as the node's altitude",
    # The ping timer's two dials. Harmless only while node_pingcall is empty,
    # which is not a property of this script -- DK5EN-90 carries a configured
    # ping target right now, and `--pingtime 1` on such a node is a ping per
    # second until something stops it. Held back with `pingcall` and `ping`
    # rather than relying on the node's current state.
    "pingtime": "sets the ping interval; on a node with a ping target configured this is a transmit every N seconds",
    "pingmax": "sets how many pings the armed timer sends",
}


def command_scripts(commands: List[extract_commands.Command]) -> Dict[str, str]:
    """The driveable command script, plus the destructive names held back.

    Four inputs per command, per the test plan: canonical, out-of-range,
    non-numeric, and -- for every shadowed pair -- the adversarial spelling
    that tells the two branches apart.
    """
    safe: List[str] = []
    destructive: List[str] = []

    for cmd in commands:
        token = cmd.name.rstrip()
        # Matched on the first word too: the ladder spells the AP switch
        # `wifiap on` / `wifiap off`, so an exact-token list alone would let
        # both through while `wifi on` -- which must stay in the script --
        # looks identical to a prefix rule.
        held = token in _MANUAL_ONLY or token.split(" ")[0] in _MANUAL_ONLY
        target = destructive if held else safe
        if cmd.takes_arg:
            target.append(f"--{token} 1")
            target.append(f"--{token} 999999")
            target.append(f"--{token} abc")
        else:
            target.append(f"--{token}")

    adversarial = [
        "--info", "--infoX",
        "--lora", "--loradebug on",
        "--heap", "--heap tag",
        "--softser app", "--softser app0",
        "--maxhop", "--maxhop 5",
        "--passwd", "--passwd test",
        # CS-01: put max_hop_text back to its compile-time default (4) so the
        # corpus leaves the node as it found it. The console echoes the set
        # value, so this line's answer is a stable part of the capture.
        "--maxhop 4",
    ]

    header = (
        "# Generated by test/golden/build_corpus.py -- do not edit by hand.\n"
        "# One command per line, driven over USB and over TCP 2323.\n"
    )
    return {
        "script.txt": header + "\n".join(safe) + "\n",
        "adversarial.txt": header + "\n".join(adversarial) + "\n",
        "destructive-manual.txt": (
            "# Generated by test/golden/build_corpus.py -- do not edit by hand.\n"
            "# NOT driven automatically. Each of these either destroys state or\n"
            "# cuts the link the capture runs over. Run by hand, one at a time,\n"
            "# with the settings backup at hand.\n"
            + "".join(f"#   {tok:<14} {why}\n" for tok, why in sorted(_MANUAL_ONLY.items()))
            + "\n".join(destructive) + "\n"
        ),
    }


# --------------------------------------------------------------- ble


# The 13 JSON registers the app requests after connecting (doc 11 §4.2,
# test plan §12.3). Each comes back as a 0x44 notification tagged with its
# register letter, and the MTU budget of 244 bytes is a wire contract, so the
# per-frame length is asserted separately from the content.
BLE_REGISTERS = (
    "--info", "--seset", "--wifiset", "--nodeset", "--analogset", "--wx",
    "--pos", "--io", "--tel", "--aprsset", "--conffin", "--mheard", "--path",
)

# Prefix-order pairs the table rewrite (D2-10) must not collapse. Driving both
# spellings is the only way a capture can tell them apart.
BLE_ADVERSARIAL = (
    "--info", "--infoX",
    "--heap", "--heap tag",
    "--softser app", "--softser app0",
    "--maxhop", "--maxhop 5",
)

# Leaves the node as the corpus found it (CS-01). Emitted last; the tool
# excludes RESTORE: writes from the comparison.
BLE_RESTORE = (
    "RESTORE:--maxhop 4",
)


def ble_corpus() -> str:
    """One write per line, sent as a 0xA0 text frame.

    **INCIDENT 2026-09-11 -- this corpus broadcast to the live network.**

    It carried two message lines in the serial console's form,
    `::{9}bench golden capture` and `::{DK5EN-1}bench golden dm`. Over BLE that
    form is wrong, and the failure mode is a broadcast:

      1. `phone_commands.cpp:583-588` prepends **one** `:` to any 0xA0 text
         that does not start with `--`, because the app sends a bare message
         body. So `::{9}x` became `:::{9}x` on the wire into the dispatcher.
      2. `sendMessage()` (`loop_functions.cpp:3786`) consumes the first two
         colons and looks for `{group}` at the start of what remains. What
         remained was `:{9}x` -- a colon, not a brace -- so no destination was
         parsed and it fell through to `*`.

    Four bench nodes therefore transmitted broadcast frames into the live
    MeshCom network, where they appeared publicly as `DK5EN-93>all`,
    `DK5EN-90>all`, `DK5EN-92>all` and `DK5EN-14>all`. The corpus lint did not
    catch it: the corpus text contained no `*` anywhere, and the `*` was
    created by the firmware afterwards.

    The correct BLE form is a **single** colon -- `:{9}text` -- so that the
    firmware's prepend yields `::{9}text`. That is written down here rather
    than used: this corpus now sends no messages at all. The registers and the
    adversarial prefix pairs are what it is for, messaging is covered on the
    serial path where the two-colon form is proven, and an automated corpus is
    the wrong place to put something that transmits on every run.

    The durable guard is not this comment but
    `ble_golden.verify_no_broadcast()`, which inspects the **captured frames**
    and fails on any own-originated frame addressed to `*`. A text-level check
    could never have caught this.

    `hello` and `timesync` are not in this file. The client builds them itself
    (they are opcodes 0x10 and 0x20, not text), and the hello carries the PIN
    hash, which must never be written to a corpus.
    """
    lines = [
        "# Generated by test/golden/build_corpus.py -- do not edit by hand.",
        "# Driven by tools/bench/ble_golden.py as 0xA0 text frames, in order.",
        "# hello (0x10) and timesync (0x20) are built by the client, not listed here.",
        "#",
        "# NO MESSAGE SENDS. Read the incident note in this function before",
        "# adding one back.",
        "#",
        "# the 13 JSON registers",
        *BLE_REGISTERS,
        "#",
        "# adversarial prefix pairs",
        *BLE_ADVERSARIAL,
        "#",
        "# CS-01: `--maxhop 5` above persists meshcom_settings.max_hop_text and",
        "# the node answers neither form of --maxhop over BLE, so the value cannot",
        "# be read back and restored exactly. Put it back to the compile-time",
        "# default (MAX_HOP_TEXT_DEFAULT == 4) -- this assumes the node was at the",
        "# default before the run. RESTORE: excludes the write from the golden",
        "# comparison (ble_golden.py read_corpus / Capture.on_write).",
        *BLE_RESTORE,
    ]
    return "\n".join(lines) + "\n"


# --------------------------------------------------------------- build


def build(out: Path) -> Dict[str, int]:
    counts: Dict[str, int] = {}

    frames = read_frame_file(APRS_CORPUS) + read_frame_file(GWFLOOD_FRAMES)

    lora_dir = out / "lora"
    lora_dir.mkdir(parents=True, exist_ok=True)
    for i, (name, raw) in enumerate(frames, 1):
        safe = rewrite_frame(raw, broadcast_to_group=False)
        (lora_dir / f"{i:02d}-{name}.hex").write_text(
            f"# {name}, rewritten to own callsigns by build_corpus.py\n"
            f"{safe.hex()}\n"
        )
    counts["lora"] = len(frames)

    udp_dir = out / "udp1990"
    udp_dir.mkdir(parents=True, exist_ok=True)
    n = 0
    for name, raw in frames:
        safe = rewrite_frame(raw, broadcast_to_group=True)
        n += 1
        (udp_dir / f"{n:02d}-gate-{name}.hex").write_text(
            f"# GATE wrapping {name}, own callsigns, group destination\n"
            f"{(b'GATE' + safe).hex()}\n"
        )
    for name, datagram in [("beat", b"BEAT"), ("conf", build_conf_datagram())]:
        n += 1
        (udp_dir / f"{n:02d}-{name}.hex").write_text(f"# {name}\n{datagram.hex()}\n")
    good = rewrite_frame(frames[0][1], broadcast_to_group=True)
    for name, datagram, exempt in malformed_datagrams(good):
        n += 1
        header = f"# malformed: {name}\n"
        if exempt:
            header += f"# lint-exempt: {exempt}\n"
        (udp_dir / f"{n:02d}-{name}.hex").write_text(header + datagram.hex() + "\n")
    counts["udp1990"] = n

    ext_dir = out / "extudp"
    ext_dir.mkdir(parents=True, exist_ok=True)
    literals = read_extudp_set(GETEXTERN_SRC)
    for i, literal in enumerate(literals, 1):
        (ext_dir / f"{i:02d}.json").write_text(rewrite_extudp(literal) + "\n")
    counts["extudp"] = len(literals)

    ble_dir = out / "ble"
    ble_dir.mkdir(parents=True, exist_ok=True)
    (ble_dir / "writes.txt").write_text(ble_corpus())
    counts["ble"] = len([l for l in ble_corpus().splitlines()
                         if l and not l.startswith("#")])

    cmd_dir = out / "commands"
    extract_commands.main(["--out", str(cmd_dir)])
    for fname, text in command_scripts(extract_commands.extract()).items():
        (cmd_dir / fname).write_text(text)
    counts["commands"] = len(extract_commands.extract())

    return counts


def _self_test() -> int:
    import tempfile

    failures = 0
    with tempfile.TemporaryDirectory() as tmp:
        out = Path(tmp)
        counts = build(out)

        import corpus_lint
        violations, checked = corpus_lint.lint_dir(
            out, corpus_lint.DEFAULT_ALLOWED_PREFIXES)
        # commands/ holds .txt files the lint does not read; only lora and
        # udp1990 carry frames.
        if violations:
            failures += 1
            print("FAIL: generated corpus does not pass its own lint:\n  "
                  + "\n  ".join(violations))
        if checked != counts["lora"] + counts["udp1990"]:
            failures += 1
            print(f"FAIL: lint checked {checked} files, corpus has "
                  f"{counts['lora'] + counts['udp1990']} frame entries")

        # Every rewritten frame must still verify: a wrong FCS is dropped
        # silently by the node, which would look like a behavioural diff.
        bad = 0
        for path in (out / "lora").iterdir():
            raw = corpus_lint.read_datagram(path)
            frame = mc_frame.parse(raw)
            if frame is not None and frame.compute_fcs() != frame.fcs:
                bad += 1
        if bad:
            failures += 1
            print(f"FAIL: {bad} rewritten frames carry a stale FCS")

        # The BLE corpus must not send messages at all -- see ble_corpus().
        ble = (out / "ble" / "writes.txt").read_text()
        for line in ble.splitlines():
            line = line.strip()
            if line and not line.startswith("#") and line.startswith(":"):
                failures += 1
                print(f"FAIL: the BLE corpus sends a message: {line!r}")
        if "{*}" in ble or "::*" in ble:
            failures += 1
            print("FAIL: the BLE corpus addresses '*'")
        if "--reboot" in ble or "--cleanflash" in ble:
            failures += 1
            print("FAIL: a destructive command is in the BLE corpus")

        # The manual list must hold every held-back command and nothing else.
        script = (out / "commands" / "script.txt").read_text()
        # Whole lines, not substrings: "--wifi" is a prefix of "--wifistat"
        # and "--wifiset", which are perfectly safe and must stay in the
        # script. The same loose-substring mistake that once classified
        # `rotate` as destructive.
        script_lines = {ln.strip() for ln in script.splitlines()}
        first_words = {ln.split(" ")[0] for ln in script_lines}
        for token in _MANUAL_ONLY:
            if f"--{token}" in first_words:
                failures += 1
                print(f"FAIL: manual-only command {token!r} is in the auto script")
        # Every command that stranded a node during the 2026-09-11 captures
        # must stay out, by name, so the list cannot quietly regress.
        for stranded in ("--deepsleep", "--setpwd", "--webpwd", "--webserver",
                         "--wifi on", "--wifi off", "--wifitxpower",
                         "--netconsole"):
            if stranded in script_lines or stranded.split(" ")[0] in first_words:
                failures += 1
                print(f"FAIL: {stranded} is in the auto script; it stranded a node")
        for harmless in ("rotate", "txsf", "format", "instreset"):
            if f"--{harmless}" not in script:
                failures += 1
                print(f"FAIL: harmless command {harmless!r} was held back")

        # No broadcast destination may survive into the transmitted corpus.
        broadcast = 0
        for path in (out / "udp1990").iterdir():
            raw = corpus_lint.read_datagram(path)
            if raw[:4] != b"GATE":
                continue
            frame = mc_frame.parse(raw[4:])
            if frame is not None and frame.dest == b"*":
                broadcast += 1
        if broadcast:
            failures += 1
            print(f"FAIL: {broadcast} GATE entries still address '*'")

    print("build_corpus.py self-test: "
          + ", ".join(f"{k} {v}" for k, v in counts.items())
          + " -- " + ("ok" if failures == 0 else f"{failures} failure(s)"))
    return 1 if failures else 0


def main(argv: Iterable[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--out", type=Path, default=DEFAULT_OUT)
    ap.add_argument("--self-test", action="store_true")
    args = ap.parse_args(list(argv) if argv is not None else None)

    if args.self_test:
        return _self_test()

    counts = build(args.out)
    for name, n in counts.items():
        print(f"{name:10} {n:4d} entries -> {args.out / name}", file=sys.stderr)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
