"""Structural pins for upstream issue #1279 (TRACK turn beacon sends E for W, N for S).

setSMartBeaconing() takes signed degrees. It copies them into posinfo_prev_lat/lon, and the
turn beacon in esp32_main.cpp / nrf52_main.cpp derives the hemisphere letter from their sign.
meshcom_settings.node_lat/node_lon hold magnitudes (hemisphere in node_lat_c/node_lon_c), so a
caller that passes them unconverted turns 87.6W into 87.6E. The OLED "POSITION SENT" line in
sendPosition() then gets a signed value next to its letter and must print the magnitude.

SMARTBEACON_SRC_ROOT points the test at a copy of src/ (mutation proof).
"""
import os
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SRC = Path(os.environ.get("SMARTBEACON_SRC_ROOT", ROOT / "src"))
ISSUE = "upstream issue #1279"

CALL = re.compile(r"setSMartBeaconing\s*\(([^;]*)\)\s*;")


def _code(text):
    """Source without // line comments, so a comment cannot hide or fake a call."""
    return re.sub(r"//[^\n]*", "", text)


def _calls():
    out = []
    for p in sorted(SRC.rglob("*.cpp")):
        for m in CALL.finditer(_code(p.read_text(errors="replace"))):
            args = m.group(1)
            if args.lstrip().startswith("double"):  # the definition
                continue
            out.append((p.relative_to(SRC).as_posix(), " ".join(args.split())))
    return out


def test_no_caller_passes_stored_magnitudes():
    bad = [(f, a) for f, a in _calls()
           if re.search(r"meshcom_settings\.node_l(at|on)\b(?!_c)", a)
           and "nodeSigned" not in a]
    assert not bad, f"setSMartBeaconing() needs signed degrees, see {ISSUE}: {bad}"


def test_gps_loop_converts_with_hemisphere():
    calls = [a for f, a in _calls() if f == "gps_functions.cpp"]
    assert len(calls) == 1, f"expected one call in gps_functions.cpp, got {calls}"
    a = calls[0]
    assert "nodeSignedLat(meshcom_settings.node_lat, meshcom_settings.node_lat_c)" in a, ISSUE
    assert "nodeSignedLon(meshcom_settings.node_lon, meshcom_settings.node_lon_c)" in a, ISSUE


def test_track_oled_prints_magnitude():
    src = (SRC / "loop_functions.cpp").read_text(errors="replace")
    start = src.index("void sendPosition(")
    body = src[start:src.index("\n}\n", start)]
    lines = [ln for ln in body.splitlines() if "%.4lf%c %.4lf%c" in ln]
    assert len(lines) == 1, f"TRACK OLED line not found once: {lines}"
    assert "fabs(lat), lat_c, fabs(lon), lon_c" in lines[0], (
        f"OLED would print '-87.6000W' for a signed turn point, see {ISSUE}")


def _body(rel, head):
    src = (SRC / rel).read_text(errors="replace")
    start = src.index(head)
    end = re.compile(r"\n\}[ \t]*(\n|$)").search(src, start)  # last function may end at EOF
    assert end, f"end of {head} not found in {rel}"
    return src[start:end.start()]


def test_tdeck_pro_passes_signed_degrees():
    # T-Deck Pro has its own GPS reader instead of WZ_GPS_Loop and reaches the same turn beacon.
    body = _body("t-deck-pro/tdeck_pro.cpp", "unsigned int tdeck_get_gps(")
    assert "setSMartBeaconing(dlat, dlon)" in body, "T-Deck Pro SmartBeaconing call moved"
    assert "cround4abs(" not in body, f"T-Deck Pro strips the sign before SmartBeaconing, see {ISSUE}"
    assert "dlat = cround4(lat);" in body and "dlon = cround4(lon);" in body, ISSUE


def test_rak_gps_stores_magnitudes():
    # node_lat/node_lon hold magnitudes; the nRF52 RAK GPS path stored the signed value next to 'S'/'W',
    # so every nodeSignedLat/Lon reader flipped it back to N/E.
    src = (SRC / "nrf52/nrf52_main.cpp").read_text(errors="replace")
    assert "meshcom_settings.node_lat = cround4(dlat);" not in src, "RAK GPS stores a signed latitude"
    assert "meshcom_settings.node_lon = cround4(dlon);" not in src, "RAK GPS stores a signed longitude"
    assert "meshcom_settings.node_lat = cround4abs(dlat);" in src
    assert "meshcom_settings.node_lon = cround4abs(dlon);" in src


# Readers that combine the own position with a signed coordinate (neighbour table, decoded APRS
# position) or feed courseTo/distanceBetween must sign the stored magnitudes first. Unsigned, a
# station 10 km away in Chicago (87.6W) shows up about 7,000 km away and the course is mirrored.
RAW_OWN = re.compile(r"meshcom_settings\.node_l(at|on)\b(?!_c)")
SIGNED_CALL = re.compile(r"nodeSigned(Lat|Lon)\([^)]*\)")


def _raw_own(stmt):
    """True if the statement still reads node_lat or node_lon outside a nodeSigned*() call."""
    return bool(RAW_OWN.search(SIGNED_CALL.sub("", stmt)))
OWN_POS_READERS = {
    "loop_functions.cpp": ("d_dir_to = gps.courseTo(", "dist_to = gps.distanceBetween("),
    "command_functions.cpp": ("gps.distanceBetween(v.lat, v.lon,",),
    "web_functions/web_functions.cpp": ("gps.distanceBetween(v.lat, v.lon,",),
    "mh_phone.cpp": ("mhJsonBuild(v, mhNodeEpoch(),",),
    "lora_functions.cpp": ("nbrNotePos(nbrMatrix, meshcom_settings.node_call,",),
}
# number of statements per file each anchor must find (a vanished site must fail, not pass)
OWN_POS_COUNT = {"loop_functions.cpp": 2, "command_functions.cpp": 1,
                 "web_functions/web_functions.cpp": 1, "mh_phone.cpp": 2, "lora_functions.cpp": 2}


def _statements(rel, anchor):
    code = _code((SRC / rel).read_text(errors="replace"))
    out, i = [], code.find(anchor)
    while i >= 0:
        out.append(" ".join(code[i:code.index(";", i)].split()))
        i = code.find(anchor, i + 1)
    return out


def test_own_position_readers_are_signed():
    for rel, anchors in OWN_POS_READERS.items():
        stmts = [st for a in anchors for st in _statements(rel, a)]
        assert len(stmts) == OWN_POS_COUNT[rel], f"{rel}: expected {OWN_POS_COUNT[rel]} sites, got {stmts}"
        bad = [st for st in stmts if _raw_own(st)]
        assert not bad, f"{rel}: own position passed as magnitude, see {ISSUE}: {bad}"


def test_station_course_uses_hemisphere():
    code = _code((SRC / "loop_functions.cpp").read_text(errors="replace"))
    i = code.index("dist_to = gps.distanceBetween(")
    window = code[i - 1500:i]
    assert "aprspos.lat_c == 'S'" in window and "aprspos.lon_c == 'W'" in window, (
        f"station position for course/distance lacks its hemisphere, see {ISSUE}")


# T-Deck map: without a GPS fix the map centre and own marker fall back to the stored position,
# which is then mixed with signed GPS degrees and signed station points.
MAP_FALLBACKS = {"t-deck/lv_obj_functions.cpp": 5,  # one (~2907) was already signed
                  "t-deck/event_functions.cpp": 1}


def test_tdeck_map_fallback_is_signed():
    for rel, n in MAP_FALLBACKS.items():
        code = _code((SRC / rel).read_text(errors="replace"))
        stmts = [" ".join(m.group(0).split()) for m in
                 re.finditer(r"\b(sdmap_lastKnownL(at|on)|lat|lon)\s*=\s*[^;=]*meshcom_settings\.node_l(at|on)\b[^;]*;", code)]
        assert len(stmts) == 2 * n, f"{rel}: expected {2 * n} map fallback assignments, got {stmts}"
        bad = [st for st in stmts if _raw_own(st)]
        assert not bad, f"{rel}: map falls back to the unsigned position, see {ISSUE}: {bad}"
