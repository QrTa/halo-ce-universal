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
                     "port tags back", "port tags new profile", "port tags profile button"}
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


def test_a_weapon_alone_has_a_hud_and_a_stock_tag_can_replace():
    """The HUD row is a weapon's (an AI's weapons are tags of their own), and
    REPLACE ORIGINAL a tag's of a campaign map's path: the "replace" key
    port_tag_profiles.c keeps (a boolean in profile.toml), which
    tag_injector.c plays and randomizer.c's campaign maps decide."""
    functions = (ROOT / "port/linux/game/menu_functions.c").read_text()
    injector = (ROOT / "port/linux/game/tag_injector.c").read_text()
    profiles = (ROOT / "port/linux/src/port_tag_profiles.c").read_text()
    randomizer = (ROOT / "port/linux/game/randomizer.c").read_text()
    rows = functions[functions.index("static short tags_setting_rows(short *rows)"):]
    rows = rows[:rows.index("\n}\n")]
    assert "if (group == 'weap')\n\t\trows[count++] = _tags_setting_hud;" in rows
    assert "if (tags_screen.tag_stock)\n\t\trows[count++] = _tags_setting_replace;" in rows
    assert '"replace"' in functions and '"replace"' in profiles and '"replace"' in injector
    assert "replace = true" in profiles
    assert "int randomizer_stock_tag_exists(long group_tag, char const *name)" in randomizer
    assert "randomizer_stock_tag_exists(" in injector
    # (the weapon's own tag's HUD, whichever bundle adds it)
    assert "port_tag_profiles_tag_find(profile, WEAPON_DEFINITION_TAG," in injector
    # (an AI added brings its weapons and equipment)
    assert "category == _tag_injector_ai" in injector and "EQUIPMENT_GROUP_TAG" in injector
    # (a level's references taken by index while a profile's tags are in it)
    assert "custom_edition_cache_tags_loaded() || tag_injector_tags_loaded()" in \
        (ROOT / "source/tag_files/tag_groups.c").read_text()


def test_the_button_bar_is_the_list_s():
    """The button bar's buttons, which menu_functions.c words and shows by the
    list shown (tags_buttons): none in the first list, NEW PROFILE in TAG
    PROFILES, DUPLICATE PROFILE and DELETE PROFILE in a profile's lists, NEW
    PROFILE and VIEW <name> PROFILE in MAPS' lists, B from the profile it shows
    going back there."""
    screen = "\n".join(port_settings.settings_files()[SCREEN])
    functions = (ROOT / "port/linux/game/menu_functions.c").read_text()
    profiles = (ROOT / "port/linux/src/port_tag_profiles.c").read_text()
    for name in ("button_new", "button_profile"):
        assert f'name="{port_settings.TAG_INJECTOR}/{name}"' in screen
        assert f'"{name}"' in functions
    buttons = functions[functions.index("static short tags_buttons(void)"):]
    buttons = buttons[:buttons.index("\n}\n")]
    assert "case _tags_list_top:\n\t\treturn _tags_buttons_none;" in buttons
    assert "case _tags_list_profiles:\n\t\treturn _tags_buttons_profiles;" in buttons
    assert "case _tags_list_tag:\n\t\treturn _tags_buttons_profile;" in buttons
    assert "default:\n\t\treturn _tags_buttons_maps;" in buttons
    update = functions[functions.index("static void tags_buttons_update(struct widget_instance *list)"):]
    update = update[:update.index("\n}\n")]
    for words in ('L"DUPLICATE PROFILE"', 'L"NEW PROFILE"', 'L"DELETE PROFILE"', '"VIEW %.14s%s PROFILE"'):
        assert words in update, words
    assert "port_tag_profiles_duplicate(original, name)" in functions
    assert "long port_tag_profiles_duplicate(long profile, const char *name)" in profiles
    back = functions[functions.index("static boolean tags_back("):]
    assert "tags_screen.viewed_from != NONE ? tags_screen.viewed_from : _tags_list_profiles" in \
        back[:back.index("\n}\n")]


def test_a_profile_s_tag_can_be_renamed():
    """NAME, every tag's first setting: the "name" key port_tag_profiles.c
    keeps as it was typed, which the menus show the tag by and the
    randomizer's AUTO reads words from; a protected map's tags are given
    names (tag_names.c) as the map opens and loads again."""
    functions = (ROOT / "port/linux/game/menu_functions.c").read_text()
    profiles = (ROOT / "port/linux/src/port_tag_profiles.c").read_text()
    injector = (ROOT / "port/linux/game/tag_injector.c").read_text()
    randomizer = (ROOT / "port/linux/game/randomizer.c").read_text()
    rows = functions[functions.index("static short tags_setting_rows(short *rows)"):]
    rows = rows[:rows.index("\n}\n")]
    assert rows.index("_tags_setting_name;") < rows.index("_tags_setting_enabled;")
    assert '"faction", "type", "class", "custom_type", "hud", "replace", "name",\n\t"failed" }' in profiles
    assert '!strcmp(tag_option_keys[option], "name")' in profiles
    assert 'port_tag_profiles_tag_set_option(tags_screen.profile, tags_screen.tag, "name", name)' in functions
    assert 'port_tag_profiles_tag_option(profile, tag, "name")' in injector
    assert "tag_injector_tag_display_name(custom_profile" in randomizer
    assert injector.count("menu_names_restore(header);") == 2


def test_a_tag_s_model_is_previewed_under_its_details():
    """tag_preview, under the details, is the widget ui_widget.c draws the
    preview's picture over (tag_preview.c), of the shape the picture is
    drawn in; the menu chooses its tag in a map's tags and a profile's, and
    lets it go as the screen does."""
    screen = "\n".join(port_settings.settings_files()[SCREEN])
    header = (ROOT / "port/linux/game/tag_preview.h").read_text()
    widgets = (ROOT / "source/interface/ui_widget.c").read_text()
    functions = (ROOT / "port/linux/game/menu_functions.c").read_text()
    left, top, width, height = port_settings.TAG_PREVIEW_BOUNDS
    assert (f'name="{port_settings.TAG_INJECTOR}/tag_preview" controller="1" left="{left}" top="{top}" '
            f'width="{width}" height="{height}" bitmap="blank"') in screen
    assert f'<child widget="{port_settings.TAG_INJECTOR}/tag_preview"/>' in screen
    assert f"#define TAG_PREVIEW_WIDTH {width}\n" in header
    assert f"#define TAG_PREVIEW_HEIGHT {height}\n" in header
    assert '!strcmp(widget->name, "tag_preview")' in widgets
    # (within the frame, below the details' lines above it, and above the
    # lines of what the tag needs)
    lines = int(re.search(r"#define TAG_PREVIEW_DETAILS_LINES (\d+)", functions).group(1))
    assert left == 412 and 130 + lines * 272 / 13 <= top
    needs_left, needs_top, needs_width, needs_height = port_settings.TAG_NEEDS_BOUNDS
    needs_lines = int(re.search(r"#define TAG_NEEDS_LINES (\d+)", functions).group(1))
    assert needs_left == left and needs_width == width and top + height <= needs_top
    assert needs_top + needs_lines * 272 / 13 <= 130 + 272 + 1
    assert f'name="{port_settings.TAG_INJECTOR}/tag_needs" type="text"' in screen
    assert f'<child widget="{port_settings.TAG_INJECTOR}/tag_needs"/>' in screen
    assert 'named(details, "tag_needs", 0)' in functions
    update = functions[functions.index("static void tags_update(struct widget_instance *list)"):]
    update = update[:update.index("\n}\n")]
    assert "tag_preview_map_tag(tags_screen.category, *chosen);" in update
    assert "tag_preview_profile_tag(tags_screen.profile, tags_profile_tag_of(*chosen));" in update
    assert "tag_preview_none();" in update
    assert "tag_injector_map_close();\n\ttag_preview_dispose();" in functions


def test_a_tag_that_failed_to_load_is_turned_off_and_said():
    """A bundle a level could not play is turned off with its "failed"
    setting saying why (turning it on clears it), the menu says so, and the
    level's HUD says which, once it has played a moment (game.c's tick)."""
    injector = (ROOT / "port/linux/game/tag_injector.c").read_text()
    profiles = (ROOT / "port/linux/src/port_tag_profiles.c").read_text()
    functions = (ROOT / "port/linux/game/menu_functions.c").read_text()
    game = (ROOT / "source/game/game.c").read_text()
    read = injector[injector.index("long tag_injector_tags_read("):]
    read = read[:read.index("\n}\n")]
    assert 'port_tag_profiles_tag_set_option(profile, bundle->profile_tag, "failed",' in read
    assert "port_tag_profiles_tag_set_enabled(profile, bundle->profile_tag, FALSE);" in read
    assert "level_notice_failures(profile, failed, failed_count, skipped);" in read
    # (every reason a bundle is refused for is one the menu describes)
    table = injector[injector.index("} const failures[] ="):]
    values = set(re.findall(r'\{ "([a-z ]+)", "', table[:table.index("};")]))
    assigned = re.findall(r'bundle->failure = ([^;]+);', injector)
    assert len(assigned) >= 7
    for value in re.findall(r'"([a-z ]+)"', " ".join(assigned)):
        assert value in values, value
    assert '"failed",\n\t\t\tbundle->failure ? bundle->failure : "unreadable");' in read
    assert 'found->options[tag_option_index("failed")][0] = 0;' in profiles
    assert "tag_injector_failure_describe(failed)" in functions
    assert '"FAILED  %s"' in functions
    assert "tag_injector_tick();" in game
    assert "hud_print_message(local_player_index, text);" in injector


def test_the_frame_says_what_a_tag_requires_and_what_is_missing():
    """A map's tag's details are how many tags it requires (no path, no word
    of A), a profile's tag's the map it came from (no path); under the
    preview, what is missing, else that nothing is; what failed is yellow,
    up to TAG PROFILES; and the preview's model is drawn with the object's
    change colors (an actor variant's)."""
    functions = (ROOT / "port/linux/game/menu_functions.c").read_text()
    injector = (ROOT / "port/linux/game/tag_injector.c").read_text()
    preview = (ROOT / "port/linux/game/tag_preview.c").read_text()
    assert "tags with what it names" not in functions
    assert '"%ld tags required.%s"' in functions
    assert "A %s the profile %s" not in functions
    assert '"No data missing."' in functions and '"MISSING: "' in functions
    assert "Has all it needs" not in functions
    assert "tags_screen.row_failed[row_index] = tags_row_failed(place);" in functions
    assert "!tags_screen.row_failed[tag_row_index]" in functions
    row_failed = functions[functions.index("static boolean tags_row_failed(short place)"):]
    row_failed = row_failed[:row_failed.index("\n}\n")]
    for case in ("_tags_list_top", "_tags_list_profiles", "_tags_list_profile_categories", "_tags_list_profile"):
        assert f"case {case}:" in row_failed, case
    assert "preview_change_colors(source, root, group_tag, &build);" in injector
    assert "SHADER_MODEL_CHANGE_COLOR_SOURCE_OFFSET = 0x4C" in injector
    assert "SHADER_MODEL_MULTIPURPOSE_MAP_OFFSET = 0xBC" in injector
    assert "base[channel] *= 1.0f - amount + amount * material->tint[channel];" in preview
    # (the notice waits for the level to be played: past a cutscene)
    tick = injector[injector.index("void tag_injector_tick("):]
    tick = tick[:tick.index("\n}\n")]
    assert "cinematic_in_progress()" in tick and "unit_index == NONE" in tick
