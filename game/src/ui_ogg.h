/* Music and ambience shipped as Ogg Vorbis (ORMEQ_AUDIO_OGG). Copyright (c)
 * 2026 John Horton. Source terms: LICENSE.
 * For a host that ships its long recordings compressed (Android, where the
 * WAVs would come to some 190 MB). Each file is held in memory as it is and
 * decoded a block at a time as it plays, so the audio thread neither reads
 * files nor allocates, and a megabyte or two stands in for each PCM master. */
#ifndef INCLUDED_UI_OGG_H
#define INCLUDED_UI_OGG_H

typedef struct ui_ogg ui_ogg;

/* NULL if the file cannot be read or is not Ogg Vorbis. */
extern ui_ogg *ui_ogg_open(const char *path, int *rate, int *channels, int *frames);

/* Decodes up to frames frames, interleaved, at the file's own channel count;
 * fewer only at the end. */
extern int ui_ogg_read(ui_ogg *ogg, float *out, int frames);

extern void ui_ogg_seek(ui_ogg *ogg, int frame);
extern void ui_ogg_close(ui_ogg *ogg);

#endif
