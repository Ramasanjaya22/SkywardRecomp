// Software keyboard (naming Link on a new file, ...). On the Switch nn::swkbd::ShowKeyboard launches the keyboard
// library applet; our am service has no applets, so the game crashed. ShowKeyboard is replaced here: the overlay
// (runtime/ui/menu.mm menu_keyboard_run) asks for the text - typed directly with a keyboard, an on-screen keyboard
// with a controller - while this (game) thread keeps re-presenting the last frame.
#include <stdio.h>
#include <string.h>

#include "../hle.h"

int menu_keyboard_run(const char *initial, int max_len, char *out, int cap);  // runtime/ui/menu.mm

// nn::swkbd::ShowKeyboard(nn::swkbd::String *out, const nn::swkbd::ShowKeyboardArg &arg) -> nn::Result
// String = {void *ptr; size_t bufSize}
// ShowKeyboardArg (probed 2026-10-08, naming Link): +0x3ac text max length, +0x3b0 min length, +0x3c0 / +0x3c4
// initial text offset / length (UTF-16) in the work buffer at +0x4d0, +0x3bd KeyboardConfig::isUseUtf8 (read by the
// SDK's ShowKeyboard). The answer is UTF-8 only when that flag is set (LA); SS leaves it clear and reads UTF-16, so
// UTF-8 "Link" was two CJK code units, drawn as two blank glyphs.
HLE_FUNC("_ZN2nn5swkbd12ShowKeyboardEPNS0_6StringERKNS0_15ShowKeyboardArgE", swkbd_show) {
    uint64_t out = c->x[0], arg = c->x[1];
    uint64_t ptr = LD64(out), size = LD64(out + 8);
    int max_len = (int)*(const int32_t *)(uintptr_t)(arg + 0x3ac);
    uint32_t off = *(const uint32_t *)(uintptr_t)(arg + 0x3c0), n = *(const uint32_t *)(uintptr_t)(arg + 0x3c4);
    uint64_t wb = LD64(arg + 0x4d0);
    char initial[96] = "";
    for (uint32_t i = 0; wb && i < n && i < sizeof initial - 1; i++) {
        uint16_t ch = *(const uint16_t *)(uintptr_t)(wb + off + 2 * i);
        initial[i] = ch < 0x80 ? (char)ch : '?';
    }
    char text[96];
    int ok = menu_keyboard_run(initial, max_len, text, sizeof text);
    if (!ok) snprintf(text, sizeof text, "%s", initial);  // cancelled: keep what was there
    LOG("[swkbd] name entry (max %d, initial \"%s\"): %s \"%s\"\n", max_len, initial, ok ? "entered" : "cancelled", text);
    if (ptr && size) {
        memset((void *)(uintptr_t)ptr, 0, size);
        if (*(const uint8_t *)(uintptr_t)(arg + 0x3bd)) {
            snprintf((char *)(uintptr_t)ptr, size, "%s", text);
        } else {  // UTF-8 -> UTF-16 (BMP only), NUL-terminated
            uint16_t *o = (uint16_t *)(uintptr_t)ptr;
            size_t cap = size >= 2 ? size / 2 - 1 : 0, k = 0;
            for (const unsigned char *t = (const unsigned char *)text; *t && k < cap;) {
                uint32_t ch = *t++;
                int more = ch >= 0xf0 ? 3 : ch >= 0xe0 ? 2 : ch >= 0xc0 ? 1 : 0;
                ch &= more ? 0x3f >> more : 0x7f;
                for (; more && (*t & 0xc0) == 0x80; more--) ch = ch << 6 | (*t++ & 0x3f);
                o[k++] = ch > 0xffff ? '?' : (uint16_t)ch;
            }
        }
    }
    c->x[0] = 0;
}
