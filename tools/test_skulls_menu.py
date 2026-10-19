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
    # (ALL SKULLS, which menu_functions.c shows only once UNLOCK BY PLAYING was chosen)
    assert 'setting="game.skulls_all_unlocked"' in gameplay
    assert 'name="main_menu/settings_select/player_setup/player_profile_edit/gameplay/op_skulls_all_unlocked"' in gameplay
    assert 'input="gameplay screen update"' in gameplay
    # (INSTANT UNLOCK, shown with ALL SKULLS off)
    assert 'setting="game.skulls_instant_unlock"' in gameplay
    assert 'name="main_menu/settings_select/player_setup/player_profile_edit/gameplay/op_skulls_instant_unlock"' in gameplay
    assert len(port_settings.SKULLS) == 29
    for key, _ in port_settings.SKULLS:
        assert f'name="main_menu/difficulty_select/skulls/skull_{key}"' in screen
    assert screen.count('run="skull toggle"') == 2 * len(port_settings.SKULLS)
    assert 'run="skulls start"' in screen
    assert 'run="skulls all off"' in screen
    assert 'input="skulls screen update"' in screen
    assert 'bitmap="main_menu/difficulty_select/skulls/header_skulls"' in screen
    # (their text is menu_functions.c's: a string list's would overwrite it)
    assert not re.search(r'name="[^"]*/skull_[a-z_]+"[^>]* text=', screen)


def test_skulls_grid_is_one_list():
    """One list of the skulls and buttons, which steps no focus itself (a list
    of a row each kept every row's item lit): each item moves it in C."""
    screen = "\n".join(port_settings.settings_files()["main_menu.difficulty_select.skulls.xml"])
    menu = re.search(r'<widget name="main_menu/difficulty_select/skulls/options_menu"[^>]*>(.*?)</widget>', screen,
                     re.DOTALL)
    children = re.findall(r'<child widget="main_menu/difficulty_select/skulls/([a-z_]+)"', menu.group(1))
    items = [f"skull_{key}" for key, _ in port_settings.SKULLS] + ["button_all_off", "button_ok", "button_cancel"]

    assert 'flags="pass_unhandled_to_focused_child"' in menu.group(0)
    assert children == items
    assert "row_" not in screen
    for event, function in port_settings.SKULL_MOVES:
        assert screen.count(f'<on event="{event}" run="{function}"/>') == len(items)


def test_skull_helps_fit_the_help_line():
    """Each skull's help fits the help line's two lines (the line wraps a long
    one): 88 characters at most."""
    table = (ROOT / "port/linux/game/skulls.c").read_text()
    helps = re.findall(r'^\t\{ "[a-z_]+", "[^"]+", "((?:[^"\\]|\\.)*)" \},$', table, re.MULTILINE)

    assert len(helps) == len(port_settings.SKULLS)
    for text in helps:
        lines = text.replace("\\'", "'").split("\\n")
        assert len(lines) <= 2, text
        assert sum(len(line) for line in lines) <= 88, text


def test_skulls_unlock_question():
    """The first opening asks how the skulls are had, its two answers run in C."""
    screen = "\n".join(port_settings.settings_files()["main_menu.difficulty_select.skulls.xml"])

    assert 'name="main_menu/difficulty_select/skulls/unlock_screen"' in screen
    assert 'run="skulls unlock all"' in screen
    assert 'run="skulls unlock play"' in screen
    for line in port_settings.SKULLS_UNLOCK_TEXT:
        assert len(line) <= 44, line


def test_screens_after_the_difficulty_keep_the_menu_going():
    """The randomizer's and the skulls' screens are the main menu's: they do not
    pause the game, whose clock runs the menu's music and video."""
    files = port_settings.settings_files()
    for name in ("main_menu.difficulty_select.randomizer.xml", "main_menu.difficulty_select.skulls.xml"):
        assert "pause_game" not in "\n".join(files[name]), name


def test_each_skull_has_its_icon():
    """port/assets/skulls has an icon of each skull, and the menus' bitmap
    skulls/icons has them in the skulls' order (skulls.c draws frame n for
    skull n), embedded."""
    import json
    bitmaps = (ROOT / "port/assets/menus/ce/bitmaps.xml").read_text()
    files = json.loads((ROOT / "port/assets/menus/menus.json").read_text())["files"]
    frames = re.search(r'<bitmap name="skulls/icons">(.*?)</bitmap>', bitmaps, re.DOTALL).group(1)
    pngs = re.findall(r'png="([^"]+)"', frames)

    # (the skulls', then the cross over one lost)
    assert len(pngs) == len(port_settings.SKULLS) + 1
    for index, key in enumerate([key for key, _ in port_settings.SKULLS] + ["lost"]):
        source = ROOT / "port/assets/skulls" / f"{key}.png"
        assert source.is_file(), key
        assert pngs[index] in files
        assert (ROOT / "port/assets/menus" / pngs[index]).read_bytes() == source.read_bytes(), key


def test_skulls_are_alphabetical():
    """The skulls screen lists them in alphabetical order of their keys."""
    keys = [key for key, _ in port_settings.SKULLS]
    assert keys == sorted(keys)


def test_skull_handlers_are_registered():
    """menu_tags.c names the screen's functions, and menu_functions.c runs them."""
    tags = (ROOT / "port/linux/game/menu_tags.c").read_text()
    functions = (ROOT / "port/linux/game/menu_functions.c").read_text()

    for name in ("skull toggle", "skulls all off", "skulls start", "skulls up", "skulls down", "skulls left",
                 "skulls right", "skulls unlock all", "skulls unlock play", "skulls screen update", "gameplay screen update"):
        assert f'"{name}"' in tags
        assert f'!strcmp(name, "{name}")' in functions
