package com.ormeq.game

import android.content.Context
import android.graphics.Canvas
import android.graphics.Color
import android.graphics.Paint
import android.graphics.RectF
import android.graphics.Typeface
import android.os.Handler
import android.os.Looper
import android.os.SystemClock
import android.view.HapticFeedbackConstants
import android.view.MotionEvent
import android.view.View
import android.view.ViewConfiguration
import kotlin.math.abs
import kotlin.math.atan2
import kotlin.math.cos
import kotlin.math.hypot
import kotlin.math.ln
import kotlin.math.max
import kotlin.math.min
import kotlin.math.roundToInt
import kotlin.math.sin

/**
 * Touch controls drawn over the game, following what the game is waiting for
 * ([GameContext], from android/context.c). The game is full screen; the
 * controls float over the map, clear of the message line and the status line:
 *
 * - a rail of system buttons down one side, in the camera-cutout strip
 *   where there is one: the map goes on under it, the game's text keeps
 *   clear of it;
 * - an 8-way d-pad bottom left: a tap steps, holding runs (the game's own
 *   run, which stops when something comes into view);
 * - bottom middle, at the command prompt: four quick slots and Again;
 * - bottom right, one large button and up to three around it. Their places
 *   never change, only their labels, so a thumb learns them;
 * - the More drawer: every command, grouped.
 *
 * Every control presses the game's own keys. Touches elsewhere go to the game
 * as mouse clicks: a tap travels there or picks a row; holding on the map
 * shows the look card for what is there; two fingers zoom. On screens of text
 * and lists, which have no d-pad, a drag scrolls and a sideways swipe turns
 * the page; in character creation, holding a choice asks for help on it.
 */
class TouchControlsView(context: Context) : View(context) {
    var onToggleKeyboard: (() -> Unit)? = null

    /** Maps a view position to the game's drawing pixels, or null if outside the game. */
    var toGamePoint: ((Float, Float) -> Pair<Float, Float>?)? = null

    private class Button(var command: Command?, val round: Boolean) {
        val rect = RectF()
        /** Where a touch presses it, if not just [rect] (the rail's buttons). */
        val touch = RectF()
        var pressed = false
    }

    private var gameContext = GameContext()
    private var metrics = GameMetrics()

    /** The item whose description is up, if any: the cluster acts on it. */
    private var itemFocus: ItemFocus? = null

    /**
     * A monster tapped on the map, if any: the cluster acts on it, and its
     * look card shows (from the tapped point, in game pixels). The next tap
     * anywhere else only ends it.
     */
    private var monsterFocus: MonsterFocus? = null
    private var lastTapX = 0f
    private var lastTapY = 0f

    private val railButtons = (ControlLayouts.railTop + ControlLayouts.railBottom).map { Button(it, round = false) }
    private val primaryButton = Button(null, round = true)
    private val quickButtons = List(3) { Button(null, round = true) }
    private val digitButtons = ControlLayouts.digits.map { Button(it, round = false) }
    private val slotButtons = List(SLOTS_PER_ROW * ROWS_MAX) { Button(null, round = false) }
    private val againButton = Button(ControlLayouts.again, round = false)
    private val rowsButton = Button(Command("", Action.CycleRows), round = false)
    private val drawerButtons = ControlLayouts.drawer.map { (_, commands) -> commands.map { Button(it, round = false) } }
    private val drawerClose = Button(Command("Done", Action.CloseDrawer), round = false)
    private val drawerTitles = ArrayList<Pair<String, Float>>()
    private var drawerOpen = false
    private var drawerScroll = 0f
    private var drawerContentHeight = 0f

    private val activeButtons = HashMap<Int, Button>()

    private val density = resources.displayMetrics.density
    private fun dp(value: Float) = value * density
    private val touchSlop = ViewConfiguration.get(context).scaledTouchSlop.toFloat()

    // The part of the screen the game's text and these controls use, the rail
    // beside it, any camera cutout and the game's whole drawing, which goes
    // under the rail too (view pixels).
    private val game = RectF()
    private val rail = RectF()
    private val cutout = RectF()
    private val drawing = RectF()

    // ---- Quick slots: pinned actions (android/pins.c) --------------------------
    private var pins: List<PinSlot?> = List(SLOTS_PER_ROW * ROWS_MAX) { null }

    /** How many rows of slots are up: the player's choice, kept for the device. */
    private val preferences = context.getSharedPreferences("controls", Context.MODE_PRIVATE)
    private var rows = preferences.getInt("barRows", 1).coerceIn(1, ROWS_MAX)

    /** A phone shows two rows at most: three leave too little of its map. */
    private fun tablet() = resources.configuration.smallestScreenWidthDp >= TABLET_DP
    private fun rowsMax() = if (tablet()) ROWS_MAX else 2
    private fun rowsShown() = min(rows, rowsMax())

    private fun setRows(count: Int) {
        val next = count.coerceIn(1, rowsMax())
        if (next == rows) return
        rows = next
        preferences.edit().putInt("barRows", rows).apply()
        layoutControls()
        invalidate()
    }

    /** The slots of the rows that are up. */
    private fun shownSlots() = slotButtons.take(SLOTS_PER_ROW * rowsShown())

    // ---- D-pad -----------------------------------------------------------------
    private var padX = 0f
    private var padY = 0f
    private var padRadius = 0f
    private var padPointer = -1
    private var padDirection = 0 // keypad digit, 0 = none
    private var padCentreTap = false
    /** The centre was held and has waited on its own: lifting it adds nothing. */
    private var centreRepeated = false
    /** This press is over (the game asked something else, or a step went nowhere) until lifted. */
    private var padSpent = false
    private var dpad = Dpad(visible = false)
    private val handler = Handler(Looper.getMainLooper())

    /**
     * Held, a direction repeats: at the map it walks, a step at a time at a
     * pace a thumb can stop -- the game's run went on after the thumb was
     * lifted, and would not start at all beside anything worth stopping for --
     * and in a prompt it moves the cursor. The hold belongs to its press: it
     * ends when the game asks something else, or when a step goes nowhere
     * (see setGameContext).
     */
    private val padHold = object : Runnable {
        override fun run() {
            if (padDirection == 0 || padSpent) return
            step(padDirection)
            handler.postDelayed(this, if (dpad.walks) WALK_REPEAT_MS else REPEAT_MS)
        }
    }

    /** Ends this press's repeats; the thumb has to lift and press again. */
    private fun endPadHold() {
        if (padPointer == -1) return
        handler.removeCallbacks(padHold)
        padSpent = true
    }

    /**
     * Held, the centre keeps waiting at the d-pad's pace until it is lifted
     * or slides off, the pad hides (a -more-), a monster comes into view or
     * the character is hurt (see setGameContext), as resting stops. Each
     * wait is its own press, so it can stop between any two.
     */
    private val centreHold = object : Runnable {
        override fun run() {
            if (!padCentreTap || dpad.centre == null) return
            if (!centreRepeated) performHapticFeedback(HapticFeedbackConstants.LONG_PRESS)
            centreRepeated = true
            send(Action.Keys("${Keys.RAW},"))
            handler.postDelayed(this, REPEAT_MS)
        }
    }

    private fun stopWaiting() {
        // Only a Wait held at the map: elsewhere the centre is one answer (5).
        if (!padCentreTap || dpad.centre == null) return
        handler.removeCallbacks(centreHold)
        // A wait stopped for a reason does not happen again on lifting.
        centreRepeated = true
    }

    // ---- Touches on the game: tap, hold-to-look, pinch -------------------------
    private var gamePointer = -1
    private var gameDownX = 0f
    private var gameDownY = 0f
    private var gameMoved = false
    private var held = false
    private var looking = false
    private val lookHold = Runnable {
        if (gamePointer == -1 || gameMoved) return@Runnable
        looking = true
        performHapticFeedback(HapticFeedbackConstants.LONG_PRESS)
        toGamePoint?.invoke(gameDownX, gameDownY)?.let { NativeBridge.hover(it.first, it.second) }
        invalidate()
    }

    /**
     * Held on a row of a list: a spell goes on the bar; in character creation,
     * the game's help for a choice (android/context.c).
     */
    private val rowHold = Runnable {
        if (gamePointer == -1 || gameMoved) return@Runnable
        // The hold has done its job; lifting the finger is not a tap as well,
        // nor is moving it a swipe.
        gameMoved = true
        held = true
        performHapticFeedback(HapticFeedbackConstants.LONG_PRESS)
        toGamePoint?.invoke(gameDownX, gameDownY)?.let { NativeBridge.click(it.first, it.second, BUTTON_HOLD) }
    }

    private var pinchPointer = -1
    private var pinchSpread = 0f

    // ---- Paint -----------------------------------------------------------------
    private val fill = Paint(Paint.ANTI_ALIAS_FLAG)
    private val stroke = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        style = Paint.Style.STROKE
        strokeWidth = dp(1.2f)
    }
    private val text = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        color = Color.argb(240, 242, 238, 228)
        textAlign = Paint.Align.CENTER
        textSize = dp(13f)
    }
    /** A dark edge drawn under a faint button's label. */
    private val textOutline = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        style = Paint.Style.STROKE
        strokeWidth = dp(3f)
        strokeJoin = Paint.Join.ROUND
        color = Color.argb(230, 0, 0, 0)
        textAlign = Paint.Align.CENTER
    }
    private val title = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        color = Color.argb(255, 217, 179, 106)
        textSize = dp(13f)
        typeface = Typeface.DEFAULT_BOLD
    }

    init {
        applyContext()
    }

    fun setGameContext(context: GameContext) {
        if (context == gameContext) return
        val wasMore = gameContext.mode == ControlMode.MORE
        if (wasMore && context.mode != ControlMode.MORE) messagesEndedAt = SystemClock.uptimeMillis()
        val inPlayChanged = context.has(ContextFlags.IN_PLAY) != gameContext.has(ContextFlags.IN_PLAY)
        // A held Wait stops, as resting does, when a monster comes into view or
        // the character is hurt.
        if ((context.has(ContextFlags.MONSTER_IN_VIEW) && !gameContext.has(ContextFlags.MONSTER_IN_VIEW)) ||
            context.has(ContextFlags.HURT) || context.mode != ControlMode.MAP) {
            stopWaiting()
        }
        // A held direction walks on through anything but a question or a wall.
        if (context.mode != gameContext.mode || context.has(ContextFlags.BLOCKED)) endPadHold()
        gameContext = context
        // A description is only ever up inside an item screen, and a monster
        // is only acted on from the map.
        if (context.mode != ControlMode.ITEM) {
            itemFocus = null
            closeSheet()
        }
        if (context.mode != ControlMode.MAP) endMonsterFocus()
        if (inPlayChanged) layoutControls()
        applyContext(afterMessages = wasMore)
        if (!padShown) releasePad()
        invalidate()
    }

    /** The rows of the menu on screen, four ints each: left, top, right and bottom in drawing pixels (android/context.c). */
    private var menuRows = IntArray(0)

    fun setMenuRows(rows: IntArray) {
        if (rows.contentEquals(menuRows)) return
        menuRows = rows
        fitPad()
        if (!padShown) releasePad()
        invalidate()
    }

    // Where the d-pad is drawn and takes touches: where it is laid out, or a
    // list's, fitted beside the menu's rows (fitPad).
    private var padCx = 0f
    private var padCy = 0f
    private var padR = 0f
    private var padFits = true

    private val padShown get() = dpad.visible && padFits

    /**
     * A list's pad keeps off the menu's rows: on the start screen they lie in
     * its corner, and on a smaller phone a list can reach it. It needs only up
     * and down, so it shrinks in its corner until it clears them, and goes only
     * if even a small one would cover a row.
     */
    private fun fitPad() {
        padCx = padX
        padCy = padY
        padR = padRadius
        padFits = true
        if (!dpad.list) return
        // Before play no rail is drawn, and the pad may use its strip.
        val railFree = !railShown() && !rail.isEmpty && rail.right <= game.left + 1f
        val left = if (railFree) rail.left + dp(12f) else padX - padRadius
        val bottom = padY + padRadius
        var r = padRadius
        while (r >= dp(LIST_PAD_SMALLEST_DP)) {
            if (!coversRows(left + r, bottom - r, r)) {
                padCx = left + r
                padCy = bottom - r
                padR = r
                return
            }
            r -= dp(4f)
        }
        padFits = false
    }

    /** Whether a pad there would cover a row of the menu (but for the row's first cell, a margin). */
    private fun coversRows(cx: Float, cy: Float, r: Float): Boolean {
        val cell = metrics.cellWidth.toFloat()
        for (i in 0 until menuRows.size / 4) {
            val left = drawing.left + menuRows[i * 4] + cell
            val top = drawing.top + menuRows[i * 4 + 1]
            val right = drawing.left + menuRows[i * 4 + 2]
            val bottom = drawing.top + menuRows[i * 4 + 3]
            if (right <= left) continue
            // The point of the row nearest the pad's centre.
            val x = cx.coerceIn(left, right)
            val y = cy.coerceIn(top, bottom)
            if (hypot(x - cx, y - cy) < r + dp(4f)) return true
        }
        return false
    }

    fun setItemFocus(focus: ItemFocus?) {
        if (focus == itemFocus) return
        itemFocus = focus
        closeSheet()
        applyContext()
        invalidate()
    }

    fun setMonsterFocus(focus: MonsterFocus) {
        monsterFocus = focus
        // Its look card, as holding on it shows.
        NativeBridge.hover(lastTapX, lastTapY)
        performHapticFeedback(HapticFeedbackConstants.CONTEXT_CLICK)
        applyContext()
        invalidate()
    }

    private fun endMonsterFocus() {
        if (monsterFocus == null) return
        monsterFocus = null
        NativeBridge.hover(-10000f, -10000f)
        applyContext()
        invalidate()
    }

    fun setPins(next: List<PinSlot?>) {
        if (next == pins) return
        pins = next
        applyContext()
        invalidate()
    }

    fun setGameMetrics(next: GameMetrics) {
        if (next == metrics) return
        metrics = next
        layoutControls()
        invalidate()
    }

    /** When the large button last changed what it does (uptime ms), and for how long it then ignores taps. */
    private var primaryChangedAt = 0L
    private var primaryGuardMs = PRIMARY_CHANGE_GUARD_MS

    /** When the last -more- went (uptime ms): a tap that was meant for it must not travel. */
    private var messagesEndedAt = 0L

    private fun applyContext(afterMessages: Boolean = false) {
        dpad = ControlLayouts.dpad(gameContext)
        fitPad()
        val cluster = ControlLayouts.cluster(gameContext, itemFocus, monsterFocus)
        if (cluster.primary?.label != primaryButton.command?.label) {
            primaryChangedAt = SystemClock.uptimeMillis()
            // Tapping Next through messages is quick and rhythmic, and when
            // they end the same tap would land on Enter, or Yes: longer then.
            primaryGuardMs = if (afterMessages) AFTER_MESSAGES_GUARD_MS else PRIMARY_CHANGE_GUARD_MS
        }
        primaryButton.command = cluster.primary
        quickButtons.forEachIndexed { index, button -> button.command = cluster.quick.getOrNull(index) }
        slotButtons.forEachIndexed { index, button ->
            val pin = pins.getOrNull(index)
            button.command = Command(pin?.name ?: "+", Action.RunPin(index))
        }
    }

    /**
     * Where the game's text and the controls go, where the rail goes, the
     * camera cutout to keep buttons away from, and the game's whole drawing,
     * which its metrics are measured from (all view pixels; [cutoutRect] may
     * be empty).
     */
    fun setFrame(gameRect: RectF, railRect: RectF, cutoutRect: RectF, drawingRect: RectF) {
        game.set(gameRect)
        rail.set(railRect)
        cutout.set(cutoutRect)
        drawing.set(drawingRect)
        layoutControls()
        invalidate()
    }

    val drawerIsOpen get() = drawerOpen

    fun closeDrawer(): Boolean {
        if (!drawerOpen) return false
        drawerOpen = false
        invalidate()
        return true
    }

    /** The lowest point the bottom controls may reach: just above the game's status line. */
    private fun controlsBottom(): Float {
        val m = metrics
        if (m.cellHeight <= 0 || m.rows <= 0) return game.bottom - dp(8f)
        val statusTop = drawing.top + m.originY + (m.rows - 1) * m.cellHeight
        return min(game.bottom, statusTop) - dp(4f)
    }

    /** The highest point the bottom controls should reach: below the message line's usual two rows. */
    private fun controlsTop(): Float {
        val m = metrics
        if (m.cellHeight <= 0) return game.top + dp(40f)
        return drawing.top + m.originY + 3 * m.cellHeight
    }

    private fun layoutControls() {
        if (game.isEmpty) return
        layoutRail()

        val margin = dp(12f)
        val bottom = controlsBottom()
        val room = bottom - controlsTop()

        // D-pad: bottom left, held off the rail -- a thumb reaching for its
        // left side pressed Log or Char in the Ammerow port -- and larger on a
        // tablet.
        padRadius = min(dp(if (tablet()) 88f else 72f), room * 0.33f)
        padX = game.left + dp(22f) + padRadius
        padY = bottom - padRadius
        fitPad()

        // Cluster: a large button in the corner and three on an arc around it --
        // in play, the corner left of the sidebar, whose list of conditions
        // grows down towards the bottom of the screen.
        val big = min(dp(36f), room * 0.13f)
        val small = big * 0.70f
        val besideSidebar = metrics.sidebarLeft > 0 && gameContext.has(ContextFlags.IN_PLAY)
        val right = if (besideSidebar) min(game.right, drawing.left + metrics.sidebarLeft) - dp(6f) else game.right - margin
        val cx = right - big
        val cy = bottom - big
        primaryButton.rect.set(cx - big, cy - big, cx + big, cy + big)
        val arc = big + small + dp(10f)
        // Left, up-left, up: comfortable for a right thumb resting near the corner.
        val angles = doubleArrayOf(180.0, 225.0, 270.0)
        quickButtons.forEachIndexed { index, button ->
            val angle = Math.toRadians(angles[index])
            val x = cx + (arc * cos(angle)).toFloat()
            val y = cy + (arc * sin(angle)).toFloat()
            button.rect.set(x - small, y - small, x + small, y + small)
        }
        val clusterLeft = quickButtons.minOf { it.rect.left }

        // Hotbar, along the bottom between the two thumbs: the rows button, a
        // row of four quick slots and Again; further rows of four stack above
        // the slots, as many as the player has asked for.
        val slotHeight = dp(44f)
        val gap = dp(6f)
        val rowsWidth = dp(28f)
        val hotbarLeft = padX + padRadius + dp(16f)
        val hotbarRight = clusterLeft - dp(16f)
        val slotWidth = min(dp(76f),
            (hotbarRight - hotbarLeft - rowsWidth - gap * (SLOTS_PER_ROW + 1)) / (SLOTS_PER_ROW + 1.2f))
        val againWidth = slotWidth * 1.2f
        val total = rowsWidth + gap + slotWidth * SLOTS_PER_ROW + againWidth + gap * SLOTS_PER_ROW
        val rowLeft = (hotbarLeft + hotbarRight - total) / 2 + rowsWidth + gap
        rowsButton.rect.set(rowLeft - gap - rowsWidth, bottom - slotHeight, rowLeft - gap, bottom)
        slotButtons.forEachIndexed { index, button ->
            val row = index / SLOTS_PER_ROW
            val x = rowLeft + (index % SLOTS_PER_ROW) * (slotWidth + gap)
            val rowBottom = bottom - row * (slotHeight + gap)
            button.rect.set(x, rowBottom - slotHeight, x + slotWidth, rowBottom)
        }
        val againLeft = rowLeft + SLOTS_PER_ROW * (slotWidth + gap)
        againButton.rect.set(againLeft, bottom - slotHeight, againLeft + againWidth, bottom)
        val slotTop = bottom - rowsShown() * slotHeight - (rowsShown() - 1) * gap
        if (reportedBarSlots != SLOTS_PER_ROW * rowsMax()) {
            reportedBarSlots = SLOTS_PER_ROW * rowsMax()
            NativeBridge.setBarSlots(reportedBarSlots)
        }

        // Digits: one row along the bottom, left of the cluster (the d-pad is hidden then).
        val digitGap = dp(6f)
        val digitRight = clusterLeft - dp(16f)
        val digitWidth = min(dp(52f), (digitRight - game.left - margin - digitGap * 9) / 10)
        val digitHeight = min(dp(46f), room * 0.2f)
        digitButtons.forEachIndexed { index, button ->
            val left = game.left + margin + index * (digitWidth + digitGap)
            button.rect.set(left, bottom - digitHeight, left + digitWidth, bottom)
        }

        layoutDrawer()
        if (sheetOpen) layoutSheet()
        reportMapInsets(min(padY - padRadius, min(quickButtons.minOf { it.rect.top }, slotTop)))
    }

    private var reportedBottomInset = -1
    private var reportedBarSlots = -1

    /**
     * Tells the game how much of the bottom of its grid the controls cover, so
     * the map keeps the player above them (a small level is composed on the
     * screen rather than following the player, and could put them under a thumb).
     */
    private fun reportMapInsets(controlsTop: Float) {
        val m = metrics
        if (m.cellHeight <= 0 || m.rows <= 0) return
        val gridBottom = drawing.top + m.originY + m.rows * m.cellHeight
        val bottom = max(0f, gridBottom - controlsTop).roundToInt()
        if (bottom == reportedBottomInset) return
        reportedBottomInset = bottom
        NativeBridge.setMapInsets(0, 0, 0, bottom)
    }

    /**
     * Buttons from the top of the rail and from the bottom, clear of any
     * cutout in the rail's column: one at an end of it (a camera at the top of
     * an unfolded screen) shortens the rail; one part way down (a punch-hole
     * beside the game in landscape) goes between the two groups.
     */
    private fun layoutRail() {
        if (rail.isEmpty) return
        val gap = dp(6f)
        val side = min(rail.width() - dp(8f), dp(48f))
        val x = rail.centerX()
        val top = ControlLayouts.railTop.size
        val bottom = ControlLayouts.railBottom.size
        var railTop = rail.top
        var railBottom = rail.bottom
        var middleTop = rail.centerY()
        var middleBottom = rail.centerY()
        if (!cutout.isEmpty && cutout.right > rail.left && cutout.left < rail.right) {
            when {
                cutout.top <= rail.top + gap -> railTop = max(railTop, cutout.bottom)
                cutout.bottom >= rail.bottom - gap -> railBottom = min(railBottom, cutout.top)
                else -> {
                    middleTop = cutout.top
                    middleBottom = cutout.bottom
                }
            }
            if (railTop >= railBottom) return
            if (middleTop == middleBottom) {
                middleTop = (railTop + railBottom) / 2
                middleBottom = middleTop
            }
        }
        // Shrink the buttons if the halves are short.
        val upperSpace = middleTop - railTop - gap
        val lowerSpace = railBottom - middleBottom - gap
        val size = min(side, min((upperSpace - gap * top) / top, (lowerSpace - gap * bottom) / bottom))
        if (size <= 0f) return
        // However they are drawn, each is pressed anywhere in its share of the
        // rail, edge to edge -- but not round the camera between the groups.
        railButtons.forEachIndexed { index, button ->
            val y = if (index < top) {
                railTop + gap + index * (size + gap)
            } else {
                railBottom - gap - (railButtons.size - index) * (size + gap) + gap
            }
            button.rect.set(x - size / 2, y, x + size / 2, y + size)
            val touchTop = if (index == 0) railTop else y - gap / 2
            val touchBottom = if (index == railButtons.size - 1) railBottom else y + size + gap / 2
            button.touch.set(rail.left, touchTop, rail.right, touchBottom)
        }
    }

    /**
     * The item commands with something to act on (android/items.c), or null
     * before the game has said: then the drawer shows every command.
     */
    private var itemVerbs: String? = null

    fun setItemVerbs(keys: String) {
        if (keys == itemVerbs) return
        itemVerbs = keys
        layoutDrawer()
        invalidate()
    }

    /** Whether a drawer command is left out for now: an item command with nothing to act on. */
    private fun drawerHides(button: Button): Boolean {
        val verbs = itemVerbs ?: return false
        val key = button.command?.let(ControlLayouts::itemVerb) ?: return false
        return key !in verbs
    }

    private fun layoutDrawer() {
        drawerTitles.clear()
        val left = game.left + dp(16f)
        val right = game.right - dp(16f)
        val buttonWidth = dp(112f)
        val buttonHeight = dp(40f)
        val gap = dp(8f)
        var y = game.top + dp(84f)
        ControlLayouts.drawer.forEachIndexed { group, (name, _) ->
            val shown = drawerButtons[group].filter { button ->
                val hidden = drawerHides(button)
                if (hidden) button.rect.setEmpty()
                !hidden
            }
            // A group with nothing to do goes too.
            if (shown.isEmpty()) return@forEachIndexed
            drawerTitles.add(name to y)
            y += dp(10f)
            var x = left
            for (button in shown) {
                if (x + buttonWidth > right) {
                    x = left
                    y += buttonHeight + gap
                }
                button.rect.set(x, y, x + buttonWidth, y + buttonHeight)
                x += buttonWidth + gap
            }
            y += buttonHeight + dp(28f)
        }
        drawerContentHeight = y - game.top
        drawerClose.rect.set(right - dp(96f), game.top + dp(8f), right, game.top + dp(44f))
        drawerScroll = drawerScroll.coerceIn(0f, maxDrawerScroll())
    }

    private fun maxDrawerScroll() = max(0f, drawerContentHeight - game.height())

    // ---- Drawing ---------------------------------------------------------------

    /** The rail's commands are map commands: nothing to do before a character is in play. */
    private fun railShown() = gameContext.has(ContextFlags.IN_PLAY)

    override fun onDraw(canvas: Canvas) {
        if (game.isEmpty) return
        if (drawerOpen) {
            // Over the whole drawing, the map under the rail included; the
            // rail's buttons stay on top.
            fill.color = Color.argb(238, 10, 14, 11)
            canvas.drawRect(drawing, fill)
        }
        // See-through, as the d-pad is: the map runs on under the rail.
        if (railShown()) for (button in railButtons) drawButton(canvas, button, faint = true)
        if (drawerOpen) {
            drawDrawer(canvas)
            return
        }
        // A look card keeps to the part of the map the controls leave clear
        // (change 0006), so nothing here makes way for it.
        if (padShown) drawPad(canvas)
        if (ControlLayouts.showsDigits(gameContext)) for (button in digitButtons) drawButton(canvas, button)
        if (ControlLayouts.showsHotbar(gameContext)) {
            shownSlots().forEachIndexed { index, button -> drawSlot(canvas, button, pins.getOrNull(index)) }
            drawRowsButton(canvas)
            drawButton(canvas, againButton)
        }
        drawButton(canvas, primaryButton, accent = true)
        for (button in quickButtons) drawButton(canvas, button)
        if (sheetOpen) drawSheet(canvas)
    }

    /**
     * A quick slot: the action and how many are left on a small top line, what
     * it acts on below; greyed when there is nothing to act on. An empty slot
     * is a dashed "+".
     */
    private fun drawSlot(canvas: Canvas, button: Button, pin: PinSlot?) {
        if (pin == null) {
            drawButton(canvas, button, dashed = true)
            return
        }
        fill.color = if (button.pressed) Color.argb(210, 240, 150, 60) else Color.argb(150, 10, 14, 11)
        stroke.color = Color.argb(if (pin.usable) 90 else 45, 255, 255, 255)
        canvas.drawRoundRect(button.rect, dp(7f), dp(7f), fill)
        canvas.drawRoundRect(button.rect, dp(7f), dp(7f), stroke)
        val alpha = if (pin.usable) 240 else 110
        val room = button.rect.width() - dp(8f)
        val size = text.textSize
        val color = text.color
        // A command is its label alone; an item or a spell, the action over its name.
        val nameY = if (pin.verb.isEmpty()) {
            button.rect.centerY() - (text.descent() + text.ascent()) / 2
        } else {
            text.color = Color.argb(alpha, 217, 179, 106)
            text.textSize = dp(10f)
            val top = if (pin.count >= 0) "${pin.verb} ×${pin.count}" else pin.verb
            canvas.drawText(fitText(top, text, room), button.rect.centerX(), button.rect.top + dp(15f), text)
            button.rect.bottom - dp(9f)
        }
        text.color = Color.argb(alpha, 242, 238, 228)
        // A long name shrinks a little before it is cut short.
        text.textSize = dp(12f)
        val width = text.measureText(pin.name)
        if (width > room) text.textSize = max(dp(9.5f), dp(12f) * room / width)
        canvas.drawText(fitText(pin.name, text, room), button.rect.centerX(), nameY, text)
        text.textSize = size
        text.color = color
    }

    /**
     * The rows button: a bar for each row of slots that is up. A tap shows
     * one more row, and after the third, one again.
     */
    private fun drawRowsButton(canvas: Canvas) {
        val r = rowsButton.rect
        fill.color = if (rowsButton.pressed) Color.argb(210, 240, 150, 60) else Color.argb(150, 10, 14, 11)
        stroke.color = Color.argb(90, 255, 255, 255)
        canvas.drawRoundRect(r, dp(7f), dp(7f), fill)
        canvas.drawRoundRect(r, dp(7f), dp(7f), stroke)
        fill.color = Color.argb(220, 242, 238, 228)
        val barWidth = r.width() * 0.5f
        val barHeight = dp(3f)
        val step = dp(7f)
        val firstY = r.centerY() + (rowsShown() - 1) * step / 2f
        for (i in 0 until rowsShown()) {
            val y = firstY - i * step
            canvas.drawRoundRect(r.centerX() - barWidth / 2, y - barHeight / 2, r.centerX() + barWidth / 2,
                y + barHeight / 2, barHeight / 2, barHeight / 2, fill)
        }
    }

    /** Text cut short with an ellipsis to fit [width]. */
    private fun fitText(value: String, paint: Paint, width: Float): String =
        android.text.TextUtils.ellipsize(value, android.text.TextPaint(paint), width,
            android.text.TextUtils.TruncateAt.END).toString()

    private fun drawSheet(canvas: Canvas) {
        fill.color = Color.argb(235, 10, 14, 11)
        canvas.drawRoundRect(sheetPanel, dp(10f), dp(10f), fill)
        stroke.color = Color.argb(90, 255, 255, 255)
        canvas.drawRoundRect(sheetPanel, dp(10f), dp(10f), stroke)
        itemFocus?.let {
            // The panel is as wide as the name allows; a longer one is cut short.
            canvas.drawText(fitText(it.name, title, sheetPanel.width() - dp(24f)),
                sheetPanel.left + dp(12f), sheetPanel.top + dp(24f), title)
        }
        for (button in sheetButtons) drawButton(canvas, button)
    }

    private fun drawPad(canvas: Canvas) {
        val aiming = dpad.label != null
        fill.color = Color.argb(110, 10, 14, 11)
        canvas.drawCircle(padCx, padCy, padR, fill)
        stroke.color = if (aiming) Color.argb(220, 240, 168, 80) else Color.argb(110, 255, 255, 255)
        canvas.drawCircle(padCx, padCy, padR, stroke)
        canvas.drawCircle(padCx, padCy, padR * DEAD_ZONE, stroke)
        for (digit in if (dpad.list) intArrayOf(2, 8) else intArrayOf(1, 2, 3, 4, 6, 7, 8, 9)) {
            val (dx, dy) = offsetOf(digit)
            val active = digit == padDirection
            fill.color = when {
                active -> Color.argb(220, 240, 150, 60)
                aiming -> Color.argb(200, 240, 168, 80)
                else -> Color.argb(120, 255, 255, 255)
            }
            canvas.drawCircle(padCx + dx * padR * 0.7f, padCy + dy * padR * 0.7f, dp(if (active) 8f else 4.5f), fill)
        }
        if (padCentreTap) {
            fill.color = Color.argb(120, 240, 150, 60)
            canvas.drawCircle(padCx, padCy, padR * DEAD_ZONE, fill)
        }
        dpad.label?.let {
            val size = text.textSize
            text.textSize = dp(11f)
            text.color = Color.argb(255, 240, 168, 80)
            canvas.drawText(it, padCx, padCy - (text.descent() + text.ascent()) / 2, text)
            text.color = Color.argb(240, 242, 238, 228)
            text.textSize = size
        }
        dpad.centre?.let {
            // As faint as the direction marks, brighter while pressed.
            val size = text.textSize
            text.textSize = dp(12f)
            text.color = Color.argb(if (padCentreTap) 235 else 125, 242, 238, 228)
            canvas.drawText(it, padCx, padCy - (text.descent() + text.ascent()) / 2, text)
            text.color = Color.argb(240, 242, 238, 228)
            text.textSize = size
        }
    }

    /** [faint]: a light veil and an edge, the label outlined to read over the map. */
    private fun drawButton(canvas: Canvas, button: Button, accent: Boolean = false, dashed: Boolean = false,
            faint: Boolean = false) {
        val command = button.command ?: return
        fill.color = when {
            button.pressed -> Color.argb(210, 240, 150, 60)
            accent -> Color.argb(200, 200, 118, 38)
            faint -> Color.argb(24, 255, 255, 255)
            else -> Color.argb(150, 10, 14, 11)
        }
        stroke.color = when {
            accent -> Color.argb(240, 245, 182, 112)
            faint -> Color.argb(120, 255, 255, 255)
            else -> Color.argb(90, 255, 255, 255)
        }
        stroke.pathEffect = if (dashed) android.graphics.DashPathEffect(floatArrayOf(dp(4f), dp(3f)), 0f) else null
        if (button.round) {
            canvas.drawOval(button.rect, fill)
            canvas.drawOval(button.rect, stroke)
        } else {
            canvas.drawRoundRect(button.rect, dp(7f), dp(7f), fill)
            canvas.drawRoundRect(button.rect, dp(7f), dp(7f), stroke)
        }
        stroke.pathEffect = null
        // Long labels shrink to fit their button.
        val size = text.textSize
        val width = text.measureText(command.label)
        val room = button.rect.width() - dp(6f)
        if (width > room) text.textSize = size * room / width
        val y = button.rect.centerY() - (text.descent() + text.ascent()) / 2
        if (faint) {
            textOutline.textSize = text.textSize
            canvas.drawText(command.label, button.rect.centerX(), y, textOutline)
        }
        canvas.drawText(command.label, button.rect.centerX(), y, text)
        text.textSize = size
    }

    /** The drawer's fixed header: the commands scroll beneath it, never under Done. */
    private fun drawerHeaderBottom() = game.top + dp(52f)

    private fun drawDrawer(canvas: Canvas) {
        canvas.save()
        canvas.clipRect(game.left, drawerHeaderBottom(), game.right, game.bottom)
        canvas.translate(0f, -drawerScroll)
        for ((name, y) in drawerTitles) canvas.drawText(name, game.left + dp(16f), y, title)
        for (group in drawerButtons) for (button in group) if (!button.rect.isEmpty) drawButton(canvas, button)
        canvas.restore()
        canvas.drawText("Commands", game.left + dp(16f), game.top + dp(33f), title)
        // How a command gets onto the bar, beside the title.
        val size = text.textSize
        val color = text.color
        val align = text.textAlign
        text.textSize = dp(12f)
        text.color = Color.argb(170, 242, 238, 228)
        text.textAlign = Paint.Align.LEFT
        canvas.drawText("Tap to use. Hold to put it on the bar.",
            game.left + dp(28f) + title.measureText("Commands"), game.top + dp(33f), text)
        text.textSize = size
        text.color = color
        text.textAlign = align
        drawButton(canvas, drawerClose, accent = true)
    }

    // ---- Touch -----------------------------------------------------------------

    override fun onTouchEvent(event: MotionEvent): Boolean {
        val index = event.actionIndex
        val id = event.getPointerId(index)
        when (event.actionMasked) {
            MotionEvent.ACTION_DOWN, MotionEvent.ACTION_POINTER_DOWN -> touchDown(id, event.getX(index), event.getY(index))
            MotionEvent.ACTION_MOVE -> touchMove(event)
            MotionEvent.ACTION_UP, MotionEvent.ACTION_POINTER_UP -> touchUp(id, event.getX(index), event.getY(index))
            MotionEvent.ACTION_CANCEL -> {
                for (pointer in activeButtons.keys.toList()) releaseButton(pointer, cancelled = true)
                releasePad()
                cancelGameTouch()
            }
        }
        return true
    }

    private fun visibleButtons(): List<Button> {
        val buttons = ArrayList<Button>()
        if (ControlLayouts.showsDigits(gameContext)) buttons += digitButtons
        if (ControlLayouts.showsHotbar(gameContext)) {
            buttons += shownSlots()
            buttons += rowsButton
            buttons += againButton
        }
        buttons += quickButtons
        buttons += primaryButton
        return buttons.filter { it.command != null }
    }

    private fun touchDown(id: Int, x: Float, y: Float) {
        // The More… sheet takes every touch while it is up; outside it, one closes it.
        if (sheetOpen) {
            sheetButtons.firstOrNull { it.rect.contains(x, y) }?.let { return pressButton(id, it) }
            closeSheet()
            return
        }
        if (railShown()) railButtons.firstOrNull { it.touch.contains(x, y) }?.let { return pressButton(id, it) }
        if (drawerOpen) {
            if (drawerClose.rect.contains(x, y)) return pressButton(id, drawerClose)
            startGameTouch(id, x, y)
            return
        }
        visibleButtons().firstOrNull { it.rect.contains(x, y) }?.let { button ->
            // A tap meant for the old action must not become the new one: the
            // large button ignores taps briefly after it changes.
            if (button === primaryButton &&
                SystemClock.uptimeMillis() - primaryChangedAt < primaryGuardMs) return
            return pressButton(id, button)
        }
        // At the map a touch a little outside the ring still counts: a thumb
        // finds the pad by feel.
        val reach = if (gameContext.mode == ControlMode.MAP) PAD_REACH_MAP else PAD_REACH
        if (padShown && padPointer == -1 && hypot(x - padCx, y - padCy) <= padR * reach) {
            padPointer = id
            padCentreTap = hypot(x - padCx, y - padCy) < padR * DEAD_ZONE
            centreRepeated = false
            padSpent = false
            pressPad(x, y)
            if (padCentreTap && dpad.centre != null) handler.postDelayed(centreHold, FIRST_REPEAT_MS)
            invalidate()
            return
        }
        if (game.contains(x, y)) {
            // A second finger on the game turns the touch into a pinch.
            if (gamePointer != -1 && pinchPointer == -1 && gameContext.mode.pinchZooms) {
                startPinch(id, x, y)
            } else {
                startGameTouch(id, x, y)
            }
        }
    }

    private fun startPinch(id: Int, x: Float, y: Float) {
        pinchPointer = id
        pinchSpread = hypot(x - gameDownX, y - gameDownY).coerceAtLeast(1f)
        // Neither finger is a tap or a look any more.
        gameMoved = true
        handler.removeCallbacks(lookHold)
        endLook()
    }

    /** One wheel notch is the desktop's zoom step, a ratio of 1.12. */
    private fun updatePinch(event: MotionEvent) {
        val a = event.findPointerIndex(gamePointer)
        val b = event.findPointerIndex(pinchPointer)
        if (a < 0 || b < 0) return
        val spread = hypot(event.getX(a) - event.getX(b), event.getY(a) - event.getY(b)).coerceAtLeast(1f)
        val notches = (ln(spread / pinchSpread) / ln(ZOOM_STEP)).toInt()
        if (notches != 0) {
            NativeBridge.zoom(notches)
            pinchSpread *= Math.pow(ZOOM_STEP.toDouble(), notches.toDouble()).toFloat()
        }
    }

    private fun touchMove(event: MotionEvent) {
        if (padPointer != -1) {
            val padIndex = event.findPointerIndex(padPointer)
            if (padIndex >= 0) movePad(event.getX(padIndex), event.getY(padIndex))
        }
        if (pinchPointer != -1) {
            updatePinch(event)
            return
        }
        if (gamePointer != -1) {
            val gameIndex = event.findPointerIndex(gamePointer)
            if (gameIndex < 0) return
            val x = event.getX(gameIndex)
            val y = event.getY(gameIndex)
            if (looking) {
                // Slide to look at something else.
                toGamePoint?.invoke(x, y)?.let { NativeBridge.hover(it.first, it.second) }
                return
            }
            if (!gameMoved && hypot(x - gameDownX, y - gameDownY) > touchSlop) {
                gameMoved = true
                handler.removeCallbacks(lookHold)
                handler.removeCallbacks(rowHold)
                handler.removeCallbacks(drawerHold)
                swipeX = gameDownX
                swipeY = gameDownY
                swipeTurned = false
            }
            if (drawerOpen && gameMoved) {
                drawerScroll = (drawerScroll - (y - gameDownY)).coerceIn(0f, maxDrawerScroll())
                gameDownY = y
                invalidate()
            } else if (gameMoved && !held && ControlLayouts.swipes(gameContext)) {
                swipe(x, y)
            }
        }
    }

    private fun touchUp(id: Int, x: Float, y: Float) {
        if (id == padPointer) {
            // A tap on the centre stays in place (or answers 5) for a turn; a
            // list's centre is nothing.
            if (padCentreTap && !centreRepeated && !dpad.list) {
                if (gameContext.mode == ControlMode.MAP) send(Action.Keys("${Keys.RAW},")) else step(5)
            }
            releasePad()
        }
        releaseButton(id, cancelled = false)
        if (id == pinchPointer || (id == gamePointer && pinchPointer != -1)) {
            // Lifting either finger ends the pinch; the other is not a tap.
            pinchPointer = -1
            gamePointer = -1
            return
        }
        if (id != gamePointer) return
        handler.removeCallbacks(lookHold)
        handler.removeCallbacks(rowHold)
        handler.removeCallbacks(drawerHold)
        val tapped = !gameMoved && !looking
        gamePointer = -1
        if (looking) {
            endLook()
            return
        }
        if (!tapped) return
        if (drawerOpen) {
            if (y < drawerHeaderBottom()) return
            val scrolledY = y + drawerScroll
            val button = drawerButtons.flatten().firstOrNull { !it.rect.isEmpty && it.rect.contains(x, scrolledY) } ?: return
            drawerOpen = false
            invalidate()
            button.command?.let { run(it.action, it.label) }
            return
        }
        if (gameContext.mode == ControlMode.MAP &&
            SystemClock.uptimeMillis() - messagesEndedAt < AFTER_MESSAGES_GUARD_MS) return
        // With a monster in focus, a tap elsewhere only ends the focus.
        if (monsterFocus != null) {
            endMonsterFocus()
            return
        }
        toGamePoint?.invoke(x, y)?.let {
            // Kept for the monster's look card and Walk to, should it focus one.
            lastTapX = it.first
            lastTapY = it.second
            NativeBridge.click(it.first, it.second, 1)
        }
    }

    // A drag on a text screen: where the last step was counted from, and
    // whether this gesture has already turned a page.
    private var swipeX = 0f
    private var swipeY = 0f
    private var swipeTurned = false

    /**
     * On a page of text, dragging up scrolls on (the down arrow) and dragging
     * down scrolls back, a line for each row of text the finger passes, so the
     * text keeps up with it. In a list, the lit row follows the finger
     * instead, up as it goes up, as the d-pad's up moves it (0.1.1-android.2:
     * it went down, which a player found backwards). A sideways swipe is one
     * press of an arrow per gesture -- the next page, or a change of setting:
     * towards the left is right, as a page turns.
     */
    private fun swipe(x: Float, y: Float) {
        val dx = x - swipeX
        val dy = y - swipeY
        // In Look a sideways swipe goes to the next thing in view or the one
        // before, and a drag scrolls the creature's card, with its own keys;
        // in Past Reckonings it goes to the next or previous result.
        val look = gameContext.mode == ControlMode.LOOK
        if (!swipeTurned && abs(dx) > dp(SWIPE_PAGE_DP) && abs(dx) > abs(dy) * 1.5f &&
            ControlLayouts.swipesSideways(gameContext)) {
            swipeTurned = true
            when {
                look -> arrow(if (dx < 0) ' ' else '-')
                gameContext.mode == ControlMode.RECKONING -> arrow(if (dx < 0) ']' else '[')
                else -> arrow(if (dx < 0) Keys.RIGHT else Keys.LEFT)
            }
            swipeX = x
            swipeY = y
            return
        }
        val line = max(dp(SWIPE_LINE_MIN_DP), metrics.cellHeight.toFloat())
        val row = ControlLayouts.dragMovesRow(gameContext)
        while (abs(y - swipeY) > line && abs(dy) > abs(dx)) {
            if (y < swipeY) {
                arrow(if (look) ']' else if (row) Keys.UP else Keys.DOWN)
                swipeY -= line
            } else {
                arrow(if (look) '[' else if (row) Keys.DOWN else Keys.UP)
                swipeY += line
            }
        }
    }

    private fun arrow(key: Char) {
        performHapticFeedback(HapticFeedbackConstants.CLOCK_TICK)
        NativeBridge.pressKeys(key.toString(), false)
    }

    private fun startGameTouch(id: Int, x: Float, y: Float) {
        if (gamePointer != -1) return
        gamePointer = id
        gameDownX = x
        gameDownY = y
        gameMoved = false
        held = false
        looking = false
        if (drawerOpen) {
            handler.postDelayed(drawerHold, ViewConfiguration.getLongPressTimeout().toLong())
        } else if (gameContext.mode.looksOnHold) {
            handler.postDelayed(lookHold, ViewConfiguration.getLongPressTimeout().toLong())
        } else if (ControlLayouts.holdsRows(gameContext)) {
            handler.postDelayed(rowHold, ViewConfiguration.getLongPressTimeout().toLong())
        }
    }

    /** The drawer command at a point on the screen, or null (the header covers the top). */
    private fun drawerButtonAt(x: Float, y: Float): Button? {
        if (y < drawerHeaderBottom()) return null
        return drawerButtons.flatten().firstOrNull { !it.rect.isEmpty && it.rect.contains(x, y + drawerScroll) }
    }

    /** Held on a drawer command: it goes on the bar. The release is then not a tap. */
    private val drawerHold = Runnable {
        if (gamePointer == -1 || gameMoved || !drawerOpen) return@Runnable
        val command = drawerButtonAt(gameDownX, gameDownY)?.command ?: return@Runnable
        gameMoved = true
        held = true
        performHapticFeedback(HapticFeedbackConstants.LONG_PRESS)
        when (val action = command.action) {
            // The game answers through pinAdded.
            is Action.Keys -> NativeBridge.pinCommand(command.label, action.keys)
            is Action.Zoom -> NativeBridge.pinZoom(command.label, action.notches)
            else -> note("${command.label} can't go on the bar.")
        }
    }

    private fun endLook() {
        if (!looking) return
        looking = false
        // Off the window: the card goes.
        NativeBridge.hover(-10000f, -10000f)
        invalidate()
    }

    private fun cancelGameTouch() {
        handler.removeCallbacks(lookHold)
        handler.removeCallbacks(rowHold)
        handler.removeCallbacks(drawerHold)
        endLook()
        gamePointer = -1
        pinchPointer = -1
    }

    private fun pressButton(pointer: Int, button: Button) {
        val command = button.command ?: return
        button.pressed = true
        activeButtons[pointer] = button
        performHapticFeedback(HapticFeedbackConstants.VIRTUAL_KEY)
        when {
            // Quick slots act on release, so a long press can clear one instead.
            button in slotButtons -> handler.postDelayed(slotHold, ViewConfiguration.getLongPressTimeout().toLong())
            // A rail button pressed again closes the screen it opened.
            button in railButtons && opensScreenUp(command) -> NativeBridge.backToMap()
            else -> run(command.action, command.label)
        }
        // Whatever was pressed, a monster's focus has done its job.
        endMonsterFocus()
        invalidate()
    }

    /**
     * Whether [command] is the one whose screen is up now: the game says which
     * command is running (android/context.c), however it was started.
     */
    private fun opensScreenUp(command: Command): Boolean {
        val keys = (command.action as? Action.Keys)?.takeIf { it.mapCommand }?.keys ?: return false
        val key = keys.removePrefix(Keys.RAW.toString()).singleOrNull() ?: return false
        return gameContext.mode != ControlMode.MAP && gameContext.command == key.code
    }

    /**
     * The game put a pin in [slot] (-1: none) and says [message] about it. A
     * pin added to a row that is not up brings that row up.
     */
    fun pinAdded(slot: Int, message: String) {
        note(message)
        if (slot >= 0 && slot / SLOTS_PER_ROW + 1 > rowsShown()) setRows(slot / SLOTS_PER_ROW + 1)
    }

    private val slotHold = Runnable {
        val button = activeButtons.values.firstOrNull { it in slotButtons } ?: return@Runnable
        val index = slotButtons.indexOf(button)
        // Holding a slot empties it; the release is then not a tap.
        val pin = pins.getOrNull(index)
        if (pin != null) {
            performHapticFeedback(HapticFeedbackConstants.LONG_PRESS)
            button.pressed = false
            activeButtons.values.remove(button)
            NativeBridge.removePin(index)
            note("${"${pin.verb} ${pin.name}".trim()} is off the bar.")
            invalidate()
        }
    }

    private fun releaseButton(pointer: Int, cancelled: Boolean) {
        val button = activeButtons.remove(pointer) ?: return
        button.pressed = false
        if (button in slotButtons) {
            handler.removeCallbacks(slotHold)
            if (!cancelled) slotTapped(slotButtons.indexOf(button))
        }
        invalidate()
    }

    private fun slotTapped(index: Int) {
        val pin = pins.getOrNull(index)
        when {
            pin == null -> note("Hold a command in More or a spell in a spell list to put it here, " +
                "or use an item's More….")
            // A command acting on items, with none to act on; a spell without
            // its book; an item with none left.
            !pin.usable && pin.verb.isEmpty() -> note("There is nothing to ${pin.name.lowercase()}.")
            !pin.usable && pin.count < 0 -> note("You need the book for ${pin.name}.")
            !pin.usable -> note("You have no ${pin.name} to ${pin.verb.lowercase()}.")
            else -> NativeBridge.runPin(index)
        }
    }

    /** A short message over the game. */
    private fun note(message: String) {
        if (message.isNotEmpty()) android.widget.Toast.makeText(context, message, android.widget.Toast.LENGTH_SHORT).show()
    }

    private fun run(action: Action, label: String) {
        when (action) {
            is Action.Keys -> {
                if (action.keys.isEmpty()) return
                send(action)
            }
            // Slots run on release (slotTapped).
            is Action.RunPin -> Unit
            Action.PinFocused -> {
                closeSheet()
                // The game answers through pinAdded.
                NativeBridge.pinFocusedItem()
            }
            Action.CycleRows -> setRows(rowsShown() % rowsMax() + 1)
            Action.FireAtTarget -> NativeBridge.fireAtTarget()
            Action.WalkToFocus -> NativeBridge.click(lastTapX, lastTapY, BUTTON_WALK)
            is Action.Zoom -> NativeBridge.zoom(action.notches)
            Action.Keyboard -> onToggleKeyboard?.invoke()
            Action.Drawer -> {
                drawerOpen = !drawerOpen
                releasePad()
                invalidate()
            }
            Action.CloseDrawer -> closeDrawer()
            is Action.ItemVerb -> {
                closeSheet()
                NativeBridge.itemAction(action.key)
            }
            Action.FocusMore -> openSheet()
        }
    }

    // ---- The focused item's More… sheet ---------------------------------------
    private var sheetOpen = false
    private val sheetButtons = ArrayList<Button>()
    private val sheetPanel = RectF()

    private fun openSheet() {
        val focus = itemFocus ?: return
        sheetButtons.clear()
        focus.actions.forEach { sheetButtons += Button(it, round = false) }
        // Its natural action, if worth one, can go in a quick slot.
        if (focus.pin.isNotEmpty()) sheetButtons += Button(Command("Add to bar", Action.PinFocused), round = false)
        sheetOpen = true
        layoutSheet()
        invalidate()
    }

    private fun closeSheet() {
        if (!sheetOpen) return
        sheetOpen = false
        sheetButtons.clear()
        invalidate()
    }

    /** Every action as a button, in columns above the cluster, the item's name on top. */
    private fun layoutSheet() {
        if (sheetButtons.isEmpty()) return
        val width = dp(132f)
        val height = dp(40f)
        val gap = dp(6f)
        val pad = dp(10f)
        val heading = dp(30f)
        val right = primaryButton.rect.right
        val bottom = quickButtons.minOf { it.rect.top } - dp(10f)
        val room = bottom - (game.top + dp(8f)) - 2 * pad - heading
        val fits = max(1, ((room + gap) / (height + gap)).toInt())
        val columns = (sheetButtons.size + fits - 1) / fits
        // As many rows as it takes to share the buttons evenly between the columns.
        val perColumn = (sheetButtons.size + columns - 1) / columns
        val rows = perColumn
        // Wide enough for the item's name, within reason.
        val nameWidth = min(title.measureText(itemFocus?.name ?: "") + dp(24f), game.width() * 0.6f)
        val panelWidth = max(columns * width + (columns - 1) * gap + 2 * pad, nameWidth)
        val panelHeight = rows * height + (rows - 1) * gap + 2 * pad + heading
        sheetPanel.set(right - panelWidth, bottom - panelHeight, right, bottom)
        // Buttons share the panel's width when the name made it wider.
        val columnWidth = (panelWidth - 2 * pad - (columns - 1) * gap) / columns
        sheetButtons.forEachIndexed { index, button ->
            val x = sheetPanel.left + pad + (index / perColumn) * (columnWidth + gap)
            val y = sheetPanel.top + pad + heading + (index % perColumn) * (height + gap)
            button.rect.set(x, y, x + columnWidth, y + height)
        }
    }

    private fun send(action: Action.Keys) {
        NativeBridge.pressKeys(action.keys, action.mapCommand)
    }

    private fun releasePad() {
        handler.removeCallbacks(padHold)
        handler.removeCallbacks(centreHold)
        // The centre is lit while pressed, so redraw whichever was down.
        if (padDirection != 0 || padCentreTap) invalidate()
        padPointer = -1
        padCentreTap = false
        centreRepeated = false
        padSpent = false
        padDirection = 0
    }

    /**
     * A press on the pad is one input until it is lifted, as in other touch
     * roguelikes (0.1.1-android.2, at a player's asking): the direction it
     * lands on, which steps at once and, held, repeats; or the centre. The
     * thumb sliding turns nothing, so one that drifts takes no step it was not
     * meant to (in 0.1.1-android.1 the step turned to follow it).
     */
    private fun pressPad(x: Float, y: Float) {
        if (padCentreTap) return
        val angle = Math.toDegrees(atan2(y - padCy, x - padCx).toDouble()) + 360
        // Eight 45-degree sectors, clockwise from east.
        val digit = SECTOR_DIGITS[((angle + 22.5) / 45).toInt() % 8]
        // A list's pad has up and down only: the upper and lower three sectors.
        padDirection = if (!dpad.list) digit else when (digit) { 7, 8, 9 -> 8; 1, 2, 3 -> 2; else -> 0 }
        if (padDirection == 0) return
        step(padDirection)
        handler.postDelayed(padHold, if (dpad.walks) WALK_FIRST_MS else FIRST_REPEAT_MS)
    }

    /** A press on the centre that slides off it is let go: it waits no more, and lifting it adds nothing. */
    private fun movePad(x: Float, y: Float) {
        if (!padCentreTap || hypot(x - padCx, y - padCy) < padR * DEAD_ZONE) return
        handler.removeCallbacks(centreHold)
        padCentreTap = false
        invalidate()
    }

    /**
     * A step, or a direction where the game asks for one: the keypad digit.
     * In a menu, the arrow key, which moves its lit row.
     */
    private fun step(digit: Int) {
        performHapticFeedback(HapticFeedbackConstants.CLOCK_TICK)
        val keys = if (dpad.list) (if (digit == 8) Keys.UP else Keys.DOWN).toString() else digit.toString()
        NativeBridge.pressKeys(keys, false)
    }

    private fun offsetOf(digit: Int): Pair<Float, Float> {
        val dx = when (digit) { 1, 4, 7 -> -1f; 3, 6, 9 -> 1f; else -> 0f }
        val dy = when (digit) { 7, 8, 9 -> -1f; 1, 2, 3 -> 1f; else -> 0f }
        val length = hypot(dx, dy)
        return Pair(dx / length, dy / length)
    }

    private companion object {
        const val SLOTS_PER_ROW = 4
        const val ROWS_MAX = 3
        /** SDL's X2 mouse button, which carries a long press to android/context.c. */
        const val BUTTON_HOLD = 5
        /** SDL's X1 mouse button: walk to the point, monster or not (android/context.c). */
        const val BUTTON_WALK = 4
        const val DEAD_ZONE = 0.28f
        /** A list's pad shrinks to clear a menu's rows, but no smaller than this (dp). */
        const val LIST_PAD_SMALLEST_DP = 44f
        /** How far out, in radii, a touch still takes the d-pad: at the map, and elsewhere. */
        const val PAD_REACH_MAP = 1.35f
        const val PAD_REACH = 1.15f
        /** The smallest screen width of a tablet, in dp. */
        const val TABLET_DP = 600
        const val PRIMARY_CHANGE_GUARD_MS = 400L
        const val AFTER_MESSAGES_GUARD_MS = 700L
        /** Held at the map: the first repeated step, then one every so often (about 6.7 a second). */
        const val WALK_FIRST_MS = 300L
        const val WALK_REPEAT_MS = 150L
        const val FIRST_REPEAT_MS = 320L
        const val REPEAT_MS = 130L
        const val ZOOM_STEP = 1.12f
        const val SWIPE_LINE_MIN_DP = 14f
        const val SWIPE_PAGE_DP = 72f
        // atan2 sectors from east, clockwise (screen y grows downwards).
        val SECTOR_DIGITS = intArrayOf(6, 3, 2, 1, 4, 7, 8, 9)
    }
}
