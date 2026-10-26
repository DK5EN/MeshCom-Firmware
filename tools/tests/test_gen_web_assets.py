"""web_assets.h must match src/web_functions/web/*, and the firmware must serve and reference every asset."""
import importlib.util
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location("gen_web_assets", ROOT / "tools" / "gen_web_assets.py")
gen = importlib.util.module_from_spec(spec)
spec.loader.exec_module(gen)
CPP = (ROOT / "src" / "web_functions" / "web_functions.cpp").read_text(encoding="utf-8")


def test_header_is_up_to_date():
    # run `python3 tools/gen_web_assets.py` after editing a source in src/web_functions/web/
    shipped = gen.decode_header(gen.HEADER.read_text(encoding="utf-8"))
    assert shipped == gen.decode_header(gen.render())


def test_blobs_are_the_stripped_sources_and_tags_follow_the_content():
    shipped = gen.decode_header(gen.HEADER.read_text(encoding="utf-8"))
    for fname, ident, _ in gen.ASSETS:
        raw = gen.strip(fname, (gen.WEB / fname).read_text(encoding="utf-8"))
        assert shipped[ident] == (gen.tag(raw), raw), fname


def test_generation_is_deterministic():
    assert gen.render() == gen.render()


def test_every_asset_is_served_and_referenced_once():
    for fname, ident, _ in gen.ASSETS:
        assert 'startsWith("GET /%s")' % fname in CPP, fname
        assert "web_%s_gz" % ident.lower() in CPP, fname
        assert "/%s?v=\" WEB_%s_TAG" % (fname, ident) in CPP, fname


def test_assets_are_served_before_the_login_gate():
    assert CPP.index("deliver_web_asset(web_header)") < CPP.index("if (bget_password)\n                    {")


def test_blob_is_written_never_printed():
    body = CPP[CPP.index("static bool deliver_web_asset") : CPP.index("String work_webpage")]
    assert re.search(r"web_client\.write\(blob, len\)", body)
    assert not re.search(r"printf?\(.*blob", body)
