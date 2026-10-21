#!/usr/bin/env python3
"""Checks this repository's published files.

The repository is exactly the files listed in tools/source-files.txt: a new
file is not published until it is listed there. The check fails on

- a listed file that is missing, or a file that is not listed;
- separately licensed artwork or audio: anything under game/lib/art/,
  game/lib/tiles/ or the sound and music packs, and any plate, sound or image
  file except the launcher icon;
- signing files and local settings;
- in the Android edition's own text (everything but game/, which is the
  Windows edition's published source, and the licence texts), a local path,
  an e-mail address, or one of a maintainer's private names
  (tools/private-patterns.txt, one regular expression per line, untracked);
- a broken link in its Markdown files, or a backquoted path into the
  repository that does not exist;
- a game file list naming a missing file: game/runtime-files.txt (what the APK
  installs) and the source lists in game/CMakeLists.txt (what it compiles);
- a missing ORMEQ_VER in game/src/defines.h, which gives the app its version.

Passing it does not establish licence clearance.

  python tools/check_source.py
"""

from __future__ import annotations

import os
import re
import subprocess
import sys
from pathlib import Path, PurePosixPath

ROOT = Path(__file__).resolve().parent.parent
FILE_LIST = "tools/source-files.txt"
LOCAL_PATTERNS = "tools/private-patterns.txt"
RUNTIME_LIST = "game/runtime-files.txt"
GAME_CMAKE = "game/CMakeLists.txt"
DEFINES = "game/src/defines.h"

# Separately licensed media, signing material and build output.
FORBIDDEN_SUFFIXES = {".art", ".wav", ".ogg", ".mp3", ".flac", ".png", ".jpg", ".jpeg", ".gif", ".bmp",
                      ".webp", ".svg", ".ico", ".icns", ".jks", ".keystore", ".p12", ".apk", ".aab",
                      ".so", ".dex", ".zip", ".pyc"}
FORBIDDEN_FOLDERS = ("game/lib/art/", "game/lib/tiles/", "game/lib/xtra/sound/pack/",
                     "game/lib/xtra/music/pack/")
# The binaries the build needs: the Gradle wrapper, the launcher icon (made
# from the Windows edition's own icon) and the game's fonts.
ALLOWED_BINARIES = (re.compile(r"gradle/wrapper/gradle-wrapper\.jar"),
                    re.compile(r"app/src/main/res/mipmap-[a-z]+/ic_launcher\.png"),
                    re.compile(r"game/font/[\w.-]+\.ttf"))
SECRET_NAMES = {"keystore.properties", "local.properties"}
PRIVATE_PATTERNS = (
    r"[A-Za-z]:[/\\]Users[/\\]",
    r"/home/[^/\s]+/|/Users/[^/\s]+/",
    r"(?m)^\s*(?:storePassword|keyPassword)=\S",
)
EMAIL = r"[\w.+-]+@[\w-]+(?:\.[\w-]+)+"
# Others' text, kept as published: the Windows edition's source and the
# licence texts, with their authors' notices.
THIRD_PARTY_TEXT = ("game/", "licenses/")
# Text that names the patterns themselves.
PROSE_EXEMPT = ("tools/check_source.py",)
# What a checkout grows when it is built.
BUILD_FOLDERS = (".git/", ".gradle/", ".kotlin/", ".idea/", "build/", "app/build/", "app/.cxx/",
                 "third_party/", "captures/", "tools/__pycache__/")
LOCAL_FILES = {"local.properties", "keystore.properties", LOCAL_PATTERNS}


def read_list(text: str) -> list[str]:
    names = [line.strip() for line in text.splitlines() if line.strip() and not line.startswith("#")]
    for name in names:
        if "\\" in name or ":" in name or any(part in ("", ".", "..") for part in name.split("/")):
            raise ValueError(f"unsafe path in {FILE_LIST}: {name!r}")
    if len({name.casefold() for name in names}) != len(names):
        raise ValueError(f"duplicate or case-colliding entry in {FILE_LIST}")
    return names


def git(*args: str) -> str:
    result = subprocess.run(["git", "--no-optional-locks", "-C", str(ROOT), *args],
                            capture_output=True, text=True)
    if result.returncode != 0:
        raise ValueError(f"git {' '.join(args)}: {result.stderr.strip()}")
    return result.stdout


def present_files() -> tuple[list[str], list[str]]:
    errors: list[str] = []
    if (ROOT / ".git").exists():
        names = git("ls-files", "-z").split("\0")[:-1]
        for name in git("ls-files", "-z", "--others", "--exclude-standard").split("\0")[:-1]:
            errors.append(f"untracked file (list it in {FILE_LIST} or ignore it): {name}")
        for name in names:
            if PurePosixPath(name).name in SECRET_NAMES:
                errors.append(f"local or secret file committed: {name}")
    else:
        names = []
        for folder, subfolders, files in os.walk(ROOT):
            base = Path(folder).relative_to(ROOT).as_posix()
            prefix = "" if base == "." else base + "/"
            # Never into a build folder (third_party/ may be a link to a large tree).
            subfolders[:] = sorted(d for d in subfolders
                                   if not (prefix + d + "/").startswith(BUILD_FOLDERS) and d != "__pycache__")
            names += [prefix + name for name in files if prefix + name not in LOCAL_FILES]
    return names, errors


def local_patterns() -> list[str]:
    path = ROOT / LOCAL_PATTERNS
    if not path.is_file():
        return []
    return [line.strip() for line in path.read_text(encoding="utf-8").splitlines()
            if line.strip() and not line.startswith("#")]


def check_file(name: str, patterns: list[str], names: set[str]) -> list[str]:
    errors: list[str] = []
    path = PurePosixPath(name)
    suffix = path.suffix.lower()
    binary = suffix in FORBIDDEN_SUFFIXES or suffix in {".jar", ".ttf"}
    if binary and not any(rule.fullmatch(name) for rule in ALLOWED_BINARIES):
        errors.append(f"artwork, audio, binary or signing file: {name}")
    if name.startswith(FORBIDDEN_FOLDERS) or any(part.startswith("_art") for part in path.parts):
        errors.append(f"separately licensed folder: {name}")
    if path.name in SECRET_NAMES:
        errors.append(f"local or secret file: {name}")
    if binary or name.startswith(THIRD_PARTY_TEXT) or name in PROSE_EXEMPT:
        return errors
    try:
        content = (ROOT / name).read_bytes().decode("utf-8").replace("\r\n", "\n")
    except UnicodeDecodeError:
        return errors + [f"not UTF-8 text: {name}"]
    for pattern in patterns:
        if re.search(pattern, content, re.IGNORECASE):
            errors.append(f"private reference in {name}: {pattern}")
    for address in re.findall(EMAIL, content):
        errors.append(f"e-mail address in {name}: {address}")
    if name.endswith(".md"):
        errors.extend(check_links(name, content, names))
    return errors


def exists(target: str, names: set[str]) -> bool:
    return target in names or any(name.startswith(target + "/") for name in names)


def check_links(name: str, content: str, names: set[str]) -> list[str]:
    errors = []
    folder = PurePosixPath(name).parent
    for target in re.findall(r"\[[^\]\n]*\]\(([^)\s]+)\)", content):
        target = target.strip("<>").split("#", 1)[0]
        if not target or re.match(r"[a-z]+:", target):
            continue
        parts: list[str] = []
        for part in (folder / target).parts:
            if part == "..":
                if parts:
                    parts.pop()
            elif part != ".":
                parts.append(part)
        if not exists("/".join(parts), names):
            errors.append(f"broken link in {name}: {target}")
    for target in re.findall(r"`((?:app|game|licenses|tools)/[\w./-]*)`", content):
        target = target.rstrip("/")
        if not (target + "/").startswith(BUILD_FOLDERS) and not exists(target, names):
            errors.append(f"missing path named in {name}: {target}")
    return errors


def check_game(names: set[str]) -> list[str]:
    errors = []
    if RUNTIME_LIST in names:
        for entry in read_list((ROOT / RUNTIME_LIST).read_text(encoding="utf-8")):
            if f"game/{entry}" not in names:
                errors.append(f"{RUNTIME_LIST} names a missing file: {entry}")
            if f"game/{entry}".startswith(FORBIDDEN_FOLDERS):
                errors.append(f"{RUNTIME_LIST} names a separately licensed file: {entry}")
    else:
        errors.append(f"missing {RUNTIME_LIST}")
    if GAME_CMAKE in names:
        text = (ROOT / GAME_CMAKE).read_text(encoding="utf-8")
        blocks = [re.search(r"set\(ANGBAND_SOURCES[^)]*\)", text),
                  re.search(r"set\(FRONTEND_SOURCES src/main-sdl3\.c[^)]*\)", text)]
        if not all(blocks):
            errors.append(f"{GAME_CMAKE} has lost the source lists the Android build reads")
        for block in filter(None, blocks):
            for source in re.findall(r"src/[A-Za-z0-9_-]+\.c", block.group(0)):
                if f"game/{source}" not in names:
                    errors.append(f"{GAME_CMAKE} lists a missing file: {source}")
    else:
        errors.append(f"missing {GAME_CMAKE}")
    if DEFINES not in names or not re.search(r'#define\s+ORMEQ_VER\s+"\d+\.\d+\.\d+"',
                                             (ROOT / DEFINES).read_text(encoding="utf-8", errors="replace")):
        errors.append(f"no ORMEQ_VER in {DEFINES}")
    return errors


def main() -> int:
    try:
        listed = read_list((ROOT / FILE_LIST).read_text(encoding="utf-8"))
        present, errors = present_files()
    except (OSError, ValueError) as error:
        print(f"source check error: {error}", file=sys.stderr)
        return 1
    listed_set, present_set = set(listed), set(present)
    errors += [f"unlisted file: {name}" for name in sorted(present_set - listed_set)]
    errors += [f"missing listed file: {name}" for name in sorted(listed_set - present_set)]
    patterns = list(PRIVATE_PATTERNS) + local_patterns()
    for name in sorted(listed_set & present_set):
        errors += check_file(name, patterns, listed_set)
    errors += check_game(listed_set)
    for error in errors:
        print(f"source check error: {error}", file=sys.stderr)
    if errors:
        return 1
    print(f"Source OK: {len(listed)} files")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
