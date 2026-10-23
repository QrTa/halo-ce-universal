"""The Tag Injector's names for a protected map's tags
(port/linux/game/tag_names.c), run on a tag table tools/tag_names_check.c
makes, as the game's units are built: by ninja as
build/web/tools/tag_names_check.js (the web build's, which needs
Emscripten), run with node."""
import shutil
import subprocess
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parent.parent
CHECK = ROOT / "build/web/tools/tag_names_check.js"


@pytest.fixture(scope="module")
def check_output():
    if not shutil.which("ninja") or not shutil.which("node"):
        pytest.skip("ninja and node are needed")
    built = subprocess.run(["ninja", str(CHECK.relative_to(ROOT))], cwd=ROOT, capture_output=True, text=True)
    if built.returncode != 0:
        if "unknown target" in built.stdout + built.stderr:
            pytest.skip("the web build is not configured (Emscripten is needed)")
        pytest.fail(built.stdout + built.stderr)
    run = subprocess.run(["node", str(CHECK)], cwd=ROOT, capture_output=True, text=True, timeout=60)
    assert run.returncode == 0, run.stdout + run.stderr
    names = {}
    values = {}
    for line in run.stdout.splitlines():
        fields = line.split("\t")
        if len(fields) == 4:
            names[int(fields[0])] = (fields[1], fields[2] == "1", fields[3])
        elif len(fields) == 2:
            values[fields[0]] = fields[1]
    return names, values


def test_items_are_named_by_their_pickup_text(check_output):
    names, _ = check_output
    assert names[4] == ("weap", True, "descent-quarantine\\weapons\\battle rifle\\battle rifle")
    # (another of the same text is numbered, folder and all)
    assert names[5] == ("weap", True, "descent-quarantine\\weapons\\battle rifle 2\\battle rifle 2")


def test_objects_are_named_by_the_stock_folder_of_their_tags(check_output):
    names, _ = check_output
    assert names[7] == ("bipd", True, "descent-quarantine\\characters\\elite\\elite")


def test_actor_variants_are_named_by_their_unit_and_weapon(check_output):
    names, _ = check_output
    assert names[10] == ("actv", True, "descent-quarantine\\characters\\elite\\elite battle rifle")
    # (one whose name is empty, as a protector may leave it)
    assert names[11] == ("actv", True, "descent-quarantine\\characters\\elite\\elite battle rifle 2")


def test_other_tags_are_named_in_the_folder_of_what_names_them(check_output):
    names, _ = check_output
    assert names[6] == ("mod2", True, "descent-quarantine\\weapons\\battle rifle\\model")
    assert names[8] == ("mod2", True, "descent-quarantine\\characters\\elite\\model")
    assert names[0] == ("scnr", True, "descent-quarantine\\descent-quarantine")
    assert names[1] == ("matg", True, "descent-quarantine\\globals")
    # (what nothing names)
    assert names[2] == ("hudg", True, "descent-quarantine\\unnamed\\hudg")


def test_names_of_their_own_are_kept(check_output):
    names, _ = check_output
    assert names[3] == ("ustr", False, "ui\\hud\\hud item messages")
    assert names[9] == ("coll", False, "characters\\elite\\elite")
    # (a resource map's keeps its path, which another of its group has)
    assert names[14] == ("bitm", False, "levels\\shared\\stock")


def test_a_path_tags_share_is_none_of_theirs(check_output):
    names, _ = check_output
    assert names[12] == ("bitm", True, "descent-quarantine\\unnamed\\bitm")
    assert names[13] == ("bitm", True, "descent-quarantine\\unnamed\\bitm 2")
    assert names[15] == ("bitm", True, "descent-quarantine\\unnamed\\bitm 3")


def test_the_names_are_the_same_each_time_and_unique(check_output):
    names, values = check_output
    assert values["made"] == "13"
    assert values["same"] == "1"
    assert values["again"] == "0"
    paths = [(group, name.lower()) for group, _, name in names.values()]
    assert len(paths) == len(set(paths))
