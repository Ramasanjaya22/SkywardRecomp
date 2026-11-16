// RenderDoc in-app API (third_party/renderdoc/renderdoc_app.h, MIT): frame captures of the game's Vulkan rendering for
// debugging (textures, shaders, uniforms of one draw). Off unless asked for:
//   LARECOMP_RENDERDOC=1        load RenderDoc from its default install folder before the Vulkan instance exists
//   LARECOMP_RENDERDOC=<dll>    ... from that renderdoc.dll
//   (or start lasrecomp.exe from RenderDoc's own "Launch Application": it is then already loaded)
// F9 (the debug frame key, hle/input.c) then also captures the next frame, into build/rdoc/skywardrecomp_*.rdc.
#include <stdlib.h>
#include <string.h>

#include "../../rt.h"
#include "../../../third_party/renderdoc/renderdoc_app.h"

#ifdef _WIN32
#include <windows.h>

static RENDERDOC_API_1_1_2 *g_rdoc;

// Call before vkCreateInstance: RenderDoc captures Vulkan through its implicit layer, which the loader enables only
// when ENABLE_VULKAN_RENDERDOC_CAPTURE is set at instance creation.
void rdoc_init(void) {
    const char *e = getenv("LARECOMP_RENDERDOC");
    HMODULE m = GetModuleHandleA("renderdoc.dll");  // (already injected: launched from RenderDoc)
    if (!m && e && *e && strcmp(e, "0"))
        m = LoadLibraryA(strcmp(e, "1") ? e : "C:\\Program Files\\RenderDoc\\renderdoc.dll");
    if (!m) {
        if (e && *e && strcmp(e, "0")) LOG("[rdoc] renderdoc.dll not found (LARECOMP_RENDERDOC=%s): is RenderDoc installed?\n", e);
        return;
    }
    pRENDERDOC_GetAPI get = (pRENDERDOC_GetAPI)(void *)GetProcAddress(m, "RENDERDOC_GetAPI");
    if (!get || !get(eRENDERDOC_API_Version_1_1_2, (void **)&g_rdoc) || !g_rdoc) {
        g_rdoc = NULL;
        LOG("[rdoc] RENDERDOC_GetAPI 1.1.2 unavailable\n");
        return;
    }
    SetEnvironmentVariableA("ENABLE_VULKAN_RENDERDOC_CAPTURE", "1");
    _putenv("ENABLE_VULKAN_RENDERDOC_CAPTURE=1");  // (the loader reads the CRT's copy too)
    CreateDirectoryA("build", NULL);
    CreateDirectoryA("build\\rdoc", NULL);
    g_rdoc->SetCaptureFilePathTemplate("build/rdoc/skywardrecomp");
    int maj = 0, min = 0, pat = 0;
    g_rdoc->GetAPIVersion(&maj, &min, &pat);
    LOG("[rdoc] RenderDoc API %d.%d.%d loaded: F9 captures the next frame into build/rdoc/\n", maj, min, pat);
}

void rdoc_trigger(void) {
    if (!g_rdoc) return;
    g_rdoc->TriggerCapture();
    LOG("[rdoc] capture requested (next frame)\n");
}
#else
void rdoc_init(void) {}
void rdoc_trigger(void) {}
#endif
