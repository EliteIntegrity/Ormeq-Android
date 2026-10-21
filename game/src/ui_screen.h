/*
 * File: ui_screen.h
 *
 * What is on the screen right now.
 *
 * This exists because three separate bugs came from a frontend *inferring* it.
 * The zoomed map painted over menus; menus came up with bands of canvas through
 * them; and a menu's frame was left stranded on the dungeon after it closed.
 * Every one was a rule bolted onto a system with no model of what the screen
 * currently holds, and every fix revealed the next gap.
 *
 * So: model it. There is a **phase** -- where the game is in its life -- and a
 * set of **layers** stacked over it. The map owns the screen when the phase is
 * WORLD and no layer is up; anything else means something is covering it.
 *
 * The other half of the job is teardown. A frontend may draw things Term knows
 * nothing about -- a panel's background, for one, since Term has no way to
 * express a cell's ground colour. Anything of that sort has to be undone when
 * the layer that justified it goes away, and the region repainted, or it
 * survives on screen with nothing to remove it. Layer changes are announced for
 * exactly that reason.
 *
 * Copyright (c) 2026 John Horton.
 *
 * Added since the fork and offered on the same terms as the rest of the
 * tree -- the Angband licence in LICENSE.
 */

#ifndef INCLUDED_UI_SCREEN_H
#define INCLUDED_UI_SCREEN_H

typedef enum
{
    UI_PHASE_STARTUP = 0,   /* before anything is drawn                     */
    UI_PHASE_HOME,          /* the start screen                             */
    UI_PHASE_BIRTH,         /* rolling a character                          */
    UI_PHASE_WORLD          /* playing: the map is the screen               */
} ui_phase_t;

/* Layers over the phase. A bitmask, because a prompt can sit over a menu. */
#define UI_LAYER_MODAL   0x01   /* a full-screen takeover: sheet, help, shop */
#define UI_LAYER_MENU    0x02   /* a document menu, with a panel             */
#define UI_LAYER_PROMPT  0x04   /* a one-line question                       */

extern void       ui_screen_phase_set(ui_phase_t phase);
extern ui_phase_t ui_screen_phase(void);

extern void ui_screen_layer_push(int layer);
extern void ui_screen_layer_pop(int layer);
extern bool ui_screen_has_layer(int layer);

/* TRUE when the map region is showing the map and nothing else. */
extern bool ui_screen_map_owns(void);
/* Clear a saved screen for a legacy chooser, without restoring map cells.
 * The caller pairs its existing screen_save()/screen_load() as usual. */
extern void ui_screen_selection(void);
/* Optional frontend measurement. Zero means a terminal with no pixel size.
 * Responsive pages must not infer physical room solely from a shrunken font. */
extern void ui_screen_pixel_width_set(int width);
extern int  ui_screen_pixel_width(void);

/*
 * What a prompt is asking for, while the game waits on it.
 *
 * A keyboard does not need telling: every prompt reads keys, and the player
 * reads the prompt. A touch screen has no keys, so its buttons have to change
 * with the question -- Yes and No for a yes/no, digits for an amount,
 * directions and Nearest for an aim. Each prompt says what it is for as long
 * as it waits.
 *
 * Prompts nest -- a quantity inside a sale inside a shop -- so this is a stack,
 * and the top is what is being asked now. Asking for "any key" or for a line of
 * text is how most of the others are built, so those two take on the kind of
 * the prompt they serve: a direction prompt reading its key is still a
 * direction prompt, and an amount typed as text is still an amount.
 *
 * This is separate from the layers on purpose. A layer covers the map; a
 * direction prompt does not, and its map must stay tappable.
 */
typedef enum
{
    UI_PROMPT_NONE = 0,
    UI_PROMPT_MORE,         /* -more- on the message line                   */
    UI_PROMPT_CHECK,        /* one of a few keys: yes/no and the like       */
    UI_PROMPT_KEY,          /* any one key                                  */
    UI_PROMPT_DIRECTION,    /* an adjacent direction                        */
    UI_PROMPT_AIM,          /* a direction or a target                      */
    UI_PROMPT_TARGET,       /* interactive targeting: choosing a target     */
    UI_PROMPT_ITEM,         /* choose an object                             */
    UI_PROMPT_SPELL,        /* choose a spell or a power                    */
    UI_PROMPT_MENU,         /* choose from a lettered menu                  */
    UI_PROMPT_TEXT,         /* type a line                                  */
    UI_PROMPT_NUMBER,       /* type an amount                               */
    UI_PROMPT_SHOP,         /* a shop's counter                             */
    UI_PROMPT_HOME,         /* the home                                     */
    UI_PROMPT_LOOK,         /* looking around at what is in view            */
    UI_PROMPT_CARD          /* a questgiver's card                          */
} ui_prompt_t;

/* keys: for UI_PROMPT_CHECK, the answers it accepts (e.g. "ny"); for
 * UI_PROMPT_CARD, the one that answers it ("a" accepts an offer, "\r"
 * continues from any other card); else NULL. */
extern void ui_screen_prompt_push(int kind, cptr keys);
extern void ui_screen_prompt_pop(void);
extern int  ui_screen_prompt(void);           /* top of the stack, or NONE    */
extern cptr ui_screen_prompt_keys(void);      /* the top's answers, or NULL   */
extern bool ui_screen_prompt_within(int kind);/* anywhere on the stack        */

#endif
