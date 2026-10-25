/*
 * Tells the touch controls what the game is waiting for, and feeds what they
 * press back in as the game's own keys.
 *
 * Every time the game is about to wait for input the frontend calls
 * SDL_WaitEvent or SDL_WaitEventTimeout. The link step wraps both (-Wl,--wrap,
 * see CMakeLists.txt), so this runs on the game thread at exactly the moments
 * the player can act. It reads the game's own account of what it is doing --
 * the prompt stack (change 0003), the screen phase and inkey_flag -- and
 * passes the result to OrmeqActivity.onGameContext() when it changes.
 *
 * Keys from the controls queue up here and go in one per wait through
 * Term_keypress(), exactly as the frontend's -x script player sends them. One
 * per wait matters: the game flushes typed-ahead keys at every -more- and on
 * many failures, so a whole string pushed at once would lose its tail.
 *
 * Taps reach the game as mouse clicks. Menu rows the game declares are
 * answered by the frontend itself; for the rest, sdl3_click_hook brings them
 * here, where a tap on an "a)" line becomes that letter, a tap on a row of a
 * list with a cursor moves the cursor there, a tap on a monster while aiming
 * targets it, and taps that would otherwise reach a prompt as a stray
 * keypress are dropped.
 *
 * The same wrap lets a game left alone stop animating (see watch_input).
 *
 * Keep the numbers in step with ControlLayouts.kt.
 */

#include <SDL3/SDL.h>
#include <jni.h>

#include "sdl3_local.h"
#include "ui_sound.h"
#include "ormeq-android.h"

enum primary_action {
    PRIMARY_REST = 0,
    PRIMARY_GET = 1,
    PRIMARY_DOWN = 2,
    PRIMARY_UP = 3,
    PRIMARY_SHOP = 4,
    PRIMARY_OPEN = 5,
    PRIMARY_ENTER = 6
};

enum context_flag {
    FLAG_CAN_CAST = 1,
    FLAG_CAN_FIRE = 2,
    FLAG_HAS_TARGET = 4,
    FLAG_ROGUELIKE_KEYS = 8,
    FLAG_BIRTH = 16,
    FLAG_IN_PLAY = 32,
    FLAG_MONSTER_IN_VIEW = 64,
    FLAG_HURT = 128,
    FLAG_BLOCKED = 256,
    /* A page of character creation: what it offers besides its rows. */
    FLAG_PAGE_RETURN = 512,
    FLAG_PAGE_CURSOR = 1024,
    FLAG_PAGE_RANDOM = 2048,
    FLAG_PAGE_MORE_INFO = 4096,
    /* One of the game's menus, with a lit row the d-pad moves: OK chooses it,
     * with Return or, on a page of character creation, with right. */
    FLAG_MENU_LIST = 8192,
    FLAG_MENU_RIGHT = 16384,
    /* A page of text (help, a character sheet): OK closes it. */
    FLAG_TEXT_PAGE = 32768,
    /* Another list whose lit row the up and down arrows move. */
    FLAG_ROW_CURSOR = 65536,
    /* A page of options, whose values left and right change. */
    FLAG_OPTION_PAGE = 131072,
    /* The d-pad is the square one, a numpad (option s_deco, change 0016). */
    FLAG_SQUARE_PAD = 262144,
    /* A lettered list with no lit row of its own: the controls light one
     * (find_choices). */
    FLAG_ROW_FOCUS = 524288
};

/* ---- What the game is waiting for ------------------------------------- */

static _Bool in_play(void)
{
    return character_generated && character_dungeon && p_ptr &&
        !p_ptr->is_dead && p_ptr->playing;
}

/* A hostile monster in plain view: the game's own reason to stop resting
 * (monster2.c disturbs as one comes into view). */
static _Bool monster_in_view(void)
{
    int i;

    for (i = 1; i < m_max; i++) {
        monster_type *m_ptr = &m_list[i];

        if (m_ptr->r_idx && (m_ptr->mflag & MFLAG_VIEW) && is_hostile(m_ptr)) return 1;
    }
    return 0;
}

/*
 * A step from the d-pad that went nowhere -- into a wall, say -- stops a held
 * direction (TouchControlsView), or it would walk into the wall for as long as
 * it was held. One into a monster is an attack, and holding goes on attacking.
 * A step fed at the command prompt notes where the player stood and where it
 * was going; by the next wait for input the game has taken it.
 */
static int step_x = -1, step_y = -1, step_to_x, step_to_y;
static _Bool step_blocked;

static void check_step(void)
{
    if (step_x < 0) return;
    if (px == step_x && py == step_y &&
            !(in_bounds2(step_to_y, step_to_x) && cave[step_to_y][step_to_x].m_idx > 0)) {
        step_blocked = 1;
    }
    step_x = step_y = -1;
}

/* Whether the character has lost hit points since the last time this was
 * asked, which is once per report of the context. */
static _Bool hurt_since_last_report(void)
{
    static int last_hp = -1;
    _Bool hurt = last_hp >= 0 && p_ptr->chp < last_hp;

    last_hp = p_ptr->chp;
    return hurt;
}

static _Bool closed_door_adjacent(void)
{
    int d;

    for (d = 0; d < 8; d++) {
        int y = py + ddy_ddd[d];
        int x = px + ddx_ddd[d];

        if (!in_bounds(y, x)) continue;
        /* What the player knows, not what is really there. */
        if (!(cave[y][x].info & CAVE_MARK)) continue;
        if (is_closed_door(get_feat_mimic(&cave[y][x]))) return 1;
    }
    return 0;
}

static int primary_action(void)
{
    cave_type *c_ptr = &cave[py][px];

    if (c_ptr->o_idx) return PRIMARY_GET;
    if (p_ptr->wild_mode) return PRIMARY_DOWN;
    if (cave_have_flag_bold(py, px, FF_MORE)) return PRIMARY_DOWN;
    if (cave_have_flag_bold(py, px, FF_LESS)) return PRIMARY_UP;
    if (cave_have_flag_bold(py, px, FF_QUEST_ENTER)) return PRIMARY_ENTER;
    if (cave_have_flag_bold(py, px, FF_STORE) || cave_have_flag_bold(py, px, FF_BLDG)) {
        return PRIMARY_SHOP;
    }
    if (closed_door_adjacent()) return PRIMARY_OPEN;
    return PRIMARY_REST;
}

static int current_mode(int *detail, cptr *keys)
{
    *detail = 0;
    *keys = NULL;
    switch (ui_screen_prompt()) {
    case UI_PROMPT_MORE: return MODE_MORE;
    case UI_PROMPT_CHECK: *keys = ui_screen_prompt_keys(); return MODE_CHECK;
    case UI_PROMPT_KEY: return MODE_KEY;
    case UI_PROMPT_DIRECTION: return MODE_DIRECTION;
    case UI_PROMPT_AIM: return MODE_AIM;
    case UI_PROMPT_TARGET: return MODE_TARGET;
    case UI_PROMPT_LOOK: return MODE_LOOK;
    case UI_PROMPT_ITEM: *detail = ormeq_items_prompt_detail(); return MODE_ITEM;
    case UI_PROMPT_SPELL: return MODE_SPELL;
    case UI_PROMPT_MENU: return MODE_MENU;
    case UI_PROMPT_TEXT: return MODE_TEXT;
    case UI_PROMPT_NUMBER: *detail = 1; return MODE_TEXT;
    case UI_PROMPT_SHOP: return MODE_SHOP;
    case UI_PROMPT_HOME: *detail = 1; return MODE_SHOP;
    case UI_PROMPT_CARD: *keys = ui_screen_prompt_keys(); return MODE_QUEST_CARD;
    default: break;
    }
    if (ui_screen_phase() != UI_PHASE_WORLD) return MODE_NONE;
    if (inkey_flag && ui_screen_map_owns()) return MODE_MAP;
    /* Past Reckonings, unless its help or exact working is open over it:
     * those are ordinary documents. */
    if (reckoner_history_active() && !doc_viewer_active()) return MODE_RECKONING;
    return MODE_MENU;
}

/*
 * The rows the document on screen declares to the frontend, which are what a
 * tap chooses from: where each is and its key, whether one of them is lit,
 * and whether one is "*", Random. Seen on their way past, as the frontend
 * sees them.
 */
#define PAGE_ROWS_MAX 64

static struct { int x, y, width; char key; } page_rows[PAGE_ROWS_MAX];
static int page_row_count;
static bool page_row_lit, page_random;

extern bool __real_ui_event_menu_clear(void);
extern bool __real_ui_event_menu_item(int x, int y, int width, char key);
extern bool __real_ui_event_menu_select(int x, int y, int width);

bool __wrap_ui_event_menu_clear(void)
{
    page_row_count = 0;
    page_row_lit = page_random = FALSE;
    return __real_ui_event_menu_clear();
}

bool __wrap_ui_event_menu_item(int x, int y, int width, char key)
{
    if (key == '*') page_random = TRUE;
    if (page_row_count < PAGE_ROWS_MAX) {
        page_rows[page_row_count].x = x;
        page_rows[page_row_count].y = y;
        page_rows[page_row_count].width = width;
        page_rows[page_row_count].key = key;
        page_row_count++;
    }
    return __real_ui_event_menu_item(x, y, width, key);
}

bool __wrap_ui_event_menu_select(int x, int y, int width)
{
    page_row_lit = TRUE;
    return __real_ui_event_menu_select(x, y, width);
}

/* Whether a row of the screen shows this text. */
static bool screen_shows(cptr text)
{
    char line[256];
    int x, y, cols = MIN(sdl3.cols, (int)sizeof(line) - 1);

    for (y = 0; y < sdl3.rows; y++) {
        for (x = 0; x < cols; x++) {
            byte a;
            char c;

            if (Term_what(x, y, &a, &c) || !c) c = ' ';
            line[x] = c;
        }
        line[cols] = '\0';
        if (strstr(line, text)) return TRUE;
    }
    return FALSE;
}

/*
 * A page of character creation that goes on with Return, as its footer says
 * ("OK)" or "OK accepts", as android/touch-text.c words the desktop's RET):
 * the summary, the stats, the split personality. Its rows are changes to
 * make, and OK means go on.
 */
static bool page_takes_return(void)
{
    return screen_shows("OK) ") || screen_shows("OK accepts");
}

/*
 * Which of the game's two menu readers it is waiting in, if either: the
 * Settings and Escape menus' (and the start screen's), where Return chooses
 * the lit row, or most pages of character creation's, where right does
 * (Return belongs to the page). The race and class lists read keys
 * themselves, and light their current choice, which Return takes.
 */
enum { MENU_NONE, MENU_RETURN, MENU_RIGHT };
static int menu_reader;

/* The key of the row showing the page's current choice, which pages of
 * character creation draw in light blue (the personality, the game speed,
 * a patron, a realm...), or 0. */
static char current_choice_key(void)
{
    int i, x;

    for (i = 0; i < page_row_count; i++) {
        for (x = page_rows[i].x; x < page_rows[i].x + page_rows[i].width; x++) {
            byte a;
            char c;

            if (Term_what(x, page_rows[i].y, &a, &c)) break;
            if (a == TERM_L_BLUE && c != ' ') return page_rows[i].key;
        }
    }
    return 0;
}

/*
 * A menu shows a lit row from the start, so OK always has something to
 * choose and the d-pad something to move (Ammerow's way; CONTROLS.md). The
 * game lights none until an arrow is pressed: on the desktop the bar belongs
 * to the keyboard, beside the mouse's own highlight, and a touch screen has
 * no pointer to show one. Lights the page's current choice where it shows
 * one, else the first row, and says whether it did: the caller then draws
 * the page again, as after an arrow. Not where Return goes on.
 */
static bool light_a_row(doc_ptr doc)
{
    int ct = doc_hotspot_count(doc), choice = 0, i;
    char key;

    if (ct <= 0 || doc_selected(doc) >= 0) return FALSE;
    if (menu_reader == MENU_RIGHT && page_takes_return()) return FALSE;
    key = menu_reader == MENU_RIGHT ? current_choice_key() : 0;
    for (i = 0; key && i < ct; i++) {
        if (doc_hotspot_key(doc, i) == key) {
            choice = i;
            break;
        }
    }
    doc_select(doc, choice);
    return TRUE;
}

/*
 * Every menu's wait for a key (ui_menu_hook, change 0015): with its document
 * before the wait, NULL after. A link-time wrap of the readers would miss the
 * Settings and Escape menus, whose calls to them the compiler inlines. Both
 * readers return 0 for a key they kept (an arrow), and every menu then draws
 * itself again: so it does for a row just lit.
 */
static bool menu_hook(doc_ptr doc, bool claim_return)
{
    if (!doc) {
        menu_reader = MENU_NONE;
        return FALSE;
    }
    menu_reader = claim_return ? MENU_RETURN : MENU_RIGHT;
    if (!light_a_row(doc)) return FALSE;
    menu_reader = MENU_NONE;
    return TRUE;
}

/*
 * One of the game's menus, its lit row for OK to choose and the d-pad to
 * move: not a page of character creation that goes on with Return.
 */
static bool menu_list(void)
{
    if (menu_reader == MENU_NONE || page_row_count == 0) return FALSE;
    return menu_reader == MENU_RETURN || !page_takes_return();
}

/*
 * What a page of character creation offers besides its rows, as the page
 * itself says (see CONTROLS.md). Return, where its footer offers it, or where
 * the page lights its own current choice, which Return takes (the race and
 * class lists). Random, where a "*" row is listed. More information, where
 * the race and class summary is shown (its first line ends "Game Speed:"),
 * which Tab steps through. The other pages are lists, whose lit row OK
 * chooses (FLAG_MENU_LIST). In 0.1.0 an OK that did nothing there made the
 * first page, the type of game, look frozen; 0.1.1 took it away, and a
 * player then missed it.
 */
static int birth_page_flags(void)
{
    int flags = 0;

    if (page_takes_return()) flags |= FLAG_PAGE_RETURN;
    if (page_row_lit && menu_reader == MENU_NONE) flags |= FLAG_PAGE_CURSOR;
    if (page_random) flags |= FLAG_PAGE_RANDOM;
    if (screen_shows("Game Speed:")) flags |= FLAG_PAGE_MORE_INFO;
    return flags;
}

/*
 * A page of options, as its footer says (android/touch-text.c's words):
 * left and right change the lit one, which a sideways swipe sends, in
 * character creation too, where elsewhere right chooses (TouchControlsView).
 * Character creation's Birth options is one, opened from its summary.
 */
static bool option_page(void)
{
    return screen_shows("swipe sideways to change it");
}

/*
 * A list whose lit row the up and down arrows move, other than a menu that
 * lights one for OK (FLAG_MENU_LIST): a page of character creation that goes
 * on with Return, whose arrows light a row and move it; the option and
 * knowledge lists, as their footers say (in android/touch-text.c's words);
 * Past Reckonings' list on a small screen; and the quest journal's list. A
 * drag there moves the row with the finger, as in a menu; on a page of text
 * it scrolls the text (TouchControlsView).
 */
static bool row_cursor(void)
{
    if (menu_reader != MENU_NONE && page_row_count > 0) return TRUE;
    if (screen_shows("swipe to move") || option_page()) return TRUE;
    if (screen_shows("Tap a row to read its results.")) return TRUE;
    return page_row_lit && screen_shows("Quest journal");
}

/*
 * The square d-pad (option s_deco, change 0016) is the player's, not a
 * character's: the app remembers the last setting and gives it here at
 * startup (TouchControlsView), and every character, new or loaded, keeps to
 * it. A change on the options page, or in Settings (touch_settings_key),
 * becomes the setting; anything else that changes the option -- a save made
 * with the other value, a pref file -- gives way to it. -1 until there is one
 * to remember.
 */
static SDL_AtomicInt square_pad_setting = { -1 };
static int square_pad_seen = -1;

static void keep_square_pad(bool options)
{
    int setting = SDL_GetAtomicInt(&square_pad_setting);

    if (setting < 0)
        SDL_CompareAndSwapAtomicInt(&square_pad_setting, -1, s_deco ? 1 : 0);
    else if ((s_deco ? 1 : 0) != square_pad_seen && options)
        SDL_SetAtomicInt(&square_pad_setting, s_deco ? 1 : 0);
    else
        s_deco = setting ? TRUE : FALSE;
    square_pad_seen = s_deco ? 1 : 0;
}

/*
 * Settings' Touch section, through the desktop's hooks for a host's own rows
 * (0.1.3), on the start screen and in play alike: the d-pad's shape, the
 * option s_deco by the name its player gave it. The desktop keeps p for it.
 */
static void touch_settings_rows(doc_ptr doc)
{
    doc_newline(doc);
    ui_settings_section(doc, "Touch");
    ui_settings_row(doc, 'p', "s_deco", s_deco ? "square/Numpad shaped" : "round");
}

static bool touch_settings_key(int key)
{
    if (key != 'p' && key != 'P') return FALSE;
    s_deco = !s_deco;
    /* The player's choice, as on the options page: it becomes the setting. */
    SDL_SetAtomicInt(&square_pad_setting, s_deco ? 1 : 0);
    square_pad_seen = s_deco ? 1 : 0;
    return TRUE;
}

/*
 * A lettered list on screen -- items, spells, a shop's stock, a building's
 * services -- that lights no row of its own, as the game's menus and cursors
 * do: the controls light one, which the d-pad and a drag move and OK takes
 * (FLAG_ROW_FOCUS), and a tap anywhere along a row takes it (tapped_label).
 * Read off the screen, as a tap's label always was (ormeq_label_at): "a)"
 * labels down a column. An item, spell or shop list may be one row long;
 * elsewhere it takes two, so that a stray "a)" in some text is not a list.
 * Column by column, top to bottom, as a long spell list runs on in a second
 * column.
 */
#define CHOICES_MAX 64
static struct { int x, y, end; char key; } choices[CHOICES_MAX];
static int choice_count;
/* Its page and how many there are, as the list says ("Page 1 of 2"); 0 if
 * it has one. */
static int choice_page, choice_pages;

static void find_page(void)
{
    char line[256];
    int x, y, cols = MIN(sdl3.cols, (int)sizeof(line) - 1);

    choice_page = choice_pages = 0;
    for (y = 0; y < sdl3.rows; y++) {
        cptr at;

        for (x = 0; x < cols; x++) {
            byte a;
            char c;

            if (Term_what(x, y, &a, &c) || !c) c = ' ';
            line[x] = c;
        }
        line[cols] = '\0';
        at = strstr(line, "Page ");
        if (at && sscanf(at, "Page %d of %d", &choice_page, &choice_pages) == 2) return;
        choice_page = choice_pages = 0;
    }
}

/* An "a)" label at (x, y): a letter, ")", and a space or "|" (or the edge of
 * the screen) before it -- a letter, as for a tap (ormeq_label_at): "(Page 2
 * of 2)" is no row. */
static bool label_cell(int x, int y, char *key)
{
    byte a;
    char c, close, before = ' ';

    if (x + 1 >= sdl3.cols) return FALSE;
    if (Term_what(x, y, &a, &c) || Term_what(x + 1, y, &a, &close)) return FALSE;
    if (!isalpha((unsigned char)c) || close != ')') return FALSE;
    if (x > 0 && !Term_what(x - 1, y, &a, &before) && before && before != ' ' && before != '|')
        return FALSE;
    *key = c;
    return TRUE;
}

/* The questions whose answer is a row of the list on screen. */
static bool list_prompt(void)
{
    return ui_screen_prompt_within(UI_PROMPT_ITEM) || ui_screen_prompt_within(UI_PROMPT_SPELL) ||
        ui_screen_prompt_within(UI_PROMPT_MENU) || ui_screen_prompt_within(UI_PROMPT_SHOP) ||
        ui_screen_prompt_within(UI_PROMPT_HOME);
}

static void find_choices(int mode, int flags)
{
    static struct { int x, y; char key; } found[CHOICES_MAX * 2];
    int n = 0, max = (int)(sizeof(found) / sizeof(found[0]));
    int least, i, j, x, y;

    choice_count = 0;
    switch (mode) {
    case MODE_ITEM:
    case MODE_SPELL:
    case MODE_SHOP:
        least = 1;
        break;
    case MODE_MENU:
    case MODE_KEY:
        least = list_prompt() ? 1 : 2;
        break;
    default:
        return;
    }
    if (flags & (FLAG_BIRTH | FLAG_MENU_LIST | FLAG_ROW_CURSOR | FLAG_TEXT_PAGE | FLAG_OPTION_PAGE)) return;
    /* An item's description is up, not the list (items.c), or something over
     * the list waits for any key (a shop's description of a ware). */
    if (ormeq_items_focused()) return;
    if (screen_shows("Any Key") || screen_shows("any key") || screen_shows("Tap anywhere to go back")) return;

    /* Row 0 is the message line: a question there is no list. */
    for (y = 1; y < sdl3.rows && n < max; y++) {
        for (x = 0; x + 1 < sdl3.cols && n < max; x++) {
            char key;

            if (!label_cell(x, y, &key)) continue;
            found[n].x = x;
            found[n].y = y;
            found[n].key = key;
            n++;
            x++;
        }
    }
    for (x = 0; x < sdl3.cols && choice_count < CHOICES_MAX; x++) {
        int count = 0;

        for (i = 0; i < n; i++) if (found[i].x == x) count++;
        if (count < least) continue;
        for (i = 0; i < n && choice_count < CHOICES_MAX; i++) {
            int limit = sdl3.cols, end = x + 1;

            if (found[i].x != x) continue;
            /* The row runs to the end of its text, short of a label beside it. */
            for (j = 0; j < n; j++) {
                if (found[j].y == found[i].y && found[j].x > x && found[j].x < limit) limit = found[j].x;
            }
            for (j = x + 2; j < limit; j++) {
                byte a;
                char c;

                if (!Term_what(j, found[i].y, &a, &c) && c && c != ' ') end = j;
            }
            choices[choice_count].x = x;
            choices[choice_count].y = found[i].y;
            choices[choice_count].end = end;
            choices[choice_count].key = found[i].key;
            choice_count++;
        }
    }
    if (choice_count) find_page();
}

/* The choice on a tapped row: the nearest to the left of the tap, or the
 * row's first, so that a tap anywhere along the row takes it. 0 if none. */
static int row_choice_at(int col, int row)
{
    int i, best = -1, first = -1;

    for (i = 0; i < choice_count; i++) {
        if (choices[i].y != row) continue;
        if (first < 0 || choices[i].x < choices[first].x) first = i;
        if (choices[i].x <= col + 1 && (best < 0 || choices[i].x > choices[best].x)) best = i;
    }
    if (best < 0) best = first;
    return best < 0 ? 0 : (unsigned char)choices[best].key;
}

static void current_context(int *mode, int *primary, int *flags, int *detail, cptr *keys)
{
    bool options = option_page();

    *mode = current_mode(detail, keys);
    *primary = PRIMARY_REST;
    *flags = 0;
    if (ui_screen_phase() == UI_PHASE_BIRTH) {
        *flags |= FLAG_BIRTH;
        /* Not on Birth options: it declares no rows of its own, so the rows
         * seen are the summary's, under it -- Random among them. */
        if (*mode == MODE_NONE && !options) *flags |= birth_page_flags();
    }
    if (options) *flags |= FLAG_OPTION_PAGE;
    /* On every screen, the start screen's and character creation's too: an
     * option keeps the value the last character left until another is
     * loaded (the game sets defaults once, at startup). */
    keep_square_pad(options);
    if (s_deco) *flags |= FLAG_SQUARE_PAD;
    if (menu_list()) {
        *flags |= FLAG_MENU_LIST;
        if (menu_reader == MENU_RIGHT) *flags |= FLAG_MENU_RIGHT;
    } else if (doc_viewer_active()) {
        *flags |= FLAG_TEXT_PAGE;
    } else if (row_cursor()) {
        *flags |= FLAG_ROW_CURSOR;
    }
    find_choices(*mode, *flags);
    if (choice_count > 0) *flags |= FLAG_ROW_FOCUS;
    if (!in_play()) return;

    *flags |= FLAG_IN_PLAY;
    if (get_caster_info()) *flags |= FLAG_CAN_CAST;
    /* Fire, only when it would shoot something: a launcher, and ammunition
     * for it (items.c). */
    if (ormeq_items_can_fire()) *flags |= FLAG_CAN_FIRE;
    if (target_okay()) *flags |= FLAG_HAS_TARGET;
    /* A held Wait stops for these, as resting does (TouchControlsView). */
    if (monster_in_view()) *flags |= FLAG_MONSTER_IN_VIEW;
    if (hurt_since_last_report()) *flags |= FLAG_HURT;
    if (step_blocked) *flags |= FLAG_BLOCKED;
    step_blocked = 0;
    if (rogue_like_commands) *flags |= FLAG_ROGUELIKE_KEYS;
    if (*mode == MODE_MAP) *primary = primary_action();

    /* The fourth run in a session without a walk in between asks whether to
     * turn on online_macros -- a question about keyboards on an online
     * server. Holding the d-pad runs, so a touch player would meet it for
     * nothing. Walking once has the same effect (cmd2.c). */
    if (run_count < 5) run_count = 5;
}

static jmethodID context_method, metrics_method;

void ormeq_call_activity(const char *name, const char *signature, jmethodID *cache,
    void (*call)(JNIEnv *, jobject, jmethodID, void *), void *data)
{
    JNIEnv *env = (JNIEnv *)SDL_GetAndroidJNIEnv();
    jobject activity = (jobject)SDL_GetAndroidActivity();

    if (!env || !activity) return;
    if (!*cache) {
        jclass activity_class = (*env)->GetObjectClass(env, activity);
        *cache = (*env)->GetMethodID(env, activity_class, name, signature);
        (*env)->DeleteLocalRef(env, activity_class);
    }
    if (*cache) call(env, activity, *cache, data);
    if ((*env)->ExceptionCheck(env)) (*env)->ExceptionClear(env);
    (*env)->DeleteLocalRef(env, activity);
}

struct context_args { int mode, primary, flags, detail; cptr keys; int command; };

static void call_context(JNIEnv *env, jobject activity, jmethodID method, void *data)
{
    struct context_args *a = data;
    jstring keys = (*env)->NewStringUTF(env, a->keys ? a->keys : "");

    (*env)->CallVoidMethod(env, activity, method, a->mode, a->primary, a->flags,
        a->detail, keys, a->command);
    (*env)->DeleteLocalRef(env, keys);
}

static void call_metrics(JNIEnv *env, jobject activity, jmethodID method, void *data)
{
    int *m = data;

    (*env)->CallVoidMethod(env, activity, method, m[0], m[1], m[2], m[3], m[4], m[5], m[6]);
}

/* The menu's rows: their count, then four ints each. */
static void call_rows(JNIEnv *env, jobject activity, jmethodID method, void *data)
{
    int *rows = data;
    jintArray array = (*env)->NewIntArray(env, rows[0] * 4);

    if (!array) return;
    (*env)->SetIntArrayRegion(env, array, 0, rows[0] * 4, (const jint *)(rows + 1));
    (*env)->CallVoidMethod(env, activity, method, array);
    (*env)->DeleteLocalRef(env, array);
}

/*
 * Where the rows of the menu on screen are, in drawing pixels, so that the
 * controls can keep a list's d-pad off them (on the start screen they lie in
 * its corner).
 */
static void publish_rows(void)
{
    static jmethodID rows_method;
    static int last[1 + PAGE_ROWS_MAX * 4] = { -1 };
    int rows[1 + PAGE_ROWS_MAX * 4];
    int i, ox, oy;

    sdl3_grid_origin(&ox, &oy);
    rows[0] = page_row_count;
    for (i = 0; i < page_row_count; i++) {
        rows[1 + i * 4] = ox + page_rows[i].x * sdl3.cell_w;
        rows[2 + i * 4] = oy + page_rows[i].y * sdl3.cell_h;
        rows[3 + i * 4] = ox + (page_rows[i].x + page_rows[i].width) * sdl3.cell_w;
        rows[4 + i * 4] = oy + (page_rows[i].y + 1) * sdl3.cell_h;
    }
    if (memcmp(rows, last, sizeof(int) * (1 + page_row_count * 4)) == 0) return;
    memcpy(last, rows, sizeof(int) * (1 + page_row_count * 4));
    ormeq_call_activity("onMenuRows", "([I)V", &rows_method, call_rows, rows);
}

static void call_ints(JNIEnv *env, jobject activity, jmethodID method, void *data)
{
    int *ints = data;   /* ints[0], then that many values */
    jintArray array = (*env)->NewIntArray(env, ints[0]);

    if (!array) return;
    (*env)->SetIntArrayRegion(env, array, 0, ints[0], (const jint *)(ints + 1));
    (*env)->CallVoidMethod(env, activity, method, array);
    (*env)->DeleteLocalRef(env, array);
}

/*
 * The rows of a lettered list the controls light (find_choices): its page and
 * how many there are, then five ints a row: left, top, right and bottom in
 * drawing pixels, and the row's key. Sent before the context that says to
 * light one.
 */
static void publish_choices(void)
{
    static jmethodID choices_method;
    static int last[3 + CHOICES_MAX * 5] = { -1 };
    int ints[3 + CHOICES_MAX * 5];
    int i, ox, oy;

    sdl3_grid_origin(&ox, &oy);
    ints[0] = 2 + choice_count * 5;
    ints[1] = choice_count ? choice_page : 0;
    ints[2] = choice_count ? choice_pages : 0;
    for (i = 0; i < choice_count; i++) {
        int *c = ints + 3 + i * 5;

        c[0] = ox + choices[i].x * sdl3.cell_w;
        c[1] = oy + choices[i].y * sdl3.cell_h;
        c[2] = ox + (choices[i].end + 1) * sdl3.cell_w;
        c[3] = oy + (choices[i].y + 1) * sdl3.cell_h;
        c[4] = (unsigned char)choices[i].key;
    }
    if (memcmp(ints, last, sizeof(int) * (1 + ints[0])) == 0) return;
    memcpy(last, ints, sizeof(int) * (1 + ints[0]));
    ormeq_call_activity("onChoiceRows", "([I)V", &choices_method, call_ints, ints);
}

static void publish_context(void)
{
    static int last_mode = -1, last_primary = -1, last_flags = -1, last_detail = -1;
    static int last_command = -1;
    static char last_keys[16];
    static int last_metrics[7];
    struct context_args args;
    int metrics[7];
    rect_t side;

    if (!SDL_IsMainThread()) return;

    current_context(&args.mode, &args.primary, &args.flags, &args.detail, &args.keys);
    publish_choices();
    if (args.mode == MODE_MAP) {
        ormeq_items_at_map();
        ormeq_items_publish_available();
        ormeq_pins_publish();
    }
    /* Which command the screen now up belongs to (0 at the map), so a rail
     * button pressed again can close the screen it opened. */
    args.command = ormeq_items_last_command();
    if (args.mode != last_mode || args.primary != last_primary ||
            args.flags != last_flags || args.detail != last_detail ||
            args.command != last_command ||
            strcmp(args.keys ? args.keys : "", last_keys) != 0) {
        last_mode = args.mode;
        last_primary = args.primary;
        last_flags = args.flags;
        last_detail = args.detail;
        last_command = args.command;
        my_strcpy(last_keys, args.keys ? args.keys : "", sizeof(last_keys));
        ormeq_call_activity("onGameContext", "(IIIILjava/lang/String;I)V", &context_method,
            call_context, &args);
    }

    /* Where the grid is and how big a cell is, so the controls can keep clear
     * of the status line and the message line; and where the sidebar starts,
     * because its list of conditions grows down towards the bottom corner. */
    metrics[0] = sdl3.cell_w;
    metrics[1] = sdl3.cell_h;
    sdl3_grid_origin(&metrics[2], &metrics[3]);
    metrics[4] = sdl3.cols;
    metrics[5] = sdl3.rows;
    side = ui_char_info_rect();
    metrics[6] = side.cx > 0 ? metrics[2] + side.x * sdl3.cell_w : -1;
    if (memcmp(metrics, last_metrics, sizeof(metrics)) != 0) {
        memcpy(last_metrics, metrics, sizeof(metrics));
        ormeq_call_activity("onGameMetrics", "(IIIIIII)V", &metrics_method, call_metrics, metrics);
    }
    publish_rows();
}

/* ---- Keys from the controls ------------------------------------------- */

/*
 * A queue of keys, each tagged with the press it came from. A press of
 * several keys is a sequence: should a -more- or a question turn up part-way
 * through, the rest of it is dropped rather than typed into that.
 */
#define QUEUE_MAX 512
#define MAP_BACKS_MAX 4

struct queued_key {
    int key;
    int press;
    int expect;    /* a mode the key may only go into, or 0 */
};

static struct queued_key queue[QUEUE_MAX];
static int queue_head, queue_tail;
static SDL_Mutex *queue_lock;
static int press_counter;
static int map_press;        /* a press waiting to get back to the map first */
static int map_backs;
static int fed_press;        /* the press the last key came from */
static int fed_count;        /* how many of its keys have gone in */
static Uint32 wake_event;

static void wake_game(void)
{
    SDL_Event event;

    SDL_zero(event);
    event.type = wake_event;
    SDL_PushEvent(&event);
}

void ormeq_wake_game(void)
{
    if (wake_event) wake_game();
}

/*
 * The arrows and page keys, which are not characters: the controls send the
 * game's own codes for them (SKEY_*), and they go in as the frontend makes a
 * real key press of them -- the macro trigger ^_[name], all in one go -- so
 * the pref files turn them into what the desktop would get: inkey_special()
 * sees the arrow, plain inkey() the keypad digit.
 */
static cptr special_key_name(int key)
{
    switch (key) {
    case SKEY_DOWN: return "down";
    case SKEY_LEFT: return "left";
    case SKEY_RIGHT: return "right";
    case SKEY_UP: return "up";
    case SKEY_PGUP: return "page_up";
    case SKEY_PGDOWN: return "page_down";
    case SKEY_TOP: return "home";
    case SKEY_BOTTOM: return "end";
    default: return NULL;
    }
}

static void press_key(int key)
{
    cptr name = special_key_name(key);

    if (!name) {
        Term_keypress(key);
        return;
    }
    Term_keypress(31);
    Term_keypress('[');
    while (*name) Term_keypress(*name++);
    Term_keypress(']');
}

static void drop_press(int press)
{
    int read = queue_head, write = queue_head;

    while (read != queue_tail) {
        if (queue[read].press != press) {
            queue[write] = queue[read];
            write = (write + 1) % QUEUE_MAX;
        }
        read = (read + 1) % QUEUE_MAX;
    }
    queue_tail = write;
    if (map_press == press) map_press = 0;
}

/* One key per wait. Returns whether one went in. Game thread. */
static _Bool feed_one_key(void)
{
    int mode, detail, key, count;
    cptr keys;
    struct queued_key next;

    SDL_LockMutex(queue_lock);
    if (queue_head == queue_tail) {
        SDL_UnlockMutex(queue_lock);
        return 0;
    }
    next = queue[queue_head];
    mode = current_mode(&detail, &keys);

    /* A command from the rail means something else inside another screen:
     * press Back until the map is reached, a few times at most. */
    if (next.press == map_press) {
        if (mode != MODE_MAP) {
            if (mode != MODE_NONE && map_backs++ < MAP_BACKS_MAX) {
                SDL_UnlockMutex(queue_lock);
                Term_keypress(ESCAPE);
                return 1;
            }
            drop_press(next.press);
            SDL_UnlockMutex(queue_lock);
            return 0;
        }
        map_press = 0;
    }

    /* KEY_NOTHING: a press that only went back to the map (backToMap). */
    if (next.key == KEY_NOTHING) {
        queue_head = (queue_head + 1) % QUEUE_MAX;
        SDL_UnlockMutex(queue_lock);
        return 0;
    }

    /* Part-way through a sequence, a -more- or a question means something
     * went other than planned: drop the rest rather than type it in there.
     * So does a key meant for one kind of prompt finding another. */
    if ((next.press == fed_press && fed_count > 0 &&
            (mode == MODE_MORE || mode == MODE_CHECK)) ||
            (next.expect && mode != next.expect)) {
        drop_press(next.press);
        SDL_UnlockMutex(queue_lock);
        return 0;
    }

    queue_head = (queue_head + 1) % QUEUE_MAX;
    if (next.press != fed_press) {
        fed_press = next.press;
        fed_count = 0;
    }
    fed_count++;
    key = next.key;
    count = fed_count;
    SDL_UnlockMutex(queue_lock);

    /* A step of its own at the command prompt: see check_step. */
    if (mode == MODE_MAP && count == 1 && key >= '1' && key <= '9' && key != '5') {
        step_x = px;
        step_y = py;
        step_to_x = px + ddx[key - '0'];
        step_to_y = py + ddy[key - '0'];
        step_blocked = 0;
    }
    /* A press of a touch control is a fresh press, as a key pressed on the
     * desktop is: the next bump into a wall may sound again. Its keys go in
     * here rather than as SDL key events, which is where the desktop tells
     * (ui_sound_input; a held key's repeats do not). */
    if (count == 1) ui_sound_input(FALSE);
    press_key(key);
    /* Which command is running, and whether an item action's command is
     * about to ask for its item (items.c). */
    ormeq_items_key_fed(next.press, count, key, mode);
    return 1;
}

/* Queues keys as one press, and returns which press it is. Any thread. */
int ormeq_queue_press(const int *keys, int count, _Bool map_command)
{
    return ormeq_queue_press_expect(keys, NULL, count, map_command);
}

int ormeq_queue_press_expect(const int *keys, const int *expect, int count, _Bool map_command)
{
    int i, press;

    SDL_LockMutex(queue_lock);
    press = ++press_counter;
    for (i = 0; i < count; i++) {
        int next = (queue_tail + 1) % QUEUE_MAX;

        if (next == queue_head) break;
        queue[queue_tail].key = keys[i];
        queue[queue_tail].press = press;
        queue[queue_tail].expect = expect ? expect[i] : 0;
        queue_tail = next;
    }
    if (map_command) {
        map_press = press;
        map_backs = 0;
    }
    SDL_UnlockMutex(queue_lock);
    wake_game();
    return press;
}

/* Runs at every wait for input. Returns whether the wait should be skipped
 * because a key has just gone in. */
static _Bool on_wait(void)
{
    check_step();
    if (feed_one_key()) return 1;
    publish_context();
    return 0;
}

static _Bool fake_event(SDL_Event *event)
{
    if (event) {
        SDL_zero(*event);
        event->type = wake_event;
    }
    return 1;
}

/*
 * Idle pacing. The surface's weather and the start screen animate on a timer:
 * waiting for input, the frontend wakes every 40-50 ms to draw the next frame
 * (the only timed wait it has), for as long as the game is left alone -- a few
 * percent of a core, which a phone put down on a table pays for in battery.
 * After a minute with no touch and no key, the picture is left as it is until
 * the next one. The animations advance a frame per wake and do not catch up,
 * so nothing jumps when they start again.
 */
#define IDLE_PAUSE_MS 60000

static SDL_AtomicInt last_input;   /* SDL_GetTicks() of the last input, mod 2^32 */

static _Bool SDLCALL watch_input(void *userdata, SDL_Event *event)
{
    (void)userdata;
    switch (event->type) {
    case SDL_EVENT_KEY_DOWN:
    case SDL_EVENT_TEXT_INPUT:
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    case SDL_EVENT_MOUSE_MOTION:
    case SDL_EVENT_MOUSE_WHEEL:
    case SDL_EVENT_FINGER_DOWN:
        SDL_SetAtomicInt(&last_input, (int)SDL_GetTicks());
        break;
    default:
        /* The controls' own keys come with a wake event. */
        if (event->type == wake_event) SDL_SetAtomicInt(&last_input, (int)SDL_GetTicks());
        break;
    }
    return 1;
}

static _Bool idle(void)
{
    return (Uint32)SDL_GetTicks() - (Uint32)SDL_GetAtomicInt(&last_input) > IDLE_PAUSE_MS;
}

_Bool __real_SDL_WaitEvent(SDL_Event *event);
_Bool __real_SDL_WaitEventTimeout(SDL_Event *event, Sint32 timeoutMS);

/* Waits for an event, however long -- but while an audio device is open after
 * a sound, no longer than it takes to close it (sound.c). */
static _Bool wait_for_event(SDL_Event *event)
{
    int quiet;

    while ((quiet = ormeq_sound_update()) >= 0) {
        if (__real_SDL_WaitEventTimeout(event, quiet > 0 ? quiet : 1)) return 1;
    }
    return __real_SDL_WaitEvent(event);
}

_Bool __wrap_SDL_WaitEvent(SDL_Event *event)
{
    if (on_wait()) return fake_event(event);
    return wait_for_event(event);
}

_Bool __wrap_SDL_WaitEventTimeout(SDL_Event *event, Sint32 timeoutMS)
{
    if (on_wait()) return fake_event(event);
    if (idle()) return wait_for_event(event);
    ormeq_sound_update();
    return __real_SDL_WaitEventTimeout(event, timeoutMS);
}

/* ---- Taps -------------------------------------------------------------- */

/*
 * The letter of the label nearest the left of a tapped cell on that row, or
 * 0. Lettered lists everywhere in the game are drawn "a)", including the ones
 * that do not yet declare their rows to the frontend: spell lists, shop stock
 * and the older menus. The document viewer draws its links "[a]".
 */
int ormeq_label_at(int col, int row, int *at)
{
    int x, best = 0;

    for (x = 0; x + 1 < sdl3.cols && x <= col + 1; x++) {
        byte a, a2, a0;
        char c, close, before = ' ';

        if (Term_what(x, row, &a, &c) || Term_what(x + 1, row, &a2, &close)) break;
        if (!isalpha((unsigned char)c)) continue;
        if (x > 0) Term_what(x - 1, row, &a0, &before);
        if ((close == ')' && (before == ' ' || before == '|' || x == 0)) ||
                (close == ']' && before == '[')) {
            best = c;
            if (at) *at = x;
        }
    }
    return best;
}

static int label_at(int col, int row)
{
    return ormeq_label_at(col, row, NULL);
}

/* A tapped list row's key: anywhere along a lit list's row, else the label
 * to the left of the tap. */
static int tapped_label(int col, int row)
{
    int key = row_choice_at(col, row);

    return key ? key : label_at(col, row);
}

/*
 * The stat rows of character creation are labelled "s/S)": the small letter
 * lowers the stat and the capital raises it. Returns the small letter of such
 * a label on the row, or 0.
 */
static int stat_label_at(int row)
{
    int x;

    for (x = 0; x + 3 < sdl3.cols; x++) {
        byte a;
        char c[4];
        int i;

        for (i = 0; i < 4; i++) {
            if (Term_what(x + i, row, &a, &c[i])) return 0;
        }
        if (islower((unsigned char)c[0]) && c[1] == '/' &&
                c[2] == toupper((unsigned char)c[0]) && c[3] == ')') {
            return c[0];
        }
    }
    return 0;
}

/*
 * Lists with a cursor -- the options, the knowledge browsers -- draw it as a
 * '>' before the chosen row and a space before the others.
 */
static _Bool is_cursor_mark(int x, int y)
{
    byte a;
    char c, next, before = ' ';

    if (Term_what(x, y, &a, &c) || c != '>') return 0;
    if (Term_what(x + 1, y, &a, &next) || next == ' ') return 0;
    if (x > 0) Term_what(x - 1, y, &a, &before);
    return before == ' ';
}

static _Bool is_list_row(int x, int y)
{
    byte a;
    char c, next;

    if (Term_what(x, y, &a, &c) || Term_what(x + 1, y, &a, &next)) return 0;
    return c == ' ' && next != ' ';
}

/* Whether the screen reads text at (x, y). */
static _Bool text_at(int x, int y, cptr text)
{
    for (; *text; x++, text++) {
        byte a;
        char c;

        if (Term_what(x, y, &a, &c) || c != *text) return 0;
    }
    return 1;
}

/*
 * A tap on another row of such a list moves the cursor there with the arrows,
 * as many as it takes; every row in between must belong to the same list.
 * The knowledge browsers head their left-hand list "Groups" and step through
 * it with left and right. Returns whether it moved.
 */
static _Bool move_list_cursor(int col, int row)
{
    int keys[64];
    int x, y, mark_x = -1, mark_y = -1, step, count, key;
    _Bool groups = 0;

    /* The cursor of the list the tap is in: the nearest mark to its left. */
    for (y = 0; y < sdl3.rows; y++) {
        for (x = 0; x + 1 < sdl3.cols && x <= col; x++) {
            if (x > mark_x && is_cursor_mark(x, y)) {
                mark_x = x;
                mark_y = y;
            }
        }
    }
    if (mark_x < 0 || mark_y == row) return 0;

    step = row > mark_y ? 1 : -1;
    count = (row - mark_y) * step;
    if (count > (int)(sizeof(keys) / sizeof(keys[0]))) return 0;
    for (y = mark_y + step; ; y += step) {
        if (!is_list_row(mark_x, y)) return 0;
        if (y == row) break;
    }

    for (y = 0; y < mark_y && !groups; y++) groups = text_at(mark_x, y, "Groups");
    if (groups) key = step > 0 ? SKEY_RIGHT : SKEY_LEFT;
    else key = step > 0 ? SKEY_DOWN : SKEY_UP;
    for (x = 0; x < count; x++) keys[x] = key;
    ormeq_queue_press(keys, count, 0);
    return 1;
}

/* The dungeon grid under a tap, from the frontend's own transform. */
static _Bool grid_at(int col, int row, float px_in, float py_in, int *gy, int *gx)
{
    if (sdl3_map_is_ours()) {
        sdl3_map_view v;
        int lvl_w, lvl_h;

        sdl3_map_view_get(&v);
        ui_map_size(&lvl_w, &lvl_h);
        if (v.scale_x <= 0.0f || v.scale_y <= 0.0f) return 0;
        *gx = (int)((px_in - v.origin_x) / v.scale_x);
        *gy = (int)((py_in - v.origin_y) / v.scale_y);
        if (*gx < 0 || *gy < 0 || *gx >= lvl_w || *gy >= lvl_h) return 0;
    } else {
        point_t pt = ui_xy_to_cave_pt(col, row);

        *gx = pt.x;
        *gy = pt.y;
    }
    return in_bounds(*gy, *gx);
}

/* Whether the player has seen what is at a grid: whether the map shows
 * anything there. */
static _Bool grid_known(int gy, int gx)
{
    byte a;
    char c;

    return ui_map_cell(gx, gy, &a, &c) && c != ' ';
}

/* While aiming: a tap on a monster targets it, a tap elsewhere targets the
 * spot, and the aim then uses that target, as 't' would. */
static _Bool aim_at(int gy, int gx)
{
    int m_idx = cave[gy][gx].m_idx;

    if (m_idx > 0 && target_able(m_idx)) {
        health_track(m_idx);
        target_who = m_idx;
    } else {
        target_who = -1;
    }
    target_row = gy;
    target_col = gx;
    Term_keypress('t');
    return 1;
}

/* The direction of an adjacent grid, as a keypad digit, or 0. */
static int direction_to(int gy, int gx)
{
    int dy = gy - py, dx = gx - px;

    if (dy < -1 || dy > 1 || dx < -1 || dx > 1 || (dy == 0 && dx == 0)) return 0;
    return 5 + dx - 3 * dy;
}

/*
 * A long press on a screen of choices (the controls send it as the X2 mouse
 * button, which the frontend has no use for). On a spell it puts the spell in
 * a quick slot. In character creation it asks for help on the choice -- the
 * capital letter, as the screens' tips say -- and on a stat it lowers it.
 */
static void hold_on(int col, int row)
{
    int key, x = 0;

    /* A spell in a spell list: a quick slot for it (pins.c). Its name is the
     * text after its label, up to the next column. */
    if (ui_screen_prompt() == UI_PROMPT_SPELL) {
        char name[48];
        int n = 0;

        key = ormeq_label_at(col, row, &x);
        if (!key) return;
        for (x += 3; x < sdl3.cols && n + 1 < (int)sizeof(name); x++) {
            byte a;
            char c, next = ' ';

            if (Term_what(x, row, &a, &c)) break;
            Term_what(x + 1, row, &a, &next);
            if (c == ' ' && next == ' ') break;
            name[n++] = c;
        }
        name[n] = '\0';
        ormeq_pins_add_spell(ormeq_items_last_command(), ormeq_items_last_book(), key, name);
        return;
    }

    if (ui_screen_prompt() != UI_PROMPT_NONE || ui_screen_phase() != UI_PHASE_BIRTH) return;
    if ((key = stat_label_at(row)) != 0) {
        Term_keypress(key);
        return;
    }
    key = label_at(col, row);
    if (islower(key)) Term_keypress(toupper(key));
}

static jmethodID monster_method, notice_method;

/* Calls an activity method that takes one string. */
static void call_string(JNIEnv *env, jobject activity, jmethodID method, void *data)
{
    jstring text = (*env)->NewStringUTF(env, data);

    (*env)->CallVoidMethod(env, activity, method, text);
    (*env)->DeleteLocalRef(env, text);
}

void ormeq_notice(const char *text)
{
    ormeq_call_activity("onNotice", "(Ljava/lang/String;)V", &notice_method, call_string, (void *)text);
}

/*
 * A tap on a monster across the map, at the command prompt: rather than walk
 * up to it, it becomes the target, and the controls offer what can be done to
 * it (see CONTROLS.md). A monster next to the player is still attacked by a
 * tap, as walking into it does; one that cannot be targeted is walked to.
 */
static _Bool focus_monster(int gy, int gx)
{
    int m_idx = cave[gy][gx].m_idx;
    char name[80];

    if (m_idx <= 0 || !m_list[m_idx].ml || !target_able(m_idx)) return 0;
    if (distance(py, px, gy, gx) <= 1) return 0;
    health_track(m_idx);
    target_who = m_idx;
    target_row = gy;
    target_col = gx;
    monster_desc(name, &m_list[m_idx], 0);
    name[0] = toupper((unsigned char)name[0]);
    ormeq_call_activity("onMonsterFocus", "(Ljava/lang/String;)V", &monster_method, call_string, name);
    return 1;
}

static bool click_hook(int col, int row, float px_in, float py_in, int button)
{
    int gy, gx, key;

    if (button == SDL_BUTTON_X2) {
        hold_on(col, row);
        return TRUE;
    }
    /* "Walk to" from a monster's focus: travel there, as a tap would have. */
    if (button == SDL_BUTTON_X1) {
        if (ui_screen_prompt() == UI_PROMPT_NONE && inkey_flag && ui_screen_map_owns() &&
                grid_at(col, row, px_in, py_in, &gy, &gx)) {
            ui_mouse_click_cave(gx, gy, 1);
        }
        return TRUE;
    }
    if (button != SDL_BUTTON_LEFT) return FALSE;

    switch (ui_screen_prompt()) {
    case UI_PROMPT_MORE:
        /* Anywhere continues. */
        Term_keypress(' ');
        return TRUE;

    case UI_PROMPT_AIM:
        if (grid_at(col, row, px_in, py_in, &gy, &gx)) return aim_at(gy, gx);
        return TRUE;

    case UI_PROMPT_DIRECTION:
        if (grid_at(col, row, px_in, py_in, &gy, &gx) && (key = direction_to(gy, gx))) {
            Term_keypress('0' + key);
        }
        return TRUE;

    case UI_PROMPT_SHOP:
    case UI_PROMPT_HOME:
        key = tapped_label(col, row);
        if (key) {
            /* At the counter a letter means nothing: buy (or take) that row.
             * Inside "Buy which item?" the letter is the answer. */
            if (!msg_line_waiting()) Term_keypress('p');
            Term_keypress(key);
        }
        return TRUE;

    case UI_PROMPT_ITEM:
        /* An item's description is up (items.c): a tap anywhere puts it
         * away, as any key does. */
        if (ormeq_items_focused()) {
            Term_keypress(ESCAPE);
            return TRUE;
        }
        key = tapped_label(col, row);
        if (key) Term_keypress(key);
        return TRUE;

    case UI_PROMPT_SPELL:
    case UI_PROMPT_MENU:
    case UI_PROMPT_KEY:
    case UI_PROMPT_CARD:    /* a questgiver's card: its own buttons */
        key = tapped_label(col, row);
        if (key) Term_keypress(key);
        return TRUE;

    case UI_PROMPT_NONE:
        /* At the command prompt the map is the frontend's: a tap travels --
         * except onto a monster across the map, which it focuses, and onto a
         * square never seen, where it does nothing. Travel would set off into
         * the unknown, and such a tap is nearly always a finger that missed a
         * button. */
        if (inkey_flag && ui_screen_map_owns()) {
            /* Off the level or on its outer wall: nowhere to go. */
            if (!grid_at(col, row, px_in, py_in, &gy, &gx)) return TRUE;
            if (focus_monster(gy, gx)) return TRUE;
            return !grid_known(gy, gx);
        }
        /* A stat in character creation: a tap raises it. */
        if (ui_screen_phase() == UI_PHASE_BIRTH && (key = stat_label_at(row)) != 0) {
            Term_keypress(toupper(key));
            return TRUE;
        }
        key = tapped_label(col, row);
        if (key) Term_keypress(key);
        else move_list_cursor(col, row);
        return TRUE;

    default:
        /* Yes/no, typing, targeting: a tap is never an answer. */
        return TRUE;
    }
}

/* ---- JNI ---------------------------------------------------------------- */

void ormeq_android_init(void)
{
    queue_lock = SDL_CreateMutex();
    wake_event = SDL_RegisterEvents(1);
    SDL_SetAtomicInt(&last_input, (int)SDL_GetTicks());
    SDL_AddEventWatch(watch_input, NULL);
    sdl3_click_hook = click_hook;
    sdl3_look_label_hook = ormeq_touch_label;
    ui_menu_hook = menu_hook;
    ui_settings_host_rows = touch_settings_rows;
    ui_settings_host_key = touch_settings_key;
    ui_touch = TRUE;
    ormeq_items_init();
    ormeq_pins_init();
}

/*
 * Queues keys for the game: characters are the game's own key codes (ESC is
 * 27, Return 13, Ctrl-X 24, the arrows SKEY_UP and the rest). A map command
 * first backs out of whatever screen is open, as a rail button must.
 */
JNIEXPORT void JNICALL Java_com_ormeq_game_NativeBridge_pressKeys(JNIEnv *env, jclass cls,
    jstring keys, jboolean map_command)
{
    const jchar *chars;
    int codes[QUEUE_MAX];
    jsize length, i;

    (void)cls;
    if (!keys || !queue_lock) return;
    length = (*env)->GetStringLength(env, keys);
    chars = (*env)->GetStringChars(env, keys, NULL);
    if (!chars) return;
    for (i = 0; i < length && i < QUEUE_MAX; i++) {
        if (chars[i] > 255 && !special_key_name(chars[i])) break;
        codes[i] = chars[i];
    }
    (*env)->ReleaseStringChars(env, keys, chars);
    ormeq_queue_press(codes, i, map_command);
}

/* Forgets keys not yet sent (a released d-pad, say). */
JNIEXPORT void JNICALL Java_com_ormeq_game_NativeBridge_cancelKeys(JNIEnv *env, jclass cls)
{
    (void)env;
    (void)cls;
    if (!queue_lock) return;
    SDL_LockMutex(queue_lock);
    queue_head = queue_tail = 0;
    map_press = 0;
    SDL_UnlockMutex(queue_lock);
}

static SDL_WindowID game_window(void)
{
    int count = 0;
    SDL_Window **windows = SDL_GetWindows(&count);
    SDL_WindowID window = (windows && count > 0) ? SDL_GetWindowID(windows[0]) : 0;

    SDL_free(windows);
    return window;
}

/* A tap on the game, as a mouse click at drawing pixel (x, y). */
JNIEXPORT void JNICALL Java_com_ormeq_game_NativeBridge_click(JNIEnv *env, jclass cls,
    jfloat x, jfloat y, jint button)
{
    SDL_Event event;

    (void)env;
    (void)cls;
    /* No motion first: motion moves menu highlights and the look card. */
    SDL_zero(event);
    event.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
    event.button.windowID = game_window();
    event.button.button = (Uint8)button;
    event.button.down = 1;
    event.button.clicks = 1;
    event.button.x = x;
    event.button.y = y;
    SDL_PushEvent(&event);
    event.type = SDL_EVENT_MOUSE_BUTTON_UP;
    event.button.down = 0;
    SDL_PushEvent(&event);
}

/* The pointer, without a click: shows the look card for what is under it.
 * A position off the window takes it away. */
JNIEXPORT void JNICALL Java_com_ormeq_game_NativeBridge_hover(JNIEnv *env, jclass cls,
    jfloat x, jfloat y)
{
    SDL_Event event;

    (void)env;
    (void)cls;
    SDL_zero(event);
    event.type = SDL_EVENT_MOUSE_MOTION;
    event.motion.windowID = game_window();
    event.motion.x = x;
    event.motion.y = y;
    SDL_PushEvent(&event);
}

/*
 * How far the controls reach in over each edge of the game's grid, in pixels;
 * the map camera keeps the player out from under them (change 0002).
 */
JNIEXPORT void JNICALL Java_com_ormeq_game_NativeBridge_setMapInsets(JNIEnv *env, jclass cls,
    jint left, jint top, jint right, jint bottom)
{
    (void)env;
    (void)cls;
    sdl3_map_set_insets(left, top, right, bottom);
    wake_game();
}

/*
 * The strips at the left and right that the game's text keeps clear of, in
 * pixels: the rail's. The map goes on under them (change 0007).
 */
JNIEXPORT void JNICALL Java_com_ormeq_game_NativeBridge_setGridInsets(JNIEnv *env, jclass cls,
    jint left, jint right)
{
    (void)env;
    (void)cls;
    sdl3_set_grid_insets(left, right);
    ormeq_wake_game();   /* this can come before the game is running */
}

/* The square d-pad setting the app remembers (keep_square_pad). Any thread. */
JNIEXPORT void JNICALL Java_com_ormeq_game_NativeBridge_setSquarePad(JNIEnv *env, jclass cls,
    jboolean square)
{
    (void)env;
    (void)cls;
    SDL_SetAtomicInt(&square_pad_setting, square ? 1 : 0);
}

/* Zoom by notches of the mouse wheel, as the desktop does: + in, - out. Any thread. */
void ormeq_zoom(int notches)
{
    SDL_Event event;
    int i;

    SDL_zero(event);
    event.type = SDL_EVENT_MOUSE_WHEEL;
    event.wheel.windowID = game_window();
    event.wheel.y = notches > 0 ? 1.0f : -1.0f;
    event.wheel.integer_y = notches > 0 ? 1 : -1;
    for (i = 0; i < (notches > 0 ? notches : -notches); i++) SDL_PushEvent(&event);
}

JNIEXPORT void JNICALL Java_com_ormeq_game_NativeBridge_zoom(JNIEnv *env, jclass cls,
    jint notches)
{
    (void)env;
    (void)cls;
    ormeq_zoom(notches);
}

/*
 * Back out of whatever screen is open, to the map, and do nothing there: a
 * rail button pressed again closes the screen it opened.
 */
JNIEXPORT void JNICALL Java_com_ormeq_game_NativeBridge_backToMap(JNIEnv *env, jclass cls)
{
    int nothing = KEY_NOTHING;

    (void)env;
    (void)cls;
    if (queue_lock) ormeq_queue_press(&nothing, 1, 1);
}
