# Ormeq for Android: third-party notices

This file lists the third-party components in the Android APK. The licence
texts are in `game/licenses/`, which the app installs with the game, and in
`licenses/` for what only the Android build uses. Update it whenever a pinned
dependency changes.

## SDL libraries

The APK contains `libSDL3.so`, `libSDL3_image.so` and `libSDL3_ttf.so`, built
from these sources (pinned in `tools/fetch-deps.ps1` and `tools/fetch-deps.sh`),
together with SDL's Java glue (`android-project/app/src/main/java`):

| library | release | source revision |
|---|---:|---|
| SDL | 3.4.16 | `fa2c02bb6e21974a89ea9824bc53c9932abe5f9c` |
| SDL_image | 3.4.6 | `f661fa1ad24ab1b81e43662532f9a6a9fcf67ea6` |
| SDL_ttf | 3.2.2 | `a1ce3670aec736ecbf0936c43f2f0cc53aa61e5b` |
| SDL_mixer | 3.2.4 | `72a81869b45e249e8e67102db4e98dd2441f05a1` |

SDL, SDL_image and SDL_ttf use the zlib licence: `game/licenses/SDL3.txt`,
`game/licenses/SDL3_image.txt` and `game/licenses/SDL3_ttf.txt`. Only
SDL_mixer's SDL-adapted copy of stb_vorbis is used, compiled into
`libmain.so` to decode the music and ambience; the rest of SDL_mixer is not
built. SDL_mixer's zlib licence: `licenses/SDL_mixer.txt`.

### Code inside those libraries

- HIDAPI and other code in SDL with notices of its own:
  `game/licenses/SDL-hidapi-BSD.txt`, `game/licenses/SDL-Khronos.txt`,
  `game/licenses/SDL-libm.txt` and `game/licenses/SDL-yuv2rgb.txt`.
- FreeType 2.13.2, revision `9973564cfa63763a3e4ac67c09147899539b1e07`, built
  into SDL_ttf, under the FreeType Project Licence. Portions of this software
  are copyright (c) 1996-2023 The FreeType Project (www.freetype.org). All
  rights reserved. `game/licenses/FreeType-FTL.txt` and the other
  `game/licenses/FreeType-*.txt`.
- HarfBuzz 8.5.0, revision `564bf9818a18709776856533829c0c04950773d6`, built
  into SDL_ttf, under its MIT licence, including the separately noticed
  Microsoft Universal Shaping Engine data: `game/licenses/HarfBuzz.txt` and
  `game/licenses/HarfBuzz-Microsoft-USE.txt`.
- stb_image (SDL_image's PNG decoder), stb_rect_pack (SDL_ttf) and stb_vorbis
  (from SDL_mixer), by Sean Barrett and contributors, under their offered MIT
  or public-domain terms: `game/licenses/SDL_image-stb_image.txt` and
  `licenses/stb.txt`.

PlutoSVG and PlutoVG, which SDL_ttf can also include, are not built.

## Kotlin standard library

The app's Kotlin code uses the Kotlin standard library, compiled into
`classes.dex`. Copyright JetBrains s.r.o. and Kotlin Programming Language
contributors. Apache License 2.0: `game/licenses/Apache-2.0.txt`.

## Fonts

The five fonts in `game/font/` are described in `game/licenses/fonts.txt`,
under the SIL Open Font License 1.1 (`game/licenses/OFL-1.1.txt`) and the MIT
licence (`game/licenses/MIT-Cozette.txt`).

## Build tools

`gradle/wrapper/gradle-wrapper.jar` is part of Gradle, under the Apache
License 2.0 (`game/licenses/Apache-2.0.txt`). It is not in the APK.
