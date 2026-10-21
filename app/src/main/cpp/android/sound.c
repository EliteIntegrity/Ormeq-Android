/*
 * Sound without an audio device held open.
 *
 * The desktop opens its effects mixer (ui_sound.c), its title music
 * (ui_music.c) and its ambience (ui_ambient.c) once at start-up, each a
 * stream on a device of its own that runs for the life of the program, its
 * callback mixing silence when there is nothing to play. On a phone that
 * keeps the audio path awake, and the callbacks waking the processor, the
 * whole time the game is open.
 *
 * So the streams the game opens are made without a device (the calls are
 * wrapped at link time, see CMakeLists.txt) and share one that is open only
 * while there is something to hear: from an effect until a few seconds after
 * the last, and while the title music or the ambience plays or fades
 * (ui_music_audible, ui_ambient_audible: change 0010). With the ambience on,
 * that is all the time the game is played; with it off, the device sleeps
 * between sounds. An effect opens it as it starts; the waits for input
 * (android/context.c) open it for the music and ambience, and close it.
 */

#include <SDL3/SDL.h>

#include "sdl3_local.h"
#include "ui_music.h"
#include "ui_ambient.h"
#include "ormeq-android.h"

/* Long enough for any effect to finish (the longest is under a second and a
 * half), and for a fight's sounds to share one device rather than open one
 * each. */
#define QUIET_CLOSE_MS 5000

/* How often a wait looks again while music or ambience plays, to close the
 * device once it has faded out. */
#define MUSIC_CHECK_MS 500

/* After a device would not open, it is not tried again at once. */
#define RETRY_MS 2000

#define MAX_STREAMS 4

static SDL_AudioStream *streams[MAX_STREAMS];
static int stream_count;
static SDL_AudioSpec device_spec;
static SDL_AudioDeviceID device;
static Uint64 last_effect, failed_at;

static _Bool managed(SDL_AudioStream *stream)
{
    int i;

    for (i = 0; i < stream_count; i++) {
        if (streams[i] == stream) return 1;
    }
    return 0;
}

static void open_device(void)
{
    Uint64 now = SDL_GetTicks();

    if (device || !stream_count || (failed_at && now - failed_at < RETRY_MS)) return;
    /* A device opened this way starts playing; binding gives the streams
     * their output format. */
    device = SDL_OpenAudioDevice(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &device_spec);
    if (device && !SDL_BindAudioStreams(device, streams, stream_count)) {
        SDL_CloseAudioDevice(device);
        device = 0;
    }
    failed_at = device ? 0 : now;
}

static void close_device(void)
{
    /* Closing unbinds the streams; each keeps what it was playing. */
    if (device) SDL_CloseAudioDevice(device);
    device = 0;
}

SDL_AudioStream *__real_SDL_OpenAudioDeviceStream(SDL_AudioDeviceID devid,
    const SDL_AudioSpec *spec, SDL_AudioStreamCallback callback, void *userdata);
void __real_SDL_DestroyAudioStream(SDL_AudioStream *stream);

SDL_AudioStream *__wrap_SDL_OpenAudioDeviceStream(SDL_AudioDeviceID devid,
    const SDL_AudioSpec *spec, SDL_AudioStreamCallback callback, void *userdata)
{
    SDL_AudioStream *stream;

    /* The kind the game opens: a stream on the default device, filled by its
     * callback whenever the device wants more. */
    if (!callback || !spec || devid != SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK || stream_count == MAX_STREAMS) {
        return __real_SDL_OpenAudioDeviceStream(devid, spec, callback, userdata);
    }
    stream = SDL_CreateAudioStream(spec, spec);
    if (!stream) return NULL;
    if (!SDL_SetAudioStreamGetCallback(stream, callback, userdata)) {
        __real_SDL_DestroyAudioStream(stream);
        return NULL;
    }
    if (!stream_count) device_spec = *spec;
    streams[stream_count++] = stream;
    if (device) SDL_BindAudioStream(device, stream);
    return stream;
}

bool __real_SDL_ResumeAudioStreamDevice(SDL_AudioStream *stream);

bool __wrap_SDL_ResumeAudioStreamDevice(SDL_AudioStream *stream)
{
    /* Its device comes and goes; the game need not know. */
    if (managed(stream)) return true;
    return __real_SDL_ResumeAudioStreamDevice(stream);
}

void __wrap_SDL_DestroyAudioStream(SDL_AudioStream *stream)
{
    int i;

    for (i = 0; i < stream_count; i++) {
        if (streams[i] != stream) continue;
        streams[i] = streams[--stream_count];
        if (!stream_count) close_device();
        break;
    }
    /* Destroying a bound stream unbinds it first, so its callback has
     * finished before the game frees what the callback reads. */
    __real_SDL_DestroyAudioStream(stream);
}

void __real_ui_sound_play(int event);

void __wrap_ui_sound_play(int event)
{
    /* An effect starts at once, so its device opens first -- but not for
     * effects the player has turned off. */
    if (display_sound_get() && display_sound_volume_get() > 0) {
        last_effect = SDL_GetTicks();
        open_device();
    }
    __real_ui_sound_play(event);
}

int ormeq_sound_update(void)
{
    Uint64 since = SDL_GetTicks() - last_effect;
    int effects = last_effect && since < QUIET_CLOSE_MS ? (int)(QUIET_CLOSE_MS - since) : -1;

    if (ui_music_audible() || ui_ambient_audible()) {
        open_device();
        return effects >= 0 ? SDL_min(effects, MUSIC_CHECK_MS) : MUSIC_CHECK_MS;
    }
    if (effects >= 0) return effects;
    close_device();
    return -1;
}
