package com.ormeq.game

/** Calls into android/context.c. Safe from any thread: the game takes them on its own. */
object NativeBridge {
    /**
     * Queues keys for the game, one per wait for input. Characters are the
     * game's own key codes ([Keys]). A map command first backs out of
     * whatever screen is open.
     */
    @JvmStatic external fun pressKeys(keys: String, mapCommand: Boolean)

    /** Forgets queued keys not yet sent. */
    @JvmStatic external fun cancelKeys()

    /** A tap at drawing pixel ([x], [y]); [button] is an SDL mouse button (1 left, 3 right). */
    @JvmStatic external fun click(x: Float, y: Float, button: Int)

    /** The pointer without a click, for the look card; off the window clears it. */
    @JvmStatic external fun hover(x: Float, y: Float)

    /** Zoom the map by wheel notches: positive in, negative out. */
    @JvmStatic external fun zoom(notches: Int)

    /** Grid pixels the controls cover along each edge; the map keeps the player clear of them. */
    @JvmStatic external fun setMapInsets(left: Int, top: Int, right: Int, bottom: Int)

    /**
     * Drawing pixels at the left and right that the game's text keeps clear
     * of; the map is drawn on under them, out to the edges (change 0007).
     */
    @JvmStatic external fun setGridInsets(left: Int, right: Int)

    /**
     * Runs the item command [key] (original keyset) on the item whose
     * description is up, answering the command's "which item?" with it
     * (android/items.c). Backs out to the map first.
     */
    @JvmStatic external fun itemAction(key: Char)

    /** Runs quick slot [slot]'s pinned action (android/pins.c). */
    @JvmStatic external fun runPin(slot: Int)

    /** Empties quick slot [slot]. */
    @JvmStatic external fun removePin(slot: Int)

    /** How many quick slots the screen shows; a new pin goes in one of them. */
    @JvmStatic external fun setBarSlots(count: Int)

    /**
     * Puts the focused item's natural action in a free slot. The game answers
     * through OrmeqActivity.onPinAdded, as for the two below.
     */
    @JvmStatic external fun pinFocusedItem()

    /** Puts a drawer command -- its label and the keys it sends -- in a free slot. */
    @JvmStatic external fun pinCommand(label: String, keys: String)

    /** Puts the drawer's Zoom in or Zoom out in a free slot. */
    @JvmStatic external fun pinZoom(label: String, notches: Int)

    /** Backs out of whatever screen is open, to the map, and does nothing there. */
    @JvmStatic external fun backToMap()

    /** Fires the first ammunition the launcher takes at the target (android/items.c). */
    @JvmStatic external fun fireAtTarget()
}

/** The game's key codes, as the touch controls send them. */
object Keys {
    const val ESCAPE = '\u001b'
    const val RETURN = '\r'
    const val BACKSPACE = '\b'

    // The arrows and page keys, by the game's own codes (SKEY_* in defines.h):
    // they reach the game as the desktop's key presses do.
    const val DOWN = ''
    const val LEFT = ''
    const val RIGHT = ''
    const val UP = ''
    const val PAGE_UP = ''
    const val PAGE_DOWN = ''

    /** Ctrl and a letter. */
    fun ctrl(c: Char): Char = (c.uppercaseChar().code and 0x1f).toChar()

    /** Sent before a command so it means the original keyset's command whatever keymaps say. */
    const val RAW = '\\'
}
