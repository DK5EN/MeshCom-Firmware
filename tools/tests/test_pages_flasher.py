"""Tests for tools/pages_flasher.py.

Run: uv run --with pytest pytest tools/tests/test_pages_flasher.py
"""

import json
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import pages_flasher as pf  # noqa: E402

REPO = pf.REPO


@pytest.fixture(scope="module")
def cfg():
    return pf.load_config(REPO)


def fake_build(tmp_path: Path, envs) -> Path:
    """A .pio/build stand-in whose every file carries its own identity."""
    build = tmp_path / "build"
    for env in envs:
        d = build / env
        d.mkdir(parents=True)
        for name in ("bootloader.bin", "partitions.bin", "firmware.bin"):
            (d / name).write_bytes(f"{env}/{name}".encode())
    return build


# --------------------------------------------------------------------------
# upload_command parsing
# --------------------------------------------------------------------------


def test_parse_upload_command_pairs():
    cmd = (
        'esptool --port "$UPLOAD_PORT" -b 921600 write_flash '
        "0x0000 $BUILD_DIR/bootloader.bin 0xE000 otadata.bin "
        "0x8000 $BUILD_DIR/partitions.bin 0x10000 safeboot-s3.bin "
        "0xC0000 $BUILD_DIR/firmware.bin"
    )
    assert pf.parse_upload_command(cmd) == [
        (0x0, "$BUILD_DIR/bootloader.bin"),
        (0xE000, "otadata.bin"),
        (0x8000, "$BUILD_DIR/partitions.bin"),
        (0x10000, "safeboot-s3.bin"),
        (0xC0000, "$BUILD_DIR/firmware.bin"),
    ]


def test_parse_upload_command_rejects_odd_arguments():
    with pytest.raises(ValueError):
        pf.parse_upload_command("write_flash 0x0 a.bin 0x1000")


def test_parse_upload_command_rejects_missing_write_flash():
    with pytest.raises(ValueError):
        pf.parse_upload_command("esptool erase_flash")


def test_resolve_token_forms(tmp_path):
    build = tmp_path / "build"
    for token in ("$BUILD_DIR/firmware.bin",
                  "${platformio.build_dir}/${this.__env__}/firmware.bin"):
        got = pf.resolve_token(token, "t_deck", REPO, build)
        assert got == build / "t_deck" / "firmware.bin"
    assert pf.resolve_token("safeboot.bin", "t_deck", REPO, build) == REPO / "safeboot.bin"


# --------------------------------------------------------------------------
# chip family and layout, derived from the real tree
# --------------------------------------------------------------------------


@pytest.mark.parametrize(
    "env,family",
    [
        ("heltec_wifi_lora_32_V3", "ESP32-S3"),
        ("ttgo_tbeam_SX1262", "ESP32"),
        ("wiscore_rak4631", "NRF52"),
    ],
)
def test_chip_family_from_board_json(cfg, env, family):
    assert pf.chip_family(cfg, env, REPO) == family


def test_every_release_env_resolves(cfg):
    for env in pf.RELEASE_ENVS:
        assert pf.chip_family(cfg, env, REPO) in pf.MCU_FAMILY.values()
        assert env in pf.DISPLAY, f"{env} has no display name"


def test_bootloader_offset_differs_by_family(cfg, tmp_path):
    build = tmp_path / "build"
    s3 = pf.esp_parts(cfg, "heltec_wifi_lora_32_V3", "ESP32-S3", REPO, build)
    classic = pf.esp_parts(cfg, "ttgo_tbeam_SX1262", "ESP32", REPO, build)
    assert [o for o, _, _ in s3] == [0x0, 0x8000, 0xE000, 0x10000, 0xC0000]
    assert [o for o, _, _ in classic] == [0x1000, 0x8000, 0xE000, 0x10000, 0xC0000]
    assert [n for _, _, n in s3][3] == "safeboot-s3.bin"
    assert [n for _, _, n in classic][3] == "safeboot.bin"


def test_t_deck_pro_falls_back_to_family_default(cfg, tmp_path):
    # Its upload_command is commented out in the variant file.
    assert pf.resolve(cfg, "t_deck_pro", "upload_command") is None
    parts = pf.esp_parts(cfg, "t_deck_pro", "ESP32-S3", REPO, tmp_path)
    assert [o for o, _, _ in parts] == [0x0, 0x8000, 0xE000, 0x10000, 0xC0000]


# --------------------------------------------------------------------------
# radio chip, derived from the variant header
# --------------------------------------------------------------------------


@pytest.mark.parametrize(
    "env,chip",
    [
        ("ttgo_tbeam", "SX1278"),            # SX127X
        ("ttgo_tbeam_SX1262", "SX1262"),     # SX1262X
        ("ttgo_tbeam_SX1268", "SX1268"),     # SX126X -> declares an SX1268
        ("E22-DevKitC", "SX1268"),           # SX126X, NOT an SX1262
        ("E22_1262_S3-DevKitC-1-N16R8", "SX1262"),  # SX126x_V3 + SX1262_E22
        ("E22_1268_S3-DevKitC-1-N16R8", "SX1268"),  # SX126x_V3 + SX1268_E22
        ("heltec_wifi_lora_32_V3", "SX1262"),  # SX1262_V3
        ("wireless-paper", "SX1262"),          # two defines, one chip
        ("wiscore_rak4631", "SX1262"),         # nRF52, no variant define
    ],
)
def test_radio_chip_from_variant_header(env, chip, cfg):
    assert pf.radio_chip(env, pf.chip_family(cfg, env, REPO), REPO) == chip


def test_every_release_env_yields_exactly_one_radio(cfg):
    for env in pf.RELEASE_ENVS:
        chip = pf.radio_chip(env, pf.chip_family(cfg, env, REPO), REPO)
        assert chip in pf.CHIP_LABEL


def test_sx127x_family_is_labelled_as_one_part():
    """SX1276/77/78/79 are one piece of silicon; one image covers all four."""
    assert pf.CHIP_LABEL["SX1278"] == "SX1276/77/78/79"
    assert pf.CHIP_LABEL["SX1262"] == "SX1262"
    assert pf.CHIP_LABEL["SX1268"] == "SX1268"


def test_display_names_carry_no_chip_text():
    """The chip belongs to radio_chip(), not to the hand-maintained table."""
    for env, (_group, name) in pf.DISPLAY.items():
        assert "SX12" not in name.upper(), f"{env}: chip text in the display name"


def test_boards_sharing_a_name_are_told_apart_by_the_radio(cfg):
    labels = {}
    for env in pf.RELEASE_ENVS:
        group, name = pf.DISPLAY[env]
        radio = pf.CHIP_LABEL[pf.radio_chip(env, pf.chip_family(cfg, env, REPO), REPO)]
        key = (group, name, radio)
        assert key not in labels, f"{env} and {labels.get(key)} render identically"
        labels[key] = env


# --------------------------------------------------------------------------
# detect.json: hardware IDs for the "Board erkennen" button
# --------------------------------------------------------------------------


def test_hardware_ids_from_the_global_table():
    ids = pf.hardware_ids(REPO)
    assert ids["TBEAM"] == 4
    assert ids["RAK4631"] == 9
    assert ids["TBEAM_AXP2101"] == 12
    assert ids["HELTEC_V3"] == 43


def test_every_release_env_has_a_hardware_id():
    table = pf.detect_table(REPO)
    assert sorted(table["boards"]) == sorted(pf.RELEASE_ENVS)
    for env, b in table["boards"].items():
        assert isinstance(b["hwid"], int), env
        assert b["radio"] in pf.CHIP_LABEL, env
        assert b["chipFamily"] in pf.MCU_FAMILY.values(), env


@pytest.mark.parametrize(
    "env,hwid",
    [
        ("heltec_wifi_lora_32_V3", 43),
        ("wiscore_rak4631", 9),
        ("ttgo_tbeam", 4),
        ("ttgo_tbeam_SX1262", 45),
        ("ttgo_tbeam_SX1268", 5),
        ("ttgo_tbeam_supreme", 47),
        ("t_deck_plus", 46),
    ],
)
def test_hardware_id_per_env(env, hwid):
    assert pf.detect_table(REPO)["boards"][env]["hwid"] == hwid


def test_axp2101_id_covers_the_tbeam_images_but_not_the_supreme():
    """A T-Beam image on a v1.2 board answers 12 whatever its radio.

    The Supreme has an AXP2101 too, but BOARD_TBEAM_V3 keeps its own ID.
    """
    aliases = pf.detect_table(REPO)["aliases"]
    assert sorted(aliases["12"]) == ["ttgo_tbeam", "ttgo_tbeam_SX1262", "ttgo_tbeam_SX1268"]


def test_axp2101_override_still_in_the_firmware():
    """reports_axp2101_id() mirrors this block; if it moves, the alias is stale."""
    src = (REPO / "src" / "esp32" / "esp32_pmu.cpp").read_text()
    assert "#ifndef BOARD_TBEAM_V3\n            BOARD_HARDWARE = TBEAM_AXP2101;" in src


def test_write_detect_lands_next_to_releases_json(tmp_path):
    pf.write_detect(tmp_path, REPO)
    data = json.loads((tmp_path / "detect.json").read_text())
    assert data["boards"]["wiscore_rak4631"]["chipFamily"] == "NRF52"


# --------------------------------------------------------------------------
# staging
# --------------------------------------------------------------------------


def test_manifest_for_s3_classic_and_nrf52(cfg, tmp_path, monkeypatch):
    envs = ["heltec_wifi_lora_32_V3", "ttgo_tbeam_SX1262", "wiscore_rak4631"]
    build = fake_build(tmp_path, envs)
    (build / "wiscore_rak4631" / "firmware.hex").write_bytes(b":00000001FF\n")
    monkeypatch.setattr(
        pf, "uf2_from_hex", lambda h, o: Path(o).write_bytes(b"UF2\n")
    )
    out = tmp_path / "flash"
    for env in envs:
        pf.stage_board(cfg, env, "v1", out / "v1" / env, REPO, build)

    s3 = json.loads((out / "v1" / "heltec_wifi_lora_32_V3" / "manifest.json").read_text())
    assert s3["builds"][0]["chipFamily"] == "ESP32-S3"
    assert s3["builds"][0]["parts"][0] == {"path": "bootloader.bin", "offset": 0}
    assert s3["new_install_prompt_erase"] is True
    assert s3["version"] == "v1"

    classic = json.loads((out / "v1" / "ttgo_tbeam_SX1262" / "manifest.json").read_text())
    assert classic["builds"][0]["chipFamily"] == "ESP32"
    assert classic["builds"][0]["parts"][0] == {"path": "bootloader.bin", "offset": 4096}

    nrf = json.loads((out / "v1" / "wiscore_rak4631" / "manifest.json").read_text())
    assert nrf["builds"][0] == {"chipFamily": "NRF52", "parts": [{"path": "firmware.uf2"}]}
    assert (out / "v1" / "wiscore_rak4631" / "firmware.uf2").exists()


def test_board_folder_is_self_contained_per_env(cfg, tmp_path):
    """Each board gets ITS OWN bootloader and partition table, never a shared one.

    This is what keeps the 16 MB T-Deck table out of the 4 MB Heltec folder.
    """
    envs = ["t_deck", "heltec_wifi_lora_32_V3"]
    build = fake_build(tmp_path, envs)
    out = tmp_path / "flash"
    for env in envs:
        pf.stage_board(cfg, env, "v1", out / "v1" / env, REPO, build)
    for env in envs:
        for name in ("bootloader.bin", "partitions.bin", "firmware.bin"):
            assert (out / "v1" / env / name).read_bytes() == f"{env}/{name}".encode()


def test_missing_artifact_refuses_to_ship_a_partial_board(cfg, tmp_path):
    build = fake_build(tmp_path, ["t_deck"])
    (build / "t_deck" / "partitions.bin").unlink()
    with pytest.raises(SystemExit):
        pf.stage_board(cfg, "t_deck", "v1", tmp_path / "out", REPO, build)


# --------------------------------------------------------------------------
# releases.json and pruning
# --------------------------------------------------------------------------


def test_prune_keeps_newest_n(tmp_path):
    root = tmp_path
    for v in ("v3", "v2", "v1"):
        info = pf.update_releases(root, v, [{"env": "x", "group": "g", "name": "n",
                                             "chipFamily": "ESP32"}], keep=3)
    assert info["kept"] == ["v1", "v2", "v3"]
    assert info["pruned"] == []

    info = pf.update_releases(root, "v4", [], keep=3)
    assert info["kept"] == ["v4", "v1", "v2"]
    assert info["pruned"] == ["v3"]
    assert [r["version"] for r in json.loads((root / "releases.json").read_text())["releases"]] \
        == ["v4", "v1", "v2"]


def test_republishing_a_version_does_not_duplicate_it(tmp_path):
    pf.update_releases(tmp_path, "v1", [], keep=3)
    info = pf.update_releases(tmp_path, "v1", [], keep=3)
    assert info["kept"] == ["v1"]
