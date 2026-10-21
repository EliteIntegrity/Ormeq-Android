# Developing Ormeq for Android

This repository is the Android edition's complete source: the game, the app
around it and what builds them. The separately licensed artwork and audio are
not in it, so a build from it draws glyphs, uses the game's synthesised sound
effects and plays no music. The Android edition follows the Windows edition,
with the same version numbers.

## Licence

- **The game's source and data** are under the Angband licence: they may be
  copied and distributed for educational, research and not-for-profit
  purposes, and **may not be sold**. See [game/LICENSE](game/LICENSE) and the
  texts in `game/licenses/`.
- **The artwork, questgiver portraits, sound effects and music are not in this
  repository.** They are licensed separately (see *Artwork* and *Sound effects
  and music* in [game/LICENSE](game/LICENSE)) and come only in the official
  APKs.
- **Third-party libraries and fonts** keep their own licences:
  [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
- A fork should use its own name and application ID (this one's is
  `com.ormeq.game`); an APK signed with another key cannot update an installed
  official release anyway.

## Layout

| Path | Contents |
| --- | --- |
| `game/` | The game: C source, data, fonts and licence texts. It is the Windows edition's source at the same version, with the changes the Android edition needs ([ANDROID-CHANGES.md](ANDROID-CHANGES.md)). It holds only what the Android build compiles and installs. |
| `app/` | The Android app: the Kotlin touch controls, native glue in `app/src/main/cpp/android/`, and the Gradle and CMake build. |
| `licenses/` | Licence texts for what the Android build adds to the game's. |
| `tools/` | Dependency fetchers and the source check. |

## Requirements

- Android SDK platform 36, NDK `30.0.16248370` and CMake `3.31.6` (from
  Android Studio's SDK Manager), and JDK 17 or later (Android Studio's
  bundled one works).
- Git, and Python 3 for the source check.

## Build and install

1. `tools/fetch-deps.sh` (or `tools/fetch-deps.ps1` on Windows) clones SDL
   3.4.16, SDL_image 3.4.6, SDL_ttf 3.2.2 (with FreeType and HarfBuzz) and
   SDL_mixer 3.2.4 into `third_party/`, checking each pinned commit.
2. `./gradlew assembleDebug` (`gradlew.bat` on Windows), with `JAVA_HOME`
   set and the SDK named by `ANDROID_HOME` or by `sdk.dir` in
   `local.properties`. `-Pormeq.abis=x86_64` builds only what an emulator
   needs. Or open the folder in Android Studio after step 1.
3. With a device (USB debugging on) or an emulator connected:
   `./gradlew installDebug`. The app needs Android 8.0 or later on arm64-v8a
   or x86_64.

The build lays out the game's runtime files from `game/` (the list is
`game/runtime-files.txt`) with the index the app installs them from on first
run. `./gradlew assembleRelease` leaves the APK unsigned unless a
`keystore.properties` file (not tracked) names a key with `storeFile`,
`storePassword`, `keyAlias` and `keyPassword`.

Official builds add the separately licensed files from a private asset pack:
a folder laid out as they install (`lib/art/`, `lib/tiles/pack/`,
`lib/xtra/sound/pack/` and `lib/xtra/music/pack/`), named by `ormeq.assetPack`
in `local.properties` or with `-Pormeq.assetPack=...`. The version comes from
`ORMEQ_VER` in `game/src/defines.h`, the Windows edition's version, and
`ormeq.androidRevision` in `gradle.properties`, an Android-only update on top
of it: 0.1.1 with revision 1 is 0.1.1-android.1. The revision goes back to 0
when the game takes the next Windows version.

## Checks

`python tools/check_source.py` checks that the repository holds exactly the
files in `tools/source-files.txt`, with no separately licensed artwork or
audio, no signing files or local paths, working links, and complete game file
lists.

## How it fits together

- `app/src/main/cpp/CMakeLists.txt` builds SDL, then the game from the source
  lists in `game/CMakeLists.txt` (the Windows build's), with the Android glue.
- `android-main.c` installs the runtime files from the APK into internal
  storage on first run or after an update, then runs the game's `main()`.
- `context.c` tells the touch controls what the game is waiting for, through
  link-time wraps of the game's input functions, and feeds their keys in.
- `items.c` offers what can be done with an item, `pins.c` keeps the quick
  slots, `sound.c` opens the audio device only while there is something to
  hear, and `touch-text.c` rewords the game's keyboard hints for touch.
- `OrmeqActivity` (an `SDLActivity`), `ControlLayouts` (the buttons for each
  mode) and `TouchControlsView` (a Canvas overlay) are the Kotlin side. Every
  control presses the game's own keys. [CONTROLS.md](CONTROLS.md) is the
  design of the touch controls.

Bugs and suggestions:
[issues](https://github.com/EliteIntegrity/Ormeq-Android/issues).
