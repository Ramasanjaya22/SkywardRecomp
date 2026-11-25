#!/usr/bin/env python3
"""Frozen skywardrecomp.ini keys and saved value names. No game files needed.

Old ini files must keep their meaning when new settings (e.g. a new upscaler)
land: keys and saved strings may only be appended, never renamed, removed or
reordered. Each frozen list below is a prefix of what settings.c writes.
"""

from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SETTINGS_C = ROOT / "runtime" / "settings.c"
BS = chr(92)

# Every key settings_save writes to skywardrecomp.ini, in write order.
# Append new keys at the end; never rename, remove or reorder an old one.
FROZEN_KEYS = (
    "aspect", "fullscreen", "volume", "reverb", "movie_audio", "show_fps",
    "fps", "anisotropy", "lod_bias", "upscaler", "antialias", "sharpen",
    "shadows", "ambient_occlusion", "resolution", "inv_keys", "dpad_move",
    "free_cam", "zr_attack", "free_cam_sens", "rumble", "rumble_strength",
    "controller_light", "background_input", "move_360", "blur_removal",
    "hide_hero_icon", "prompts", "menu_confirm", "mirror_mode", "language",
    "tracker", "tracker_filters", "aspect_asked", "menu_tip_seen",
    "onedrive_kept", "chaos_letters", "cheat_hearts", "cheat_ammo",
    "cheat_rupees", "cheat_hookshot", "no_buff_drops", "diff_damage",
    "diff_ohko", "diff_hero", "walk_speed", "walk_speed_dungeons",
    "timer_position", "timer", "livesplit", "livesplit_host",
    "enemy_colours", "screen_filter", "rando_seed", "rando_options",
)

# Saved strings per key, in write order. Table names are C tables in
# settings.c; for the enum-like tables the index is the saved meaning.
# Append only: the source must start with exactly these values in order.
FROZEN_VALUES = {
    "aspect": ("original", "unlocked"),
    "fps": ("unlocked", "auto"),
    "upscaler": ("auto", "metalfx", "fsr1", "bilinear"),
    "antialias": ("smaa", "off"),
    "shadows": ("high", "default"),
    "ambient_occlusion": ("high", "off", "switch"),
    "resolution": ("auto",),
    "res_name": ("%dx", "dynamic", "75%", "50%", "25%", "match"),
    "menu_confirm": ("controller", "game"),
    "timer_position": ("right", "left"),
    "g_language_codes": (
        "auto", "ja", "en-US", "es", "fr", "de", "it", "nl", "ru", "ko",
        "zh-Hans", "zh-Hant",
    ),
    "prompts": ("auto", "nintendo", "xbox", "playstation"),
    "letters": ("off", "explored", "map"),
    "k_enemy_colour_keys": ("off", "seed", "load"),
    "k_screen_filter_keys": ("off", "gameboy", "gameboy_color", "grayscale", "vivid"),
}

TABLES = frozenset((
    "g_language_codes", "k_enemy_colour_keys", "k_screen_filter_keys",
    "prompts", "letters",
))


def string_literals(text):
    """C string literal contents, in source order."""
    result = []
    i = 0
    while i < len(text):
        if text[i] != '"':
            i += 1
            continue
        j = i + 1
        while j < len(text) and text[j] != '"':
            if text[j] == BS:
                j += 1
            j += 1
        result.append(text[i + 1:j])
        i = j + 1
    return result


def saved_keys(src):
    save = src[src.index("void settings_save"):]
    keys = []
    for literal in string_literals(save):
        for line in literal.split(BS + "n"):
            line = line.strip()
            if " = " not in line:
                continue
            key = line.split(" = ", 1)[0].strip()
            if (key and key[0].isalpha()
                    and all(c.isalnum() or c == "_" for c in key)
                    and key not in keys):
                keys.append(key)
    return keys


def table_values(src, name):
    marker = "const char *const " + name
    start = src.find(marker)
    assert start >= 0, "missing value table " + name
    brace = src.find("{", start)
    end = src.find("};", brace)
    assert brace >= 0 and end > brace, "malformed value table " + name
    return string_literals(src[brace:end])


def fprintf_values(src, key):
    needle = 'fprintf(f, "' + key + ' = %s'
    start = src.find(needle)
    assert start >= 0, "missing saved string for " + key
    end = src.find(");", start)
    assert end > start, "malformed saved string for " + key
    return string_literals(src[start:end])[1:]


def res_name_values(src):
    start = src.find("res_name(int m)")
    assert start >= 0, "missing res_name()"
    brace = src.find("{", start)
    end = src.find(chr(10) + "}", brace)
    assert brace >= 0 and end > brace, "malformed res_name()"
    return string_literals(src[brace:end])


def check_values(label, actual, frozen):
    assert len(actual) >= len(frozen), (
        label + ": value removed: " + str(actual) + " (frozen " + str(frozen) + ")"
    )
    assert tuple(actual[:len(frozen)]) == tuple(frozen), (
        label + ": saved value renamed or reordered: "
        + str(actual) + " (frozen " + str(frozen) + ")"
    )


def main():
    src = SETTINGS_C.read_text(encoding="utf-8")

    keys = saved_keys(src)
    missing = [key for key in FROZEN_KEYS if key not in keys]
    assert not missing, "ini keys removed or renamed: " + ", ".join(missing)
    frozen_set = frozenset(FROZEN_KEYS)
    order = [key for key in keys if key in frozen_set]
    assert order == list(FROZEN_KEYS), "ini keys reordered: " + str(order)

    for name, frozen in FROZEN_VALUES.items():
        if name in TABLES:
            actual = table_values(src, name)
        elif name == "res_name":
            actual = res_name_values(src)
        else:
            actual = fprintf_values(src, name)
        check_values(name, actual, frozen)

    print("OK: %d ini keys and %d saved value lists unchanged"
          % (len(FROZEN_KEYS), len(FROZEN_VALUES)))


if __name__ == "__main__":
    main()
