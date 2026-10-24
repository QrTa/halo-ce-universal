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
    settings = "\n".join(files[
        "main_menu.settings_select.player_setup.player_profile_edit.gameplay.skulls.xml"])
    screen = "\n".join(files["main_menu.difficulty_select.skulls.xml"])
    base = "main_menu/settings_select/player_setup/player_profile_edit/gameplay"

    # (Gameplay's three rows, each opening its screen)
    for folder, screen_name in (("ragdolls", "ragdolls_settings_screen"), ("randomizer", "randomizer_settings_screen"),
                                ("skulls", "skulls_settings_screen")):
        assert f'open="{base}/{folder}/{screen_name}"' in gameplay
    assert 'setting="game.skulls"' in settings
    assert "SKULLS:" in settings
    # (ALL SKULLS, which menu_functions.c shows only once the skulls screen's question was answered)
    assert 'setting="game.skulls_all_unlocked"' in settings
    assert f'name="{base}/skulls/op_skulls_all_unlocked"' in settings
    assert 'input="gameplay screen update"' in settings
    # (INSTANT UNLOCK, shown with ALL SKULLS off)
    assert 'setting="game.skulls_instant_unlock"' in settings
    assert f'name="{base}/skulls/op_skulls_instant_unlock"' in settings
    # (LOSE ON DEATH, shown with INSTANT UNLOCK off too)
    assert 'setting="game.skulls_lose_on_death"' in settings
    assert f'name="{base}/skulls/op_skulls_lose_on_death"' in settings
    assert '"op_skulls_lose_on_death"' in (ROOT / "port/linux/game/menu_functions.c").read_text()
    assert '"game.skulls_lose_on_death"' in (ROOT / "port/linux/src/port_config.c").read_text()
    # (SKULLS PER LEVEL and DROP CHANCE, shown with ALL SKULLS off: the
    # drops' count and odds, the host's in co-op)
    for setting in ("skulls_drop_count", "skulls_drop_chance"):
        assert f'setting="game.{setting}"' in settings
        assert f'name="{base}/skulls/op_{setting}"' in settings
        assert f'"op_{setting}"' in (ROOT / "port/linux/game/menu_functions.c").read_text()
        assert f'"game.{setting}"' in (ROOT / "port/linux/src/port_config.c").read_text()
    # (RESET SKULL PROGRESS, the last row: its help, then the question and
    # the done that menu_functions.c shows after it)
    assert f'name="{base}/skulls/op_reset_progress"' in settings
    assert 'run="skulls reset progress"' in settings
    helps = re.findall(r'<string text="([^"]*)"/>', settings.split("help_strings")[-1])
    assert helps[-3].startswith("Forget every skull")
    assert helps[-2].startswith("Press A again")
    assert helps[-1].startswith("Skull progress reset")
    assert len(port_settings.SKULLS) == 40
    for key, _ in port_settings.SKULLS:
        assert f'name="main_menu/difficulty_select/skulls/skull_{key}"' in screen
    assert screen.count('run="skull toggle"') == 2 * len(port_settings.SKULLS)
    assert 'run="skulls start"' in screen
    assert 'run="skulls toggle all"' in screen
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
    items = [f"skull_{key}" for key, _ in port_settings.SKULLS] + ["button_toggle_all", "button_random", "button_ok",
                                                                    "button_cancel"]

    assert 'flags="pass_unhandled_to_focused_child"' in menu.group(0)
    assert children == items
    assert "skulls/row_" not in screen
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
    """The first opening asks how the skulls are had, its three answers run in C."""
    screen = "\n".join(port_settings.settings_files()["main_menu.difficulty_select.skulls.xml"])

    assert 'name="main_menu/difficulty_select/skulls/unlock_screen"' in screen
    assert 'run="skulls unlock all"' in screen
    assert 'run="skulls unlock play"' in screen
    assert 'run="skulls unlock pickup"' in screen
    for line in port_settings.SKULLS_UNLOCK_TEXT:
        assert len(line) <= 56, line
    # (UNLOCK VIA GAMEPLAY's question of a death, B back to the first)
    assert 'name="main_menu/difficulty_select/skulls/death_screen"' in screen
    assert 'run="skulls death lose"' in screen
    assert 'run="skulls death keep"' in screen
    assert '<on event="b" run="skulls death back"/>' in screen
    for line in port_settings.SKULLS_DEATH_TEXT:
        assert len(line) <= 56, line


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

    # (the skulls', then the cross over one lost, the mark over one dropped,
    # the check over one unlocked)
    assert len(pngs) == len(port_settings.SKULLS) + 3
    for index, key in enumerate([key for key, _ in port_settings.SKULLS] + ["lost", "dropped", "unlocked"]):
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

    for name in ("skull toggle", "skulls toggle all", "skulls start", "skulls up", "skulls down", "skulls left",
                 "skulls right", "skulls unlock all", "skulls unlock play", "skulls unlock pickup", "skulls death lose",
                 "skulls death keep", "skulls death back", "skulls screen update", "gameplay screen update"):
        assert f'"{name}"' in tags
        assert f'!strcmp(name, "{name}")' in functions


def test_server_setup_campaign_rows():
    """Server Setup has rows opening the randomizer's and the skulls'
    screens for a co-op game, which menu_tags.c names and menu_functions.c
    runs and shows."""
    server = "\n".join(port_settings.multiplayer_files()["main_menu.multiplayer_type_select.server_settings.xml"])
    functions = (ROOT / "port/linux/game/menu_functions.c").read_text()
    # (a function the menus run is one menu_tags.c names: one it does not
    # is a problem, and the game then keeps its own menus, every file of ours
    # left out)
    tags = (ROOT / "port/linux/game/menu_tags.c").read_text(encoding="latin-1")
    for key, title, _ in port_settings.CAMPAIGN_SETUP_SCREENS:
        assert f'"ss {key}"' in tags
        assert f'name="main_menu/multiplayer_type_select/server_settings/op_{key}"' in server
        assert f'run="ss {key}"' in server
        assert f'"{title}:"' in server or f'text="{title}:"' in server
        assert f'"ss {key}"' in functions
        assert f'"op_{key}"' in functions


def test_skulls_random_screen():
    """RANDOM opens a screen of the fewest and the most skulls, settings of
    port_config.c's, whose ROLL menu_tags.c lists and menu_functions.c runs."""
    screen = "\n".join(port_settings.settings_files()["main_menu.difficulty_select.skulls.xml"])
    base = "main_menu/difficulty_select/skulls"

    assert f'open="{base}/random_screen"' in screen
    assert f'name="{base}/random_screen"' in screen
    assert 'run="skulls random roll"' in screen
    for name in ("skulls random roll",):
        assert f'"{name}"' in (ROOT / "port/linux/game/menu_tags.c").read_text()
        assert f'"{name}"' in (ROOT / "port/linux/game/menu_functions.c").read_text()
    choices = "|".join(str(count) for count in range(len(port_settings.SKULLS) + 1))
    for key, _, setting in port_settings.SKULLS_RANDOM_ROWS:
        assert f'setting="{setting}"' in screen
        assert f'name="{base}/op_{key}"' in screen
        assert f'"{setting}"' in (ROOT / "port/linux/src/port_config.c").read_text()
        assert f'`{setting}`' in (ROOT / "port/linux/README.md").read_text()
    assert f'values="{choices}"' in screen
