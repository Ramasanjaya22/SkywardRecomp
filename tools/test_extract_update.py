"""The 1.0.1 update (tools/extract.py): the BKTR patch RomFS is merged over the base game's RomFS, and an update that isn't
a supported one is refused with a message that says what to do. Synthetic tables, no game data."""
import struct
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import extract

BUCKET = 0x4000


def section(entries, subsections, virtual_size):
    """A section holding BKTR tables (relocation at 0x1000, subsections after it) and the fs header pointing at them."""
    reloc = struct.pack('<IIQ', 0, 1, virtual_size).ljust(0x10, b'\0')
    reloc = reloc.ljust(BUCKET, b'\0') + struct.pack('<IIQ', 0, len(entries), virtual_size) + \
        b''.join(struct.pack('<QQI', *e) for e in entries)
    reloc = reloc.ljust(2 * BUCKET, b'\0')
    sub = struct.pack('<IIQ', 0, 1, 0).ljust(BUCKET, b'\0') + struct.pack('<IIQ', 0, len(subsections), 0) + \
        b''.join(struct.pack('<QII', o, 0, c) for o, c in subsections)
    sub = sub.ljust(2 * BUCKET, b'\0')
    fs = bytearray(0x200)
    struct.pack_into('<QQ', fs, 0x100, 0x1000, len(reloc))
    fs[0x110:0x114] = b'BKTR'
    struct.pack_into('<QQ', fs, 0x120, 0x1000 + len(reloc), len(sub))
    fs[0x130:0x134] = b'BKTR'
    return bytes(fs), (0x1000 + len(reloc)) , reloc + sub, len(reloc)


def test_merge():
    # virtual 0x000-0x100 base, 0x100-0x300 patch (two subsections: counters 7 and 9), 0x300-0x400 base again
    entries = [(0, 0, 0), (0x100, 0x2000, 1), (0x300, 0x300, 0)]
    subsections = [(0, 0), (0x2000, 7), (0x2080, 9)]
    fs, _, tables, reloc_size = section(entries, subsections, 0x400)

    def read_patch(where, size, generation=None):
        if generation is None:  # (the tables: plain AES-CTR)
            return tables[where - 0x1000:where - 0x1000 + size]
        return bytes([generation]) * size

    def read_base(where, size):
        return bytes([0xB0 + (where >> 8)]) * size

    read = extract.bktr_merged_reader(read_patch, fs, read_base)
    whole = read(0, 0x400)
    assert whole[:0x100] == bytes([0xB0]) * 0x100, 'base range'
    assert whole[0x100:0x180] == bytes([7]) * 0x80, 'patch range, first subsection counter'
    assert whole[0x180:0x300] == bytes([9]) * 0x180, 'patch range, second subsection counter'
    assert whole[0x300:] == bytes([0xB3]) * 0x100, 'base range after the patch'
    assert read(0xF0, 0x20) == bytes([0xB0]) * 0x10 + bytes([7]) * 0x10, 'a read across a base/patch boundary'
    try:
        read(0x3F0, 0x20)
    except ValueError as error:
        assert 'out of bounds' in str(error)
    else:
        raise AssertionError('a read past the virtual size must be refused')


def test_bad_tables():
    fs, _, tables, _ = section([(0, 0, 0), (0x100, 0x2000, 1)], [(0, 0)], 0x200)
    broken = bytearray(fs)
    broken[0x110:0x114] = b'XXXX'
    try:
        extract.bktr_tables(lambda o, n: tables[o - 0x1000:o - 0x1000 + n], bytes(broken))
    except ValueError as error:
        assert 'BKTR' in str(error)
    else:
        raise AssertionError('a section without BKTR tables must be refused')
    fs, _, tables, _ = section([(0x10, 0, 0)], [(0, 0)], 0x200)  # (the first entry must start at 0)
    try:
        extract.bktr_tables(lambda o, n: tables[o - 0x1000:o - 0x1000 + n], fs)
    except ValueError as error:
        assert 'Invalid BKTR' in str(error)
    else:
        raise AssertionError('an invalid relocation table must be refused')


def test_identify():
    versions = extract.load_expected()
    assert versions['1.0.1']['version'] == 65536 and versions['1.0.1']['display_version'] == '1.0.1'
    with tempfile.TemporaryDirectory() as directory:
        exefs = Path(directory)
        (exefs / 'main').write_bytes(b'not the game code of any supported update')
        try:
            extract.identify_update(exefs, versions)
        except ValueError as error:
            message = str(error)
            assert 'not a version SkywardRecomp supports' in message and '1.0.1' in message and '1.0.0' in message, message
        else:
            raise AssertionError('an unknown update must be refused')


for test in (test_merge, test_bad_tables, test_identify):
    test()
print('the update RomFS merge, its refusals and the update identification work')
