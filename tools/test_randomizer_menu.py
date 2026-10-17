"""The campaign randomizer's menus, as tools/port_settings.py writes them."""
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
    screen = "\n".join(files["main_menu.difficulty_select.randomizer.xml"])

    assert 'setting="game.randomizer"' in gameplay
    assert "RANDOMIZER:" in gameplay
    for setting in (
        "game.randomizer_enemies",
        "game.randomizer_allies",
        "game.randomizer_pickups",
        "game.randomizer_enemy_weapons",
        "game.randomizer_pool",
        "game.randomizer_type",
    ):
        assert f'setting="{setting}"' in screen
    assert 'values="any|covenant|flood|human|one"' in screen
    assert 'run="randomizer profile save"' in screen
    assert 'run="randomizer profile load"' in screen
    assert 'run="randomizer start"' in screen
    assert 'input="randomizer profiles update"' in screen
    for index in range(8):
        assert f'name="main_menu/difficulty_select/randomizer/profile_{index}"' in screen
