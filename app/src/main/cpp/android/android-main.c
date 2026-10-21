/*
 * Android entry point for Ormeq.
 *
 * Installs the runtime tree (lib/, font/ and the licence texts) from the APK
 * into internal storage and makes that the working directory, so the desktop's
 * own paths work unchanged: SDL_GetBasePath() is "./" on Android, so the game
 * looks for "./lib/" and "./font/" exactly where they now are. Then it runs the
 * desktop main(), compiled as ormeq_main.
 *
 * Around that, the things a desktop program never meets:
 *
 * - The character is saved when Android sends the app to the background,
 *   because the system may end the process at any moment after that -- a
 *   dead one too, so it cannot be continued from before its death -- and the
 *   player is told on coming back if that save failed.
 * - A process ended that way leaves the game's floor files behind, and the
 *   next start would stop to ask about them. The savefile holds every floor,
 *   so they are simply cleared.
 * - The last character played is offered as Continue (the desktop's -u).
 * - Back is Escape; the game's stdout and stderr go to logcat.
 *
 * The JNI surface for the touch controls is in context.c.
 */

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <android/asset_manager.h>
#include <android/asset_manager_jni.h>
#include <android/log.h>
#include <dirent.h>
#include <errno.h>
#include <jni.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* SDL first, then the game: see the note at the top of sdl3_local.h. From
 * here on "bool" is the game's own, so SDL callbacks say _Bool. */
#include "sdl3_local.h"
#include "ormeq-android.h"

#define INDEX_NAME "ormeq-assets.txt"
#define INDEX_HEADER "ormeq-assets 1"
#define STAMP_NAME ".ormeq-assets-stamp"
#define SAVE_DIR "lib/save"

int ormeq_main(int argc, char *argv[]);

static AAssetManager *open_asset_manager(JNIEnv *env, jobject *global_ref)
{
    jobject activity = (jobject)SDL_GetAndroidActivity();
    AAssetManager *manager = NULL;
    jclass activity_class;
    jmethodID get_assets;
    jobject assets;

    if (!env || !activity) return NULL;
    activity_class = (*env)->GetObjectClass(env, activity);
    get_assets = (*env)->GetMethodID(env, activity_class, "getAssets",
        "()Landroid/content/res/AssetManager;");
    assets = get_assets ? (*env)->CallObjectMethod(env, activity, get_assets) : NULL;
    if (assets) {
        *global_ref = (*env)->NewGlobalRef(env, assets);
        manager = AAssetManager_fromJava(env, *global_ref);
        (*env)->DeleteLocalRef(env, assets);
    }
    (*env)->DeleteLocalRef(env, activity_class);
    (*env)->DeleteLocalRef(env, activity);
    return manager;
}

/* mkdir -p for the directories above path. */
static void make_parents(char *path)
{
    char *p;

    for (p = path + 1; *p; p++) {
        if (*p != '/') continue;
        *p = '\0';
        mkdir(path, 0755);
        *p = '/';
    }
}

static _Bool read_installed_stamp(const char *root, char *stamp, size_t size)
{
    char path[1024];
    FILE *fp;
    _Bool ok;

    snprintf(path, sizeof(path), "%s/%s", root, STAMP_NAME);
    fp = fopen(path, "r");
    if (!fp) return 0;
    ok = fgets(stamp, (int)size, fp) != NULL;
    fclose(fp);
    if (ok) stamp[strcspn(stamp, "\r\n")] = '\0';
    return ok;
}

/*
 * Copies every file listed in the packaged index unless the installed stamp
 * already matches. Only listed files are written, so the saves, scores and
 * settings in the writable parts of lib/ survive an upgrade.
 */
static _Bool install_runtime(const char *root, char *error, size_t error_size)
{
    static char buffer[1 << 16];
    JNIEnv *env = (JNIEnv *)SDL_GetAndroidJNIEnv();
    jobject assets_ref = NULL;
    AAssetManager *manager = open_asset_manager(env, &assets_ref);
    AAsset *index_asset;
    char *index = NULL, *line, *save = NULL;
    char installed[64] = "", stamp[64] = "", stamp_path[1024];
    _Bool ok = 0;
    Uint64 started = SDL_GetTicks();
    int files = 0;

    if (!manager) {
        snprintf(error, error_size, "The Android asset manager is unavailable.");
        goto done;
    }
    index_asset = AAssetManager_open(manager, INDEX_NAME, AASSET_MODE_BUFFER);
    if (!index_asset) {
        snprintf(error, error_size, "The packaged file index is missing.");
        goto done;
    }
    {
        off64_t length = AAsset_getLength64(index_asset);
        index = malloc((size_t)length + 1);
        if (index) {
            memcpy(index, AAsset_getBuffer(index_asset), (size_t)length);
            index[length] = '\0';
        }
        AAsset_close(index_asset);
    }
    if (!index) {
        snprintf(error, error_size, "Out of memory reading the file index.");
        goto done;
    }

    line = strtok_r(index, "\r\n", &save);
    if (!line || strcmp(line, INDEX_HEADER) != 0) {
        snprintf(error, error_size, "The packaged file index is not recognised.");
        goto done;
    }
    line = strtok_r(NULL, "\r\n", &save);
    if (!line || strncmp(line, "stamp ", 6) != 0) {
        snprintf(error, error_size, "The packaged file index has no stamp.");
        goto done;
    }
    snprintf(stamp, sizeof(stamp), "%s", line + 6);
    if (read_installed_stamp(root, installed, sizeof(installed)) &&
            strcmp(installed, stamp) == 0) {
        ok = 1;
        goto done;
    }

    /* Half an install must not look like a whole one: the stamp goes first
     * and is written again last. */
    snprintf(stamp_path, sizeof(stamp_path), "%s/%s", root, STAMP_NAME);
    unlink(stamp_path);

    while ((line = strtok_r(NULL, "\r\n", &save)) != NULL) {
        char *space = strchr(line, ' ');
        char target[1024];
        long long expected, written = 0;
        int count = 0;
        AAsset *asset;
        FILE *out;

        if (!space || space == line) continue;
        *space = '\0';
        expected = atoll(line);
        asset = AAssetManager_open(manager, space + 1, AASSET_MODE_STREAMING);
        if (!asset) {
            snprintf(error, error_size, "A packaged file is missing: %s", space + 1);
            goto done;
        }
        snprintf(target, sizeof(target), "%s/%s", root, space + 1);
        make_parents(target);
        out = fopen(target, "wb");
        while (out && (count = AAsset_read(asset, buffer, sizeof(buffer))) > 0) {
            if (fwrite(buffer, 1, (size_t)count, out) != (size_t)count) break;
            written += count;
        }
        AAsset_close(asset);
        if (!out || fclose(out) != 0 || count < 0 || written != expected) {
            snprintf(error, error_size, "Could not install %s (storage full?)", space + 1);
            goto done;
        }
        files++;
    }

    {
        FILE *fp = fopen(stamp_path, "w");
        if (!fp || fprintf(fp, "%s\n", stamp) < 0 || fclose(fp) != 0) {
            snprintf(error, error_size, "Could not record the installed game files.");
            goto done;
        }
    }
    SDL_Log("Installed %d runtime files in %llu ms", files,
        (unsigned long long)(SDL_GetTicks() - started));
    ok = 1;

done:
    free(index);
    if (assets_ref) (*env)->DeleteGlobalRef(env, assets_ref);
    return ok;
}

/* The writable directories, which nothing in the package creates. */
static void make_writable_dirs(void)
{
    static const char *dirs[] = {
        "lib/save", "lib/user", "lib/apex", "lib/data", "lib/bone",
        "lib/script", "lib/info", "lib/xtra", NULL
    };
    int i;

    for (i = 0; dirs[i]; i++) mkdir(dirs[i], 0755);
}

/* Whether a file in lib/save is a savefile, by the start screen's own rule
 * (start_screen.c): savefiles have no extension. */
static _Bool is_savefile_name(const char *name)
{
    if (!name[0] || name[0] == '.') return 0;
    if (!strcmp(name, "Makefile") || !strcmp(name, "delete.me")) return 0;
    return strchr(name, '.') == NULL;
}

/* A floor file: <savefile>.F00 to .F99. */
static _Bool is_floor_file_name(const char *name)
{
    size_t len = strlen(name);

    return len > 4 && name[len - 4] == '.' && name[len - 3] == 'F' &&
        name[len - 2] >= '0' && name[len - 2] <= '9' &&
        name[len - 1] >= '0' && name[len - 1] <= '9';
}

/*
 * Clears what a process ended in the background leaves in lib/save, and finds
 * the character most recently saved.
 *
 * The game keeps the floors it has visited in temporary files beside the
 * savefile while it runs, and writes all of them into the savefile when it
 * saves (save.c). A process that is killed never removes them, and the next
 * start then stops on "Do you delete old temporal files?" -- where No quits.
 * At this point no game is running, so any that exist are left over, and the
 * savefile already holds what they held.
 */
static void tidy_saves(char *latest, size_t latest_size)
{
    DIR *dir = opendir(SAVE_DIR);
    struct dirent *entry;
    time_t newest = 0;
    int cleared = 0;

    latest[0] = '\0';
    if (!dir) return;
    while ((entry = readdir(dir)) != NULL) {
        char path[1024];
        struct stat info;

        snprintf(path, sizeof(path), "%s/%s", SAVE_DIR, entry->d_name);
        if (is_floor_file_name(entry->d_name)) {
            if (unlink(path) == 0) cleared++;
            continue;
        }
        if (!is_savefile_name(entry->d_name) || stat(path, &info) != 0 ||
                !S_ISREG(info.st_mode)) {
            continue;
        }
        if (info.st_mtime >= newest) {
            newest = info.st_mtime;
            snprintf(latest, latest_size, "%s", entry->d_name);
        }
    }
    closedir(dir);
    if (cleared) SDL_Log("Cleared %d floor files left by an ended process", cleared);
}

/* Copies the game's stdout and stderr (quit messages, warnings) to logcat. */
static int SDLCALL forward_output(void *data)
{
    int fd = (int)(intptr_t)data;
    char line[1024];
    size_t used = 0;
    char c;

    while (read(fd, &c, 1) == 1) {
        if (c == '\n' || used == sizeof(line) - 1) {
            line[used] = '\0';
            __android_log_write(ANDROID_LOG_INFO, "Ormeq", line);
            used = 0;
            if (c == '\n') continue;
        }
        line[used++] = c;
    }
    return 0;
}

static void redirect_output_to_logcat(void)
{
    int fds[2];
    SDL_Thread *thread;

    if (pipe(fds) != 0) return;
    setvbuf(stdout, NULL, _IOLBF, 0);
    setvbuf(stderr, NULL, _IONBF, 0);
    dup2(fds[1], STDOUT_FILENO);
    dup2(fds[1], STDERR_FILENO);
    close(fds[1]);
    thread = SDL_CreateThread(forward_output, "stdio-logcat", (void *)(intptr_t)fds[0]);
    if (thread) SDL_DetachThread(thread);
}

/* Android's Back key arrives as AC_BACK; the game treats it as Escape. */
static _Bool SDLCALL map_back_to_escape(void *userdata, SDL_Event *event)
{
    (void)userdata;
    if ((event->type == SDL_EVENT_KEY_DOWN || event->type == SDL_EVENT_KEY_UP) &&
            event->key.key == SDLK_AC_BACK) {
        event->key.key = SDLK_ESCAPE;
        event->key.scancode = SDL_SCANCODE_ESCAPE;
    }
    return 1;
}

/*
 * Whether the last save worked. The game's autosave says so only at the top
 * of a screen nobody is looking at once Android has the app in the background,
 * so every save is noted here: the link step wraps save_player().
 */
static bool last_save_ok = TRUE;
static _Bool background_save_failed;

bool __real_save_player(void);
bool __wrap_save_player(void)
{
    last_save_ok = __real_save_player();
    return last_save_ok;
}

/*
 * Saves the character when Android sends the app to the background.
 *
 * SDL delivers SDL_EVENT_WILL_ENTER_BACKGROUND from the game thread's own
 * event pump -- while the game waits for a key -- and then blocks that thread
 * until the app comes back. The save is the game's own quiet autosave, the
 * one it makes on every change of level, and it is only made where the game
 * itself would allow a save: at the command prompt (can_save). Saving from
 * inside a command, mid-way through the monsters' turn, could compact the
 * monster list under the feet of the code that is walking it.
 *
 * A save that fails is reported when the app comes back: otherwise the play
 * since the last good save would be lost without a word if Android ended it.
 */
static _Bool SDLCALL save_on_background(void *userdata, SDL_Event *event)
{
    Uint64 started;

    (void)userdata;
    if (!SDL_IsMainThread()) return 1;
    if (event->type == SDL_EVENT_DID_ENTER_FOREGROUND && background_save_failed) {
        background_save_failed = 0;
        ormeq_notice("Ormeq could not save the game when you left it. "
            "Menu, then Save and quit, tries again.");
        return 1;
    }
    if (event->type != SDL_EVENT_WILL_ENTER_BACKGROUND) return 1;
    if (!character_generated || !character_dungeon || !p_ptr || !savefile[0]) {
        SDL_Log("Background: no character in play, nothing to save");
        return 1;
    }
    started = SDL_GetTicks();
    if (p_ptr->is_dead) {
        /*
         * Dead, and not yet saved as dead: the game does that once the last
         * messages have been read (close_game), and a phone is often left at
         * the -more- before then until Android ends the app. Its last save was
         * from before the death, which Continue would then bring back. So the
         * death is saved now, as close_game() will save it.
         */
        if (ui_screen_phase() != UI_PHASE_WORLD) return 1;
        save_player();
    } else if (!p_ptr->playing) {
        SDL_Log("Background: no character in play, nothing to save");
        return 1;
    } else if (can_save) {
        do_cmd_save_game(TRUE);
    } else {
        /*
         * Inside a command or a prompt -- the inventory, a spell list, a
         * -more-. Android may end the process while it is in the background,
         * which would lose everything since the last save, so save anyway:
         * the game is waiting for input and its state is whole. Not through
         * do_cmd_save_game(), which processes pending updates and draws in
         * the middle of whatever screen is open, but as the game's own panic
         * save does, writing the file and nothing else. A command interrupted
         * like this is simply not done when the save is loaded.
         */
        (void)strcpy(p_ptr->died_from, "(saved)");
        save_player();
        (void)strcpy(p_ptr->died_from, "(alive and well)");
    }
    if (!last_save_ok) background_save_failed = 1;
    SDL_Log("Background save of %s %s%s, %llu ms", savefile,
        p_ptr->is_dead ? "(dead) " : can_save ? "" : "inside a prompt ",
        last_save_ok ? "done" : "FAILED", (unsigned long long)(SDL_GetTicks() - started));
    return 1;
}

int main(int argc, char *argv[])
{
    char error[512] = "";
    char latest[256] = "";
    static char continue_arg[300];
    char *args[] = { "ormeq", NULL, NULL };
    int count = 1;
    const char *root;

    (void)argc;
    (void)argv;
    redirect_output_to_logcat();
    SDL_SetHint(SDL_HINT_ORIENTATIONS, "LandscapeLeft LandscapeRight");
    SDL_SetHint(SDL_HINT_ANDROID_TRAP_BACK_BUTTON, "1");
    /* The on-screen keyboard appears only when the touch controls ask for
     * it; the desktop frontend keeps SDL text input on permanently. */
    SDL_SetHint(SDL_HINT_ENABLE_SCREEN_KEYBOARD, "0");

    root = SDL_GetAndroidInternalStoragePath();
    if (!root) {
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Ormeq",
            "Internal storage is unavailable.", NULL);
        return 1;
    }
    if (!install_runtime(root, error, sizeof(error))) {
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Ormeq", error, NULL);
        return 1;
    }
    if (chdir(root) != 0) {
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Ormeq", strerror(errno), NULL);
        return 1;
    }
    make_writable_dirs();
    tidy_saves(latest, sizeof(latest));

    /* SDL 3.4.16 bug: on Android, SDL_WaitEvent's loop pushes a poll sentinel
     * on every pass, and each push wakes the wait at once, so the game's
     * blocking waits would spin at 100% CPU. Without sentinels the wait
     * blocks as on desktop. Initialising the events subsystem here keeps the
     * setting when the game's SDL_Init(SDL_INIT_VIDEO) runs. */
    SDL_InitSubSystem(SDL_INIT_EVENTS);
    SDL_SetEventEnabled(SDL_EVENT_POLL_SENTINEL, 0);
    SDL_SetEventFilter(map_back_to_escape, NULL);
    SDL_AddEventWatch(save_on_background, NULL);
    ormeq_android_init();

    /* Continue offers the character saved most recently, as -u does on the
     * desktop. The start screen itself declines a dead one, and a new
     * character's suggested name is bumped from this one (_fresh_name in
     * py_birth.c), so it is not overwritten by accepting the defaults. */
    if (latest[0]) {
        snprintf(continue_arg, sizeof(continue_arg), "-u%s", latest);
        args[count++] = continue_arg;
    }
    return ormeq_main(count, args);
}
