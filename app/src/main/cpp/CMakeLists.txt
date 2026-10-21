cmake_minimum_required(VERSION 3.22)
project(OrmeqAndroid LANGUAGES C)

# Debug builds stay debuggable but optimised: data parsing and level
# generation are far too slow at -O0 on phones.
add_compile_options($<$<CONFIG:Debug>:-O2>)

get_filename_component(REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../../.." ABSOLUTE)
set(THIRD_PARTY "${REPO_ROOT}/third_party")
if(NOT EXISTS "${THIRD_PARTY}/SDL/CMakeLists.txt")
    message(FATAL_ERROR "SDL sources missing: run tools/fetch-deps.ps1")
endif()

# SDL and its satellite libraries, built from the pinned sources.
set(SDL_SHARED ON CACHE BOOL "" FORCE)
set(SDL_STATIC OFF CACHE BOOL "" FORCE)
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
add_subdirectory("${THIRD_PARTY}/SDL" SDL)

# PNG (the optional tile sheets) through the built-in stb decoder; no external
# image libraries.
set(SDLIMAGE_VENDORED OFF CACHE BOOL "" FORCE)
set(SDLIMAGE_BACKEND_STB ON CACHE BOOL "" FORCE)
set(SDLIMAGE_PNG_LIBPNG OFF CACHE BOOL "" FORCE)
set(SDLIMAGE_AVIF OFF CACHE BOOL "" FORCE)
set(SDLIMAGE_JXL OFF CACHE BOOL "" FORCE)
set(SDLIMAGE_TIF OFF CACHE BOOL "" FORCE)
set(SDLIMAGE_WEBP OFF CACHE BOOL "" FORCE)
set(SDLIMAGE_SAMPLES OFF CACHE BOOL "" FORCE)
set(SDLIMAGE_TESTS OFF CACHE BOOL "" FORCE)
add_subdirectory("${THIRD_PARTY}/SDL_image" SDL_image)

# FreeType and HarfBuzz come from SDL_ttf's vendored submodules.
set(SDLTTF_VENDORED ON CACHE BOOL "" FORCE)
set(SDLTTF_HARFBUZZ ON CACHE BOOL "" FORCE)
set(SDLTTF_PLUTOSVG OFF CACHE BOOL "" FORCE)
set(SDLTTF_SAMPLES OFF CACHE BOOL "" FORCE)
add_subdirectory("${THIRD_PARTY}/SDL_ttf" SDL_ttf)

# The game: the Windows edition's source with the Android changes
# (ANDROID-CHANGES.md).
set(GAME "${REPO_ROOT}/game")
if(NOT EXISTS "${GAME}/src/main-sdl3.c")
    message(FATAL_ERROR "Game source missing: ${GAME}")
endif()

# The source lists come from the Windows build's CMakeLists.txt
# (game/CMakeLists.txt), so a new module needs no change here: every entry
# of ANGBAND_SOURCES, plus the C files of the sdl3 FRONTEND_SOURCES (its .rc
# icon is Windows-only).
file(READ "${GAME}/CMakeLists.txt" GAME_CMAKE)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${GAME}/CMakeLists.txt")
string(REGEX MATCH "set\\(ANGBAND_SOURCES[^)]*\\)" CORE_BLOCK "${GAME_CMAKE}")
string(REGEX MATCHALL "src/[A-Za-z0-9_-]+\\.c" CORE_SOURCES "${CORE_BLOCK}")
string(REGEX MATCH "set\\(FRONTEND_SOURCES src/main-sdl3\\.c[^)]*\\)" SDL3_BLOCK "${GAME_CMAKE}")
string(REGEX MATCHALL "src/[A-Za-z0-9_-]+\\.c" SDL3_SOURCES "${SDL3_BLOCK}")
list(REMOVE_DUPLICATES CORE_SOURCES)
list(REMOVE_DUPLICATES SDL3_SOURCES)
list(LENGTH CORE_SOURCES CORE_COUNT)
list(LENGTH SDL3_SOURCES SDL3_COUNT)
if(CORE_COUNT LESS 150 OR SDL3_COUNT LESS 5)
    message(FATAL_ERROR "Could not read the desktop source lists (${CORE_COUNT} core, ${SDL3_COUNT} SDL3)")
endif()
message(STATUS "Ormeq: ${CORE_COUNT} core sources, ${SDL3_COUNT} SDL3 frontend sources")
list(TRANSFORM CORE_SOURCES PREPEND "${GAME}/")
list(TRANSFORM SDL3_SOURCES PREPEND "${GAME}/")

# The title music and ambience are shipped as Ogg Vorbis (change 0010),
# decoded by SDL_mixer's SDL-adapted stb_vorbis (public domain); only that
# header of SDL_mixer is used.
if(NOT EXISTS "${THIRD_PARTY}/SDL_mixer/src/stb_vorbis/stb_vorbis.h")
    message(FATAL_ERROR "stb_vorbis missing: run tools/fetch-deps.ps1")
endif()
set_source_files_properties("${GAME}/src/ui_ogg.c" PROPERTIES
    INCLUDE_DIRECTORIES "${THIRD_PARTY}/SDL_mixer/src"
    COMPILE_OPTIONS "-Wno-unused-value;-Wno-unused-function")

add_library(main SHARED
    ${CORE_SOURCES}
    ${SDL3_SOURCES}
    "${GAME}/src/ui_ogg.c"
    android/android-main.c
    android/context.c
    android/items.c
    android/pins.c
    android/sound.c
    android/touch-text.c)
set_target_properties(main PROPERTIES C_STANDARD 99)
target_include_directories(main PRIVATE "${GAME}/src")

# The desktop build's own defines, less the Windows ones: a Unix build of the
# same game. mkdir() and mkstemp() exist here, and without HAVE_MKDIR the game
# quietly fails to create its save and user directories. Wizard commands stay
# off, as on the desktop by default.
target_compile_definitions(main PRIVATE
    USE_SDL3 DISABLE_WIZARD HAVE_MKDIR HAVE_MKSTEMP ORMEQ_AUDIO_OGG)

# The desktop main() becomes ormeq_main(), called by android-main.c once the
# runtime files are installed. SDL_MAIN_HANDLED stops SDL_main.h renaming it.
set_source_files_properties("${GAME}/src/main-sdl3.c" PROPERTIES
    COMPILE_DEFINITIONS "SDL_MAIN_HANDLED;main=ormeq_main")

# The inherited code is 1990s C, warned about at length by a modern compiler;
# the desktop build waives the same classes under MSVC (see its CMakeLists).
target_compile_options(main PRIVATE
    -Wno-comment -Wno-unused-variable -Wno-unused-but-set-variable
    -Wno-unused-function -Wno-misleading-indentation -Wno-parentheses
    -Wno-logical-not-parentheses -Wno-tautological-overlap-compare
    -Wno-pointer-sign -Wno-char-subscripts)

# Every wait for input passes through android/context.c, which tells the touch
# controls what the game is waiting for and feeds queued commands one key per
# wait; item prompts pass through android/items.c, which answers one with the
# item an action was chosen for; the game's sound streams and its effects
# pass through android/sound.c, which holds an audio device open only while
# there is something to hear;
# and the functions that put keyboard hints on screen pass through
# android/touch-text.c, which rewords them for touch; the rows a document
# declares to the frontend, and the menu reader of character creation, pass
# through android/context.c too, which tells the controls what a page there
# offers; and every save passes
# through android/android-main.c, which notes whether it worked. A wrap only
# catches calls from other files, which is where these are called from.
target_link_options(main PRIVATE
    -Wl,--wrap=SDL_WaitEvent
    -Wl,--wrap=SDL_WaitEventTimeout
    -Wl,--wrap=obj_prompt
    -Wl,--wrap=SDL_OpenAudioDeviceStream
    -Wl,--wrap=SDL_ResumeAudioStreamDevice
    -Wl,--wrap=SDL_DestroyAudioStream
    -Wl,--wrap=ui_sound_play
    -Wl,--wrap=doc_insert
    -Wl,--wrap=doc_printf
    -Wl,--wrap=get_com
    -Wl,--wrap=prt
    -Wl,--wrap=c_prt
    -Wl,--wrap=put_str
    -Wl,--wrap=c_put_str
    -Wl,--wrap=msg_prompt
    -Wl,--wrap=msg_print
    -Wl,--wrap=cmsg_print
    -Wl,--wrap=Term_putstr
    -Wl,--wrap=Term_addstr
    -Wl,--wrap=ui_event_menu_clear
    -Wl,--wrap=ui_event_menu_item
    -Wl,--wrap=ui_event_menu_select
    -Wl,--wrap=ui_menu_inkey_nav
    -Wl,--wrap=save_player)

target_link_libraries(main PRIVATE
    SDL3::SDL3
    SDL3_image::SDL3_image
    SDL3_ttf::SDL3_ttf
    android
    log
    m)
