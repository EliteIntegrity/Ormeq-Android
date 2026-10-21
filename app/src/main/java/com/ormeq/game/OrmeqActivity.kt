package com.ormeq.game

import android.content.Context
import android.content.pm.ActivityInfo
import android.content.res.Configuration
import android.graphics.Color
import android.graphics.Rect
import android.graphics.RectF
import android.media.AudioManager
import android.os.Build
import android.os.Bundle
import android.text.InputType
import android.view.KeyEvent
import android.view.Surface
import android.view.SurfaceHolder
import android.view.ViewGroup
import android.view.WindowInsets
import android.view.WindowManager
import android.widget.RelativeLayout
import android.widget.Toast
import org.libsdl.app.SDLActivity
import org.libsdl.app.SDLSurface
import kotlin.math.max
import kotlin.math.min
import kotlin.math.roundToInt

class OrmeqActivity : SDLActivity() {
    private var controls: TouchControlsView? = null
    private var keyboardShown = false

    override fun getLibraries(): Array<String> =
        arrayOf("SDL3", "SDL3_image", "SDL3_ttf", "main")

    /**
     * SDL's surface listens to the accelerometer at game rate from every
     * resume, to offer it as a joystick and to notice the display turning
     * over. The game uses neither -- the controls follow the window's insets
     * -- and fifty wake-ups a second would keep a phone busy while the game
     * sits still.
     */
    override fun createSDLSurface(context: Context): SDLSurface = object : SDLSurface(context) {
        override fun enableSensor(sensortype: Int, enabled: Boolean) = Unit
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        chooseOrientation()
        super.onCreate(savedInstanceState)
        val layout = SDLActivity.mLayout ?: return // SDL could not start and is showing its own error.
        // The volume keys set the game's volume. Its audio device is closed
        // between sounds (android/sound.c), and with nothing playing some
        // phones would turn them to the ringer instead.
        volumeControlStream = AudioManager.STREAM_MUSIC
        // The window draws the system bars' backgrounds itself, clear: before
        // Android 15 a hidden navigation bar could leave a black strip over
        // the bottom of the game (seen in the Ammerow port on Android 13).
        @Suppress("DEPRECATION")
        window.apply {
            addFlags(WindowManager.LayoutParams.FLAG_DRAWS_SYSTEM_BAR_BACKGROUNDS)
            navigationBarColor = Color.TRANSPARENT
            statusBarColor = Color.TRANSPARENT
        }
        SDLActivity.mSurface.layoutParams = RelativeLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT)
        // No keyboard-focus outline around the game when a hardware keyboard is used.
        SDLActivity.mSurface.defaultFocusHighlightEnabled = false
        val touchControls = TouchControlsView(this)
        touchControls.onToggleKeyboard = ::toggleKeyboard
        touchControls.toGamePoint = ::gamePoint
        layout.addView(touchControls, RelativeLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT))
        controls = touchControls
        SDLActivity.mSurface.addOnLayoutChangeListener { _, _, _, _, _, _, _, _, _ -> updateFrame() }
        SDLActivity.mSurface.holder.addCallback(object : SurfaceHolder.Callback {
            override fun surfaceCreated(holder: SurfaceHolder) {
                // 60 Hz is plenty for a turn-based game and spares the battery on 90/120 Hz screens.
                if (Build.VERSION.SDK_INT >= 30) {
                    holder.surface.setFrameRate(60f, Surface.FRAME_RATE_COMPATIBILITY_DEFAULT)
                }
            }
            override fun surfaceChanged(holder: SurfaceHolder, format: Int, width: Int, height: Int) = Unit
            override fun surfaceDestroyed(holder: SurfaceHolder) = Unit
        })
        layout.setOnApplyWindowInsetsListener { _, insets ->
            applyInsets(insets)
            insets
        }
    }

    override fun onConfigurationChanged(newConfig: Configuration) {
        super.onConfigurationChanged(newConfig)
        // Folding or unfolding changes which screen the game is on.
        chooseOrientation()
    }

    /**
     * Landscape on a phone, where the game's 80 columns would be too narrow
     * upright (the manifest asks for it, so a phone never starts the other way
     * round). A tablet or an unfolded foldable has room either way, and held
     * to landscape a near-square screen shows the game in a box with bars
     * above and below -- Android 16 lifts orientation locks on large screens,
     * but not for games.
     */
    private fun chooseOrientation() {
        val smallestDp = if (Build.VERSION.SDK_INT >= 30) {
            val display = windowManager.maximumWindowMetrics.bounds
            min(display.width(), display.height()) / resources.displayMetrics.density
        } else {
            resources.configuration.smallestScreenWidthDp.toFloat()
        }
        val wanted = if (smallestDp >= LARGE_SCREEN_DP) {
            ActivityInfo.SCREEN_ORIENTATION_UNSPECIFIED
        } else {
            ActivityInfo.SCREEN_ORIENTATION_SENSOR_LANDSCAPE
        }
        if (requestedOrientation != wanted) requestedOrientation = wanted
    }

    /** Called by the game (android/context.c) from its own thread. */
    @Suppress("unused")
    fun onGameContext(mode: Int, primary: Int, flags: Int, detail: Int, keys: String, command: Int) {
        runOnUiThread {
            val context = GameContext(ControlMode.of(mode), primary, flags, detail, keys, command)
            // The keyboard closes when the prompt that needed it ends.
            if (keyboardShown && !context.mode.typesText) toggleKeyboard()
            controls?.setGameContext(context)
        }
    }

    /**
     * Called by the game (android/items.c) when an item's description goes up,
     * with the keys of the commands that can act on it, their labels one per
     * line, its name, and what a quick slot for it would do (empty if not
     * worth one); with empty strings when it goes away.
     */
    @Suppress("unused")
    fun onItemFocus(keys: String, labels: String, name: String, pin: String) {
        val focus = if (keys.isEmpty()) {
            null
        } else {
            ItemFocus(name, keys.toList().zip(labels.split('\n')) { key, label -> Command(label, Action.ItemVerb(key)) }, pin)
        }
        runOnUiThread { controls?.setItemFocus(focus) }
    }

    /**
     * Called by the game (android/pins.c) when the quick slots change: a line
     * per slot, "verb TAB name TAB count TAB usable", empty for an empty slot.
     */
    @Suppress("unused")
    fun onPins(bar: String) {
        val slots = bar.split('\n').map { line ->
            val fields = line.split('\t')
            if (fields.size < 4) null else PinSlot(fields[0], fields[1], fields[2].toIntOrNull() ?: -1, fields[3] == "1")
        }
        runOnUiThread { controls?.setPins(slots) }
    }

    /**
     * Called by the game (android/context.c) when a tap on a monster across
     * the map has made it the target, with its name.
     */
    @Suppress("unused")
    fun onMonsterFocus(name: String) {
        runOnUiThread { controls?.setMonsterFocus(MonsterFocus(name)) }
    }

    /**
     * Called by the game (android/items.c), at the map, with the keys of the
     * item commands that have something to act on.
     */
    @Suppress("unused")
    fun onItemVerbs(keys: String) {
        runOnUiThread { controls?.setItemVerbs(keys) }
    }

    /**
     * Called by the game (android/pins.c) when a pin has been added, with the
     * slot it went to (-1 if none) and what to tell the player.
     */
    @Suppress("unused")
    fun onPinAdded(slot: Int, message: String) {
        runOnUiThread { controls?.pinAdded(slot, message) }
    }

    /** Called by the game (android/context.c) to tell the player something briefly. */
    @Suppress("unused")
    fun onNotice(message: String) {
        runOnUiThread { Toast.makeText(this, message, Toast.LENGTH_LONG).show() }
    }

    /** Called by the game when the grid's geometry changes (drawing pixels). */
    @Suppress("unused")
    fun onGameMetrics(cellWidth: Int, cellHeight: Int, originX: Int, originY: Int, columns: Int, rows: Int, sidebarLeft: Int) {
        runOnUiThread {
            controls?.setGameMetrics(GameMetrics(cellWidth, cellHeight, originX, originY, columns, rows, sidebarLeft))
        }
    }

    /** Back closes the command drawer; otherwise SDL passes it to the game as Escape. */
    override fun dispatchKeyEvent(event: KeyEvent): Boolean {
        if (event.keyCode == KeyEvent.KEYCODE_BACK && controls?.drawerIsOpen == true) {
            if (event.action == KeyEvent.ACTION_UP) controls?.closeDrawer()
            return true
        }
        return super.dispatchKeyEvent(event)
    }

    /**
     * The desktop frontend keeps SDL text input on all the time, so the screen
     * keyboard is disabled in android-main.c and shown only on request here.
     * Typed text reaches the game through SDL's own input connection.
     */
    private fun toggleKeyboard() {
        keyboardShown = !keyboardShown
        if (keyboardShown) {
            // A password-style keyboard commits each character as it is typed;
            // the game takes typed text one character at a time.
            SDLActivity.showTextInput(InputType.TYPE_CLASS_TEXT or InputType.TYPE_TEXT_VARIATION_VISIBLE_PASSWORD or
                InputType.TYPE_TEXT_FLAG_NO_SUGGESTIONS, 0, 0, 1, 1)
        } else {
            sendCommand(COMMAND_TEXTEDIT_HIDE, null)
        }
    }

    private var railOnRight = false
    private var railWidth = 0
    private val cutoutRect = RectF()

    /**
     * The rail of system buttons goes in the camera-cutout strip, which the
     * game cannot use for text anyway; without a side cutout it takes a strip
     * on the left. The map is drawn on under the rail, around the camera, out
     * to the edge of the screen; the game's text (the sidebar, every screen)
     * keeps clear of it, as do the other buttons. Any other cutout is kept
     * clear of.
     */
    private fun applyInsets(insets: WindowInsets) {
        val cutout = if (Build.VERSION.SDK_INT >= 28) insets.displayCutout else null
        var left = cutout?.safeInsetLeft ?: 0
        val top = cutout?.safeInsetTop ?: 0
        var right = cutout?.safeInsetRight ?: 0
        val bottom = cutout?.safeInsetBottom ?: 0
        val minimum = (RAIL_WIDTH_DP * resources.displayMetrics.density).roundToInt()
        railOnRight = right > left
        railWidth = max(if (railOnRight) right else left, minimum)
        if (railOnRight) right = 0 else left = 0
        cutoutRect.setEmpty()
        cutout?.boundingRects?.firstOrNull { !it.isEmpty && (it.left == 0 || it.right >= window.decorView.width - 1) }
            ?.let { cutoutRect.set(Rect(it)) }
        // Before the drawing changes size, so the game fits its text to both at once.
        NativeBridge.setGridInsets(if (railOnRight) 0 else railWidth, if (railOnRight) railWidth else 0)

        val params = SDLActivity.mSurface.layoutParams as RelativeLayout.LayoutParams
        if (params.leftMargin != left || params.topMargin != top || params.rightMargin != right || params.bottomMargin != bottom) {
            params.setMargins(left, top, right, bottom)
            SDLActivity.mSurface.layoutParams = params
        }
        updateFrame()
    }

    private fun updateFrame() {
        val surface = SDLActivity.mSurface ?: return
        if (surface.width <= 0) return
        val drawing = RectF(surface.left.toFloat(), surface.top.toFloat(), surface.right.toFloat(), surface.bottom.toFloat())
        // The other buttons go in the drawing beside the rail, as the game's text does.
        val game = RectF(drawing)
        val rail = if (railOnRight) {
            game.right -= railWidth
            RectF(game.right, 0f, drawing.right, drawing.bottom)
        } else {
            game.left += railWidth
            RectF(drawing.left, 0f, game.left, drawing.bottom)
        }
        controls?.setFrame(game, rail, cutoutRect, drawing)
    }

    /** Maps a position on screen to the game's drawing pixels, or null if outside the game. */
    private fun gamePoint(x: Float, y: Float): Pair<Float, Float>? {
        val surface = SDLActivity.mSurface ?: return null
        val surfaceX = x - surface.left
        val surfaceY = y - surface.top
        if (surfaceX < 0 || surfaceY < 0 || surfaceX >= surface.width || surfaceY >= surface.height) return null
        return Pair(surfaceX, surfaceY)
    }

    private companion object {
        const val RAIL_WIDTH_DP = 56f
        const val LARGE_SCREEN_DP = 600f
    }
}
