# Touch controls: design and plan

_Agreed 5 October 2026, after testing showed the controls growing by
special case ("button X should appear when Y"). This file is the
design the code follows._

## The rule: the cluster acts on the focus

The bottom-right cluster (one large button, three around it) always shows
what can be done with the current **focus**. The rail, the d-pad and the
command drawer do not change.

| Focus | How it gets focus | Large button | Around it |
|---|---|---|---|
| A question from the game | the game asks it (prompt stack, change 0003) | the likely answer | the other answers, Back |
| An item | tapped in Inventory or Equipment (or Inspect) | its natural action | the next actions, then More… |
| A monster | tapped on the map | Fire, Cast or Attack | Throw, Look, More… |
| Your square | nothing else focused, at the map | Get / Down / Up / Enter / Open / Rest | Cast, Fire, Look |

So an action appears on the things it applies to, decided by the game's own
rules, never by a list of situations here.

## Items

**Focus.** The Inventory (`i`) and Equipment (`e`) screens are the game's own
"Examine which item?" prompt (`gear_ui`, obj.c); Inspect (`I`) is the same
prompt with the floor added. A tap on an item shows its description
(`_inspector`) until any key. Change 0005 adds a host hook there,
`ui_obj_focus_hook(obj)` before that wait and `ui_obj_focus_hook(NULL)` after
it, so the focus starts and ends exactly with the description (unidentified
flavours included, which never reach `obj_display_doc`). NULL on the desktop.

**Which actions.** Every item command filters its "which item?" prompt with a
predicate, and those encode the game's real rules (an Android can quaff oil, a
demon eats some corpses). The verbs come from a table of commands, each with
its prompt's filter and the places its prompt looks, in this order (the first
that applies is the large button):

| Key | Label | Accepts (the command's own filter) | Looks in |
|---|---|---|---|
| `t` | Take off | any | equipment, quiver |
| `w` | Wield / Wear / Quiver | `obj_can_wield` (equip.c `_can_wield`) | pack, floor |
| `q` | Quaff | `obj_can_quaff` (cmd6.c `_can_quaff`) | pack, floor |
| `r` | Read | `obj_can_read` (cmd6.c `_can_read`) | pack, floor |
| `E` | Eat | `obj_can_eat` (cmd6.c `_can_eat`) | pack, floor |
| `u` | Use | `obj_is_staff` | pack, floor |
| `a` | Aim | `obj_is_wand` | pack, floor |
| `z` | Zap | `obj_is_rod` | pack, floor |
| `A` | Activate | `obj_can_activate` (cmd6.c `_activate_p`) | equipment |
| `f` | Fire | ammunition: `obj_can_shoot`; the wielded launcher while there is ammunition for it | quiver, pack, floor; equipment |
| `F` | Refill | lantern: `item_tester_refill_lantern`; torch: `obj_is_torch` (cmd3.c `_is_torch`) | pack, floor |
| `m` | Cast | `obj_is_readable_book` | pack, floor |
| `b` | Browse | `obj_is_readable_book` | pack, floor |
| `G` | Study | `obj_can_learn_spell` (cmd5.c `item_tester_learn_spell`), and new spells to learn | pack, floor |
| `g` | Pick up | any | floor |
| `v` | Throw | any | pack, equipment, quiver, floor |
| `d` | Drop | any | pack, equipment, quiver |
| `k` | Destroy | `obj_can_destroy` (obj.c `_can_destroy`) | pack, quiver, floor |

Change 0005 exposes the filters that are `static` in the game as public
functions of those names; nothing else about the commands changes. Inscribe
and Uninscribe are left out: their prompts work through a command handler
inside the list, not by returning the item (see below). The drawer keeps them.

**Doing it.** Choosing an action backs out to the map (a map command, as the
rail's are), runs the game's own command in the original keyset, and answers
that command's first "which item?" prompt with the focused item directly:
`obj_prompt` is wrapped at link time, and the answer is given only if the item
is still where it was (same location, same kind), passes the prompt's own
filter, is in a place the prompt looks, and the prompt has no command handler.
Otherwise the prompt runs as normal. The answer is used once and forgotten when
the game is back at the map. No item letters are involved, so it cannot pick a
different item when the pack has changed. Any further questions (how many,
which direction or target, which spell) are asked by the game as usual.

## Pins (replacing the recorded quick slots)

The centre-bottom slots recorded the next command's keystrokes and replayed
them. Nobody could tell what they did, two recorded spells both read "Cast",
and the replay carries item letters, which the game reassigns as the pack
changes, so a recorded "quaff Cure Light Wounds" could quaff something else.

A pin is an explicit action on a thing, labelled with both:

- **Item pins** ("Quaff · Cure Light Wounds ×3"): added with "Add to bar" in an
  item's More… list; run like a focused action, finding the first carried item
  of that kind; show the count, greyed at zero.
- **Spell pins** ("Cast · Magic Missile"): added by holding a spell in the
  spell list; run as Cast, answered with the book by kind (if the class uses
  books) and the spell's letter in its book.

Tap runs a pin, holding one removes it, an empty slot says how to fill it.
Pins are kept per character between sessions.

### The player chooses what goes on the bar (round 2)

Asked for after the first hands-on look (5 October):

- **Any command can go on the bar**, not only items and spells: holding a
  command in the More drawer pins it (Search, Tunnel, Look, Rest, Zoom...).
  A command pin runs exactly as the drawer would; one that acts on an item
  (Quaff, Aim...) greys out when there is nothing for it to act on.
- **Up to three rows** of four slots, Again staying at the end of the bottom
  row. A small button at the start of the bottom row shows how many rows are
  up and cycles 1, 2, 3. Rows stack upwards; the camera's bottom inset grows
  with them, so the player is never under a row. The number of rows is a
  device preference; the pins are the character's (12 slots). Adding a pin
  to a row that is hidden shows that row.
- **A rail button toggles its screen**: pressed again while the screen it
  opened is still up (Inv, Equip, Map, Char, Log), it closes it -- back to
  the map, rather than closing and reopening it. The game reports the
  command running (the context's `command`), so this also holds when the
  screen was opened from the drawer. Menu and More already toggled.

## Monsters

A tap on a visible monster at the map, more than a step away, focuses it
instead of travelling there: its look card shows, and the target is set to it.
Actions: Fire (with a launcher: the first ammunition it can shoot, at the
target), Cast and Throw (their aim prompt then offers the target, as its large
button), Walk to (the old tap). Tapping anywhere else ends the focus. A monster
a step away is still attacked by a tap.

## Tidying

The command drawer hides item commands with nothing to act on, using the same
table (not the magic ones: some classes cast without books); a group left
empty goes too. The map's cluster keeps its situational large button.

## Round 3: the desktop's new Look, Wait, and Ammerow's lessons

_Agreed 13 October, after the desktop added automatic Look cards, a
health-coloured @ and a warning border, and a review of what Ammerow's public
Android release had learned since this port began._

- **Sync** to desktop c2890000: Look opens the creature's card at once,
  floating beside it over the map; the @ turns yellow, orange and red with
  health; an optional static warning border.
- **Look** is always one of the map's buttons: the large button is what the
  square offers (or Rest); around it the ranged attack, Look, and Rest when the
  large button is busy (else Fire or Powers). In Look: Next (large), Back,
  Prev and Target; a sideways swipe browses, a drag on the card scrolls it, and
  the d-pad still browses. The card's key hints are worded for touch and it
  stops above the controls.
- **Wait** in the d-pad's centre, faintly labelled on the map: a tap waits a
  turn; held, it keeps waiting until lifted, a -more-, a monster coming into
  view or the character being hurt.
- **Fixes** for faults Ormeq shared with Ammerow: a dead character is saved
  when the app goes to the background (Continue could otherwise bring back the
  character from before its death); a failed background save is reported on
  return; a tap on a square never seen does nothing (it pathfound into the
  unknown, usually from a finger that missed a button).
- **Polish** from Ammerow: the d-pad 22 dp off the rail, 72 dp (88 dp on a
  tablet), taking touches a little outside its ring at the map; the rail
  see-through with outlined labels, each button taking taps anywhere in its
  share of the rail; Back always left of the large button; at most two bar
  rows on a phone; the system bars drawn clear (Ammerow saw a black strip at
  the bottom before Android 15).
