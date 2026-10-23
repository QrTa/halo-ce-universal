"""The Tag Injector's menu (Settings > Gameplay > Tag Injector), as
tools/port_settings.py writes it, and the code that runs it
(port/linux/game/menu_functions.c, menu_tags.c, tag_injector.c)."""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))

import port_settings  # noqa: E402

SCREEN = "main_menu.settings_select.player_setup.player_profile_edit.gameplay.tag_injector.xml"


def test_gameplay_opens_the_tag_injector():
    files = port_settings.settings_files()
    gameplay = "\n".join(files["main_menu.settings_select.player_setup.player_profile_edit.gameplay.xml"])
    assert f'open="{port_settings.TAG_INJECTOR}/tag_injector_screen"' in gameplay
    assert "TAG INJECTOR" in gameplay
    # (its file is one the game embeds)
    assert f'"ce/{SCREEN}"' in (ROOT / "port/assets/menus/menus.json").read_text()


def test_the_screen_runs_the_tag_injector_functions():
    """Each function and input the screen names is one menu_tags.c lists and
    menu_functions.c runs."""
    screen = "\n".join(port_settings.settings_files()[SCREEN])
    tags = (ROOT / "port/linux/game/menu_tags.c").read_text()
    functions = (ROOT / "port/linux/game/menu_functions.c").read_text()
    names = set(re.findall(r'run="(port tags [^"]+)"', screen))
    inputs = set(re.findall(r'input="(port tags [^"]+)"', screen))
    assert names == {"port tags init", "port tags close", "port tags choose", "port tags left", "port tags right",
                     "port tags back", "port tags new profile", "port tags delete profile"}
    assert inputs == {"port tags update"}
    for name in names | inputs:
        assert f'"{name}"' in tags, name
        assert f'"{name}"' in functions, name
    # (B goes up a list, rather than away at once)
    assert '<on event="b" run="port tags back"/>' in screen
    assert 'back="true"' not in screen


def test_the_rows_and_texts_are_those_the_code_finds():
    """menu_functions.c finds the rows by op_tag_<n> and the texts by their
    names, which share the Mods screen's measures."""
    screen = "\n".join(port_settings.settings_files()[SCREEN])
    functions = (ROOT / "port/linux/game/menu_functions.c").read_text()
    for index in range(port_settings.TAG_INJECTOR_ROWS):
        assert f'name="{port_settings.TAG_INJECTOR}/op_tag_{index + 1}"' in screen
    assert '"op_tag_"' in functions
    assert "#define TAG_ROWS MOD_ROWS" in functions and port_settings.TAG_INJECTOR_ROWS == port_settings.MOD_ROWS
    for name in ("tag_details_name", "tag_details_text", "tag_name", "tag_state", "tags_list"):
        assert f'name="{port_settings.TAG_INJECTOR}/{name}"' in screen
        assert f'"{name}"' in functions, name


def test_the_settings_are_port_config_s():
    config = (ROOT / "port/linux/src/port_config.c").read_text()
    for setting in ("game.tag_injection", "game.tag_injection_profile", "game.randomizer_tag_profile",
                    "game.randomizer_custom_type", "game.randomizer_pickup_custom", "game.randomizer_weapon_custom"):
        assert f'"{setting}"' in config
    assert '"game.randomizer_crazy_custom_tags"' not in config
    injector = (ROOT / "port/linux/game/tag_injector.c").read_text()
    assert '"game.tag_injection"' in injector and '"game.randomizer_tag_profile"' in injector
    assert '"game.tag_injection_profile"' in (ROOT / "port/linux/game/menu_functions.c").read_text()
    assert '"game.tag_injection_textures"' in config and '"game.tag_injection_textures"' in injector
    randomizer = (ROOT / "port/linux/game/randomizer.c").read_text()
    for setting in ("game.randomizer_custom_type", "game.randomizer_pickup_custom", "game.randomizer_weapon_custom"):
        assert f'"{setting}"' in randomizer


def test_a_tag_s_settings_are_the_randomizer_s():
    """The values the menu gives a tag's faction, type and class are those
    randomizer.c reads, and the keys port_tag_profiles.c keeps."""
    functions = (ROOT / "port/linux/game/menu_functions.c").read_text()
    randomizer = (ROOT / "port/linux/game/randomizer.c").read_text()
    profiles = (ROOT / "port/linux/src/port_tag_profiles.c").read_text()
    for key in ("faction", "type", "class", "custom_type"):
        assert f'"{key}"' in profiles and f'"{key}"' in functions
    for name in ("tags_actor_types",):
        table = functions[functions.index(f"{name}[] ="):]
        values = re.findall(r'\{ "([a-z_]+)", "', table[:table.index("};")])
        for value in values:
            if value != "none":
                assert f'{{ "{value}", TYPE_' in randomizer, value
    for value in ("precision", "automatic", "power"):
        assert f'"{value}"' in randomizer


def test_a_tag_s_huds_are_the_injector_s():
    """The stock HUDs the menu's HUD row chooses are those tag_injector.c
    gives a weapon (OWN and AUTO aside), and the key port_tag_profiles.c
    keeps; each is a campaign map's weapon HUD."""
    functions = (ROOT / "port/linux/game/menu_functions.c").read_text()
    injector = (ROOT / "port/linux/game/tag_injector.c").read_text()
    profiles = (ROOT / "port/linux/src/port_tag_profiles.c").read_text()
    assert '"hud"' in profiles and '"hud"' in functions
    table = functions[functions.index("tags_weapon_huds[] ="):]
    shown = re.findall(r'\{ "([a-z_]*)", "', table[:table.index("};")])
    stock = injector[injector.index("} const stock_huds[] ="):]
    rows = re.findall(r'\{ "([a-z_]+)", "([^"]+)", "([a-z0-9]+)" \}', stock[:stock.index("};")])
    assert shown[:2] == ["", "auto"]
    assert shown[2:] == [value for value, _, _ in rows]
    for value, tag, level in rows:
        assert tag.startswith("weapons\\\\"), tag
        assert level in ("a10", "a30", "a50", "b30", "b40", "c10", "c20", "c40", "d20", "d40"), level
    words = injector[injector.index("} const stock_hud_words[] ="):]
    for value in re.findall(r'\{ "[a-z ]+", "([a-z_]+)" \}', words[:words.index("};")]):
        assert value in [row[0] for row in rows], value
