# Controller button art

The controller buttons the port draws are high-resolution textures drawn in
place of the game's own 32x32 bitmaps. This folder holds their sources, the
PNGs made from them, and `buttons.json`, which says what each PNG stands for.
This guide covers changing them, adding a design, and giving a port
(the Switch, say) designs of its own.

## What is drawn where

| Family | Seen | PNGs | Stands for | Drawn by |
| --- | --- | --- | --- | --- |
| Menu keys | Beside the menus' key labels (A SELECT, B BACK), and smaller in dialogs, the pause menu and the help screens | `a_butn__0` … `y_butn__0`, `a_butn_sm__0`, `b_butn_sm__0`, `black_butn__0`, `white_butn__0` | The map's `ui\shell\bitmaps\*_butn` bitmaps | The texture cache, in place of the bitmap (`port/linux/src/hud_hires.c`) |
| Message icons | Inside text: "Press (A) to join" | `hud_msg_icons_sm__0`: white stencils of each oval with its letter cut out, which the game tints | The A, B, X and Y sprites of the sheet `ui\hud\bitmaps\hud_msg_icons_sm` | A placeholder for those sprites (`port/linux/game/hud_hires_tags.c`) |
| Gameplay prompts | The HUD's button prompts in a game, with `display.high_res_hud` on | `classic_*__0`, `gem_*__0`, and for Dark Gems the menu keys above | Bitmaps of the port's own, `ui\hud\buttons\*` (no map has them) | `hud_hires_button_draw` (`hud_hires_tags.c`), in the style `display.hud_button_style` names |
| Sticks and triggers | Gameplay prompts for them, in every style | `trigger_*__0`, `thumbstick*_*__0` | `ui\hud\buttons\*` | The same |

The menu keys and message icons count as the menus' art: `display.high_res_text`
turns them on and off. The gameplay prompts are the HUD's: `display.high_res_hud`.

## The pipeline

```
svg/*.svg, source/*.png ──tools/button_assets.py build──▶ *.png (committed)
                                                        buttons.json (committed)
          *.png + buttons.json ──tools/embed_assets.py──▶ hud_hires_assets.c (each build)
                                                        hud_hires_embedded[] in the game
```

- `svg/` has hand-drawn redraws of the Xbox buttons, one per letter, in the
  texels of a 32x32 icon (`viewBox="0 0 32 32"`). Each has a shape with
  `id="oval"` and one with `id="letter"`, and Y also has `id="shadow"`. The
  message icons' stencils are cut from the oval and letter, and the classic
  style fills them with one colour.
- `source/` has art supplied as PNGs: the gems, triggers and sticks. They
  are scaled into the bitmap's cell, keeping their proportions.
- `tools/button_assets.py` has three commands:
  - `layout --map ui.map` reads each map bitmap's size, format and pixel
    CRC, fits each redraw on it, and writes `buttons.json`.
  - `build` renders the PNGs from `buttons.json`.
  - `check --map ui.map --out DIR` writes each PNG, reduced to its bitmap's
    size, beside the map's bitmap.

  It needs `rsvg-convert`, Pillow, NumPy and SciPy.
- `tools/embed_assets.py` turns every PNG listed in `buttons.json` (and the
  HUD's and titles' lists) into C data at build time. Every port's build
  calls it (`hud_assets_build`), so the PNGs are the only thing committed.

### `buttons.json`

One entry per PNG:

| Key | Meaning |
| --- | --- |
| `name` | The PNG, without `.png` |
| `tag`, `bitmap` | The bitmap it stands for: a map's tag, or `ui\hud\buttons\…` for the port's own |
| `width`, `height`, `format` | That bitmap's size and format |
| `scale` | The PNG is this many times the bitmap's size: 8 |
| `crc` | The CRC-32 of the map bitmap's pixels. The game draws the PNG only over a bitmap with exactly these pixels: another language's or a modified map's are drawn as they are (and logged). `0` for the port's own bitmaps |
| `svg` / `source_png` | What it is rendered from |
| `place`, `letter` | Where `layout` put the redraw on the bitmap, and where it moved the letter to sit as the map's does |
| `classic` | The classic style's fill colour |
| `sprites` | For the message icon sheet: each sprite's sequence and place |
| `control_cell` | For triggers and sticks: the cell the art is fitted into |

## Change an existing button's look

1. Edit its SVG in `svg/`, or replace its PNG in `source/`. Keep:
   - the 32x32 viewBox, drawing where the old one did (the menus draw the
     bitmap from its top left, one texel to a pixel, so art elsewhere is cut
     off or misplaced);
   - the `oval` and `letter` ids (and `shadow` on Y), which the stencils and
     the classic style are cut from.
2. `python tools/button_assets.py build`.
3. `python tools/button_assets.py check --map <data>/maps/ui.map --out /tmp/buttons`,
   and look at the pairs: the new art should cover what the old did.
4. Build a port and look at each place in "What is drawn where": a menu
   (A SELECT), the pause menu (the small A and B), a "Press A" message, and
   the HUD prompts in each `display.hud_button_style`.
5. Commit the SVG or source and the PNGs together.

Run `layout` again only for a new map bitmap or a new kind of entry: it
rewrites `buttons.json` from the map.

## Add a gameplay button style

The gameplay prompts are the one family the player chooses between.

1. Add the art: a list like `GEM_BUTTONS` in `tools/button_assets.py`, with
   tags `ui\hud\buttons\<style>_a` … (the port's own, `crc` 0), and
   `build`. Black and White can reuse an existing style's, as gems do.
2. Name the tags in `hud_hires_button_draw` (`port/linux/game/hud_hires_tags.c`):
   a `<style>_names[]` beside `classic_names[]` and `gem_names[]`, and a
   number for the style where `display.hud_button_style` is read.
3. Add the value to `display.hud_button_style` (its help text in
   `port/linux/src/port_config.c`, its row in `port/linux/README.md`) and to
   the BUTTON STYLE spinner in `tools/port_settings.py`. Then regenerate the
   Graphics screen (`settings_files()`) into `port/assets/menus/ce/`; the
   test `tools/test_web_build.py` checks it matches.

## Art for one port only

> **Not built yet.** Today every port embeds the same PNGs. This is how it
> should work; the steps marked *(to build)* are the work to do first.

A port's art is a folder of overrides beside the common art:

```
port/assets/buttons/
    svg/a_butn.svg            common sources
    a_butn__0.png             common PNGs
    platforms/switch/
        svg/a_butn.svg        the Switch's sources, where it differs
        a_butn__0.png         the Switch's PNGs, the same names
```

- **Same entries, other pixels.** A port's PNG stands for the same entry in
  `buttons.json`, with the same tag, size and CRC. Only the pixels differ.
  So the game's code is the same for every port, and nothing needs
  `#ifdef`. A name the port has no PNG for uses the common one.
- **Rendering.** *(to build)* `tools/button_assets.py build --platform switch`
  renders each entry from `platforms/switch/svg/<svg>` (or
  `platforms/switch/source/<png>`) when that file exists, into
  `platforms/switch/<name>.png`. It skips entries the port does not override.
- **Embedding.** *(to build)* `hud_assets_build(n, prefix, output)` already
  knows the port (`"switch"`, `"android"`, `"linux"`, `"windows"`). It
  should pass it to the rule (`embed_assets.py --platform switch $out`), and
  `textures()` should read `platforms/<platform>/<name>.png` where it exists.
  `hud_asset_inputs()` should list those files too, so that changing one
  rebuilds the port. `tools/switch_build.py` already passes `"switch"`.
- **Defaults.** Usually a port overrides the classic style's PNGs rather
  than adding a style, so its prompts look its own by default. If it does
  add a style and wants it as the default, the config table
  (`port/linux/src/port_config.c`) gives the Switch a setting of its own
  with an entry under `#ifdef HALO_SWITCH` (the Switch counts as
  `_platform_android` in the table's platform bits), as `display.vk_driver`
  is kept off it with `#ifndef HALO_SWITCH`.

### The Switch

- **What the letters mean.** The game's A, B, X and Y are SDL's controller
  buttons. On the Switch, SDL names a button by its label, so the game's "A"
  is the button marked A, on the right. If that holds, Switch art keeps the
  same letters and changes only the look: round, flat faces in the console's
  colours. Confirm it on the console (press the button marked A on a menu:
  it should select) before drawing anything that depends on it.
- **Black and White** have no Switch button. Their prompts (Xbox controller
  only) can keep the common art, or be drawn as the buttons the port puts
  those actions on.
- **To override:** `a_butn`, `b_butn`, `x_butn`, `y_butn` (and their `_sm`),
  the message icon sheet (from the same SVGs' `oval` and `letter`), and
  `classic_a` … `classic_y` for the HUD. The gems, Dark Gems, sticks and
  triggers can keep the common art.
