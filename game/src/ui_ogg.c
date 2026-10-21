/* Music and ambience shipped as Ogg Vorbis; see ui_ogg.h. Copyright (c) 2026
 * John Horton. Source terms: LICENSE.
 * Decoded by SDL_mixer's SDL-adapted copy of stb_vorbis (public domain),
 * compiled with SDL_mixer's own configuration, as Ammerow's Android port
 * does: allocation goes through SDL, and all of it happens at open. That copy
 * reads only through an SDL_IOStream -- here one over the file in memory. */
#include <SDL3/SDL.h>
#include "ui_ogg.h"

#define STB_VORBIS_SDL 1  /* SDL_mixer's SDL_IOStream patches */
#define STB_VORBIS_NO_STDIO 1
#define STB_VORBIS_NO_CRT 1
#define STB_VORBIS_NO_PUSHDATA_API 1
#define STB_VORBIS_MAX_CHANNELS 8
#define STB_FORCEINLINE SDL_FORCE_INLINE
#if SDL_BYTEORDER == SDL_BIG_ENDIAN
#define STB_VORBIS_BIG_ENDIAN 1
#endif
#define STBV_CDECL SDLCALL

#ifdef assert
#undef assert
#endif
#ifdef memset
#undef memset
#endif
#ifdef memcpy
#undef memcpy
#endif
#define assert SDL_assert
#define memset SDL_memset
#define memcmp SDL_memcmp
#define memcpy SDL_memcpy
#define qsort SDL_qsort
#define malloc SDL_malloc
#define realloc SDL_realloc
#define free SDL_free

#define pow SDL_pow
#define floor SDL_floor
#define ldexp(v, e) SDL_scalbn((v), (e))
#define abs(x) SDL_abs(x)
#define cos(x) SDL_cos(x)
#define sin(x) SDL_sin(x)
#define log(x) SDL_log(x)
#define exp(x) SDL_exp(x)

#define STB_VORBIS_STDINT_DEFINED 1
typedef Uint8 uint8;
typedef Sint8 int8;
typedef Uint16 uint16;
typedef Sint16 int16;
typedef Uint32 uint32;
typedef Sint32 int32;

#include "stb_vorbis/stb_vorbis.h"

struct ui_ogg
{
    void *data;          /* the whole file, which the decoder reads from */
    stb_vorbis *vorbis;
    int channels;
};

ui_ogg *ui_ogg_open(const char *path, int *rate, int *channels, int *frames)
{
    ui_ogg *ogg;
    stb_vorbis_info info;
    unsigned length;
    SDL_IOStream *io;
    size_t size = 0;
    int error = 0;
    void *data = SDL_LoadFile(path, &size);
    if (!data) return NULL;
    ogg = SDL_calloc(1, sizeof(*ogg));
    if (!ogg) { SDL_free(data); return NULL; }
    ogg->data = data;
    io = SDL_IOFromConstMem(data, size);
    if (!io) { ui_ogg_close(ogg); return NULL; }
    /* The decoder closes the stream when it is closed, though not if it fails. */
    ogg->vorbis = stb_vorbis_open_io(io, 1, &error, NULL);
    if (!ogg->vorbis) { SDL_CloseIO(io); ui_ogg_close(ogg); return NULL; }
    info = stb_vorbis_get_info(ogg->vorbis);
    ogg->channels = info.channels;
    *rate = (int)info.sample_rate;
    *channels = info.channels;
    length = stb_vorbis_stream_length_in_samples(ogg->vorbis);
    *frames = (int)SDL_min(length, (unsigned)SDL_MAX_SINT32);
    return ogg;
}

int ui_ogg_read(ui_ogg *ogg, float *out, int frames)
{
    return stb_vorbis_get_samples_float_interleaved(ogg->vorbis, ogg->channels, out, frames * ogg->channels);
}

void ui_ogg_seek(ui_ogg *ogg, int frame)
{
    if (frame <= 0) stb_vorbis_seek_start(ogg->vorbis);
    else stb_vorbis_seek(ogg->vorbis, (unsigned)frame);
}

void ui_ogg_close(ui_ogg *ogg)
{
    if (!ogg) return;
    if (ogg->vorbis) stb_vorbis_close(ogg->vorbis);
    SDL_free(ogg->data);
    SDL_free(ogg);
}
