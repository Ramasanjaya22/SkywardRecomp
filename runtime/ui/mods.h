// Mods page: one state shared by the launcher's Mods entry and the in-game menu's Mods tab.
// The HTTP/download layer (SKY-45) and the install mapper (SKY-48) fill this in later; until then available is false
// and both UIs say so plainly instead of listing invented mods. No game file or key is read by the UI.
#pragma once
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct { bool available; } ModsSource;
extern ModsSource g_mods_source;
const char *mods_not_available(void);
// Browse-tab body (search, list, thumbnail placeholder, author, description, Install). The caller owns the frame.
void mods_tab_draw(float scale);

#ifdef __cplusplus
}
#endif
