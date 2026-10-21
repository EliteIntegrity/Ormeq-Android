/* Optional title music on its own SDL3 stream. No gameplay state or RNG.
 * Copyright (c) 2026 John Horton. Source terms: LICENSE.
 * Keep a single bounded PCM16 master, converting only callback-sized blocks.
 * There is no file I/O, allocation or game access on the audio thread. */
#include <SDL3/SDL.h>
#include "scottish.h"
#include "ui_music.h"
#ifdef ORMEQ_AUDIO_OGG
#include "ui_ogg.h"
#endif

#define MUSIC_BLOCK 512
#define MUSIC_MAX_BYTES (128u * 1024u * 1024u)
#define MUSIC_MAX_SECONDS 600

static SDL_AudioStream *_music_stream;
static Uint8 *_music_pcm;
static int _music_frames, _music_rate, _music_channels, _music_cursor;
static float _music_gain, _music_target;
static int _music_ramp;
static bool _music_enabled = TRUE, _music_active, _music_focused = TRUE;
static int _music_volume = 30;
static bool _music_owns_audio;
static cptr _music_status = "Not installed";

#ifdef ORMEQ_AUDIO_OGG
/* A host that ships the track compressed (Android) decodes it a block at a
 * time as it plays; see ui_ogg.h. */
static ui_ogg *_music_ogg;
static float _music_block[MUSIC_BLOCK * 2];
static int _music_block_start, _music_block_len;
#define MUSIC_LOADED() (_music_pcm || _music_ogg)

/* The mixer reads frames in order, and from 0 again after a loop or when the
 * title returns: decode the next block when it gets there. */
static float _music_ogg_sample(int frame, int ch)
{
    if (frame < _music_block_start || frame >= _music_block_start + _music_block_len)
    {
        int want, got;
        if (frame != _music_block_start + _music_block_len) ui_ogg_seek(_music_ogg, frame);
        _music_block_start = frame;
        want = MIN(MUSIC_BLOCK, _music_frames - frame);
        if (want <= 0) return 0.0f;
        /* Read once: MAX() would decode a second block over the first. */
        got = ui_ogg_read(_music_ogg, _music_block, want);
        _music_block_len = MAX(0, got);
        /* Short of the length the file declared: silence until the loop. */
        if (_music_block_len < want)
        {
            SDL_memset(_music_block + _music_block_len * _music_channels, 0,
                (want - _music_block_len) * _music_channels * sizeof(float));
            _music_block_len = want;
        }
    }
    return _music_block[(frame - _music_block_start) * _music_channels + (_music_channels == 1 ? 0 : ch)];
}
#else
#define MUSIC_LOADED() (_music_pcm != NULL)
#endif

static void _music_target_gain(int ms)
{
    _music_target = _music_enabled && _music_active && _music_focused
        ? _music_volume / 100.0f : 0.0f;
    _music_ramp = MAX(1, _music_rate * ms / 1000);
}

static float _music_sample(int frame, int ch)
{
#ifdef ORMEQ_AUDIO_OGG
    if (_music_ogg) return _music_ogg_sample(frame, ch);
#endif
    const Uint8 *p = _music_pcm + (frame * _music_channels + (_music_channels == 1 ? 0 : ch)) * 2;
    int sample = p[0] | (p[1] << 8);
    if (sample >= 32768) sample -= 65536;
    return sample / 32768.0f;
}

static void _music_mix(float *out, int frames)
{
    int f, ch, edge = MIN(_music_rate / 100, _music_frames / 4);
    SDL_memset(out, 0, frames * 2 * sizeof(float));
    if (!MUSIC_LOADED() || !_music_enabled || !_music_focused || !_music_volume) return;
    for (f = 0; f < frames; ++f)
    {
        float envelope = 1.0f;
        if (_music_ramp)
        {
            _music_gain += (_music_target - _music_gain) / _music_ramp;
            if (!--_music_ramp) _music_gain = _music_target;
        }
        if (!_music_active && !_music_ramp && _music_gain == 0.0f)
        { _music_cursor = 0; break; }
        /* Ten-millisecond edges avoid clicks at a nonzero loop boundary.
         * Musical loop/tail quality is still reviewed with the final track. */
        if (edge > 0)
            envelope = MIN(1.0f, MIN(_music_cursor, _music_frames - 1 - _music_cursor) / (float)edge);
        for (ch = 0; ch < 2; ++ch)
            out[f*2 + ch] = _music_sample(_music_cursor, ch) * _music_gain * envelope;
        if (++_music_cursor == _music_frames) _music_cursor = 0;
    }
}

static void SDLCALL _music_fill(void *unused, SDL_AudioStream *stream, int additional, int total)
{
    float buf[MUSIC_BLOCK * 2];
    const int frame_bytes = 2 * sizeof(float);
    (void)unused; (void)total;
    while (additional > 0)
    {
        int frames = MIN(MUSIC_BLOCK, (additional + frame_bytes - 1) / frame_bytes);
        _music_mix(buf, frames);
        if (!SDL_PutAudioStreamData(stream, buf, frames * frame_bytes)) return;
        additional -= frames * frame_bytes;
    }
}

static bool _music_load(const char *path)
{
    SDL_PathInfo info;
    SDL_AudioSpec src, mixer;
    Uint8 *pcm = NULL;
    Uint32 bytes = 0;
    const char *hint;
    char *old_hint;
    bool loaded;
    if (_music_stream) return TRUE;
    _music_status = "Not installed";
    if (!SDL_GetPathInfo(path, &info)) return FALSE;
    _music_status = "Track could not be loaded";
    if (info.type != SDL_PATHTYPE_FILE || info.size > MUSIC_MAX_BYTES) return FALSE;
    /* SDL normally tolerates truncated WAVs. Fail quietly instead of looping
     * a damaged partial track; restore the caller's hint after this load. */
    hint = SDL_GetHint(SDL_HINT_WAVE_TRUNCATION);
    old_hint = hint ? SDL_strdup(hint) : NULL;
    if (hint && !old_hint) return FALSE;
    SDL_SetHintWithPriority(SDL_HINT_WAVE_TRUNCATION, "verystrict", SDL_HINT_OVERRIDE);
    loaded = SDL_LoadWAV(path, &src, &pcm, &bytes);
    if (old_hint) SDL_SetHintWithPriority(SDL_HINT_WAVE_TRUNCATION, old_hint, SDL_HINT_OVERRIDE);
    else SDL_ResetHint(SDL_HINT_WAVE_TRUNCATION);
    SDL_free(old_hint);
    if (!loaded) return FALSE;
    if ((src.freq != 44100 && src.freq != 48000) || src.format != SDL_AUDIO_S16LE ||
        (src.channels != 1 && src.channels != 2) || !bytes || bytes % (src.channels * 2) ||
        bytes > (Uint32)(src.freq * src.channels * 2 * MUSIC_MAX_SECONDS))
    { SDL_free(pcm); return FALSE; }
    _music_status = "Audio unavailable";
    if (!SDL_WasInit(SDL_INIT_AUDIO))
    {
        if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) { SDL_free(pcm); return FALSE; }
        _music_owns_audio = TRUE;
    }
    _music_pcm = pcm;
    _music_rate = src.freq;
    _music_channels = src.channels;
    _music_frames = bytes / (src.channels * 2);
    _music_cursor = 0; _music_gain = 0.0f;
    _music_target_gain(700);
    mixer.format = SDL_AUDIO_F32; mixer.channels = 2; mixer.freq = src.freq;
    _music_stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &mixer, _music_fill, NULL);
    if (!_music_stream || !SDL_ResumeAudioStreamDevice(_music_stream))
    { ui_music_shutdown(); _music_status = "Audio unavailable"; return FALSE; }
    _music_status = "Ready";
    return TRUE;
}

#ifdef ORMEQ_AUDIO_OGG
/* As _music_load, with the track decoded as it plays instead of held as PCM. */
static bool _music_load_ogg(const char *path)
{
    SDL_PathInfo info;
    SDL_AudioSpec mixer;
    int rate = 0, channels = 0, frames = 0;
    if (_music_stream) return TRUE;
    _music_status = "Not installed";
    if (!SDL_GetPathInfo(path, &info)) return FALSE;
    _music_status = "Track could not be loaded";
    if (info.type != SDL_PATHTYPE_FILE || info.size > MUSIC_MAX_BYTES) return FALSE;
    _music_ogg = ui_ogg_open(path, &rate, &channels, &frames);
    if (!_music_ogg) return FALSE;
    if ((rate != 44100 && rate != 48000) || (channels != 1 && channels != 2) ||
        frames <= 0 || frames > rate * MUSIC_MAX_SECONDS)
    { ui_ogg_close(_music_ogg); _music_ogg = NULL; return FALSE; }
    _music_status = "Audio unavailable";
    if (!SDL_WasInit(SDL_INIT_AUDIO))
    {
        if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) { ui_ogg_close(_music_ogg); _music_ogg = NULL; return FALSE; }
        _music_owns_audio = TRUE;
    }
    _music_rate = rate;
    _music_channels = channels;
    _music_frames = frames;
    _music_cursor = 0; _music_gain = 0.0f;
    _music_block_start = _music_block_len = 0;
    _music_target_gain(700);
    mixer.format = SDL_AUDIO_F32; mixer.channels = 2; mixer.freq = rate;
    _music_stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &mixer, _music_fill, NULL);
    if (!_music_stream || !SDL_ResumeAudioStreamDevice(_music_stream))
    { ui_music_shutdown(); _music_status = "Audio unavailable"; return FALSE; }
    _music_status = "Ready";
    return TRUE;
}
#endif

void ui_music_init(void)
{
    char path[1200];
    const char *base = SDL_GetBasePath();
#ifdef ORMEQ_AUDIO_OGG
    /* Where the track is shipped compressed, title.ogg stands in its place. */
    SDL_snprintf(path, sizeof(path), "%slib/xtra/music/pack/title.ogg", base ? base : "./");
    if (SDL_GetPathInfo(path, NULL)) { _music_load_ogg(path); return; }
#endif
    SDL_snprintf(path, sizeof(path), "%slib/xtra/music/pack/title.wav", base ? base : "./");
    _music_load(path);
}

void ui_music_options(bool enabled, int volume)
{
    volume = MAX(0, MIN(100, volume));
    if (_music_enabled == enabled && _music_volume == volume) return;
    if (_music_stream && !SDL_LockAudioStream(_music_stream)) return;
    _music_enabled = enabled; _music_volume = volume;
    if (!enabled || !volume)
    {
        _music_gain = 0.0f;
        if (_music_stream) SDL_ClearAudioStream(_music_stream);
    }
    _music_target_gain(100);
    if (_music_stream) SDL_UnlockAudioStream(_music_stream);
}

void ui_music_title(bool active)
{
    if (_music_active == active) return;
    if (_music_stream && !SDL_LockAudioStream(_music_stream)) return;
    _music_active = active;
    if (active) _music_cursor = 0;
    else if (!_music_enabled || !_music_volume || !_music_focused) _music_cursor = 0;
    _music_target_gain(active ? 700 : 250);
    if (_music_stream) SDL_UnlockAudioStream(_music_stream);
}

void ui_music_focus(bool focused)
{
    if (_music_focused == focused) return;
    if (_music_stream && !SDL_LockAudioStream(_music_stream)) return;
    _music_focused = focused;
    if (!focused)
    {
        _music_gain = 0.0f;
        if (_music_stream) SDL_ClearAudioStream(_music_stream);
    }
    _music_target_gain(700);
    if (_music_stream) SDL_UnlockAudioStream(_music_stream);
}

cptr ui_music_status(void) { return _music_status; }

/* Whether the track is playing or still fading out: a host that opens the
 * audio device only while there is something to hear (Android) asks before
 * each wait for input. Read without the stream's lock, so it may be a moment
 * behind. */
bool ui_music_audible(void)
{
    return _music_stream && _music_enabled && _music_volume && _music_focused
        && (_music_active || _music_gain > 0.0f);
}

void ui_music_shutdown(void)
{
    if (_music_stream) SDL_DestroyAudioStream(_music_stream);
    _music_stream = NULL;
    SDL_free(_music_pcm); _music_pcm = NULL;
#ifdef ORMEQ_AUDIO_OGG
    ui_ogg_close(_music_ogg); _music_ogg = NULL;
    _music_block_start = _music_block_len = 0;
#endif
    _music_frames = _music_rate = _music_channels = _music_cursor = 0;
    _music_gain = _music_target = 0.0f; _music_ramp = 0;
    _music_active = FALSE;
    if (_music_owns_audio) SDL_QuitSubSystem(SDL_INIT_AUDIO);
    _music_owns_audio = FALSE;
    _music_status = "Not installed";
}
