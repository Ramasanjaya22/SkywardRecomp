"""The window's crop (nvnWindowSetCrop) at the present (runtime/nvn/output_scale.c output_window_crop): SS HD's own
dynamic resolution draws 1600x900 into its 1920x1080 image and crops to it; showing the whole image left the picture
at 0.833 of the screen in a corner. No game files. Run: python3 tools/test_window_crop.py (CC: default clang)."""
import os
from pathlib import Path
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parent.parent
test_c = r'''
#include <stdbool.h>
#include <stdio.h>
#include "settings.h"
#include "nvn/output_scale.h"
Settings g_settings;  // (output_scale.c's other host symbols, as in tools/test_dynres.c)
bool g_gpu_integrated;
void pace_hitch(const char *why) { (void)why; }
void pace_settings_changed(const char *what) { (void)what; }
static void show(int x, int y, int w, int h, float c0, float c1, float c2, float c3) {
    int wc[4] = {x, y, w, h};
    float c[4] = {c0, c1, c2, c3};
    output_window_crop(wc, 1920, 1080, c);
    printf("%.4f %.4f %.4f %.4f\n", c[0], c[1], c[2], c[3]);
}
int main(void) {
    show(0, 0, 1920, 1080, 0, 0, 1, 1);    // the whole image
    show(0, 0, 0, 0, 0, 0, 1, 1);          // never set: the whole image
    show(0, 0, 1600, 900, 0, 0, 1, 1);     // dynamic resolution's part
    show(0, 0, 1600, 900, 0.125f, 0, 0.75f, 1);  // with a 16:9 box inside a wide image
    show(192, 108, 960, 540, 0, 0, 1, 1);  // an offset part
    return 0;
}
'''
want = ['0.0000 0.0000 1.0000 1.0000', '0.0000 0.0000 1.0000 1.0000', '0.0000 0.0000 0.8333 0.8333',
        '0.1042 0.0000 0.6250 0.8333', '0.1000 0.1000 0.5000 0.5000']
with tempfile.TemporaryDirectory() as d:
    src, exe = Path(d) / 't.c', Path(d) / ('t.exe' if os.name == 'nt' else 't')
    src.write_text(test_c)
    subprocess.run(shlex.split(os.environ.get('CC', 'clang')) + ['-std=gnu11', '-O1', '-Iruntime', str(src),
                   'runtime/nvn/output_scale.c', '-lm', '-o', str(exe)], cwd=ROOT, check=True)
    got = subprocess.run([str(exe)], check=True, capture_output=True, text=True).stdout.split('\n')[:-1]
    assert got == want, (got, want)
print('PASS: window crop: whole image, unset, 1600x900 part, inside a 16:9 box, offset part')
