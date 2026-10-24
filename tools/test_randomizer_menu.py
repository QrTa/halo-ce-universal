"""The campaign randomizer's menus, as tools/port_settings.py writes them."""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))

import port_settings  # noqa: E402


def test_randomizer_menus():
    """Gameplay has the switch, and difficulty opens a screen for the setup."""
    files = port_settings.settings_files()
    gameplay = "\n".join(files[
        "main_menu.settings_select.player_setup.player_profile_edit.gameplay.xml"])
    settings = "\n".join(files[
        "main_menu.settings_select.player_setup.player_profile_edit.gameplay.randomizer.xml"])
    screen = "\n".join(files["main_menu.difficulty_select.randomizer.xml"])

    # (Gameplay's Randomizer row opens its screen: the switch and Go Crazy)
    assert 'open="main_menu/settings_select/player_setup/player_profile_edit/gameplay/randomizer/randomizer_settings_screen"' in gameplay
    assert 'setting="game.randomizer"' in settings
    assert "RANDOMIZER:" in settings
    assert 'setting="game.randomizer_crazy"' in settings
    assert 'setting="game.randomizer_crazy"' not in screen
    # (Go Crazy's parts, which the screen shows only with it on)
    assert 'input="gameplay screen update"' in settings
    for setting in (
        "game.randomizer_crazy_weapons",
        "game.randomizer_crazy_enemies",
        "game.randomizer_crazy_flamethrower",
        "game.randomizer_crazy_pods",
    ):
        assert f'setting="{setting}"' in settings
        assert f'setting="{setting}"' not in screen
    assert 'values="10|20|30|40|50|60|70|80|90|100"' in settings
    # (Go Crazy's CUSTOM TAGS is gone: the profile screen before the
    # randomizer's chooses the Tag Injector's tags)
    assert "game.randomizer_crazy_custom_tags" not in settings
    for setting in (
        "game.randomizer_enemies",
        "game.randomizer_allies",
        "game.randomizer_pickups",
        "game.randomizer_enemy_weapons",
        "game.randomizer_pool",
        "game.randomizer_type",
        "game.randomizer_grunt_rank",
        "game.randomizer_elite_rank",
        "game.randomizer_jackal_rank",
        "game.randomizer_hunter_rank",
        "game.randomizer_flood_combat_rank",
        "game.randomizer_pickup_pool",
        "game.randomizer_pickup_type",
        "game.randomizer_weapon_pool",
        "game.randomizer_weapon_type",
    ):
        assert f'setting="{setting}"' in screen
    assert 'values="any|covenant|flood|human|one|tag_injector"' in screen
    assert 'values="any|covenant|human|precision|automatic|power|one|tag_injector"' in screen
    assert 'run="randomizer profile save"' in screen
    # (the code goes back from Load's screen itself: not while DELETE is on)
    assert 'run="randomizer profile load"' in screen and 'run="randomizer profile load" back=' not in screen
    assert 'open="main_menu/difficulty_select/randomizer/profiles_screen"' in screen
    assert 'run="randomizer start"' in screen
    assert 'input="randomizer screen update"' in screen
    assert 'input="randomizer profiles update"' in screen
    assert 'run="randomizer profile delete"' in screen
    assert 'run="randomizer profiles delete mode"' in screen and 'run="randomizer profiles init"' in screen
    assert 'name="main_menu/difficulty_select/randomizer/profiles_button_delete"' in screen
    # (the Tag Injector profile's custom types: rows the code fills)
    for key in ("custom_type", "pickup_custom", "weapon_custom"):
        assert f'name="main_menu/difficulty_select/randomizer/{key}_value"' in screen
    assert 'run="randomizer custom left"' in screen and 'run="randomizer custom right"' in screen
    # (the profile screen before it)
    assert 'name="main_menu/difficulty_select/randomizer/tag_profile_screen"' in screen
    assert 'input="randomizer tag profiles update"' in screen
    assert 'run="randomizer tag profile choose"' in screen
    # every spinner's arrows in the same place
    assert len(set(re.findall(r'footer_bounds="([^"]*)"', screen))) == 1
    for index in range(8):
        assert f'name="main_menu/difficulty_select/randomizer/profile_{index}"' in screen
    for index in range(port_settings.RANDOMIZER_TAG_PROFILE_ROWS):
        assert f'name="main_menu/difficulty_select/randomizer/tag_profile_{index}"' in screen
    source = (ROOT / "port/linux/game/menu_functions.c").read_text()
    assert f"#define RANDOMIZER_TAG_PROFILE_ROWS {port_settings.RANDOMIZER_TAG_PROFILE_ROWS}" in source
    assert '"tag_profile_"' in source and '"tag_profile_%d"' in source


def test_randomizer_functions_are_listed():
    """Each function and input the randomizer's screens name is one
    menu_tags.c lists and menu_functions.c runs."""
    screen = "\n".join(port_settings.settings_files()["main_menu.difficulty_select.randomizer.xml"])
    tags = (ROOT / "port/linux/game/menu_tags.c").read_text()
    functions = (ROOT / "port/linux/game/menu_functions.c").read_text()
    names = set(re.findall(r'(?:run|input)="(randomizer [^"]+)"', screen))
    assert names
    for name in names:
        assert f'"{name}"' in tags, name
        assert f'"{name}"' in functions, name


def test_randomizer_rows_match_the_screen_code():
    """menu_functions.c shows each row of RANDOMIZER_ROWS by its name, in order."""
    source = (ROOT / "port/linux/game/menu_functions.c").read_text()
    names = [f'"op_{key}"' for key, *_ in port_settings.RANDOMIZER_ROWS]
    places = [source.index(name) for name in names]
    assert places == sorted(places)
    # (a saved setup's fields: the rows', and Go Crazy's, once the last, unread)
    assert f"#define RANDOMIZER_KEY_COUNT {len(names) + 1}" in source
    keys = source[source.index("randomizer_keys[RANDOMIZER_KEY_COUNT] ="):]
    keys = re.findall(r'^\t"([^"]*)",', keys[:keys.index("};")], re.M)
    assert len(keys) == len(names) + 1 and keys[15] == ""
    assert sorted(key for key in keys if key) == sorted(setting for _, _, setting, *_ in port_settings.RANDOMIZER_ROWS)
    for key, _, setting, *_ in port_settings.RANDOMIZER_ROWS:
        assert f'"{setting}"' in (ROOT / "port/linux/src/port_config.c").read_text()


def test_crazy_rows_match_the_screen_code():
    """menu_functions.c hides Go Crazy's parts by their rows' names, each a
    setting of port_config.c's."""
    source = (ROOT / "port/linux/game/menu_functions.c").read_text()
    config = (ROOT / "port/linux/src/port_config.c").read_text()
    for _, setting, _, _, _, *named in port_settings.SCREENS["gameplay/randomizer"]["rows"]:
        assert f'"{setting}"' in config
        if setting.startswith("game.randomizer_crazy_"):
            assert f'"op_{named[0] if named else setting.split(".", 1)[1]}"' in source


def test_widget_names_fit():
    """A widget's name is a tag's char[32] (menu_tags.c): a row's, which
    menu_functions.c shows and hides by it, 31 characters at most (a longer
    one is cut short, and never found)."""
    for name, lines in port_settings.settings_files().items():
        for path in re.findall(r'<widget name="([^"]+)"', "\n".join(lines)):
            leaf = path.rsplit("/", 1)[-1]
            if leaf.startswith("op_"):
                assert len(leaf) <= 31, (name, path)
