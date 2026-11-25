// glslc (subsdk0) is bound as three HLE stubs. Game shaders come from the host translator working on the NVN
// binary, so the runtime GLSL compiler is not on the render path. The 90-second run never reached glslcCompile,
// and glslcInitialize's result is unchecked at its only call site (main 0x7000e2d0c8..0x7000e2d110).
// These no-ops are intentional: keep the observed return 0 so boot is unchanged, but drop the UNIMPLEMENTED log.
#include "../hle.h"

HLE_FUNC("glslcInitialize", glslc_initialize) { (void)c; c->x[0] = 0; }
HLE_FUNC("glslcSetAllocator", glslc_set_allocator) { (void)c; c->x[0] = 0; }
HLE_FUNC("glslcCompile", glslc_compile) { (void)c; c->x[0] = 0; }
