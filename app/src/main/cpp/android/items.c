/*
 * What can be done with an item, for touch (see CONTROLS.md).
 *
 * The Inventory and Equipment screens show an item's description when it is
 * tapped, and change 0005 says which item that is while the description is up
 * (ui_obj_focus_hook). Its actions are the item commands whose own prompt
 * filter accepts it, in the order of the table below; they go to the touch
 * controls as the focus.
 *
 * Choosing one backs out to the map, runs the command in the original keyset
 * and answers the command's "which item?" prompt with this item directly:
 * obj_prompt is wrapped at link time (CMakeLists.txt). The item is checked
 * again against that prompt first -- still where it was, still the same kind,
 * accepted by the prompt's own filter, in a place the prompt looks -- and the
 * prompt runs as normal otherwise. No item letters are involved, so a pack
 * that has changed cannot make it pick something else.
 *
 * A quick slot (pins.c) runs the same way, with any carried item of its kind.
 */

#include <SDL3/SDL.h>
#include <jni.h>

#include "sdl3_local.h"
#include "ormeq-android.h"

#define AT(where) (1 << (where))

struct item_verb {
    char key;            /* the command, in the original keyset */
    cptr label;
    obj_p accepts;       /* the command prompt's own filter; NULL: any item */
    int where;           /* the places that prompt offers items from */
    _Bool pins;          /* worth a quick slot: it uses the item up or sets it off */
};

static bool can_refill(obj_ptr obj);
static bool can_study(obj_ptr obj);
static bool can_fire_with(obj_ptr obj);

/*
 * The item commands, most particular first: the first that applies to an item
 * is its natural action, the large button. Pick up only ever applies on the
 * floor, where it is the natural one; Activate and Take off only to what is
 * worn; Study only while there is a spell to learn, when it comes before Cast.
 * Fire is on the ammunition, which answers its question, and on the launcher
 * being wielded while there is ammunition for it (its command then asks which;
 * not for the bar, where the ammunition's own Fire goes).
 */
static const struct item_verb verbs[] = {
    { 'g', "Pick up",  NULL,                 AT(INV_FLOOR), 0 },
    { 'f', "Fire",     obj_can_shoot,        AT(INV_QUIVER) | AT(INV_PACK) | AT(INV_FLOOR), 1 },
    { 'f', "Fire",     can_fire_with,        AT(INV_EQUIP), 0 },
    { 'A', "Activate", obj_can_activate,     AT(INV_EQUIP), 1 },
    { 't', "Take off", NULL,                 AT(INV_EQUIP) | AT(INV_QUIVER), 0 },
    { 'w', "Wear",     obj_can_wield,        AT(INV_PACK) | AT(INV_FLOOR), 0 },
    { 'q', "Quaff",    obj_can_quaff,        AT(INV_PACK) | AT(INV_FLOOR), 1 },
    { 'r', "Read",     obj_can_read,         AT(INV_PACK) | AT(INV_FLOOR), 1 },
    { 'E', "Eat",      obj_can_eat,          AT(INV_PACK) | AT(INV_FLOOR), 1 },
    { 'u', "Use",      obj_is_staff,         AT(INV_PACK) | AT(INV_FLOOR), 1 },
    { 'a', "Aim",      obj_is_wand,          AT(INV_PACK) | AT(INV_FLOOR), 1 },
    { 'z', "Zap",      obj_is_rod,           AT(INV_PACK) | AT(INV_FLOOR), 1 },
    { 'F', "Refill",   can_refill,           AT(INV_PACK) | AT(INV_FLOOR) | AT(INV_EQUIP), 0 },
    { 'G', "Study",    can_study,            AT(INV_PACK) | AT(INV_FLOOR), 0 },
    { 'm', "Cast",     obj_is_readable_book, AT(INV_PACK) | AT(INV_FLOOR), 1 },
    { 'b', "Browse",   obj_is_readable_book, AT(INV_PACK) | AT(INV_FLOOR), 0 },
    { 'v', "Throw",    NULL,                 AT(INV_PACK) | AT(INV_EQUIP) | AT(INV_QUIVER) | AT(INV_FLOOR), 1 },
    { 'd', "Drop",     NULL,                 AT(INV_PACK) | AT(INV_EQUIP) | AT(INV_QUIVER), 0 },
    { 'k', "Destroy",  obj_can_destroy,      AT(INV_PACK) | AT(INV_EQUIP) | AT(INV_QUIVER) | AT(INV_FLOOR), 0 },
};

#define VERB_COUNT ((int)(sizeof(verbs) / sizeof(verbs[0])))

/* The fuel the wielded light takes, as do_cmd_refill() decides it, or NULL. */
static obj_p fuel_filter(void)
{
    slot_t slot = equip_find_obj(TV_LITE, SV_ANY);
    obj_ptr lite = slot ? equip_obj(slot) : NULL;

    if (!lite) return NULL;
    if (lite->sval == SV_LITE_LANTERN) return obj_can_refill_lantern;
    if (lite->sval == SV_LITE_TORCH) return obj_is_torch;
    return NULL;
}

/*
 * Refill is for the light being wielded: offered on the light itself while
 * there is fuel for it in the pack (its command then asks which), and on the
 * fuel, which answers that question.
 */
static bool can_refill(obj_ptr obj)
{
    obj_p fuel = fuel_filter();

    if (!fuel) return FALSE;
    if (obj->loc.where == INV_EQUIP) return obj->tval == TV_LITE && pack_find_first(fuel) != 0;
    return fuel(obj);
}

/* Study only while there is a spell to learn. */
static bool can_study(obj_ptr obj)
{
    return p_ptr->new_spells > 0 && obj_can_learn_spell(obj);
}

/* "Wear" is for armour; weapons, launchers, diggers and lights are wielded,
 * rings and amulets put on, and ammunition goes in the quiver. */
static cptr wield_label(obj_ptr obj)
{
    if (obj_is_ammo(obj)) return "Quiver";
    if (object_is_melee_weapon(obj) || obj_is_shooter(obj) ||
            obj->tval == TV_DIGGING || obj->tval == TV_LITE) {
        return "Wield";
    }
    if (obj->tval == TV_RING || obj->tval == TV_AMULET) return "Put on";
    return "Wear";
}

static _Bool verb_applies(const struct item_verb *verb, obj_ptr obj)
{
    if (!(verb->where & AT(obj->loc.where))) return 0;
    return !verb->accepts || verb->accepts(obj);
}

/* An item's name for a quick slot: what the player knows of its kind, one of
 * them, without what the action already says ("Potion of" under Quaff). */
static void short_name(obj_ptr obj, int key, char *buf, size_t size)
{
    char name[MAX_NLEN];
    cptr of = NULL;

    object_desc(name, obj, OD_NAME_ONLY | OD_OMIT_PREFIX | OD_SINGULAR);
    if (strchr("qrauz", key)) of = strstr(name, " of ");
    my_strcpy(buf, of ? of + 4 : name, size);
}

/* ---- Items by place or by kind ------------------------------------------ */

/*
 * An item to give a command's prompt: one place (the item in focus), or the
 * first item of a kind the prompt will take (a quick slot). Written on the
 * game thread (the hook, the prompt) and read by the controls' thread
 * (choosing an action), so under a lock; the objects themselves are only
 * touched on the game thread.
 */
struct item_ref {
    _Bool set;
    _Bool by_kind;
    int where, slot, x, y;
    int k_idx;
};

static SDL_Mutex *lock;
static struct item_ref focus;
static struct {
    struct item_ref item;
    int press;           /* the keys that run its command */
    _Bool armed;         /* its command key has gone in */
} pending;

/* The command running now, as the controls queued it, and the spellbook its
 * prompt was given, for a quick slot made from its spell list. Game thread. */
static int last_command, last_book;
static _Bool raw_next;

/* What "Add to bar" would pin for the focused item. */
static struct {
    _Bool set;
    int key, k_idx;
    char name[48];
} focus_pin;

/* The item at a place, as the prompts see it: floor items under the player. */
static obj_ptr item_at(const struct item_ref *ref)
{
    obj_ptr obj = NULL;

    switch (ref->where) {
    case INV_PACK: obj = pack_obj(ref->slot); break;
    case INV_EQUIP: obj = equip_obj(ref->slot); break;
    case INV_QUIVER: obj = quiver_obj(ref->slot); break;
    case INV_FLOOR:
        if (ref->slot <= 0 || ref->slot >= max_o_idx) return NULL;
        obj = &o_list[ref->slot];
        if (obj->loc.where != INV_FLOOR || obj->loc.x != px || obj->loc.y != py) return NULL;
        break;
    default: return NULL;
    }
    if (!obj || obj->k_idx != ref->k_idx) return NULL;
    return obj;
}

/* The first item of a kind (0: of any kind) in one place that filter (if
 * any) accepts. */
static obj_ptr first_of_kind(int where, int k_idx, obj_p filter)
{
    int slot, max;

    if (where == INV_FLOOR) {
        s16b o_idx = cave[py][px].o_idx;

        while (o_idx) {
            obj_ptr obj = &o_list[o_idx];

            if (obj->k_idx && (!k_idx || obj->k_idx == k_idx) && (!filter || filter(obj))) return obj;
            o_idx = obj->next_o_idx;
        }
        return NULL;
    }
    max = where == INV_PACK ? pack_max() : where == INV_EQUIP ? equip_max() :
        where == INV_QUIVER ? quiver_max() : 0;
    for (slot = 1; slot <= max; slot++) {
        obj_ptr obj = where == INV_PACK ? pack_obj(slot) :
            where == INV_EQUIP ? equip_obj(slot) : quiver_obj(slot);

        if (obj && obj->k_idx && (!k_idx || obj->k_idx == k_idx) && (!filter || filter(obj))) return obj;
    }
    return NULL;
}

/*
 * Ammunition the wielded launcher can shoot, where the fire command looks for
 * it (do_cmd_fire: the quiver, the pack, underfoot), or NULL. Game thread.
 */
static obj_ptr ammo_to_fire(void)
{
    static const int places[] = { INV_QUIVER, INV_PACK, INV_FLOOR };
    obj_ptr obj = NULL;
    int i;

    for (i = 0; i < 3 && !obj; i++) obj = first_of_kind(places[i], 0, obj_can_shoot);
    return obj;
}

/* The launcher being wielded, while there is ammunition for it. */
static bool can_fire_with(obj_ptr obj)
{
    return obj_is_shooter(obj) && ammo_to_fire() != NULL;
}

/* Whether Fire would shoot anything: a launcher wielded, ammunition for it,
 * and not on the wilderness map, where the fire command does nothing. */
_Bool ormeq_items_can_fire(void)
{
    return p_ptr && !p_ptr->wild_mode && ammo_to_fire() != NULL;
}

/* ---- What there is to act on ------------------------------------------- */

static jmethodID verbs_method;

static void call_keys(JNIEnv *env, jobject activity, jmethodID method, void *data)
{
    jstring keys = (*env)->NewStringUTF(env, data);

    (*env)->CallVoidMethod(env, activity, method, keys);
    (*env)->DeleteLocalRef(env, keys);
}

static void mark_available(obj_ptr obj, _Bool *have)
{
    int i;

    if (!obj || !obj->k_idx) return;
    for (i = 0; i < VERB_COUNT; i++) {
        if (!have[i] && verb_applies(&verbs[i], obj)) have[i] = 1;
    }
}

/*
 * At the map: tells the controls which item commands have something to act
 * on -- carried, worn, in the quiver or underfoot -- when that changes, so
 * the command drawer can leave out the rest. Game thread.
 */
/* The item commands with something to act on, as last worked out at the map. */
static char available[VERB_COUNT + 1];

_Bool ormeq_items_verb_available(int key)
{
    return key > 0 && key < 256 && strchr(available, key) != NULL;
}

_Bool ormeq_items_is_verb(int key)
{
    int i;

    for (i = 0; i < VERB_COUNT; i++) {
        if (verbs[i].key == key) return 1;
    }
    return 0;
}

void ormeq_items_publish_available(void)
{
    static char shown[VERB_COUNT + 2] = "?";
    _Bool have[VERB_COUNT];
    char keys[VERB_COUNT + 1];
    int i, slot, n = 0;
    s16b o_idx;

    if (!character_dungeon || !p_ptr) return;
    SDL_zeroa(have);
    for (slot = 1; slot <= pack_max(); slot++) mark_available(pack_obj(slot), have);
    for (slot = 1; slot <= equip_max(); slot++) mark_available(equip_obj(slot), have);
    for (slot = 1; slot <= quiver_max(); slot++) mark_available(quiver_obj(slot), have);
    for (o_idx = cave[py][px].o_idx; o_idx; o_idx = o_list[o_idx].next_o_idx) {
        mark_available(&o_list[o_idx], have);
    }
    for (i = 0; i < VERB_COUNT; i++) {
        keys[n] = '\0';
        if (have[i] && !strchr(keys, verbs[i].key)) keys[n++] = verbs[i].key;
    }
    keys[n] = '\0';
    my_strcpy(available, keys, sizeof(available));
    if (strcmp(keys, shown) == 0) return;
    my_strcpy(shown, keys, sizeof(shown));
    ormeq_call_activity("onItemVerbs", "(Ljava/lang/String;)V", &verbs_method, call_keys, keys);
}

int ormeq_items_count_kind(int k_idx)
{
    int where[] = { INV_PACK, INV_QUIVER, INV_EQUIP };
    int i, slot, total = 0;

    for (i = 0; i < 3; i++) {
        int max = where[i] == INV_PACK ? pack_max() : where[i] == INV_QUIVER ? quiver_max() : equip_max();

        for (slot = 1; slot <= max; slot++) {
            obj_ptr obj = where[i] == INV_PACK ? pack_obj(slot) :
                where[i] == INV_QUIVER ? quiver_obj(slot) : equip_obj(slot);

            if (obj && obj->k_idx == k_idx) total += obj->number;
        }
    }
    return total;
}

/* ---- The item in focus -------------------------------------------------- */

static jmethodID focus_method;

struct focus_args {
    const char *keys, *labels, *name, *pin;
};

static void call_focus(JNIEnv *env, jobject activity, jmethodID method, void *data)
{
    struct focus_args *a = data;
    jstring keys = (*env)->NewStringUTF(env, a->keys);
    jstring labels = (*env)->NewStringUTF(env, a->labels);
    jstring name = (*env)->NewStringUTF(env, a->name);
    jstring pin = (*env)->NewStringUTF(env, a->pin);

    (*env)->CallVoidMethod(env, activity, method, keys, labels, name, pin);
    (*env)->DeleteLocalRef(env, keys);
    (*env)->DeleteLocalRef(env, labels);
    (*env)->DeleteLocalRef(env, name);
    (*env)->DeleteLocalRef(env, pin);
}

/* Tells the controls the focused item's actions: their keys, their labels one
 * per line, the item's name, and what a quick slot for it would say (empty
 * if it is not worth one). Empty strings: no focus. */
static void publish_focus(obj_ptr obj)
{
    char keys[VERB_COUNT + 1];
    char labels[VERB_COUNT * 12];
    char name[MAX_NLEN];
    char pin[80];
    struct focus_args args;
    int i, n = 0;

    keys[0] = labels[0] = name[0] = pin[0] = '\0';
    SDL_LockMutex(lock);
    SDL_zero(focus_pin);
    if (obj) {
        for (i = 0; i < VERB_COUNT; i++) {
            if (!verb_applies(&verbs[i], obj)) continue;
            /* Only the natural action -- the first -- goes on the bar, and
             * only if it is one worth a slot: a potion's Quaff, never a
             * breastplate's Throw. */
            if (n == 0 && verbs[i].pins) {
                focus_pin.set = 1;
                focus_pin.key = verbs[i].key;
                focus_pin.k_idx = obj->k_idx;
                short_name(obj, verbs[i].key, focus_pin.name, sizeof(focus_pin.name));
                strnfmt(pin, sizeof(pin), "%s %s", verbs[i].label, focus_pin.name);
            }
            keys[n++] = verbs[i].key;
            keys[n] = '\0';
            if (labels[0]) my_strcat(labels, "\n", sizeof(labels));
            my_strcat(labels, verbs[i].key == 'w' ? wield_label(obj) : verbs[i].label,
                sizeof(labels));
        }
        object_desc(name, obj, OD_OMIT_INSCRIPTION);
    }
    SDL_UnlockMutex(lock);
    args.keys = keys;
    args.labels = labels;
    args.name = name;
    args.pin = pin;
    ormeq_call_activity("onItemFocus",
        "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;)V",
        &focus_method, call_focus, &args);
}

/* Change 0005: the item whose description is up, then NULL. Game thread. */
static void focus_hook(obj_ptr obj)
{
    SDL_LockMutex(lock);
    SDL_zero(focus);
    if (obj) {
        focus.set = 1;
        focus.where = obj->loc.where;
        focus.slot = obj->loc.slot;
        focus.x = obj->loc.x;
        focus.y = obj->loc.y;
        focus.k_idx = obj->k_idx;
    }
    SDL_UnlockMutex(lock);
    publish_focus(obj);
}

_Bool ormeq_items_focused(void)
{
    _Bool set;

    SDL_LockMutex(lock);
    set = focus.set;
    SDL_UnlockMutex(lock);
    return set;
}

int ormeq_items_focus_pin(int *key, int *k_idx, char *name, size_t size)
{
    int set;

    SDL_LockMutex(lock);
    set = focus_pin.set;
    if (set) {
        *key = focus_pin.key;
        *k_idx = focus_pin.k_idx;
        my_strcpy(name, focus_pin.name, size);
    }
    SDL_UnlockMutex(lock);
    return set;
}

/* ---- Doing it ------------------------------------------------------------ */

static _Bool prompt_looks_in(obj_prompt_ptr prompt, int where)
{
    int i;

    for (i = 0; i < MAX_LOC && prompt->where[i]; i++) {
        if (prompt->where[i] == where) return 1;
    }
    return 0;
}

/* The item waiting for this prompt, if it will take it. */
static obj_ptr answer_for(obj_prompt_ptr prompt, const struct item_ref *ref)
{
    obj_ptr obj;
    int i;

    /* A prompt that does its work in a command handler (Inscribe, Inspect)
     * never returns the item; those are left to the player. */
    if (prompt->cmd_handler) return NULL;
    if (ref->by_kind) {
        for (i = 0; i < MAX_LOC && prompt->where[i]; i++) {
            if ((obj = first_of_kind(prompt->where[i], ref->k_idx, prompt->filter)) != NULL) return obj;
        }
        return NULL;
    }
    obj = item_at(ref);
    if (!obj || (prompt->filter && !prompt->filter(obj)) || !prompt_looks_in(prompt, obj->loc.where)) {
        return NULL;
    }
    return obj;
}

int __real_obj_prompt(obj_prompt_ptr prompt);

/* Whether one of a prompt's places has anything for it: the prompt makes a
 * tab only for those (obj_prompt.c). The special packs count as having. */
static _Bool place_has_items(int where, obj_p filter)
{
    inv_ptr inv;
    _Bool any;

    switch (where) {
    case INV_PACK: inv = pack_filter(filter); break;
    case INV_EQUIP: inv = equip_filter(filter); break;
    case INV_QUIVER: inv = quiver_filter(filter); break;
    case INV_FLOOR: inv = inv_filter_floor(point(px, py), filter); break;
    default: return 1;
    }
    any = inv_count_slots(inv, obj_exists) > 0;
    inv_free(inv);
    return any;
}

/* What the item prompt now waiting offers, for its buttons: ITEM_PROMPT_*.
 * Game thread only. */
static int prompt_detail;

int ormeq_items_prompt_detail(void)
{
    return prompt_detail;
}

/* The command's own item prompt, answered with the chosen item when one is
 * waiting for it and it still qualifies. */
int __wrap_obj_prompt(obj_prompt_ptr prompt)
{
    struct item_ref ref;
    obj_ptr obj;
    int outer = prompt_detail, result;

    SDL_LockMutex(lock);
    ref = pending.item;
    if (ref.set && pending.armed) SDL_zero(pending);
    else ref.set = 0;
    SDL_UnlockMutex(lock);

    if (ref.set && (obj = answer_for(prompt, &ref)) != NULL) {
        prompt->obj = obj;
        if (obj_is_book(obj)) last_book = obj->k_idx;
        return OP_SUCCESS;
    }

    /* Floor ('-') is only a button where the floor has something for this
     * prompt, and Tab ('/') where there is more than one list to show. */
    {
        int i, lists = 0;

        prompt_detail = 0;
        for (i = 0; i < MAX_LOC && prompt->where[i]; i++) {
            if (!place_has_items(prompt->where[i], prompt->filter)) continue;
            lists++;
            if (prompt->where[i] == INV_FLOOR) prompt_detail |= ITEM_PROMPT_FLOOR;
        }
        if (lists > 1) prompt_detail |= ITEM_PROMPT_TABS;
    }
    result = __real_obj_prompt(prompt);
    prompt_detail = outer;
    if (prompt->obj && obj_is_book(prompt->obj)) last_book = prompt->obj->k_idx;
    return result;
}

/*
 * A queued key went in (context.c). Game thread. It says which command is
 * running: the key after the RAW prefix given at the map, or a key given at
 * the map. And once an action's own command key is in -- the second key of
 * its press, after the prefix -- its item may answer the next item prompt.
 */
void ormeq_items_key_fed(int press, int count, int key, int mode)
{
    if (key == '\\' && mode == MODE_MAP) {
        raw_next = 1;
    } else if (raw_next || mode == MODE_MAP) {
        raw_next = 0;
        last_command = key;
        last_book = 0;
    }

    SDL_LockMutex(lock);
    if (pending.item.set && pending.press == press && count >= 2) pending.armed = 1;
    SDL_UnlockMutex(lock);
}

int ormeq_items_last_command(void)
{
    return last_command;
}

int ormeq_items_last_book(void)
{
    return last_book;
}

/* Back at the map: an action whose command never asked for its item (it was
 * refused, or needs none) is forgotten, and no command is running. Game
 * thread. */
void ormeq_items_at_map(void)
{
    SDL_LockMutex(lock);
    if (pending.item.set && pending.armed) SDL_zero(pending);
    SDL_UnlockMutex(lock);
    last_command = 0;
    last_book = 0;
}

void ormeq_items_init(void)
{
    lock = SDL_CreateMutex();
    ui_obj_focus_hook = focus_hook;
}

/*
 * Queues RAW + key as a map command, with item to answer its prompt and then
 * (if any) one more key, which goes in only at a prompt of then_mode -- a
 * spell's letter into its list, a target into the aim. Under the lock, so the
 * keys cannot all go in before the item waiting for them is recorded (the
 * queue's lock is never held while this one is taken, so the order is safe).
 */
static void run(int key, const struct item_ref *item, int then, int then_mode)
{
    int keys[3] = { '\\', key, then };
    int expect[3] = { 0, 0, then_mode };

    SDL_LockMutex(lock);
    SDL_zero(pending);
    pending.press = ormeq_queue_press_expect(keys, expect, then ? 3 : 2, 1);
    if (item) pending.item = *item;
    SDL_UnlockMutex(lock);
}

void ormeq_items_run_kind(int key, int k_idx, int letter)
{
    struct item_ref item;

    SDL_zero(item);
    item.set = k_idx != 0;
    item.by_kind = 1;
    item.k_idx = k_idx;
    run(key, k_idx ? &item : NULL, letter, MODE_SPELL);
}

/*
 * Fire at the target (a monster in focus): the first ammunition the fire
 * command will take, then the target at its aim prompt. Any thread.
 */
JNIEXPORT void JNICALL Java_com_ormeq_game_NativeBridge_fireAtTarget(JNIEnv *env, jclass cls)
{
    struct item_ref any;

    (void)env;
    (void)cls;
    if (!lock) return;
    SDL_zero(any);
    any.set = 1;
    any.by_kind = 1;
    run('f', &any, 't', MODE_AIM);
}

/*
 * The player chose an action for the focused item: back out to the map and
 * run its command, which the waiting item then answers. The action's key goes
 * in after the RAW prefix, so keymaps cannot change what it means.
 */
JNIEXPORT void JNICALL Java_com_ormeq_game_NativeBridge_itemAction(JNIEnv *env, jclass cls, jchar key)
{
    struct item_ref item;

    (void)env;
    (void)cls;
    if (!lock) return;
    SDL_LockMutex(lock);
    item = focus;
    SDL_UnlockMutex(lock);
    if (item.set) run(key, &item, 0, 0);
}
