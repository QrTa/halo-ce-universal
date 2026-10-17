"""The skulls' menus, as tools/port_settings.py writes them, and their table
in port/linux/game/skulls.c."""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))

import port_settings  # noqa: E402


def test_skulls_menus():
    """Gameplay has the switch, and difficulty opens a screen of every skull."""
    files = port_settings.settings_files()
    gameplay = "\n".join(files[
        "main_menu.settings_select.player_setup.player_profile_edit.gameplay.xml"])
    screen = "\n".join(files["main_menu.difficulty_select.skulls.xml"])

    assert 'setting="game.skulls"' in gameplay
    assert "SKULLS:" in gameplay
    assert len(port_settings.SKULLS) == 23
    for key, _ in port_settings.SKULLS:
        assert f'name="main_menu/difficulty_select/skulls/skull_{key}"' in screen
    assert screen.count('run="skull toggle"') == 2 * len(port_settings.SKULLS)
    assert 'run="skulls start"' in screen
    assert 'run="skulls all off"' in screen
    assert 'input="skulls screen update"' in screen
    assert 'bitmap="main_menu/difficulty_select/skulls/header_skulls"' in screen
    # (their text is menu_functions.c's: a string list's would overwrite it)
    assert not re.search(r'name="[^"]*/skull_[a-z_]+"[^>]* text=', screen)


def test_skulls_match_the_game():
    """skulls.c's table, halo_skulls.h's enum and SKULLS name the skulls in the same order."""
    table = (ROOT / "port/linux/game/skulls.c").read_text()
    header = (ROOT / "port/linux/include/halo_skulls.h").read_text()
    rows = re.findall(r'^\t\{ "([a-z_]+)", "([^"]+)",', table, re.MULTILINE)
    enum = re.findall(r"^\t_skull_([a-z_]+)(?: = 0)?,$", header[:header.index("NUMBER_OF_SKULLS")], re.MULTILINE)

    assert rows == port_settings.SKULLS
    assert enum == [key for key, _ in port_settings.SKULLS]


def test_skull_handlers_are_registered():
    """menu_tags.c names the screen's functions, and menu_functions.c runs them."""
    tags = (ROOT / "port/linux/game/menu_tags.c").read_text()
    functions = (ROOT / "port/linux/game/menu_functions.c").read_text()

    for name in ("skull toggle", "skulls all off", "skulls start", "skulls screen update"):
        assert f'"{name}"' in tags
        assert f'!strcmp(name, "{name}")' in functions
