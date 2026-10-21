/*
 * The Android port's own pieces, shared between the files in android/.
 *
 * Include after SDL (and the game, where a file uses it).
 */

#ifndef ORMEQ_ANDROID_H
#define ORMEQ_ANDROID_H

#include <jni.h>

/* What the game is waiting for (context.c; keep in step with ControlLayouts.kt). */
enum control_mode {
    MODE_NONE = 0,       /* start screen, character creation: no game in play */
    MODE_MAP = 1,        /* the command prompt, with the map on screen */
    MODE_MORE = 2,       /* -more- */
    MODE_MENU = 3,       /* a screen or menu over the map */
    MODE_CHECK = 4,      /* one of a few answers; keys says which */
    MODE_TEXT = 5,       /* a line of text; detail 1 = an amount */
    MODE_AIM = 6,        /* a direction or a target */
    MODE_DIRECTION = 7,  /* an adjacent direction */
    MODE_TARGET = 8,     /* interactive targeting: choosing a target */
    MODE_ITEM = 9,       /* choose an object */
    MODE_SPELL = 10,     /* choose a spell or power */
    MODE_SHOP = 11,      /* a shop's counter; detail 1 = the home */
    MODE_KEY = 12,       /* any one key */
    MODE_LOOK = 13,      /* looking around at what is in view */
    MODE_RECKONING = 14, /* Past Reckonings, the Reckoner's history */
    MODE_QUEST_CARD = 15 /* a questgiver's card; keys "a" on an offer */
};

/* Installs the touch layer's hooks into the frontend; call before the game
 * starts (context.c). */
extern void ormeq_android_init(void);

/* Calls a method of the activity from the game thread, looking it up once
 * into *cache (context.c). */
extern void ormeq_call_activity(const char *name, const char *signature, jmethodID *cache,
    void (*call)(JNIEnv *, jobject, jmethodID, void *), void *data);

/* Queues keys for the game as one press, fed one per wait; a map command
 * first backs out of whatever screen is open. Where expect[i] is not 0, key i
 * only goes in while the game waits in that mode (a spell's letter into a
 * spell list), and otherwise the rest of the press is dropped. Returns the
 * press's number (context.c). Any thread. */
extern int ormeq_queue_press(const int *keys, int count, _Bool map_command);
extern int ormeq_queue_press_expect(const int *keys, const int *expect, int count, _Bool map_command);

/* A queued key that goes in as nothing at all: a map command of just this
 * only backs out to the map. */
#define KEY_NOTHING (-1)

/* Zooms the map by wheel notches, + in, - out (context.c). Any thread. */
extern void ormeq_zoom(int notches);

/* Wakes the game so it looks at what has changed (context.c). Any thread. */
extern void ormeq_wake_game(void);

/* Where on its row a tapped "a)" label is: its letter, or 0, and the column
 * of the letter in *x (context.c). Game thread. */
extern int ormeq_label_at(int col, int row, int *x);

/* What can be done with an item (items.c). */
extern void ormeq_items_init(void);
extern _Bool ormeq_items_focused(void);       /* an item's description is up */
extern void ormeq_items_key_fed(int press, int count, int key, int mode); /* a queued key went in */
extern void ormeq_items_at_map(void);         /* the game is back at the map */
extern int ormeq_items_count_kind(int k_idx); /* how many of a kind are carried */
extern void ormeq_items_publish_available(void); /* at the map: item commands with something to act on */
extern _Bool ormeq_items_verb_available(int key); /* that command has something to act on */
extern _Bool ormeq_items_is_verb(int key);         /* that command acts on an item */
extern _Bool ormeq_items_can_fire(void);           /* a launcher, with ammunition for it */
extern int ormeq_items_last_command(void);    /* the command now running, as queued */
extern int ormeq_items_last_book(void);       /* the book it was given, or 0 */
/* Runs command key on the first carried item of kind k_idx (0: none),
 * answering its "which item?", then letter (0: none) into its spell list.
 * Backs out to the map first. Any thread. */
extern void ormeq_items_run_kind(int key, int k_idx, int letter);
/* The focused item's natural action that is worth a quick slot: its key and
 * kind, and the item's short name. Returns 0 if it has none. Any thread. */
extern int ormeq_items_focus_pin(int *key, int *k_idx, char *name, size_t size);

/* Quick slots (pins.c). */
extern void ormeq_pins_init(void);
extern void ormeq_pins_publish(void);         /* at the map: refresh the bar. Game thread. */
extern void ormeq_pins_add_spell(int key, int book, int letter, const char *name); /* game thread */

/* What the item prompt now waiting offers (keep in step with ControlLayouts.kt). */
#define ITEM_PROMPT_FLOOR 1   /* something on the floor for it: '-' */
#define ITEM_PROMPT_TABS  2   /* more than one list: '/' */
extern int ormeq_items_prompt_detail(void);

/* While waiting for input: opens the audio device while the title music or
 * the ambience plays, and closes it once they and the effects have been
 * quiet for a while. Returns how many milliseconds until it should be asked
 * again, or -1 when no device is open (sound.c). */
extern int ormeq_sound_update(void);

/* Tells the player something, briefly, over the game (context.c). */
extern void ormeq_notice(const char *text);

/* A button label the frontend draws, without the key it names (touch-text.c). */
extern const char *ormeq_touch_label(const char *label);

#endif
