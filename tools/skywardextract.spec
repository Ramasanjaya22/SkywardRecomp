# PyInstaller spec: the game package extractor (tools/extract.py) as a standalone onedir helper, so the start
# screen's setup can extract a base .nsp on the player's machine without Python (runtime/ui/launcher.mm,
# extract_nsp). `make extract-helper` -> build/helper/skywardextract/ (skywardextract[.exe] + _internal/). The same
# command line as extract.py (--base, --keys, --work, --flat). cryptography's OpenSSL bindings come in through
# its wheel; tomllib is stdlib (3.11+).
import glob
import os
import sys

TOOLS = os.path.normpath(SPECPATH)

a = Analysis(
    [os.path.join(TOOLS, 'extract.py')],
    pathex=[TOOLS],
    hiddenimports=['cryptography.hazmat.primitives.ciphers'],
    datas=[(p, 'tools/data') for p in sorted(glob.glob(os.path.join(TOOLS, 'data', 'expected-v*.json')))],
    excludes=['tkinter', 'pydoc', 'doctest', 'unittest', 'pdb', 'IPython', 'numpy', 'setuptools', '_distutils_hack',
              'pkg_resources'],
    noarchive=False,
)
pyz = PYZ(a.pure)
exe = EXE(
    pyz,
    a.scripts,
    [],
    exclude_binaries=True,
    name='skywardextract',
    console=True,  # stderr = the progress lines the runtime reads (no window: CREATE_NO_WINDOW)
    strip=sys.platform == 'darwin',
    upx=False,
)
coll = COLLECT(exe, a.binaries, a.datas, strip=sys.platform == 'darwin', upx=False, name='skywardextract')
