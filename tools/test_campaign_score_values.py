"""The campaign's scoring values: port/linux/game/campaign_score_values.c is
what tools/campaign_score_values.py makes of its JSON, and the JSON's names
are actor_types.h's and skulls.c's."""
import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))

import campaign_score_values  # noqa: E402


def _generate_from(values: dict) -> str:
    """the C file a JSON of these values makes (the real one left as it is)"""
    saved = campaign_score_values.JSON_PATH.read_text()
    try:
        campaign_score_values.JSON_PATH.write_text(json.dumps(values))
        return campaign_score_values.generate()
    finally:
        campaign_score_values.JSON_PATH.write_text(saved)


def _rejects(values: dict, words: str):
    try:
        _generate_from(values)
    except campaign_score_values.ValuesError as error:
        assert words in str(error), error
        return
    raise AssertionError(f"accepted, expected an error about {words}")


def test_c_file_is_the_json():
    """the checked-in C file is what the JSON makes"""
    assert campaign_score_values.C_PATH.read_text() == campaign_score_values.generate()


def test_values():
    """the difficulties, the par times (in minutes) and the time's brackets"""
    text = campaign_score_values.generate()
    assert "{ 0.5f, 1.0f, 2.0f, 4.0f }," in text
    assert '{ "a10", 900 },' in text
    assert '{ "c10", 720 },' in text
    assert "{ -1.0f, 0.5f }," in text
    assert "{ 0.8f, 1.0f, 1.3f, 1.8f }," in text
    assert "\t1.2f," in text


def test_names_are_checked():
    """an actor type or a skull that does not exist is an error"""
    values = json.loads(campaign_score_values.JSON_PATH.read_text())
    bad = json.loads(json.dumps(values))
    bad["enemies"]["brute"] = {"default": 20}
    _rejects(bad, "not an actor type")
    bad = json.loads(json.dumps(values))
    bad["skulls"]["multipliers"]["tilt"] = 1.2
    _rejects(bad, "not a skull")
    bad = json.loads(json.dumps(values))
    bad["penalties"]["death"] = 25
    _rejects(bad, "should be negative")
    bad = json.loads(json.dumps(values))
    bad["time"]["par_times"]["a10"] = "15:00"
    _rejects(bad, "is not a number")


def test_actor_types_and_skulls_are_read():
    types = campaign_score_values.actor_types()
    assert types[:3] == ["elite", "jackal", "grunt"]
    assert "mounted_weapon" in types
    assert "iron" in campaign_score_values.skull_keys()


def test_every_skull_is_listed():
    """each of skulls.c's skulls has its multiplier in the JSON"""
    values = json.loads(campaign_score_values.JSON_PATH.read_text())
    listed = set(values["skulls"]["multipliers"])
    missing = [key for key in campaign_score_values.skull_keys() if key not in listed]
    assert not missing, f"skulls with no multiplier: {missing}"


def test_every_medal_has_points_and_an_icon():
    """each of campaign_medals.c's medals has its points in the JSON, its
    icon in port/assets/medals, and its frame in the menus' medals/icons"""
    values = json.loads(campaign_score_values.JSON_PATH.read_text())
    keys = campaign_score_values.medal_keys()
    assert len(keys) == 38
    missing = [key for key in keys if key not in values["medals"]["points"]]
    assert not missing, f"medals with no points: {missing}"
    for key in keys:
        icon = ROOT / "port/assets/medals" / f"{key}.png"
        assert icon.is_file(), icon
        # (8-bit RGBA, not interlaced: hud_hires.c reads no other)
        header = icon.read_bytes()[:29]
        assert header[24:29] == bytes([8, 6, 0, 0, 0]), (key, header[24:29])
    bitmaps = (ROOT / "port/assets/menus/ce/bitmaps.xml").read_text()
    frames = re.search(r'<bitmap name="medals/icons">(.*?)</bitmap>', bitmaps, re.DOTALL).group(1)
    assert frames.count("<frame") == len(keys)
    listed = json.loads((ROOT / "port/assets/menus/menus.json").read_text())["files"]
    for index in range(len(keys)):
        assert f"ce/port/medals/icons__{index}.png" in listed
