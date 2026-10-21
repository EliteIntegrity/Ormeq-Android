#!/bin/sh
# Fetches the pinned third-party sources into third_party/ (not tracked in Git).
# The same pins as tools/fetch-deps.ps1. Pass --force to fetch again.
set -eu
root=$(cd "$(dirname "$0")/.." && pwd)
mkdir -p "$root/third_party"

fetch() { # name url tag commit submodules
    dir="$root/third_party/$1"
    if [ -d "$dir/.git" ] && [ "$(git -C "$dir" rev-parse HEAD)" = "$4" ] && [ "${FORCE:-}" != 1 ]; then
        echo "$1: $3 already present"
        return
    fi
    rm -rf "$dir"
    echo "$1: cloning $3"
    if [ "$5" = yes ]; then
        git clone --depth 1 --branch "$3" -c advice.detachedHead=false --recurse-submodules --shallow-submodules "$2" "$dir"
    else
        git clone --depth 1 --branch "$3" -c advice.detachedHead=false "$2" "$dir"
    fi
    [ "$(git -C "$dir" rev-parse HEAD)" = "$4" ] || { echo "$1: $3 is not the pinned commit $4" >&2; exit 1; }
}

[ "${1:-}" = --force ] && FORCE=1
# Ormeq mixes its own sound: of SDL_mixer only its SDL-adapted stb_vorbis is
# used, for the Ogg music and ambience. SDL_ttf brings FreeType and HarfBuzz.
fetch SDL       https://github.com/libsdl-org/SDL.git       release-3.4.16 fa2c02bb6e21974a89ea9824bc53c9932abe5f9c no
fetch SDL_image https://github.com/libsdl-org/SDL_image.git release-3.4.6  f661fa1ad24ab1b81e43662532f9a6a9fcf67ea6 no
fetch SDL_ttf   https://github.com/libsdl-org/SDL_ttf.git   release-3.2.2  a1ce3670aec736ecbf0936c43f2f0cc53aa61e5b yes
fetch SDL_mixer https://github.com/libsdl-org/SDL_mixer.git release-3.2.4  72a81869b45e249e8e67102db4e98dd2441f05a1 no
echo "Third-party sources ready."
