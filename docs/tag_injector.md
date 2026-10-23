# The Tag Injector

The Tag Injector takes weapons and AI out of Halo Custom Edition maps and
puts them in profiles, which the campaign randomizer's Go Crazy adds to a
level: a Custom Edition map's carbine lying where a plasma rifle was, its
brutes in the level's encounters. A tag is taken out with every tag it names
(its model, animations, collision, HUD, projectiles, effects, sounds and
bitmaps; an actor variant's actor, unit and weapons), as a tag bundle
(`docs/custom_edition_caches.md`, "Tag bundles").

The game's code is `port/linux/game/tag_injector.c` (the menu's maps and
tags, taking tags out, adding them to a level), `port/linux/src/port_tag_profiles.c`
(the profiles' folder), `port/linux/game/menu_functions.c`'s "port tags"
functions (the menu) and `port/linux/game/randomizer.c` (Go Crazy's
CUSTOM TAGS).

It needs Custom Edition maps to run (`game.custom_edition`, on by default)
and the maps in `custom_maps`, with Custom Edition's `bitmaps.map` and
`sounds.map` (`README.md`, "Custom Edition maps").

## The menu

**Settings > Gameplay > Tag Injector** is laid out as the Mods screen is: a
list on the left, and in the frame on the right what the row chosen is and
what A does with it. Its first list has:

- **TAG INJECTION**: ON lets Go Crazy add the profile's tags
  (`game.tag_injection`). A, left or right turns it on or off.
- **PROFILE**: the profile tags are added to, and Go Crazy plays
  (`game.tag_injection_profile`). Left and right (and A) choose another;
  the frame lists its tags.
- the Custom Edition maps, as the map lists have them (the campaign maps,
  then the multiplayer maps), each with its folder in `custom_maps`.

A on a map loads its tags, as the game would play them, and lists its
**WEAPONS**, **AI**, **EQUIPMENT**, **VEHICLES**, **BIPEDS** and
**SCENERY**, each with how many there are. A on one lists them, by the last
part of their paths (the frame has the whole path, and how many tags it
takes with it). A on a weapon or an actor variant adds it to the profile
(ADDED), or takes it out again. The randomizer can play weapons and AI
alone: the other kinds are listed, and cannot be added yet.

B, or **BACK**, goes back up a list, and from the first leaves.
**NEW PROFILE** asks for a name (letters, digits, spaces, `-` and `_`
make its folder's name) and chooses it. **DELETE PROFILE**, pressed twice,
deletes the profile shown and its tags.

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
```

A profile holds its bundles, not the maps they came from: it is played
without them, and can be copied to another machine whole. A folder without
`profile.toml` is not a profile, nor is one made for a later `format`. A
tag whose bundle is missing, or is not one this build reads, is left out
where the profile is played, and the game's log says why.

Limits: 64 profiles, 256 tags a profile.

## In a level

With Go Crazy's **CUSTOM TAGS** on (Settings > Gameplay > Randomizer,
`game.randomizer_crazy_custom_tags`; dimmed unless the Tag Injector is on
and its profile has a tag), a campaign level the randomizer swaps is given
the profile's tags as it loads, after those the randomizer copies from the
other campaign maps:

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

The weapons of the profile join the weapon pickups' pool and the actors'
weapons' pool, and its actor variants the enemy pool, but a pool of one
type (ONE in the randomizer screen). A bundle whose model cannot be made,
or whose bitmap cannot be drawn, is left out; when the tags would not fit
in the level's table, none are added.

Only a level played alone adds them: a network game's machines must have
the same tags, and the profile is this machine's. A daily challenge, or a
random run, never adds them: they are the same for everyone. A saved game
is loaded with the tags the setup gives the level then, as the randomizer's
are: one saved with a profile's tags expects the same profile.

Next: sharing the host's profile with a network game's players as Custom
Edition maps are shared (`port/linux/NETCODE.md`), so that they need neither
the maps nor the profile; a profile downloaded is listed as a map is, to
take its tags into a profile of one's own.

## The game's log

The game's log says what is taken out and added (`tag injector:`), and why
a map cannot be read, a tag cannot be taken out (a tag it names is of a
group this build cannot bundle), or a bundle is left out.
