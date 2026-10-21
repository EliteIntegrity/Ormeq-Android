# Ormeq for Android 0.1.1

Character creation fixes, with the game at Ormeq 0.1.1 as on Windows:

- Character creation's touch buttons now offer only what each page does. On
  the first page, the type of game, a choice is made by tapping it, and OK did
  nothing there, so the game looked stuck
  ([issue 1](https://github.com/EliteIntegrity/Ormeq-Android/issues/1)). OK
  now appears only where a page goes on or takes the highlighted race or
  class; a page that is a list to choose from has Back as its large button;
  Random and Info appear only where they work. The first page now says to
  tap a type of game.
- From Ormeq 0.1.1: highlights no longer carry from one character creation
  page to the next, Quick Start and the Random patron can be chosen only
  where they are shown, and Return makes its accept sound only where a page
  acts on it.

Gameplay and saves are unchanged. 0.1.1 installs over 0.1.0 and keeps your
characters.

## Ormeq for Android 0.1.0

First Android release.

The game is Ormeq 0.1.0, the same version as the Windows release, whose
[release notes](https://github.com/EliteIntegrity/Ormeq/blob/v0.1.0/RELEASE_NOTES.md)
describe it in full: FrogComposband's character choices and dungeons in a
world shared with Ammerow: Lands Beyond, the quest journal and questgiver
cards, The Reckoner, ASCII, Hybrid and Full Tile maps, and sound effects,
ambience and title music. This edition plays it by touch.

- A rail of screens down the left edge (Menu, Inventory, Equipment, More,
  Quest, Character and Log), a d-pad that walks while held, with Wait at its
  centre, and a cluster of four buttons that offers what can be done now.
- The cluster follows the focus: the answers to the game's question, an item's
  own actions, a targeted monster's attacks, or what can be done on your
  square. Its buttons keep their places; only their labels change.
- An item tapped in Inventory or Equipment offers exactly the commands whose
  own rules accept it. Fire appears for a launcher with ammunition it can
  shoot, and a character with spells and a launcher keeps Fire beside Cast
  while a hostile monster is in view.
- Look cards beside the creature's place on the map, with Next, Prev, Target
  and sideways swipes.
- Quick slots for items, spells and commands, kept with each character, and
  Again.
- Taps travel, aim and choose rows; holding on the map shows a creature's card;
  two fingers zoom the map; Android's Back is Escape.
- The quest journal, questgiver cards (Accept, Later, Continue) and Past
  Reckonings work by touch, with their help worded for it. The game's keyboard
  hints are reworded for touch wherever they are drawn.
- The on-screen keyboard for names, numbers and other text; a hardware
  keyboard works as on Windows.
- Landscape on phones. Tablets and unfolded foldables can be held either way;
  folding and split screen resize the game as it runs. The map runs under the
  rail to the edges of the screen.
- The character is saved whenever Android sends the game to the background,
  including in the middle of a prompt, and a failed save is reported.
- Music and ambience are compressed to keep the APK small. With the ambience
  off, the audio device sleeps between sounds.
- A help page on battery life: what the ambience and the weather cost.

## Platform and installation

Android 8.0 or later, on a 64-bit device (arm64-v8a, or x86_64). Install the
APK as [PLAYING.md](PLAYING.md) describes. Later official releases are signed
with the same key, so they install over this one and keep your characters.

This release was tested on an emulated Android 17 phone. Development builds
were also tested on emulated phones, tablets and foldables, and played on an
Android 9 tablet.

## Known limitations

- Not available from an app store: the APK is installed directly.
- 32-bit-only devices are not supported.
- Characters cannot yet be copied out of the app, to another device or to and
  from the Windows edition, except by Android's own backup and device
  transfer.
- Future releases may change save compatibility, as on Windows.

The project is free to download under its inherited and component licence
terms. Artwork and audio have separate terms; see `game/LICENSE`.
