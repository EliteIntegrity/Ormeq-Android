package com.ormeq.game

/** What the game is waiting for (keep in step with android/context.c). */
enum class ControlMode(val id: Int) {
    NONE(0), MAP(1), MORE(2), MENU(3), CHECK(4), TEXT(5), AIM(6), DIRECTION(7),
    TARGET(8), ITEM(9), SPELL(10), SHOP(11), KEY(12), LOOK(13), RECKONING(14), QUEST_CARD(15);

    /** Long press on the map shows the look card. */
    val looksOnHold get() = this == MAP || this == AIM || this == DIRECTION

    /**
     * Two fingers on the map zoom it. Not in Look: the game turns the zoom's
     * wheel steps into scrolling while the creature's card is open, and a
     * drag already scrolls it.
     */
    val pinchZooms get() = this == MAP || this == AIM || this == DIRECTION || this == TARGET

    /** A prompt the on-screen keyboard can answer. */
    val typesText get() = this == TEXT || this == KEY

    companion object {
        fun of(id: Int) = entries.firstOrNull { it.id == id } ?: NONE
    }
}

/** What is underfoot at the command prompt (keep in step with android/context.c). */
object Primary {
    const val REST = 0
    const val GET = 1
    const val DOWN = 2
    const val UP = 3
    const val SHOP = 4
    const val OPEN = 5
    const val ENTER = 6
}

/** Context flags (keep in step with android/context.c). */
object ContextFlags {
    const val CAN_CAST = 1
    const val CAN_FIRE = 2
    const val HAS_TARGET = 4
    const val ROGUELIKE_KEYS = 8
    const val BIRTH = 16
    const val IN_PLAY = 32
    /** A hostile monster in plain view: a held Wait stops when one appears. */
    const val MONSTER_IN_VIEW = 64
    /** Hit points lost since the last report: a held Wait stops. */
    const val HURT = 128
    /** A step from the d-pad went nowhere (a wall): a held direction stops. */
    const val BLOCKED = 256
}

/** ITEM detail bits: what the item prompt offers (keep in step with android/ormeq-android.h). */
object ItemPrompt {
    const val FLOOR = 1
    const val TABS = 2
}

data class GameContext(
    val mode: ControlMode = ControlMode.NONE,
    val primary: Int = Primary.REST,
    val flags: Int = 0,
    /** TEXT: 1 for an amount. SHOP: 1 for the home. ITEM: [ItemPrompt] bits. */
    val detail: Int = 0,
    /** CHECK: the answers the question accepts, e.g. "ny". */
    val keys: String = "",
    /** The command whose screen is up (its key, original keyset), 0 at the map. */
    val command: Int = 0,
) {
    fun has(flag: Int) = flags and flag != 0
}

/** The game's grid, in drawing pixels. */
data class GameMetrics(
    val cellWidth: Int = 0,
    val cellHeight: Int = 0,
    val originX: Int = 0,
    val originY: Int = 0,
    val columns: Int = 0,
    val rows: Int = 0,
    /** Where the sidebar starts, or -1 when it is not shown. */
    val sidebarLeft: Int = -1,
)

sealed interface Action {
    /** Keys for the game; a map command first backs out of any open screen. */
    data class Keys(val keys: String, val mapCommand: Boolean = false) : Action
    /** Zoom the map in (+) or out (-). */
    data class Zoom(val notches: Int) : Action
    data object Keyboard : Action
    data object Drawer : Action
    data object CloseDrawer : Action
    /** The item command [key] on the focused item (android/items.c). */
    data class ItemVerb(val key: Char) : Action
    /** Every action for the focused item, in a sheet. */
    data object FocusMore : Action
    /** The focused item's natural action into a quick slot (android/pins.c). */
    data object PinFocused : Action
    /** Quick slot [slot]'s pinned action. */
    data class RunPin(val slot: Int) : Action
    /** Fire at the monster in focus (android/items.c). */
    data object FireAtTarget : Action
    /** Show one more row of quick slots, or after the last, one again. */
    data object CycleRows : Action
    /** Walk to the monster in focus, as a tap used to. */
    data object WalkToFocus : Action
}

/** A monster tapped across the map: now the target, and the cluster acts on it. */
data class MonsterFocus(val name: String)

data class Command(val label: String, val action: Action)

/**
 * The item whose description is up, and what can be done with it: the game's
 * own item commands that accept it, its natural action first (CONTROLS.md).
 * [pin] says what a quick slot for it would do, empty if it is not worth one.
 */
data class ItemFocus(val name: String, val actions: List<Command>, val pin: String = "")

/**
 * A quick slot's pinned action (android/pins.c): [verb] on [name], with
 * [count] of them carried (-1: not counted), [usable] when there is
 * something to act on.
 */
data class PinSlot(val verb: String, val name: String, val count: Int, val usable: Boolean)

/** The right-hand cluster: one large button and up to three around it, in fixed places. */
data class Cluster(val primary: Command?, val quick: List<Command?>)

/**
 * The d-pad for a mode: whether it shows, and whether holding it runs (at the
 * command prompt) or repeats (moving a cursor or a highlight).
 */
/** [centre]: what a tap on the centre does, said faintly there (Wait). */
/**
 * [walks]: held at the command prompt, a direction walks, at a pace a thumb can
 * stop. [centre]: what a tap on the centre does, said faintly there (Wait).
 */
data class Dpad(val visible: Boolean, val walks: Boolean = false, val label: String? = null, val centre: String? = null)

object ControlLayouts {
    /** A command at the command prompt, in the original keyset whatever keymaps say. */
    private fun cmd(label: String, keys: String) = Command(label, Action.Keys("${Keys.RAW}$keys"))
    private fun cmd(label: String, key: Char) = cmd(label, key.toString())

    /** A rail command: backs out of any screen first. */
    private fun mapCmd(label: String, key: Char) = Command(label, Action.Keys("${Keys.RAW}$key", mapCommand = true))

    /** A key as an answer, inside a prompt. */
    private fun key(label: String, keys: String) = Command(label, Action.Keys(keys))
    private fun key(label: String, key: Char) = key(label, key.toString())

    private val ok = key("OK", Keys.RETURN)
    private val back = key("Back", Keys.ESCAPE)
    private val type = Command("Type…", Action.Keyboard)
    private val delete = key("Del", Keys.BACKSPACE)

    // Rest asks how long, with "as needed" already filled in: Return takes it.
    private val rest = cmd("Rest", "R" + Keys.RETURN)
    private val look = cmd("Look", 'l')

    val railTop = listOf(
        // Escape at the map opens the game's pause menu: Continue, Settings, Save and quit.
        key("Menu", Keys.ESCAPE),
        mapCmd("Inv", 'i'),
        mapCmd("Equip", 'e'),
        Command("More", Action.Drawer),
    )
    val railBottom = listOf(
        // The quest journal. The map zooms out far enough that its own
        // screen is seldom needed: it is in More, under Knowledge.
        mapCmd("Quest", Keys.ctrl('q')),
        mapCmd("Char", 'C'),
        mapCmd("Log", Keys.ctrl('p')),
    )

    /** Digit buttons shown at amount prompts. */
    val digits = (1..9).map { key("$it", '0' + it) } + key("0", '0')

    fun showsDigits(context: GameContext) = context.mode == ControlMode.TEXT && context.detail == 1

    /** The quick-slot bar and Again: at the command prompt only. */
    fun showsHotbar(context: GameContext) = context.mode == ControlMode.MAP

    /** Again: the game's own "repeat the last command", with every choice it made. */
    val again = cmd("Again", 'n')

    fun dpad(context: GameContext): Dpad = when (context.mode) {
        // The centre waits a turn; held, it keeps waiting (TouchControlsView).
        ControlMode.MAP -> Dpad(visible = true, walks = true, centre = "Wait")
        ControlMode.AIM -> Dpad(visible = true, label = "fire")
        // In Look the directions browse what is in view.
        ControlMode.DIRECTION, ControlMode.TARGET, ControlMode.LOOK -> Dpad(visible = true)
        // Text screens are swiped instead (see TouchControlsView): a d-pad
        // there sits on top of what the screen is showing.
        else -> Dpad(visible = false)
    }

    /**
     * Screens of text and lists: a vertical drag scrolls (or moves the
     * highlight). In Look it scrolls the creature's card, and a questgiver's
     * card scrolls the same way.
     */
    fun swipes(context: GameContext) = context.mode == ControlMode.MENU || context.mode == ControlMode.NONE ||
        context.mode == ControlMode.LOOK || context.mode == ControlMode.RECKONING ||
        context.mode == ControlMode.QUEST_CARD

    /**
     * And a sideways swipe turns the page; in Look it goes to the next or the
     * previous thing in view. Not before play: on the start and character
     * creation screens the right arrow chooses.
     */
    fun swipesSideways(context: GameContext) = context.mode == ControlMode.MENU || context.mode == ControlMode.LOOK ||
        context.mode == ControlMode.RECKONING

    /**
     * Holding a row means something: a spell goes on the bar; in character
     * creation, help on a choice, and a stat goes down.
     */
    fun holdsRows(context: GameContext) =
        context.mode == ControlMode.SPELL || (context.mode == ControlMode.NONE && context.has(ContextFlags.BIRTH))

    private fun primaryCommand(primary: Int) = when (primary) {
        Primary.GET -> cmd("Get", 'g')
        Primary.DOWN -> cmd("Down", '>')
        Primary.UP -> cmd("Up", '<')
        // Standing still on an entrance goes in.
        Primary.SHOP -> cmd("Enter", ',')
        Primary.ENTER -> cmd("Enter", '>')
        Primary.OPEN -> cmd("Open", 'o')
        else -> rest
    }

    /** Labels for the answers a question accepts. */
    private fun answer(c: Char): Command = when (c) {
        'y', 'Y' -> key("Yes", c)
        'n', 'N' -> key("No", c)
        'o', 'O' -> key("Okay", c)
        'c', 'C' -> key("Cancel", c)
        // "Press Space to continue", after the low hit point warning and others.
        ' ', '\r' -> key("OK", c)
        else -> key(c.toString(), c)
    }

    /**
     * The cluster acts on the focus (CONTROLS.md): an item when its
     * description is up, a monster tapped on the map, otherwise the game's
     * question or the player's square.
     */
    fun cluster(context: GameContext, item: ItemFocus?, monster: MonsterFocus? = null): Cluster = when {
        item != null && item.actions.isNotEmpty() -> itemCluster(item)
        monster != null && context.mode == ControlMode.MAP -> monsterCluster(context)
        else -> cluster(context)
    }

    /**
     * At a monster: shoot it with the launcher, if there is one; a spell or a
     * throw, whose aim then offers it as the target; or walk up to it.
     */
    private fun monsterCluster(context: GameContext): Cluster {
        val fire = if (context.has(ContextFlags.CAN_FIRE)) Command("Fire", Action.FireAtTarget) else null
        val cast = if (context.has(ContextFlags.CAN_CAST)) cmd("Cast", 'm') else null
        val actions = listOfNotNull(fire, cast, cmd("Throw", 'v'))
        return Cluster(actions[0], listOf(actions.getOrNull(1), actions.getOrNull(2), Command("Walk to", Action.WalkToFocus)))
    }

    /**
     * The natural action large; the next two around it; then More…, a sheet
     * of every action and Add to bar. Back is a tap anywhere else.
     */
    private fun itemCluster(item: ItemFocus): Cluster {
        val actions = item.actions
        val more = actions.size > 3 || item.pin.isNotEmpty()
        val last = if (more) Command("More…", Action.FocusMore) else actions.getOrNull(3) ?: back
        return Cluster(actions[0], listOf(actions.getOrNull(1), actions.getOrNull(2), last))
    }

    fun cluster(context: GameContext): Cluster = when (context.mode) {
        ControlMode.MAP -> {
            val canCast = context.has(ContextFlags.CAN_CAST)
            val canFire = context.has(ContextFlags.CAN_FIRE)
            val first = if (canCast) cmd("Cast", 'm') else if (canFire) cmd("Fire", 'f') else cmd("Throw", 'v')
            // A caster with a launcher keeps Cast first (the spells used on
            // arrival: detection, light) and has Fire here whenever there is
            // something to shoot, even when the big button is busy (stairs, a
            // door, an item underfoot). Otherwise Rest stays one tap away when
            // the big button is busy with something else; Look is always there.
            val shoot = canCast && canFire &&
                (context.primary == Primary.REST || context.has(ContextFlags.MONSTER_IN_VIEW))
            val third = when {
                shoot -> cmd("Fire", 'f')
                context.primary != Primary.REST -> rest
                else -> cmd("Powers", 'U')
            }
            Cluster(primaryCommand(context.primary), listOf(first, look, third))
        }
        // Space shows the next message; Escape skips the rest of this turn's.
        ControlMode.MORE -> Cluster(key("Next ▸", ' '), listOf(key("Skip", Keys.ESCAPE), null, null))
        ControlMode.CHECK -> {
            val answers = context.keys.ifEmpty { "ny" }.toList().distinctBy { it.lowercaseChar() }.map(::answer)
            // The affirmative answer is the big button; the others sit around it.
            val yes = answers.firstOrNull { it.label == "Yes" || it.label == "Okay" } ?: answers.first()
            val rest = answers.filter { it !== yes }
            Cluster(yes, listOf(rest.getOrNull(0), rest.getOrNull(1), rest.getOrNull(2)))
        }
        // Back is always left of the large button, where there is one beside it.
        ControlMode.TEXT -> if (context.detail == 1) {
            // An amount: the first digit replaces the default; a letter means all.
            Cluster(ok, listOf(back, key("All", 'a'), delete))
        } else {
            Cluster(ok, listOf(back, type, delete))
        }
        ControlMode.KEY -> Cluster(back, listOf(type, null, null))
        // "*" starts targeting at the closest monster, and "t" takes it. A
        // target already chosen -- a monster tapped on the map -- comes first.
        ControlMode.AIM -> if (context.has(ContextFlags.HAS_TARGET)) {
            Cluster(key("Target", 't'), listOf(back, key("Nearest", "*t"), key("Pick", '*')))
        } else {
            Cluster(key("Nearest", "*t"), listOf(back, key("Pick", '*'), null))
        }
        ControlMode.DIRECTION -> Cluster(back, listOf(null, null, null))
        ControlMode.TARGET -> Cluster(key("Select", 't'), listOf(back, key("Next", ' '), key("Prev", '-')))
        // Looking browses: the next thing in view, the one before, or make
        // this one the target (for Fire or Cast after).
        ControlMode.LOOK -> Cluster(key("Next", ' '), listOf(back, key("Prev", '-'), key("Target", 't')))
        // Past Reckonings: the exact working of what is shown, and older or
        // newer accounts; a sideways swipe goes through an account's results.
        ControlMode.RECKONING -> Cluster(key("Working", 'e'), listOf(back, key("Older", 'n'), key("Newer", 'p')))
        // A questgiver's card: an offer is taken or left for later (keys "a",
        // android/context.c); any other card, the next step or a receipt, is
        // read and closed.
        ControlMode.QUEST_CARD -> if ('a' in context.keys) {
            Cluster(key("Accept", 'a'), listOf(key("Later", Keys.ESCAPE), null, null))
        } else {
            Cluster(key("Continue", Keys.RETURN), listOf(null, null, null))
        }
        // Tap an item to choose it; "-" is the floor, "/" changes tab, each
        // shown only where it does something (android/items.c).
        ControlMode.ITEM -> Cluster(
            back,
            listOf(
                if (context.detail and ItemPrompt.FLOOR != 0) key("Floor", '-') else null,
                if (context.detail and ItemPrompt.TABS != 0) key("Tab", '/') else null,
                null,
            ),
        )
        ControlMode.SPELL -> Cluster(back, listOf(key("Browse", '?'), null, null))
        // The shop's keys are the player's: p buys, s sells, x examines. The
        // home takes and stashes with the same keys.
        ControlMode.SHOP -> {
            val home = context.detail == 1
            Cluster(
                key(if (home) "Take" else "Buy", 'p'),
                listOf(key("Leave", Keys.ESCAPE), key(if (home) "Stash" else "Sell", 's'), key("Info", 'x')),
            )
        }
        ControlMode.MENU -> Cluster(ok, listOf(back, key("PgUp", Keys.PAGE_UP), key("PgDn", Keys.PAGE_DOWN)))
        // Tab is "More Info" on the race and class pages.
        ControlMode.NONE -> if (context.has(ContextFlags.BIRTH)) {
            Cluster(ok, listOf(back, key("Random", '*'), key("Info", '\t')))
        } else {
            Cluster(ok, listOf(back, null, null))
        }
    }

    /**
     * The drawer's commands that act on an item, and are left out when there
     * is nothing for them to act on (android/items.c). Not the magic ones: a
     * caster without books casts all the same. Keep in step with item_bound()
     * in android/pins.c.
     */
    private const val ITEM_VERBS = "gdwtkqrEauzAFfv"

    /** The item command a drawer button runs, if it runs one. */
    fun itemVerb(command: Command): Char? {
        val keys = (command.action as? Action.Keys)?.keys ?: return null
        val key = keys.removePrefix(Keys.RAW.toString()).singleOrNull() ?: return null
        return if (key in ITEM_VERBS) key else null
    }

    /** The drawer opens over any screen: its commands back out to the map first, as the rail's do. */
    private fun Command.fromAnywhere() =
        if (action is Action.Keys) copy(action = action.copy(mapCommand = true)) else this

    /** Every command, for the More drawer (original keyset). */
    val drawer: List<Pair<String, List<Command>>> = listOf(
        "Move" to listOf(
            // Run asks which way: the game's own run, which stops where
            // anything is worth stopping for. Holding the d-pad walks instead.
            cmd("Stay", ','), cmd("Run", '.'), cmd("Search", 's'), cmd("Search mode", 'S'), rest,
            cmd("Travel", '`'), cmd("Resume travel", 'J'), cmd("To unknown item", 'H'),
            cmd("Auto-get", Keys.ctrl('g')), cmd("Open", 'o'), cmd("Close", 'c'), cmd("Tunnel", 'T'),
            // Not Locate or Centre: they move the terminal's viewport, and the
            // SDL3 map's camera follows the player whatever that says.
            cmd("Disarm", 'D'), cmd("Bash", 'B'), cmd("Jam", 'j'), cmd("Alter", '+'),
        ),
        "Items" to listOf(
            cmd("Inventory", 'i'), cmd("Equipment", 'e'), cmd("Pick up", 'g'), cmd("Drop", 'd'),
            cmd("Wear", 'w'), cmd("Take off", 't'), cmd("Swap rings", 'W'), cmd("Inspect", 'I'),
            cmd("Destroy", 'k'), cmd("Inscribe", '{'), cmd("Uninscribe", '}'),
        ),
        "Use" to listOf(
            cmd("Quaff", 'q'), cmd("Read", 'r'), cmd("Eat", 'E'), cmd("Aim wand", 'a'),
            cmd("Use staff", 'u'), cmd("Zap rod", 'z'), cmd("Activate", 'A'), cmd("Fuel", 'F'),
            cmd("Fire", 'f'), cmd("Throw", 'v'),
        ),
        "Magic" to listOf(
            cmd("Cast", 'm'), cmd("Browse", 'b'), cmd("Learn", 'G'), cmd("Powers", 'U'), cmd("Pets", 'p'),
        ),
        "Knowledge" to listOf(
            look, cmd("Target", '*'), cmd("Map", 'M'), cmd("Character", 'C'), cmd("Knowledge", '~'),
            cmd("Messages", Keys.ctrl('p')), cmd("Monsters", '['), cmd("Objects", ']'),
            cmd("Feeling", Keys.ctrl('f')), cmd("Quests", Keys.ctrl('q')), cmd("Time", Keys.ctrl('t')),
            cmd("Symbol", '/'), cmd("Note", ':'),
        ),
        "Game" to listOf(
            Command("Zoom in", Action.Zoom(2)), Command("Zoom out", Action.Zoom(-2)),
            cmd("Options", '='), cmd("Keymaps", '@'), cmd("Loot rules", '_'), cmd("Help", '?'),
            cmd("Save", Keys.ctrl('s')), cmd("Save and quit", Keys.ctrl('x')),
            Command("Keyboard", Action.Keyboard),
        ),
    ).map { (group, commands) -> group to commands.map { it.fromAnywhere() } }
}
