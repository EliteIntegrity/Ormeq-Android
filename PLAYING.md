# Playing Ormeq on Android

## Installing

Ormeq for Android needs Android 8.0 or later on a 64-bit device (arm64-v8a,
or x86_64 for emulators). It installs from its APK:

1. On the phone or tablet, download `Ormeq-0.1.0-android.apk` from the
   [0.1.0 release](https://github.com/EliteIntegrity/Ormeq-Android/releases/tag/v0.1.0).
2. Open the download. If Android asks, allow your browser or file manager to
   install apps, then choose **Install**.
3. Open **Ormeq** and choose **New character**.

The first start copies the game's files into its own storage, which takes a
moment; so does the first start after an update. A newer official APK
installs over this one and keeps your characters. Uninstalling Ormeq deletes
them.

A phone plays in landscape. A tablet or an unfolded foldable can be held
either way, and folding, unfolding and split screen resize the game as it
runs. The character is saved whenever Android sends the game to the
background, so a call or the home button loses nothing.

## The controls

Everything on screen presses the game's own keys, and a hardware keyboard
works as it does on Windows: `?` for help, Escape for the pause menu.
Without a keyboard, help is in **More**, under Game.

- **The rail**, down the left edge (in the camera cut-out strip where there is
  one): **Menu** (the pause menu and Settings), **Inv**, **Equip**, **More** (a
  drawer of every command that has something to act on), **Quest** (the quest
  journal), **Char** and **Log** (messages and Past Reckonings). Each closes
  whatever screen is open first, and pressed again on its own screen it closes
  that. The full map screen is in More, under Knowledge.
- **The d-pad**, bottom left: tap to step, hold to walk. Walking stops when you
  lift your finger, the game asks something or the way is blocked; walking
  into a monster attacks it. Its centre is **Wait**: tap to wait a turn, hold to
  keep waiting until a monster comes into view or you are hurt.
- **The cluster**, bottom right: one large button and three around it, offering
  what can be done now. Where you stand, the large button is Get, Down, Up,
  Enter, Open or Rest, with your ranged attack (Cast, Fire or Throw), **Look**
  and Rest around it. When the game asks a question, it offers the likely
  answer, the other answers and **Back**, which is always left of the large
  button. Buttons keep their places; only their labels change.
- **Items**: tap one in Inventory or Equipment and the cluster offers its own
  actions (Wear, Quaff, Read, Take off and so on), with **More…** for the rest
  and **Add to bar**. A wielded bow or sling offers Fire while you have
  ammunition it can shoot.
- **Monsters**: tap one on the map to make it the target. The cluster then
  offers Fire, Cast, Throw and Walk to. A character with spells and a launcher
  keeps Fire beside Cast while a hostile monster is in view.
- **Quick slots and Again**, bottom middle: up to three rows of four slots (two
  rows on a phone; the button at their left switches rows). A slot holds an
  item's action (from its More…), a spell (hold it in a spell list) or a
  command (hold it in the More drawer). Hold a slot to empty it. Slots are kept
  with the character. **Again** repeats the last command.
- **Taps** on the map travel there. Taps also choose rows, follow help links,
  aim at a monster and continue past `-more-`.
- **Hold** on the map to see the card of the creature there. In character
  creation, hold a choice to read its help.
- **Two fingers** zoom the map. Android's Back works as Escape. The volume keys
  set the volume the game plays at.
- **Typing**: where the game asks for a name, a number or other text, the
  cluster has **Type…** for the on-screen keyboard; **Keyboard** is also in the
  More drawer.
- **Screens of text and lists**: drag to scroll, swipe sideways to turn the
  page or change a setting. PgUp, PgDn and Back are in the cluster.

## Look at a creature

**Look** in the cluster opens the card of the first creature in view, beside
its place on the map: its description, portrait and what your character has
learned about it. Unknown statistics stay unknown. **Next**, **Prev** or a
sideways swipe go through everything in view; drag to scroll the card;
**Target** makes it the target; **Back** leaves.

## Quests

**Quest** on the rail opens the quest journal, with Active, Completed and
Failed lists. Tap its tabs, rows and buttons, drag to move through a list, and
press Quest again to close it. A questgiver's card has **Accept** and **Later**
in the cluster when it is an offer, and **Continue** otherwise. Reading cards
and the journal costs no turns.

## The Reckoner

The Reckoner shows the working behind each hit, check and spell: the dice
actually rolled and how the result was reached. Turn it on under **Menu >
Settings > The Reckoner** (Static or Animated; it is off by default). Its card
sits above the d-pad; tap the card for **Past Reckonings**, which is also a tab
beside the messages in **Log**. There **Working** shows the exact working,
**Older** and **Newer** move between accounts, and a sideways swipe goes
through an account's results. It never changes a roll or reveals what your
character does not know.

## Map, sound and text size

- **Menu > Settings > Map presentation**: ASCII, Hybrid (artwork for creatures
  and items) or Full Tile, with 32 or 64 pixel artwork, ASCII floors in Full
  Tile, and Enhanced Square walls in square ASCII. Missing artwork always falls
  back to a glyph.
- **Menu > Settings > Sound**: effects, footsteps, menu sounds, ambience (on,
  at 25%) and title music, each with its own switch.
- **Menu > Settings > Interface size**: on a small phone, Comfortable or Large
  makes everything bigger.

## Battery

Between turns the game does almost nothing, unless the ambience or the
weather on the surface keeps it busy. The help page **Battery Life on Phones
and Tablets**, linked from the help index, says what each setting costs.

## Saves

Characters, preferences and scores are kept in the app's own storage. Updates
keep them; uninstalling deletes them. Where a device backs up its apps,
Android's backup and device transfer include them.
