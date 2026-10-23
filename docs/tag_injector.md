# The Tag Injector

The Tag Injector takes weapons and AI out of Halo Custom Edition maps and
puts them in profiles, which the campaign randomizer adds to a level: a
Custom Edition map's carbine lying where a plasma rifle was, its brutes in
the level's encounters. A tag is taken out with every tag it names (its
model, animations, collision, HUD, projectiles, effects, sounds and bitmaps;
an actor variant's actor, unit and weapons), as a tag bundle
(`docs/custom_edition_caches.md`, "Tag bundles").

The game's code is `port/linux/game/tag_injector.c` (the menu's maps and
tags, taking tags out, adding them to a level), `port/linux/src/port_tag_profiles.c`
(the profiles' folder), `port/linux/game/menu_functions.c`'s "port tags"
functions (the menu) and "randomizer tag profile" ones (the screen before
the randomizer's), and `port/linux/game/randomizer.c` (the pools).

It needs Custom Edition maps to run (`game.custom_edition`, on by default)
and the maps in `custom_maps`, with Custom Edition's `bitmaps.map` and
`sounds.map` (`README.md`, "Custom Edition maps").

## The menu

**Settings > Gameplay > Tag Injector** is laid out as the Mods screen is: a
list on the left, and in the frame on the right what the row chosen is and
what A does with it. B, or **BACK**, goes back up a list, and from the
first leaves. Its first list has:

- **TAG INJECTION**: ON lets the randomizer add a profile's tags
  (`game.tag_injection`). A, left or right turns it on or off.
- **TAG PROFILES**: the profiles, each with the size of its tags' bundles
  (EMPTY without any).
- **MAPS**: the Custom Edition maps.

**TAG PROFILES**: A on a profile lists its tags, each with where it came
from (the frame) and its size, ON or OFF, or MISSING when its bundle is not
there. Left or right turns a tag on or off: one off stays in the profile and
is left out where the profile is played, to try the profile without it. A on
a tag opens its settings:

- **ENABLED**: on or off, as left and right on the tag.
- **FACTION**: the pools of the randomizer it joins. An actor variant's:
  COVENANT, FLOOD, HUMAN, or ANY POOL ONLY; a weapon's: COVENANT or HUMAN.
- **COUNTS AS** (an actor variant's): the campaign's type it is among for
  ONE TYPE (as ELITE, it is one of the elites a pool of elites plays), or
  NONE.
- **CLASS** (a weapon's): PRECISION, AUTOMATIC, POWER or NONE, the weapon
  pools of those names.
- **CUSTOM TYPE**: the name the randomizer's ONE TYPE's TAG INJECTOR chooses
  it by. A types it; nothing typed is the last part of its path. Tags of
  the same custom type (BRUTE for a brute minor, a brute major and a brute
  captain) are played together.
- **REMOVE TAG**: A, twice, takes it out of the profile and deletes its
  bundle.

AUTO, each setting's first, finds it by the tag's name as the randomizer
finds the campaign's: an actor variant's faction and type by its unit's
(a grunt, elite, jackal, hunter or engineer is the Covenant's, a flood form
the Flood's, a marine human; a brute is none of them, and is played by ANY
and TAG INJECTOR alone until its faction is set), a weapon's by the
campaign's weapon of its path, else by its name's words (plasma, needle,
carbine: the Covenant's; sniper, pistol: precision; rocket, shotgun,
sword: power; rifle: automatic).

**MAPS** has **ADD TO**, the profile a map's tags are added to (the one
opened last in TAG PROFILES, or made; left and right choose another), and
**CUSTOM SINGLEPLAYER** and **CUSTOM MULTIPLAYER**, as the map lists have
them: A on one lists its folders of `custom_maps` (each with how many maps
it has) and the maps beside them, and A on a folder lists its maps. A on a
map loads its tags, as the game would play them, and lists its
**WEAPONS**, **AI**, **EQUIPMENT**, **VEHICLES**, **BIPEDS** and
**SCENERY**, each with how many there are. A on one lists them, by the last
part of their paths (the frame has the whole path, and how many tags it
takes with it). A on a weapon or an actor variant adds it to the profile
(ADDED), or takes it out again. The randomizer can play weapons and AI
alone: the other kinds are listed, and cannot be added yet.

**NEW PROFILE** asks for a name (letters, digits, spaces, `-` and `_`
make its folder's name) and chooses it. **DELETE PROFILE**, pressed twice,
deletes the profile chosen in TAG PROFILES, or the one whose tags are
listed, and its tags.

Adding a tag loads its map again, with the sounds of the tags it takes
decoded: a second or two, and longer for an actor variant of many voices
(the brute major carbine of `firefight-fissurefall` takes 515 tags, 43 MB).

## Profiles

The profiles are in the `tag_injector` folder of the data root (the folder
with `maps/`; on Android the app's data folder, on the web the browser's
storage for the site). Each profile is a folder, with a `profile.toml` and
its tags' bundles beside it:

```
tag_injector/
  Brutes/
    profile.toml
    carbine.weap.tagbundle
    brute major carbine.actv.tagbundle
```

```toml
name = "Brutes"
format = 1

[[tags]]
map = "firefight-fissurefall"
group = "weap"
tag = "cmt\\weapons\\covenant\\carbine\\carbine"
file = "carbine.weap.tagbundle"
enabled = true
faction = "covenant"
class = "automatic"
custom_type = "Carbine"
```

A tag's `enabled`, `faction`, `type` (an actor variant's), `class` (a
weapon's) and `custom_type` are its settings in the menu; one left out is
its default (on, AUTO, and the last part of its path). The values are the
randomizer's names: `faction` `"covenant"`, `"flood"`, `"human"` or
`"none"`; `type` one of `game.randomizer_type`'s (`"elite"`) or `"none"`;
`class` `"precision"`, `"automatic"`, `"power"` or `"none"`.

A profile holds its bundles, not the maps they came from: it is played
without them, and can be copied to another machine whole. A folder without
`profile.toml` is not a profile, nor is one made for a later `format`. A
tag whose bundle is missing, or is not one this build reads, is left out
where the profile is played, and the game's log says why.

Limits: 64 profiles, 256 tags a profile.

## In a level

With **TAG INJECTION** on, the difficulty of a campaign level the randomizer
plays opens, before the randomizer's screen, **TAG INJECTOR PROFILE**: NONE
and each profile, A one to play it (`game.randomizer_tag_profile`), then the
randomizer's screen, whose CANCEL comes back to it. It is shown only with
more than one profile: one alone is played without it, and none plays none.
The randomizer's screen has the profile's choices only while it has tags on
of their kind:

- **ENEMY POOL**'s **TAG INJECTOR**: the profile's AI alone.
- **ENEMY TYPE**'s **TAG INJECTOR** (with ONE TYPE), and below it **CUSTOM
  TYPE**, which left and right choose among the custom types of the
  profile's AI: that one alone (`game.randomizer_custom_type`).
- **PICKUP POOL**'s and **WEAPON POOL**'s **TAG INJECTOR**: the profile's
  weapons alone; **PICKUP TYPE**'s and **WEAPON TYPE**'s **TAG INJECTOR**,
  with their CUSTOM TYPE: one custom type of them
  (`game.randomizer_pickup_custom`, `game.randomizer_weapon_custom`).

The profile's tags on join the other pools too, as their settings say: ANY
all of them, COVENANT, FLOOD and HUMAN those of their faction, PRECISION,
AUTOMATIC and POWER the weapons of their class, and ONE TYPE's campaign
types the AI that count as them (and are of the rank chosen, by their
names). A setup saved (SAVE) keeps the custom types.

A campaign level the randomizer swaps is given the profile's tags on as it
loads, after those the randomizer copies from the other campaign maps (none
for a pool of the profile's alone):

- each tag of a bundle that the level has not got (a tag of its group and
  path) is added to the level's tags, in the profile's order; one the level
  has is the level's own, and so is one an earlier bundle added;
- each reference and tag index of a tag added names the level's tag;
- a gbxmodel is made this build's model, as a Custom Edition map's are
  (compressed vertices, in buffers of its own);
- a bitmap's pixels and a sound's samples are read from the bundle's file as
  the game wants them, the pixels laid out for the texture cache as a Custom
  Edition map's are, with their channels where Halo PC keeps them (HUD
  meters, multipurpose maps). A bundle's tags alone are kept in memory, 64
  MB of them at most, from 64 bundles at most.

A bundle whose model cannot be made, or whose bitmap cannot be drawn, is
left out; when the tags would not fit in the level's table, none are added.

Only a level played alone adds them: a network game's machines must have
the same tags, and the profile is this machine's (Server Setup's randomizer
has none of the TAG INJECTOR choices, and a setting of one is taken as ANY).
A daily challenge, or a random run, never adds them: they are the same for
everyone. A saved game is loaded with the tags the setup gives the level
then, as the randomizer's are: one saved with a profile's tags expects the
same profile.

Next: sharing the host's profile with a network game's players as Custom
Edition maps are shared (`port/linux/NETCODE.md`), so that they need neither
the maps nor the profile; a profile downloaded is listed as a map is, to
take its tags into a profile of one's own.

## The game's log

The game's log says what is taken out and added (`tag injector:`), and why
a map cannot be read, a tag cannot be taken out (a tag it names is of a
group this build cannot bundle), or a bundle is left out.
