/*
 * Quick slots: actions pinned to the bar at the bottom of the map (see
 * CONTROLS.md). Up to three rows of four; the controls show as many rows as
 * the player asks for.
 *
 * A pin is an action the player chose, never a recording of keys:
 *
 *   - an item pin: "Quaff" and a kind of potion. It runs as an item action
 *     does (items.c): the command, answered with the first carried item of
 *     the kind. So it cannot act on something else when the pack changes;
 *     when there is none left it is greyed.
 *   - a spell pin: "Cast" and a spell -- a letter in the list of a kind of
 *     book. The letter goes in only if the game is showing a spell list.
 *   - a command pin: anything in the command drawer, with the keys the drawer
 *     sends (Search, Tunnel, Rest...). One that acts on an item is greyed
 *     when there is nothing for it to act on.
 *   - a zoom pin: the drawer's Zoom in and Zoom out.
 *
 * Pins belong to the character, kept beside its savefile ("<savefile>.pins",
 * which the start screen does not list, as it does not list ".who").
 */

#include <SDL3/SDL.h>
#include <jni.h>

#include "sdl3_local.h"
#include "ormeq-android.h"

#define PIN_MAX 12
#define PIN_KEYS 8

enum { PIN_NONE, PIN_ITEM, PIN_SPELL, PIN_COMMAND, PIN_ZOOM };

struct pin {
    int type;
    int key;            /* PIN_ITEM, PIN_SPELL: the command, original keyset */
    int k_idx;          /* the item's kind, or the spell's book (0: none) */
    int letter;         /* PIN_SPELL: the spell's letter in its list */
    int keys[PIN_KEYS]; /* PIN_COMMAND: the keys the drawer sends */
    int nkeys;
    int notches;        /* PIN_ZOOM */
    char name[48];      /* the kind, the spell, or the command's label */
};

static SDL_Mutex *lock;
static struct pin pins[PIN_MAX];
/* How many slots the screen shows (two rows on a phone, three on a tablet):
 * a new pin goes in one of those. Set from the controls' thread. */
static SDL_AtomicInt shown_slots = { PIN_MAX };
static _Bool changed;            /* to be written and shown */
static char loaded_for[1024];    /* the savefile these belong to */
static char shown[1536];         /* the bar as last sent to the controls */

void ormeq_pins_init(void)
{
    lock = SDL_CreateMutex();
}

static cptr verb_label(int key)
{
    switch (key) {
    case 'q': return "Quaff";
    case 'r': return "Read";
    case 'E': return "Eat";
    case 'u': return "Use";
    case 'a': return "Aim";
    case 'z': return "Zap";
    case 'f': return "Fire";
    case 'A': return "Activate";
    case 'v': return "Throw";
    case 'm': return "Cast";
    case 'U': return "Power";
    default: return "Use";
    }
}

/* How a pin is named to the player: "Quaff Cure Light Wounds", "Search". */
static void pin_title(const struct pin *p, char *buf, size_t size)
{
    if (p->type == PIN_COMMAND || p->type == PIN_ZOOM) my_strcpy(buf, p->name, size);
    else strnfmt(buf, size, "%s %s", verb_label(p->key), p->name);
}

/*
 * The drawer's commands that act on an item; their pins grey out when there
 * is nothing for them to act on. Not Cast, Browse or Study: a caster without
 * books casts all the same. Keep in step with ITEM_VERBS in ControlLayouts.kt.
 */
static _Bool item_bound(const struct pin *p)
{
    return p->nkeys == 2 && p->keys[0] == '\\' && p->keys[1] > 0 && p->keys[1] < 256 &&
        strchr("gdwtkqrEauzAFfv", p->keys[1]) != NULL;
}

/* ---- Kept with the character --------------------------------------------- */

static void pins_path(char *buf, size_t size)
{
    strnfmt(buf, size, "%s.pins", savefile);
}

/* "92,115" into keys. Returns how many. */
static int parse_keys(cptr text, int *keys)
{
    int n = 0;

    while (*text && n < PIN_KEYS) {
        char *end;
        long key = strtol(text, &end, 10);

        if (end == text) break;
        keys[n++] = (int)key;
        text = (*end == ',') ? end + 1 : end;
    }
    return n;
}

/* Game thread, lock held. */
static void load(void)
{
    char path[1024], line[256], list[64];
    FILE *fp;
    int i = 0;

    SDL_zeroa(pins);
    my_strcpy(loaded_for, savefile, sizeof(loaded_for));
    pins_path(path, sizeof(path));
    fp = my_fopen(path, "r");
    if (!fp) return;
    while (i < PIN_MAX && !my_fgets(fp, line, sizeof(line))) {
        struct pin *p = &pins[i++];
        int skip = 0;

        if (sscanf(line, "item %d %d %n", &p->key, &p->k_idx, &skip) == 2 && skip) {
            p->type = PIN_ITEM;
        } else if (sscanf(line, "spell %d %d %d %n", &p->key, &p->k_idx, &p->letter, &skip) == 3 && skip) {
            p->type = PIN_SPELL;
        } else if (sscanf(line, "command %63s %n", list, &skip) == 1 && skip) {
            p->type = PIN_COMMAND;
            p->nkeys = parse_keys(list, p->keys);
            if (!p->nkeys) p->type = PIN_NONE;
        } else if (sscanf(line, "zoom %d %n", &p->notches, &skip) == 1 && skip) {
            p->type = PIN_ZOOM;
        }
        if (p->type == PIN_NONE) {
            SDL_zerop(p);
            continue;
        }
        my_strcpy(p->name, line + skip, sizeof(p->name));
    }
    my_fclose(fp);
}

/* Game thread, lock held. */
static void save(void)
{
    char path[1024];
    FILE *fp;
    int i, k;

    pins_path(path, sizeof(path));
    fp = my_fopen(path, "w");
    if (!fp) return;
    for (i = 0; i < PIN_MAX; i++) {
        struct pin *p = &pins[i];

        switch (p->type) {
        case PIN_ITEM:
            fprintf(fp, "item %d %d %s\n", p->key, p->k_idx, p->name);
            break;
        case PIN_SPELL:
            fprintf(fp, "spell %d %d %d %s\n", p->key, p->k_idx, p->letter, p->name);
            break;
        case PIN_COMMAND:
            fprintf(fp, "command ");
            for (k = 0; k < p->nkeys; k++) fprintf(fp, k ? ",%d" : "%d", p->keys[k]);
            fprintf(fp, " %s\n", p->name);
            break;
        case PIN_ZOOM:
            fprintf(fp, "zoom %d %s\n", p->notches, p->name);
            break;
        default:
            fprintf(fp, "none\n");
            break;
        }
    }
    my_fclose(fp);
}

/* ---- Adding and removing ----------------------------------------------- */

static _Bool same_pin(const struct pin *a, const struct pin *b)
{
    if (a->type != b->type) return 0;
    switch (a->type) {
    case PIN_COMMAND:
        return a->nkeys == b->nkeys && memcmp(a->keys, b->keys, sizeof(int) * a->nkeys) == 0;
    case PIN_ZOOM:
        return a->notches == b->notches;
    default:
        return a->key == b->key && a->k_idx == b->k_idx && a->letter == b->letter;
    }
}

/*
 * Puts a pin in the first free slot the screen shows, unless the same one is
 * there -- or moves it there, from a row this screen does not show (a slot
 * filled on a tablet, seen on a phone). Says what happened in note, and
 * returns the slot it is in, or -1. Lock held.
 */
static int add(const struct pin *pin, char *note, size_t size)
{
    char title[96];
    int i, found = -1, shown = SDL_GetAtomicInt(&shown_slots);

    pin_title(pin, title, sizeof(title));
    for (i = 0; i < PIN_MAX && found < 0; i++) {
        if (same_pin(&pins[i], pin)) found = i;
    }
    if (found >= 0 && found < shown) {
        strnfmt(note, size, "%s is already on the bar.", title);
        return found;
    }
    for (i = 0; i < shown && i < PIN_MAX; i++) {
        if (pins[i].type == PIN_NONE) {
            pins[i] = *pin;
            if (found >= 0) SDL_zero(pins[found]);
            changed = 1;
            strnfmt(note, size, "%s is on the bar.", title);
            return i;
        }
    }
    my_strcpy(note, "The bar is full. Hold a slot to empty it.", size);
    return -1;
}

static jmethodID pins_method, added_method;

static void call_string(JNIEnv *env, jobject activity, jmethodID method, void *data)
{
    jstring text = (*env)->NewStringUTF(env, data);

    (*env)->CallVoidMethod(env, activity, method, text);
    (*env)->DeleteLocalRef(env, text);
}

struct added_args {
    int slot;
    const char *note;
};

static void call_added(JNIEnv *env, jobject activity, jmethodID method, void *data)
{
    struct added_args *a = data;
    jstring note = (*env)->NewStringUTF(env, a->note);

    (*env)->CallVoidMethod(env, activity, method, a->slot, note);
    (*env)->DeleteLocalRef(env, note);
}

/*
 * Adds a pin and tells the controls which slot it went to (so a hidden row
 * with it can be shown) and what to say. Any thread.
 */
static void add_and_tell(const struct pin *pin)
{
    struct added_args args;
    char note[128];

    SDL_LockMutex(lock);
    args.slot = add(pin, note, sizeof(note));
    SDL_UnlockMutex(lock);
    args.note = note;
    ormeq_call_activity("onPinAdded", "(ILjava/lang/String;)V", &added_method, call_added, &args);
    ormeq_wake_game();
}

/* A spell held in its list (context.c). Game thread. */
void ormeq_pins_add_spell(int key, int book, int letter, const char *name)
{
    struct pin pin;

    /* Browsing a book lists what casting it would; studying is different. */
    if (key == 'b') key = 'm';
    if (key != 'm' && key != 'U') {
        struct added_args args;

        args.slot = -1;
        args.note = "Only a spell you can cast goes on the bar.";
        ormeq_call_activity("onPinAdded", "(ILjava/lang/String;)V", &added_method, call_added, &args);
        return;
    }
    SDL_zero(pin);
    pin.type = PIN_SPELL;
    pin.key = key;
    pin.k_idx = key == 'm' ? book : 0;
    pin.letter = letter;
    my_strcpy(pin.name, name, sizeof(pin.name));
    add_and_tell(&pin);
}

/* ---- The bar ------------------------------------------------------------- */

/*
 * At the map: keeps the pins with the character, refreshes what they say,
 * and sends the bar to the controls when it has changed -- a line per slot,
 * "verb TAB name TAB count TAB usable" (count -1: not counted; verb empty for
 * a command, whose label is the name), empty for an empty slot. Game thread.
 */
void ormeq_pins_publish(void)
{
    char bar[1536], line[128];
    int i;

    if (!savefile[0] || !p_ptr || !character_dungeon) return;
    SDL_LockMutex(lock);
    if (strcmp(loaded_for, savefile) != 0) {
        load();
        shown[0] = '\0';
    }
    if (changed) {
        save();
        changed = 0;
    }
    bar[0] = '\0';
    for (i = 0; i < PIN_MAX; i++) {
        struct pin *p = &pins[i];
        int count = -1;
        _Bool usable = 1;
        cptr verb = "";

        switch (p->type) {
        case PIN_ITEM:
            count = ormeq_items_count_kind(p->k_idx);
            usable = count > 0;
            verb = verb_label(p->key);
            break;
        case PIN_SPELL:
            if (p->k_idx) usable = ormeq_items_count_kind(p->k_idx) > 0;
            verb = verb_label(p->key);
            break;
        case PIN_COMMAND:
            if (item_bound(p)) usable = ormeq_items_verb_available(p->keys[1]);
            break;
        }
        if (p->type == PIN_NONE) line[0] = '\0';
        else strnfmt(line, sizeof(line), "%s\t%s\t%d\t%d", verb, p->name, count, usable);
        if (i) my_strcat(bar, "\n", sizeof(bar));
        my_strcat(bar, line, sizeof(bar));
    }
    if (strcmp(bar, shown) == 0) bar[0] = '\0';
    else my_strcpy(shown, bar, sizeof(shown));
    SDL_UnlockMutex(lock);

    /* An empty bar is a line break per empty slot, never "": "" is no change. */
    if (bar[0]) ormeq_call_activity("onPins", "(Ljava/lang/String;)V", &pins_method, call_string, bar);
}

/* ---- From the controls --------------------------------------------------- */

JNIEXPORT void JNICALL Java_com_ormeq_game_NativeBridge_runPin(JNIEnv *env, jclass cls, jint slot)
{
    struct pin pin;

    (void)env;
    (void)cls;
    if (!lock || slot < 0 || slot >= PIN_MAX) return;
    SDL_LockMutex(lock);
    pin = pins[slot];
    SDL_UnlockMutex(lock);
    switch (pin.type) {
    case PIN_ITEM: ormeq_items_run_kind(pin.key, pin.k_idx, 0); break;
    case PIN_SPELL: ormeq_items_run_kind(pin.key, pin.k_idx, pin.letter); break;
    case PIN_COMMAND: ormeq_queue_press(pin.keys, pin.nkeys, 1); break;
    case PIN_ZOOM: ormeq_zoom(pin.notches); break;
    }
}

/* How many slots the controls show; new pins go in those (see add). */
JNIEXPORT void JNICALL Java_com_ormeq_game_NativeBridge_setBarSlots(JNIEnv *env, jclass cls, jint count)
{
    (void)env;
    (void)cls;
    SDL_SetAtomicInt(&shown_slots, count < 1 ? 1 : count > PIN_MAX ? PIN_MAX : count);
}

JNIEXPORT void JNICALL Java_com_ormeq_game_NativeBridge_removePin(JNIEnv *env, jclass cls, jint slot)
{
    (void)env;
    (void)cls;
    if (!lock || slot < 0 || slot >= PIN_MAX) return;
    SDL_LockMutex(lock);
    if (pins[slot].type != PIN_NONE) {
        SDL_zero(pins[slot]);
        changed = 1;
    }
    SDL_UnlockMutex(lock);
    ormeq_wake_game();
}

/* "Add to bar" for the item in focus: its natural action that is worth a slot. */
JNIEXPORT void JNICALL Java_com_ormeq_game_NativeBridge_pinFocusedItem(JNIEnv *env, jclass cls)
{
    struct pin pin;

    (void)env;
    (void)cls;
    if (!lock) return;
    SDL_zero(pin);
    if (!ormeq_items_focus_pin(&pin.key, &pin.k_idx, pin.name, sizeof(pin.name))) return;
    pin.type = PIN_ITEM;
    add_and_tell(&pin);
}

/* A command held in the drawer: its label and the keys it sends. */
JNIEXPORT void JNICALL Java_com_ormeq_game_NativeBridge_pinCommand(JNIEnv *env, jclass cls,
    jstring label, jstring keys)
{
    struct pin pin;
    const char *text;
    const jchar *chars;
    jsize length, i;

    (void)cls;
    if (!lock || !label || !keys) return;
    SDL_zero(pin);
    pin.type = PIN_COMMAND;
    length = (*env)->GetStringLength(env, keys);
    chars = (*env)->GetStringChars(env, keys, NULL);
    if (!chars) return;
    for (i = 0; i < length && pin.nkeys < PIN_KEYS; i++) pin.keys[pin.nkeys++] = chars[i];
    (*env)->ReleaseStringChars(env, keys, chars);
    text = (*env)->GetStringUTFChars(env, label, NULL);
    if (!text) return;
    my_strcpy(pin.name, text, sizeof(pin.name));
    (*env)->ReleaseStringUTFChars(env, label, text);
    if (pin.nkeys) add_and_tell(&pin);
}

/* The drawer's Zoom in or Zoom out. */
JNIEXPORT void JNICALL Java_com_ormeq_game_NativeBridge_pinZoom(JNIEnv *env, jclass cls,
    jstring label, jint notches)
{
    struct pin pin;
    const char *text;

    (void)cls;
    if (!lock || !label) return;
    SDL_zero(pin);
    pin.type = PIN_ZOOM;
    pin.notches = notches;
    text = (*env)->GetStringUTFChars(env, label, NULL);
    if (!text) return;
    my_strcpy(pin.name, text, sizeof(pin.name));
    (*env)->ReleaseStringUTFChars(env, label, text);
    add_and_tell(&pin);
}
