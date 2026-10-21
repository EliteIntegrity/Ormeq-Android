# Android changes to the game

`game/` is the Windows edition's source at the same version, with the changes
below. Each has a number, which comments in `app/` cite ("change 0003"); a gap
in the numbers is a change the Windows edition has since made itself.

It holds only what the Android build compiles and installs: the Windows
frontends and build scripts, the tests and the Windows edition's bundled
libraries are left out. So of the source files with the Angband notice that
`game/LICENSE` counts, `main-win.c` and `main.c` are not here.

The Android edition adds `game/src/ui_ogg.c` and `game/src/ui_ogg.h` (change
0010) and its own help page, `game/lib/help/battery.txt`.
`game/CMakeLists.txt` is the Windows build's; the Android build reads its
source lists. `game/runtime-files.txt` lists the files the app installs.

## 0001 Build and run where there is no Windows

The compile fixes the NDK needs (`sys/timeb.h`, `strcmpi`, two missing
`<math.h>`, three format strings) and the things a Unix build of the game gets
wrong on Android: `SAFE_SETUID` (Android kills an app that calls `setuid`),
uid-prefixed savefile names (which the start screen hides), temp files in
`/tmp`, a default font that only exists on Windows, and a `%lu` given a 32-bit
value.

Files: `game/src/artifact.c`, `game/src/corny.c`, `game/src/h-system.h`,
`game/src/py_info.c`, `game/src/sdl3_anim.c`, `game/src/sdl3_atlas.c`,
`game/src/sdl3_calc.c`, `game/src/ui_death.c`, `game/src/ui_graveyard.c`,
`game/src/util.c`, `game/src/z-config.h`.

## 0002 Keep the SDL3 frontend whole through Android's lifecycle, and let a host answer taps and keep the player clear of its controls

Flushing input no longer throws away resize, exposure and render-reset events;
the picture is presented again on return from the background; a host hook may
answer clicks that hit no declared menu row; and the map camera works in the
part of the screen a host's controls leave clear (insets; zero on the
desktop).

Files: `game/src/main-sdl3.c`, `game/src/sdl3_local.h`, `game/src/sdl3_map.c`.

## 0003 Say what each prompt is asking for, so touch buttons can follow it

A small stack in `ui_screen.c` that `-more-`, yes/no, direction, aim,
targeting, looking, item, spell, menu, text, amount, shop and home prompts
push while they wait, as do the two waits for Space alone (a thrown weapon
coming back, an ambush). The touch buttons follow it. It is separate from the
screen layers, because a direction prompt does not cover the map.

Files: `game/src/cmd5.c`, `game/src/home.c`, `game/src/menu.c`,
`game/src/message.c`, `game/src/obj_prompt.c`, `game/src/py_throw.c`,
`game/src/shop.c`, `game/src/spells.c`, `game/src/ui_screen.c`,
`game/src/ui_screen.h`, `game/src/util.c`, `game/src/wild.c`,
`game/src/xtra2.c`.

## 0005 Let a host offer what can be done with an item

The filters of the item commands' prompts that are `static` (wield, quaff,
read, eat, activate, study, refill, destroy) get public names, and a hook
tells a host which item is being examined while its description is up. Nothing
changes on the desktop; a right-click item menu there could use the same two
things.

Files: `game/src/cmd3.c`, `game/src/cmd5.c`, `game/src/cmd6.c`,
`game/src/equip.c`, `game/src/obj.c`, `game/src/obj.h`,
`game/src/obj_prompt.c`.

## 0006 Keep the look card clear of a host's controls, and let it word its hints

The card goes in the corner of the map furthest from the pointer, as before,
but inside the part the insets of change 0002 leave clear, its picture
shrinking by up to half to fit. Look's creature card, at the left with its row
of buttons (More opens a second), starts beside the rail and always ends above
the controls along the bottom, however few rows that leaves (its full-screen
form is a tap away); a host may relabel its buttons, which begin with their
keys, and the row is laid out to the host's wording.

Files: `game/src/sdl3_local.h`, `game/src/sdl3_look.c`.

## 0007 Keep the grid clear of a host's strips, and draw the level under them

A host can ask for strips down the left and right of the window (the rail's):
the grid, and so every page and line of text, is fitted to the window less
those strips, while the level and its weather are drawn on under them and out
to the window's edges. Turning the screen over moves the strip to the other
side, and the grid is refitted without a resize. Zero on the desktop, which
changes nothing.

Files: `game/src/main-sdl3.c`, `game/src/sdl3_anim.c`,
`game/src/sdl3_local.h`, `game/src/sdl3_map.c`.

## 0008 Keep the Reckoner's live card clear of a host's controls

With controls along the bottom the live card is always the compact one, lifted
above them, and a tap anywhere on it but its arrows opens Past Reckonings (its
Details button is small on a phone). The small account's hint names the
Working button rather than E.

Files: `game/src/sdl3_calc.c`.

## 0009 Touch footers for documents and Past Reckonings

With `ui_touch` set (android/context.c), the document viewer's footer is Back
and Help without their keys and without Export, as an Android player has no
folder to open the file from; Past Reckonings' footer leaves out what the
cluster gives (Newer, Older, the exact working, Back), so Prev and Next
result, Replay and Help stay short of it on a small phone.

Files: `game/src/ui_reckoner.c`, `game/src/z-doc.c`, `game/src/z-doc.h`.

## 0010 Play the title music and ambience from Ogg Vorbis where a host ships them compressed

With `ORMEQ_AUDIO_OGG` the title music is `title.ogg` and each ambience loop
an `.ogg` beside where its WAV would be, each held in memory as it is (under a
megabyte for a minute of ambience, rather than 11.5 MB of PCM) and decoded a
block at a time as it plays, by SDL_mixer's SDL-adapted stb_vorbis
(`ui_ogg.c`, as Ammerow's port does it). An ambience loop keeps its first
tenth of a second decoded for the desktop's blended seam, and seeks back after
it. The audio thread still neither reads files nor allocates.
`ui_music_audible()` and `ui_ambient_audible()` tell android/sound.c when the
device is needed.

Files: `game/src/ui_ambient.c`, `game/src/ui_ambient.h`,
`game/src/ui_music.c`, `game/src/ui_music.h`, `game/src/ui_ogg.c`,
`game/src/ui_ogg.h`.

## 0011 Link the Android battery page from the help index

One line in `lib/help/start.txt`, under Getting Started, for
`game/lib/help/battery.txt`.

Files: `game/lib/help/start.txt`.

## 0012 Say when a questgiver's card is waiting, and for which answer

`quest_card()` pushes a prompt of its own (change 0003's stack) while it
waits, with the one answer it takes: `a` on an offer, Return on any other
card. The cluster follows it (Accept and Later, or Continue).

Files: `game/src/ui_quest.c`, `game/src/ui_screen.h`.

## 0014 Keep the quest journal's footer short of a touch host's buttons

With `ui_touch` set, as for 0009's footers, the journal's footer is Previous,
Next, Help and Close without their keys, leaving out Open and List, which the
cluster's OK and Back give; at Large on a small phone the desktop's footer ran
under them. Its hints say tap and drag.

Files: `game/src/ui_quest.c`.
