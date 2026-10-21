/*
 * File: ui_screen.c
 *
 * What is on the screen right now. See ui_screen.h for why this is modelled
 * rather than inferred.
 *
 * Copyright (c) 2026 John Horton.
 *
 * Added since the fork and offered on the same terms as the rest of the
 * tree -- the Angband licence in LICENSE.
 */

#include "scottish.h"

static ui_phase_t _phase = UI_PHASE_STARTUP;
static int        _layers;
static int        _pixel_width;
void ui_screen_pixel_width_set(int width) { _pixel_width = MAX(0, width); }
int ui_screen_pixel_width(void) { return _pixel_width; }


/*
 * Tell the frontend the screen's shape changed.
 *
 * A frontend may have drawn things Term has no idea about -- a panel's ground
 * being the obvious one, since Term cannot express a cell's background colour.
 * Those have to come down when whatever justified them goes away, and the
 * region underneath has to be repainted, or they stay on screen with nothing
 * to remove them. That was the menu frame left stranded on the dungeon.
 */
static void _changed(void)
{
    ui_event_screen_changed();
}


void ui_screen_phase_set(ui_phase_t phase)
{
    if (_phase == phase) return;

    _phase = phase;

    /* A new phase starts clean. Layers belong to the phase that raised them,
     * and a phase change means whatever raised them is long gone. */
    _layers = 0;
    _changed();
}

ui_phase_t ui_screen_phase(void)
{
    return _phase;
}


void ui_screen_layer_push(int layer)
{
    if ((_layers & layer) == layer) return;

    _layers |= layer;
    _changed();
}

void ui_screen_layer_pop(int layer)
{
    if (!(_layers & layer)) return;

    _layers &= ~layer;
    _changed();
}

bool ui_screen_has_layer(int layer)
{
    return BOOL((_layers & layer) != 0);
}


bool ui_screen_map_owns(void)
{
    if (_phase != UI_PHASE_WORLD) return FALSE;
    if (_layers) return FALSE;

    /* Belt and braces while the layers are still being retrofitted: eight
     * files take the screen over with a bare Term_save() rather than
     * screen_save(), so screen_depth is the one signal that already covers
     * every well-behaved caller. */
    if (screen_is_saved()) return FALSE;

    /* And the shops, the home, the quest pages and birth: they take the whole
     * screen without a screen_save() and without painting a menu, so neither
     * of the rules above notices them. The game has kept this flag for exactly
     * this question since 1990 -- better to ask it than to keep inventing ways
     * to guess the answer.
     *
     * It used to be ANDed on by sdl3_map_showing() instead, which meant the
     * frontend knew something ui_screen did not, and anything else that asked
     * this got a wrong answer. lite_spot() was asking nothing at all. */
    if (character_icky) return FALSE;

    return TRUE;
}

void ui_screen_selection(void)
{
    ui_screen_layer_push(UI_LAYER_MENU);
    ui_event_map_hidden();
    ui_event_menu_clear();
    Term_clear();
    ui_event_panel_screen();
}


/*
 * The prompt stack; see ui_screen.h. Deeper than this and the extra levels are
 * counted but not recorded, which keeps push and pop balanced whatever happens.
 */
#define UI_PROMPT_MAX 16

static struct { int kind; cptr keys; } _prompts[UI_PROMPT_MAX];
static int _prompt_depth;

void ui_screen_prompt_push(int kind, cptr keys)
{
    int top = ui_screen_prompt();

    /* "Any key" and "a line of text" are how the specific prompts are built,
     * so inside one of those they are still that prompt. */
    if (kind == UI_PROMPT_KEY && top != UI_PROMPT_NONE && top != UI_PROMPT_KEY
     && top != UI_PROMPT_TEXT && top != UI_PROMPT_NUMBER && top != UI_PROMPT_CHECK
     && top != UI_PROMPT_MORE)
    {
        kind = top;
        keys = ui_screen_prompt_keys();
    }
    if (kind == UI_PROMPT_TEXT && top == UI_PROMPT_NUMBER) kind = top;

    if (_prompt_depth < UI_PROMPT_MAX)
    {
        _prompts[_prompt_depth].kind = kind;
        _prompts[_prompt_depth].keys = keys;
    }
    _prompt_depth++;
}

void ui_screen_prompt_pop(void)
{
    if (_prompt_depth > 0) _prompt_depth--;
}

int ui_screen_prompt(void)
{
    int top = MIN(_prompt_depth, UI_PROMPT_MAX) - 1;

    return top >= 0 ? _prompts[top].kind : UI_PROMPT_NONE;
}

cptr ui_screen_prompt_keys(void)
{
    int top = MIN(_prompt_depth, UI_PROMPT_MAX) - 1;

    return top >= 0 ? _prompts[top].keys : NULL;
}

bool ui_screen_prompt_within(int kind)
{
    int i;

    for (i = MIN(_prompt_depth, UI_PROMPT_MAX) - 1; i >= 0; i--)
    {
        if (_prompts[i].kind == kind) return TRUE;
    }
    return FALSE;
}
