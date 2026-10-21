/*
 * Touch wording for the game's keyboard hints.
 *
 * The desktop game explains its controls in keyboard terms ("Press SPACE every
 * time you see a -more-", "A letter chooses. Arrows move", "Esc to exit"). On a
 * touch screen those keys are the d-pad, a tap on a row, and the Next, OK and
 * Back buttons, so the functions that put hint text on screen are wrapped at
 * link time (-Wl,--wrap, see CMakeLists.txt) and the text is reworded first.
 *
 * Ormeq writes most of its hints as documents with colour markup, which the
 * terminal then receives in coloured pieces -- too late to recognise a phrase.
 * So the wrapping is done where the whole string still exists: doc_insert and
 * doc_printf, the prompt and message functions, and the terminal itself for
 * what goes straight there.
 *
 * Whole strings are replaced exactly; then phrases, every occurrence, in
 * order. Only key-hint phrases are matched, never ordinary words, so game text
 * is left alone; and text that names none of the trigger words is not searched
 * for phrases.
 */

#include <SDL3/SDL.h>
#include <stdarg.h>

#include "sdl3_local.h"
#include "ormeq-android.h"

#define N_ELEMENTS(a) (sizeof(a) / sizeof((a)[0]))

struct rewrite {
    const char *from;
    const char *to;
};

/* In a phrase, any one character: a key letter that varies. */
#define ANY "\x01"

/* Whole strings, replaced exactly. */
static const struct rewrite lines[] = {
    { "Direction ('*' to choose a target, Escape to cancel)? ",
      "Direction? D-pad, Nearest, Pick, or tap a target: " },
    { "Direction ('5' for target, '*' to re-target, Escape to cancel)? ",
      "Direction? D-pad, Target, Pick, or tap a target: " },
    { "Direction (Escape to cancel)? ", "Direction? D-pad, or tap a square next to you: " },
    { "[Press ESC to exit.]", "[Back to exit.]" },
    { "[Press Return, Space, -, =, /, |, or ESC to exit.]", "[PgUp and PgDn to page, Back to exit.]" },
    { "[Press ESC to exit. Press ? for help]", "[Back to exit.]" },
    { "[Press ESC to exit. Press M to toggle mode. Press ? for help]", "[Back to exit.]" },
    { "[Press any key to continue]", "[OK to continue]" },
    /* Look, with the creature's card over the map, or filling the screen: a
     * sideways swipe browses and a drag scrolls the card (TouchControlsView). */
    { "Look: +/- browse; R fullscreen/card; [/] scroll; ? help; Esc exit",
      "Look: Next, Prev or a swipe to browse; drag the card to scroll; Back leaves." },
    { "Look: +/- browse; [/] scroll; ? help; Esc exit",
      "Look: Next, Prev or a swipe to browse; drag to scroll; Back leaves." },
    /* The tabs of the message log and Past Reckonings, which are taps
     * already. (The footers there are worded for touch by change 0009.) */
    { "Tab: switch", "Tap a tab to switch" },
    /* A questgiver's card: Accept and Later, or Continue, are the cluster's
     * too (MODE_QUEST_CARD), and a drag scrolls the card. */
    { "[A Accept]", "[Accept]" },
    { "[Esc Later]", "[Later]" },
    { "[Enter Continue]", "[Continue]" },
    { "Up/Down: scroll  |  PgUp/PgDn: page", "Drag to scroll" },
    /* (The quest journal's footer is worded for touch by change 0014.) */
    /* An item's description, with its actions in the cluster (items.c). */
    { "<color:B>[Press <color:y>Any Key</color> to Continue]</color>\n\n",
      "<color:B>[Tap anywhere to go back]</color>\n\n" },
};

/* Phrases within hints, applied in order, every occurrence. */
static const struct rewrite phrases[] = {
    /* The welcome, on the first turn of a new character. */
    { "Press <color:y>SPACE</color> every time you see a <color:B>-more-</color> prompt and "
      "you are finished reading the current messages. ",
      "Tap <color:y>Next</color>, or anywhere, at each <color:B>-more-</color> once you "
      "have read the messages. " },
    /* After the low hit point warning, a thrown weapon coming back, an
     * ambush: the large button answers. */
    { "Press <color:y>Space</color> to continue.", "Tap <color:y>OK</color> to continue." },
    { "Press <color:y>CTRL+P</color> to review recent messages. ",
      "<color:y>Log</color> shows recent messages. " },
    { "You may press <color:y>?</color> at any time for help.",
      "Every command, and the help, is under <color:y>More</color>." },

    /* Character creation. A long press there asks for help on a choice and
     * lowers a stat (android/context.c). */
    { "A letter chooses. Arrows move, right chooses. SHIFT+letter for more.",
      "Tap a row to choose it; hold it for more." },
    { "A letter chooses. Arrows move, right chooses.", "Tap a row to choose it." },
    { "A letter toggles. Arrows move, right toggles. RET accepts.", "Tap a row to toggle it; OK accepts." },
    { "Arrows move, right chooses.", "Tap a row to choose it." },
    { "You can get general help at any time by pressing <color:keypress>?</color>.",
      "<color:keypress>?</color>) Help, below, says more." },
    { "You can often get specific help by entering the uppercase letter for a command. "
      "For example, type <color:keypress>B</color> on this screen to receive help on Beginner Mode.",
      "Hold a choice for help on it." },
    { "You can often get specific help by entering the uppercase letter for a command. "
      "For example, type <color:keypress>R</color> on this screen to receive help on your "
      "currently selected race.",
      "Hold a choice for help on it." },
    { "You may adjust each starting stat using the indicated keys. The lower case key "
      "decrements while the upper case increments the starting value.",
      "Tap a stat to raise it, and hold it to lower it." },
    { "<color:y>ESC</color>) Back\n", "" },
    { "<color:y>TAB</color>) More Info", "<color:y>Info</color>) More Info" },
    { "RET</color>) ", "OK</color>) " },
    { "<color:y>ESC</color>) Prev Screen", "<color:y>Back</color>) Prev Screen" },

    /* The character sheet. Screens of text are swiped (TouchControlsView). */
    { "4 6 or the arrows turn the page", "Swipe sideways to turn the page" },
    { ", 2 8 scroll it, a letter jumps to its section", ", up and down to scroll it" },
    { ", ESC puts it down.", ", Back puts it down." },

    /* Spell and command lists. */
    { ", *=List, ESC=exit)", ", tap one, Back to exit)" },

    /* Inventory, Equipment and Inspect: a tap shows the item and its actions. */
    { "Examine which item <color:w>(<color:keypress>Esc</color> to exit)</color>?",
      "Tap an item to see what you can do with it." },

    /* Shops and the home. */
    { "<color:keypress>b</color> to buy. ", "Tap a row or <color:keypress>Buy</color> to buy. " },
    { "<color:keypress>s</color> to sell. ", "<color:keypress>Sell</color> to sell. " },
    { "<color:keypress>s</color> to give. ", "<color:keypress>Sell</color> to give. " },
    { "<color:keypress>g</color> to get an item. ", "<color:keypress>Take</color> to get an item. " },
    { "<color:keypress>d</color> to drop an item. ", "<color:keypress>Stash</color> to drop an item. " },
    { "<color:keypress>x</color> to begin examining items.", "<color:keypress>Info</color> to examine items." },
    { "<color:keypress>Esc</color> to exit. <color:keypress>?</color> for help.",
      "<color:keypress>Leave</color> to exit." },
    /* In the item prompt's help, a table of keys: one space fewer after the
     * longer name keeps the table straight. */
    { "<color:keypress>Esc</color>       Cancel", "<color:keypress>Back</color>      Cancel" },
    { "<color:keypress>Esc</color>", "<color:keypress>Back</color>" },

    /* Knowledge and option screens. */
    { "Arrows to move, left and right to change, ESC when you are done.",
      "Tap a row, and swipe sideways to change it; Back when you are done." },
    { "Arrows to move, ", "Tap or swipe to move, " },
    { "Arrows to move. ", "Tap or swipe to move. " },
    { ", ESC to leave.", ", Back leaves." },
    { "A digit chooses, or the arrows.", "Tap one to choose it." },
    { "ESC when you are done.", "Back when you are done." },
    /* The options and knowledge menus: the Back button is always there. */
    { "  <color:D>ESC) Back</color>\n", "" },
    /* A building's list of services, the same: Back leaves it (bldg.c). */
    { "  <color:D>ESC) Leave</color>\n", "" },

    /* The quest journal's help (quests.txt): the journal is the rail's Quest
     * button, its buttons are taps, and a card's answers are the cluster's.
     * Its line breaks are hard ones, so a paragraph is reworded whole, and
     * each line kept inside the 80 columns of a help page. */
    { "Press <color:keyword>Ctrl+Q</color> to open the journal.",
      "Tap <color:keyword>Quest</color> to open the journal." },
    { "On a wide screen, select a quest on the left to read its details on the\n"
      "right. On a small screen, select a row and press Enter (or tap it) to open\n"
      "its details. Escape returns to the list; from the list it closes the journal.",
      "On a wide screen, tap a quest on the left to read its details on the\n"
      "right. On a small screen, tap a row to open its details. Back returns to\n"
      "the list; from the list it closes the journal." },
    { "Use Up/Down or P/N to select quests. Tab changes lists; A, C and F select\n"
      "Active, Completed and Failed directly. On a wide screen, [ and ] scroll\n"
      "the details and PgUp/PgDn scroll a page. On a small details page, Up/Down\n"
      "also scroll. X closes the journal from either view. The labelled controls\n"
      "and quest rows can be clicked or tapped. ? opens this help.\n"
      "When details extend beyond the page, clickable scroll controls remain at\n"
      "the bottom of the screen.",
      "Tap a quest to select it; a drag up or down the list, or Previous and\n"
      "Next below it (the arrows on a small screen), moves the selection too.\n"
      "Tap a tab to change lists. Scroll up and Scroll down move the details,\n"
      "and PgUp and PgDn a page; on a small details page a drag scrolls them\n"
      "too. Close leaves the journal from either view, and Help opens this help." },
    { "A accepts; Escape leaves the offer for later.", "Accept takes it on; Later leaves it for now." },
    { "Ctrl+Q shows the complete record.", "The journal keeps the full record." },
};

/* Every hint names one of these; other text is not searched. */
static const char *triggers[] = {
    "ESC", "Esc", "Escape", "SPACE", "CTRL+", "Arrows", "SHIFT", "any key",
    "4 6 or", "2 8 scroll", "*=List", "</color> to ", "Press <color", "You may press",
    "TAB</color>", "RET</color>", "at any time by", "uppercase letter", "the indicated keys",
    "<dir>]", "Ctrl+Q",
};

/* Where phrase first occurs in text, or NULL. ANY matches any character, so a
 * match is always as long as the phrase. */
static char *find_phrase(char *text, cptr phrase)
{
    for (; *text; text++) {
        size_t i;

        for (i = 0; phrase[i]; i++) {
            if (!text[i]) return NULL;
            if (phrase[i] != ANY[0] && phrase[i] != text[i]) break;
        }
        if (!phrase[i]) return text;
    }
    return NULL;
}

/* Rewrites into buffer; returns text itself when nothing applies. */
static cptr touch_text(cptr text, char *buffer, size_t size)
{
    size_t i;
    bool changed = FALSE;
    bool hint = FALSE;

    if (!text || !text[0]) return text;
    for (i = 0; i < N_ELEMENTS(lines); i++) {
        if (strcmp(text, lines[i].from) == 0) return lines[i].to;
    }
    for (i = 0; i < N_ELEMENTS(triggers) && !hint; i++) {
        if (strstr(text, triggers[i])) hint = TRUE;
    }
    if (!hint) return text;
    if (strlen(text) >= size) return text;
    my_strcpy(buffer, text, size);
    for (i = 0; i < N_ELEMENTS(phrases); i++) {
        char *found;
        size_t from_len = strlen(phrases[i].from);
        size_t to_len = strlen(phrases[i].to);
        char *from = buffer;

        while ((found = find_phrase(from, phrases[i].from)) != NULL) {
            size_t tail = strlen(found + from_len);

            if ((size_t)(found - buffer) + to_len + tail + 1 > size) break;
            memmove(found + to_len, found + from_len, tail + 1);
            memcpy(found, phrases[i].to, to_len);
            from = found + to_len;
            changed = TRUE;
        }
    }

    /* Look and targeting end each description with the keys they take,
     * "[q,t,p,o,x,j,+,-,?,<dir>]" ("[r,..." on a monster): the buttons say
     * all of that on a touch screen. */
    {
        char *end;

        while ((end = strstr(buffer, "<dir>]")) != NULL) {
            char *start = end;

            while (start > buffer && start[-1] != '[' && start[-1] != ' ') start--;
            if (start == buffer || start[-1] != '[') break;
            start--;
            if (start > buffer && start[-1] == ' ') start--;
            memmove(start, end + 6, strlen(end + 6) + 1);
            changed = TRUE;
        }
    }
    return changed ? buffer : text;
}

/*
 * A button the frontend draws itself, labelled with its key first ("R Full",
 * "Tab More", "[ Up" on the Look card, change 0006): the label without the
 * key, which a touch screen does not have. Game thread.
 */
const char *ormeq_touch_label(const char *label)
{
    const char *space = label ? strchr(label, ' ') : NULL;

    /* The card's way out, named for its key: Back is what does it here. */
    if (label && strcmp(label, "Esc") == 0) return "Back";
    if (!space || space - label > 3 || !space[1]) return label;
    return space + 1;
}

/* ---- Documents -------------------------------------------------------- */

doc_pos_t __real_doc_insert(doc_ptr doc, cptr text);
doc_pos_t __wrap_doc_insert(doc_ptr doc, cptr text)
{
    char buffer[4096];
    size_t len = text ? strlen(text) : 0;
    char *page = NULL;
    doc_pos_t result;

    /* A help page arrives whole (doc_read_file): room for all of it, and for
     * wording that runs longer. */
    if (len >= sizeof(buffer) / 2) page = malloc(2 * len + 1);
    if (!page) return __real_doc_insert(doc, touch_text(text, buffer, sizeof(buffer)));
    result = __real_doc_insert(doc, touch_text(text, page, 2 * len + 1));
    free(page);
    return result;
}

/* Formatted the way doc_printf formats, then inserted as above. */
doc_pos_t __wrap_doc_printf(doc_ptr doc, const char *fmt, ...)
{
    string_ptr s = string_alloc();
    char buffer[4096];
    doc_pos_t result;
    va_list vp;

    va_start(vp, fmt);
    string_vprintf(s, fmt, vp);
    va_end(vp);
    result = __real_doc_insert(doc, touch_text(string_buffer(s), buffer, sizeof(buffer)));
    string_free(s);
    return result;
}

/* ---- Prompts and messages --------------------------------------------- */

bool __real_get_com(cptr prompt, char *command, bool z_escape);
bool __wrap_get_com(cptr prompt, char *command, bool z_escape)
{
    char buffer[1024];

    return __real_get_com(touch_text(prompt, buffer, sizeof(buffer)), command, z_escape);
}

void __real_prt(cptr str, int row, int col);
void __wrap_prt(cptr str, int row, int col)
{
    char buffer[1024];

    __real_prt(touch_text(str, buffer, sizeof(buffer)), row, col);
}

void __real_c_prt(byte attr, cptr str, int row, int col);
void __wrap_c_prt(byte attr, cptr str, int row, int col)
{
    char buffer[1024];

    __real_c_prt(attr, touch_text(str, buffer, sizeof(buffer)), row, col);
}

void __real_put_str(cptr str, int row, int col);
void __wrap_put_str(cptr str, int row, int col)
{
    char buffer[1024];

    __real_put_str(touch_text(str, buffer, sizeof(buffer)), row, col);
}

void __real_c_put_str(byte attr, cptr str, int row, int col);
void __wrap_c_put_str(byte attr, cptr str, int row, int col)
{
    char buffer[1024];

    __real_c_put_str(attr, touch_text(str, buffer, sizeof(buffer)), row, col);
}

char __real_msg_prompt(cptr prompt, char keys[], int options);
char __wrap_msg_prompt(cptr prompt, char keys[], int options)
{
    char buffer[1024];

    return __real_msg_prompt(touch_text(prompt, buffer, sizeof(buffer)), keys, options);
}

void __real_msg_print(cptr msg);
void __wrap_msg_print(cptr msg)
{
    char buffer[2048];

    __real_msg_print(touch_text(msg, buffer, sizeof(buffer)));
}

void __real_cmsg_print(byte color, cptr msg);
void __wrap_cmsg_print(byte color, cptr msg)
{
    char buffer[2048];

    __real_cmsg_print(color, touch_text(msg, buffer, sizeof(buffer)));
}

/* ---- The terminal ----------------------------------------------------- */

errr __real_Term_putstr(int x, int y, int n, byte a, cptr s);
errr __wrap_Term_putstr(int x, int y, int n, byte a, cptr s)
{
    char buffer[1024];
    cptr text;

    /* A counted fragment is part of something longer; leave it as it is. */
    if (n >= 0 && s && (size_t)n < strlen(s)) return __real_Term_putstr(x, y, n, a, s);
    text = touch_text(s, buffer, sizeof(buffer));
    return __real_Term_putstr(x, y, text == s ? n : -1, a, text);
}

errr __real_Term_addstr(int n, byte a, cptr s);
errr __wrap_Term_addstr(int n, byte a, cptr s)
{
    char buffer[1024];
    cptr text;

    if (n >= 0 && s && (size_t)n < strlen(s)) return __real_Term_addstr(n, a, s);
    text = touch_text(s, buffer, sizeof(buffer));
    return __real_Term_addstr(text == s ? n : -1, a, text);
}
