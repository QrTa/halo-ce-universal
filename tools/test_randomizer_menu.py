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
    assert 'values="any|covenant|flood|human|one"' in screen
    assert 'values="any|covenant|human|precision|automatic|power|one"' in screen
    assert 'run="randomizer profile save"' in screen
    assert 'run="randomizer profile load" back="true"' in screen
    assert 'open="main_menu/difficulty_select/randomizer/profiles_screen"' in screen
    assert 'run="randomizer start"' in screen
    assert 'input="randomizer screen update"' in screen
    assert 'input="randomizer profiles update"' in screen
    assert 'run="randomizer profile delete"' in screen
    # every spinner's arrows in the same place
    assert len(set(re.findall(r'footer_bounds="([^"]*)"', screen))) == 1
    for index in range(8):
        assert f'name="main_menu/difficulty_select/randomizer/profile_{index}"' in screen


def test_randomizer_rows_match_the_screen_code():
    """menu_functions.c shows each row of RANDOMIZER_ROWS by its name, in order."""
    source = (ROOT / "port/linux/game/menu_functions.c").read_text()
    names = [f'"op_{key}"' for key, *_ in port_settings.RANDOMIZER_ROWS]
    places = [source.index(name) for name in names]
    assert places == sorted(places)
    assert f"#define RANDOMIZER_KEY_COUNT {len(names)}" in source
    for key, _, setting, *_ in port_settings.RANDOMIZER_ROWS:
        assert f'"{setting}"' in (ROOT / "port/linux/src/port_config.c").read_text()
