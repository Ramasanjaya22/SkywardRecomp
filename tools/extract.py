"""Extract The Legend of Zelda: Skyward Sword HD (title 01002DA013484000) into build/ and verify it.

Adapted for SkywardRecomp (rebuild) from the Astral Chain port's tools/extract.py (MacNativeGames lineage,
MIT): the same NCA3 AES-CTR/XTS decryption, hierarchical PFS0 and IVFC verification, in pure Python.

Two inputs, each checked against tools/data/expected-v<version>.json:

1. The base game NSP alone: version 1.0.0. ExeFS and RomFS are read from its Program NCA.
2. The base game NSP plus the update NSP (title 01002DA013484800): the version the update carries (1.0.1). Its ExeFS
   is the update's; its RomFS is the update's BKTR patch applied over the base's RomFS, in pure Python (no hactool).

All six IVFC levels of the RomFS are verified, so a wrong or damaged base/update stops here. Steps whose verified
output already exists are skipped. An update that is not a known version is refused.

--base, --update, --keys and --work name the files and the output directory. Key values are read by
the tools from the keys file and never printed.
"""
import argparse
import bisect
import contextlib
import hashlib
import io
import json
import os
import shutil
import struct
import sys
import tomllib
from pathlib import Path

from cryptography.hazmat.primitives.ciphers import Cipher, algorithms, modes

ROOT = Path(__file__).resolve().parent.parent
if getattr(sys, '_MEIPASS', None):  # (the skywardextract bundle: the expected-hashes tables ship inside it)
    DATA = Path(sys._MEIPASS) / 'tools' / 'data'
else:
    DATA = ROOT / 'tools' / 'data'

GAME_TITLE_ID = '01002DA013484000'
UPDATE_TITLE_ID = '01002DA013484800'   # the update NSP: the base id with bit 0x800 set


def require(condition, message):
    if not condition:
        raise ValueError(message)


def decrypt(key, mode, data):
    ctx = Cipher(algorithms.AES(key), mode).decryptor()
    return ctx.update(data) + ctx.finalize()


def read_at(f, offset, length):
    require(0 <= offset and 0 <= length <= 512 * 1024 * 1024, 'Invalid/big read')
    f.seek(offset)
    data = f.read(length)
    require(len(data) == length, 'Truncated input')
    return data


def partition_entries(f, offset, bound, magic=b'PFS0', stride=24):
    """Read a bounded PFS0/HFS0 table without loading its file payloads."""
    require(bound >= 16, 'Truncated partition header')
    hdr = read_at(f, offset, 16)
    require(hdr[:4] == magic, f'Expected {magic.decode()}')
    count, strings_size = struct.unpack_from('<II', hdr, 4)
    require(count <= 4096 and strings_size <= 1024 * 1024, 'Invalid partition table')
    size = 16 + count * stride + strings_size
    require(size <= bound, 'Partition table out of bounds')
    raw = read_at(f, offset, size)
    strings = raw[16 + count * stride:]
    entries = []
    names = set()
    for i in range(count):
        off, length, nameoff = struct.unpack_from('<QQI', raw, 16 + i * stride)
        require(nameoff < len(strings), 'Invalid partition filename offset')
        end = strings.find(b'\0', nameoff)
        require(end >= 0, 'Unterminated partition filename')
        name = strings[nameoff:end].decode('utf-8')
        require(name not in ('', '.', '..') and '/' not in name and '\\' not in name, 'Unsafe partition filename')
        require(name not in names, 'Duplicate partition filename')
        names.add(name)
        require(size + off + length <= bound, 'Partition entry out of bounds')
        entries.append((name, offset + size + off, length))
    end = offset + size
    for _, start, length in sorted(entries, key=lambda e: e[1]):
        require(start >= end, 'Overlapping partition entries')
        end = start + length
    return entries


def container_entries(f, size):
    if read_at(f, 0, 4) == b'PFS0':
        return 'nsp', partition_entries(f, 0, size)
    raise ValueError('Expected an NSP (PFS0) game file; XCI is not supported')


def load_keys(path):
    keys = {}
    for number, line in enumerate(path.read_text().splitlines(), 1):
        line = line.split('#', 1)[0].strip()
        if '=' in line:
            name, value = line.split('=', 1)
            try:
                keys[name.strip().lower()] = bytes.fromhex(value.strip())
            except ValueError:
                raise ValueError(f'Invalid key encoding at line {number}') from None
    return keys


def common_tickets(f, entries):
    """Keep encrypted title keys in memory only; a personalized ticket never provides a common key."""
    tickets = {}
    bodies = {0x10000: 0x240, 0x10001: 0x140, 0x10002: 0x80,
              0x10003: 0x240, 0x10004: 0x140, 0x10005: 0x80}
    for name, offset, size in entries:
        if not name.lower().endswith('.tik'):
            continue
        require(size >= 4, 'Truncated NSP ticket')
        sig = struct.unpack('<I', read_at(f, offset, 4))[0]
        require(sig in bodies, 'Unsupported NSP ticket signature type')
        body = bodies[sig]
        require(size >= body + 0x180, 'Truncated NSP ticket body')
        data = read_at(f, offset + body, 0x180)
        require(data[0x141] == 0, 'Personalized NSP tickets are unsupported; a common ticket is required')
        rights, key = data[0x160:0x170], data[0x40:0x50]
        require(rights not in tickets or tickets[rights] == key, 'Conflicting NSP tickets')
        tickets[rights] = key
    return tickets


def read_nca_header(f, offset, size, keys):
    require(size >= 0xc00, 'Truncated NCA header')
    require('header_key' in keys, 'This is not a prod.keys: header_key is missing')
    raw = read_at(f, offset, 0xc00)
    hdr = b''.join(decrypt(keys['header_key'], modes.XTS(i.to_bytes(16, 'big')),
                           raw[i * 512:(i + 1) * 512]) for i in range(6))
    require(hdr[0x200:0x204] == b'NCA3', 'The package could not be decrypted: this prod.keys does not match it, or the .nsp is damaged')
    require(struct.unpack_from('<Q', hdr, 0x208)[0] == size, 'NCA size does not match container entry')
    for i in range(4):
        start, end = struct.unpack_from('<II', hdr, 0x240 + i * 16)
        require((start == end == 0) or 0xc00 <= start * 512 < end * 512 <= size, 'NCA section out of bounds')
    return hdr


def nca_ctr_key(hdr, keys, tickets):
    generation = max(max(hdr[0x206], hdr[0x220]) - 1, 0)
    rights = hdr[0x230:0x240]
    if rights != bytes(16):
        require(rights in tickets, 'Missing common ticket for rights-managed NCA in the NSP')
        keyname = f'titlekek_{generation:02x}'
        require(keyname in keys, f'prod.keys has no {keyname}: it was dumped from a console with older firmware than this game needs; dump it again after updating the console')
        return decrypt(keys[keyname], modes.ECB(), tickets[rights])
    require(hdr[0x207] < 3, 'Unsupported NCA key area')
    area = ('application', 'ocean', 'system')[hdr[0x207]]
    keyname = f'key_area_key_{area}_{generation:02x}'
    require(keyname in keys, f'prod.keys has no {keyname}: it was dumped from a console with older firmware than this game needs; dump it again after updating the console')
    return decrypt(keys[keyname], modes.ECB(), hdr[0x300:0x340])[32:48]


def nca_inventory(f, entries, keys):
    records = []
    for name, offset, length in entries:
        if not name.lower().endswith('.nca'):
            continue
        hdr = read_nca_header(f, offset, length, keys)
        record = dict(name=name, nca_offset=offset, size=length, content_type=hdr[0x205],
                      title_id=f'{struct.unpack_from("<Q", hdr, 0x210)[0]:016X}')
        records.append((record, hdr))
    return records


def program_nca(records, update=False):
    """The one Program NCA of Skyward Sword HD; update: the file must be the update NSP.

    Both packages carry the base title id on their Program NCA; the Meta NCA (content type 1) carries the
    package's own id, so that is what tells an update from a base game.
    """
    programs = [(r, h) for r, h in records if r['content_type'] == 0]
    require(len(programs) == 1, 'Expected one Program NCA; updates, DLC and combined packages are unsupported')
    record, hdr = programs[0]
    require(record['title_id'] == GAME_TITLE_ID,
            f'This package is not the base game of Skyward Sword HD (expected title {GAME_TITLE_ID}, found {record["title_id"]})')
    metadata = [r['title_id'] for r, _ in records if r['content_type'] == 1]
    if update:
        require(UPDATE_TITLE_ID in metadata, 'The file is not the Skyward Sword HD update')
    return record, hdr


def load_expected():
    """{display version: data} from every tools/data/expected-v*.json."""
    versions = {}
    for path in sorted(DATA.glob('expected-v*.json')):
        data = json.loads(path.read_text(encoding='utf-8'))
        versions[data['display_version']] = data
    return versions


def sha256(path):
    digest = hashlib.sha256()
    with path.open('rb') as f:
        for chunk in iter(lambda: f.read(8 * 1024 * 1024), b''):
            digest.update(chunk)
    return digest.hexdigest()


def extract_exefs(game, keys, output, update=False):
    """Extract the ExeFS of the base or update Program NCA into output/; write the extraction manifest next to it."""
    require(not (output.parent / 'extraction-manifest.json').exists(), 'ExeFS already extracted')
    keys = load_keys(keys)
    with game.open('rb') as f:
        kind, entries = container_entries(f, game.stat().st_size)
        tickets = common_tickets(f, entries)
        record, hdr = program_nca(nca_inventory(f, entries, keys), update=update)
        ctr_key = nca_ctr_key(hdr, keys, tickets)
        for i in range(4):
            start, end = struct.unpack_from('<II', hdr, 0x240 + 16 * i)
            if not start:
                continue
            fs = hdr[0x400 + 512 * i:0x600 + 512 * i]
            require(hashlib.sha256(fs).digest() == hdr[0x280 + 32 * i:0x2A0 + 32 * i], 'FS header hash mismatch')
            if fs[2:4] != b'\x01\x02':
                continue
            require(fs[4] == 3, 'Only AES-CTR ExeFS is supported')
            blocksize, levels = struct.unpack_from('<II', fs, 0x28)
            hashoff, hashsize, pfsoff, pfssize = struct.unpack_from('<QQQQ', fs, 0x30)
            secstart, secsize = start * 512, (end - start) * 512
            require(levels == 2 and blocksize > 0 and pfssize <= 512 * 1024 * 1024, 'Unsupported ExeFS hash layout')

            def section_read(relative, size):
                require(relative >= 0 and relative + size <= secsize, 'Section read out of bounds')
                absolute = secstart + relative
                aligned = absolute & ~15
                iv = fs[0x140:0x148][::-1] + (aligned // 16).to_bytes(8, 'big')
                data = read_at(f, record['nca_offset'] + aligned, size + absolute - aligned)
                return decrypt(ctr_key, modes.CTR(iv), data)[absolute - aligned:]

            hashes = section_read(hashoff, hashsize)
            require(hashlib.sha256(hashes).digest() == fs[8:40], 'ExeFS hash-table mismatch')
            pfs = section_read(pfsoff, pfssize)
            for index, pos in enumerate(range(0, pfssize, blocksize)):
                expected = hashes[index * 32:(index + 1) * 32]
                require(hashlib.sha256(pfs[pos:pos + blocksize]).digest() == expected,
                        f'ExeFS block hash mismatch: {index}')
            files = [(name, pfs[off:off + size]) for name, off, size in
                     partition_entries(io.BytesIO(pfs), 0, len(pfs))]
            require(any(name == 'main.npdm' for name, _ in files), 'PFS0 is not ExeFS')
            output.mkdir(parents=True, exist_ok=True)
            extracted = []
            for name, data in files:
                (output / name).write_bytes(data)
                extracted.append({'name': name, 'size': len(data), 'sha256': hashlib.sha256(data).hexdigest()})
            manifest = {'source': str(game.resolve()), 'source_size': game.stat().st_size,
                        'source_mtime_ns': game.stat().st_mtime_ns, 'container': kind,
                        'update': update,
                        'nca_inventory': [r for r, _ in nca_inventory(f, entries, keys)], 'program': record,
                        'section_index': i,
                        'integrity': 'FS header, ExeFS hash table, and every ExeFS data block verified; RSA signatures not checked',
                        'files': extracted}
            (output.parent / 'extraction-manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
            return manifest
    raise ValueError('No ExeFS section found')


def extract_romfs(game, keys, manifest, output, base=None):
    """Verify all six IVFC levels, stream the RomFS image, and index its files.

    base: with an update NSP as `game`, the base game's NSP: the update's RomFS is a BKTR patch whose unchanged ranges
    are read from the base's RomFS section (pure Python, no hactool); every IVFC level of the merged image is verified.
    """
    require(not output.exists(), 'RomFS output already exists')
    keys = load_keys(keys)
    record = manifest['program']
    with game.open('rb') as f, (base.open('rb') if base else contextlib.nullcontext()) as base_file:
        _, entries = container_entries(f, game.stat().st_size)
        actual, hdr = program_nca(nca_inventory(f, entries, keys), update=bool(base))
        require(actual['name'] == record['name'] and actual['size'] == record['size'] and
                actual['nca_offset'] == record['nca_offset'], 'Source Program NCA changed; extract ExeFS again')
        ctr_key = nca_ctr_key(hdr, keys, common_tickets(f, entries))
        for i in range(4):
            start, end = struct.unpack_from('<II', hdr, 0x240 + i * 16)
            if not start:
                continue
            fs = hdr[0x400 + 512 * i:0x600 + 512 * i]
            require(hashlib.sha256(fs).digest() == hdr[0x280 + 32 * i:0x2A0 + 32 * i], 'FS header hash')
            if fs[2:5] != bytes([0, 3, 4 if base else 3]):
                continue
            require(fs[8:12] == b'IVFC' and struct.unpack_from('<III', fs, 12) == (0x20000, 32, 7),
                    'Unsupported IVFC layout')
            levels = [struct.unpack_from('<QQII', fs, 0x18 + j * 24) for j in range(6)]

            def read(relative, size, generation=None):
                require(0 <= relative <= relative + size <= (end - start) * 512, 'Section bounds')
                absolute = start * 512 + relative
                aligned = absolute & ~15
                skip = absolute - aligned
                high = fs[0x140:0x148][::-1]
                if generation is not None:  # (AES-CTR-EX: the subsection's counter replaces the section's generation)
                    high = high[:4] + generation.to_bytes(4, 'big')
                iv = high + (aligned // 16).to_bytes(8, 'big')
                return decrypt(ctr_key, modes.CTR(iv),
                               read_at(f, record['nca_offset'] + aligned, size + skip))[skip:]

            if base:
                read = bktr_merged_reader(read, fs, base_romfs_reader(base_file, keys))

            output.mkdir(parents=True)
            image_path = output / 'romfs.bin'
            digest = hashlib.sha256()
            counts = []
            parent = fs[0xc8:0xe8]
            with image_path.open('wb') as image:
                for level, (offset, size, power, reserved) in enumerate(levels):
                    require(12 <= power <= 24 and reserved == 0, 'IVFC block size')
                    block = 1 << power
                    require(len(parent) >= (size + block - 1) // block * 32, 'IVFC parent size')
                    captured = bytearray() if level < 5 else None
                    index = 0
                    for pos in range(0, size, 8 * 1024 * 1024):
                        chunk = read(offset + pos, min(8 * 1024 * 1024, size - pos))
                        for j in range(0, len(chunk), block):
                            payload = chunk[j:j + block]
                            require(hashlib.sha256(payload + bytes(block - len(payload))).digest()
                                    == parent[index * 32:index * 32 + 32], f'IVFC level {level} block {index} failed')
                            index += 1
                        if captured is not None:
                            captured.extend(chunk)
                        else:
                            image.write(chunk)
                            digest.update(chunk)
                    counts.append(index)
                    if captured is not None:
                        parent = bytes(captured)
                    print(f'IVFC level {level}: {index} blocks verified', flush=True)
            files = index_romfs(image_path)
            report = dict(status='pass', source_size=game.stat().st_size, program=record['name'], section=i,
                          ivfc_blocks=counts, romfs_size=levels[-1][1], romfs_sha256=digest.hexdigest(),
                          files=files,
                          integrity='FS header and all six IVFC levels verified; RSA signature not checked')
            (output / 'index.json').write_text(json.dumps(report, indent=2) + '\n')
            print(json.dumps(dict(status='pass', file_count=len(files), size=levels[-1][1],
                                  sha256=digest.hexdigest())))
            return report
    raise ValueError('No supported RomFS found')


def bktr_tables(read, fs):
    """The BKTR tables of an update RomFS section: ([(virtual offset, physical offset, from the patch)],
    [(offset, reserved, counter)], virtual size). read(relative, size) reads the section with plain AES-CTR (the tables
    are not CTR-EX)."""
    reloc_offset, reloc_size = struct.unpack_from('<QQ', fs, 0x100)
    sub_offset, sub_size = struct.unpack_from('<QQ', fs, 0x120)
    require(fs[0x110:0x114] == b'BKTR' and fs[0x130:0x134] == b'BKTR', 'Unsupported update RomFS (no BKTR tables)')
    raw = read(reloc_offset, reloc_size)
    _, buckets, virtual_size = struct.unpack_from('<IIQ', raw, 0)
    require(buckets and reloc_size >= 0x4000 * (1 + buckets), 'Truncated BKTR relocation table')
    entries = []
    for i in range(buckets):
        bucket = 0x4000 * (1 + i)
        count = struct.unpack_from('<I', raw, bucket + 4)[0]
        require(count <= 818, 'Invalid BKTR relocation bucket')
        entries += [struct.unpack_from('<QQI', raw, bucket + 0x10 + 0x14 * j) for j in range(count)]
    require(entries and entries[0][0] == 0 and all(a[0] < b[0] for a, b in zip(entries, entries[1:])) and
            entries[-1][0] < virtual_size and all(e[2] in (0, 1) for e in entries), 'Invalid BKTR relocation table')
    raw = read(sub_offset, sub_size)
    buckets = struct.unpack_from('<I', raw, 4)[0]
    require(buckets and sub_size >= 0x4000 * (1 + buckets), 'Truncated BKTR subsection table')
    subsections = []
    for i in range(buckets):
        bucket = 0x4000 * (1 + i)
        count = struct.unpack_from('<I', raw, bucket + 4)[0]
        require(count <= 1023, 'Invalid BKTR subsection bucket')
        subsections += [struct.unpack_from('<QII', raw, bucket + 0x10 + 0x10 * j) for j in range(count)]
    require(subsections and subsections[0][0] == 0 and all(a[0] < b[0] for a, b in zip(subsections, subsections[1:])),
            'Invalid BKTR subsection table')
    return entries, subsections, virtual_size


def base_romfs_reader(f, keys):
    """read(relative, size) of the base game's RomFS section (plain AES-CTR), from the open base NSP f."""
    _, entries = container_entries(f, os.fstat(f.fileno()).st_size)
    record, hdr = program_nca(nca_inventory(f, entries, keys))
    ctr_key = nca_ctr_key(hdr, keys, common_tickets(f, entries))
    for i in range(4):
        start, end = struct.unpack_from('<II', hdr, 0x240 + i * 16)
        fs = hdr[0x400 + 512 * i:0x600 + 512 * i]
        if start and fs[2:5] == bytes([0, 3, 3]):
            require(hashlib.sha256(fs).digest() == hdr[0x280 + 32 * i:0x2A0 + 32 * i], 'FS header hash')

            def read(relative, size):
                require(0 <= relative <= relative + size <= (end - start) * 512, 'Base section bounds')
                absolute = start * 512 + relative
                aligned = absolute & ~15
                iv = fs[0x140:0x148][::-1] + (aligned // 16).to_bytes(8, 'big')
                return decrypt(ctr_key, modes.CTR(iv),
                               read_at(f, record['nca_offset'] + aligned, size + absolute - aligned))[absolute - aligned:]
            return read
    raise ValueError('The base game has no RomFS section')


def bktr_merged_reader(read_patch, fs, read_base):
    """read(relative, size) of the update's RomFS as the game sees it: each range comes from the base or the patch."""
    entries, subsections, virtual_size = bktr_tables(read_patch, fs)
    starts = [e[0] for e in entries]
    sub_starts = [s[0] for s in subsections]

    def read(relative, size):
        require(0 <= relative <= relative + size <= virtual_size, 'BKTR read out of bounds')
        out = bytearray()
        while size:
            i = bisect.bisect_right(starts, relative) - 1
            virtual, physical, patched = entries[i]
            n = min(size, (entries[i + 1][0] if i + 1 < len(entries) else virtual_size) - relative)
            where = physical + (relative - virtual)
            if patched:  # (a subsection boundary starts another counter)
                j = bisect.bisect_right(sub_starts, where) - 1
                n = min(n, (subsections[j + 1][0] if j + 1 < len(subsections) else where + n) - where)
                out += read_patch(where, n, subsections[j][2])
            else:
                out += read_base(where, n)
            relative += n
            size -= n
        return bytes(out)
    return read


def index_romfs(path):
    with path.open('rb') as f:
        h = struct.unpack('<10Q', read_at(f, 0, 80))
        require(h[0] == 80, 'RomFS header')
        length = path.stat().st_size
        for off, size in [(h[1], h[2]), (h[3], h[4]), (h[5], h[6]), (h[7], h[8])]:
            require(off + size <= length, 'RomFS table bounds')
        dirs = read_at(f, h[3], h[4])
        entries = read_at(f, h[7], h[8])
        files = []
        seen_dirs = set()
        seen_files = set()
        paths = set()

        def name(table, off, n):
            require(off + n <= len(table), 'Name bounds')
            s = table[off:off + n].decode('utf-8')
            require(s not in ['.', '..'] and '/' not in s and '\\' not in s and '\0' not in s, 'Unsafe path')
            return s

        todo = [(0, '')]
        while todo:
            off, parent = todo.pop()
            require(off not in seen_dirs and off + 24 <= len(dirs), 'Invalid/cyclic directory')
            seen_dirs.add(off)
            p, sibling, child, file, hash_next, n = struct.unpack_from('<6I', dirs, off)
            part = name(dirs, off + 24, n)
            prefix = parent + part + '/' if part else parent
            if sibling != 0xffffffff:
                todo.append((sibling, parent))
            if child != 0xffffffff:
                todo.append((child, prefix))
            while file != 0xffffffff:
                require(file not in seen_files and file + 32 <= len(entries), 'Invalid/cyclic file')
                seen_files.add(file)
                p, next_file, data, size, hash_next, n = struct.unpack_from('<IIQQII', entries, file)
                part = name(entries, file + 32, n)
                require(part != '', 'Empty file name')
                filename = prefix + part
                require(filename not in paths and h[9] + data + size <= length, 'Duplicate/out-of-bounds file')
                paths.add(filename)
                files.append(dict(path=filename, offset=h[9] + data, size=size))
                file = next_file
        return sorted(files, key=lambda item: item['path'])


# The ExeFS files that identify the game build. main.npdm is the process's metadata (thread priority, stack size,
# service access), not code: dumps of the same build carry different ones (repacked NSPs, different dumpers), and the
# runtime checks the fields it uses at start (runtime/boot.c check_npdm). Comparing it would refuse a good dump.
VERSION_FILES = ('main', 'sdk', 'rtld')


def identify_update(exefs, versions):
    """The expected data of the update whose `main` hash this ExeFS has."""
    digest = sha256(exefs / 'main')
    for expected in versions.values():
        if expected.get('version') and expected['exefs_sha256'].get('main') == digest:
            return expected
    supported = ', '.join(v for v, e in versions.items() if e.get('version'))
    raise ValueError(f'This update is not a version SkywardRecomp supports (its game code, main {digest[:12]}..., is '
                     f'unknown; supported updates: {supported}). Use the 1.0.1 update (title 01002DA013484800, '
                     f'v65536), or play without an update to use 1.0.0.')


def exefs_ok(exefs_dir, expected):
    """Whether the extracted ExeFS matches the expected hashes of the code modules; (ok, problems) where problems is the
    list of differing names (or missing names when the expected file is absent)."""
    if not (exefs_dir / 'main.npdm').exists():
        return False, ['missing ExeFS']
    problems = []
    for name, want in expected.get('exefs_sha256', {}).items():
        if name not in VERSION_FILES:
            continue
        path = exefs_dir / name
        if not path.exists() or sha256(path) != want:
            problems.append(name)
    return not problems, problems


def update_expected(display_version, version, manifest, romfs_report):
    """Write the measured hashes into tools/data/expected-v<display_version>.json (first run on a new dump)."""
    path = DATA / f'expected-v{display_version}.json'
    exefs = {f['name']: f['sha256'] for f in manifest['files']}
    expected = {
        '_comment': f'Hashes of what the build produces from a genuine Skyward Sword HD {display_version} (no game data). '
                    'tools/extract.py checks every step against them; a mismatch stops the build.',
        'title_id': GAME_TITLE_ID,
        'version': version,
        'display_version': display_version,
        'program_nca': manifest['program']['name'],
        'exefs_sha256': exefs,
        'romfs': {'size': romfs_report['romfs_size'], 'sha256': romfs_report['romfs_sha256']},
    }
    path.write_text(json.dumps(expected, indent=2) + '\n')


def check_expected(display_version, manifest, romfs_report):
    expected = load_expected().get(display_version)
    if not expected:
        version = {'1.0.0': 0, '1.0.1': 65536}[display_version]
        update_expected(display_version, version, manifest, romfs_report)
        print(f'expected-v{display_version}.json: written from this dump (ExeFS: {len(manifest["files"])} files, '
              f'RomFS {romfs_report["romfs_size"]} bytes)')
        return
    problems = []
    for name, want in expected.get('exefs_sha256', {}).items():
        if name not in VERSION_FILES:
            continue
        got = next((f['sha256'] for f in manifest['files'] if f['name'] == name), None)
        if got != want:
            problems.append(name)
    want_romfs = expected.get('romfs', {})
    if want_romfs.get('size') != romfs_report['romfs_size'] or \
            want_romfs.get('sha256') != romfs_report['romfs_sha256']:
        problems.append('romfs')
    require(not problems, f'Extracted files differ from expected-v{display_version}.json: {", ".join(problems)}. '
                          f'Copy the game files again from their source and retry.')


def romfs_step(game, keys, manifest, out, flat, base=None):
    """The verified RomFS report of out/romfs (or of a finished flat run), extracting the image when it is not there."""
    romfs = out / 'romfs'
    if (romfs / 'romfs.bin').is_file() and (romfs / 'index.json').is_file():
        return json.loads((romfs / 'index.json').read_text(encoding='utf-8'))
    if flat and (out / 'romfs.bin').is_file() and (out / 'index.json').is_file():
        return json.loads((out / 'index.json').read_text(encoding='utf-8'))  # (a finished flat run)
    print('Extracting the RomFS (pure Python; needs about the image size in free space)')
    shutil.rmtree(romfs, ignore_errors=True)  # (what a stopped run left)
    return extract_romfs(game, keys, manifest, romfs, base)


def flatten_romfs(out):
    """The runtime serves the image straight from romfs.bin (runtime/services/fs.c); the report goes next to it."""
    romfs = out / 'romfs'
    if romfs.is_dir():
        (romfs / 'romfs.bin').replace(out / 'romfs.bin')
        (romfs / 'index.json').replace(out / 'index.json')
        romfs.rmdir()


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--config', type=Path, default=ROOT / 'config.local.toml')
    parser.add_argument('--base', type=Path, help='base game NSP (default: input.base of the config)')
    parser.add_argument('--update', type=Path, help='update NSP, 1.0.1 (default: input.update of the config)')
    parser.add_argument('--keys', type=Path, help='prod.keys (default: input.keys of the config)')
    parser.add_argument('--work', type=Path, help='output directory with extracted/ (default: work.dir of the '
                                                  'config, else build)')
    parser.add_argument('--flat', action='store_true',
                        help='write exefs/, romfs.bin and extraction-manifest.json straight into extracted/ '
                             '(the runtime layout), not extracted/v0/ (base) or extracted/v101/ (update)')
    args = parser.parse_args()

    config = {}
    if not (args.base and args.keys and args.work):
        config = tomllib.loads(args.config.read_text(encoding='utf-8'))
    given = config.get('input', {})
    base = args.base or Path(given['base'])
    update = args.update or (Path(given['update']) if given.get('update') else None)
    keys = args.keys or Path(given['keys'])
    work = args.work or ROOT / config.get('work', {}).get('dir', 'build')
    require(base.is_file(), f'Base game not found: {base}')
    require(not update or update.is_file(), f'Update not found: {update}')
    require(keys.is_file(), f'Keys file not found: {keys}')

    versions = load_expected()
    extracted = work / 'extracted'

    if update:
        print(f'Base game: {base.name}')
        print(f'Update: {update.name}')
        out = extracted if args.flat else extracted / 'v101'
        manifest_path = out / 'extraction-manifest.json'
        manifest = None
        if (out / 'exefs').is_dir() and manifest_path.is_file():
            manifest = json.loads(manifest_path.read_text(encoding='utf-8'))
            if not manifest.get('update'):  # (an earlier base-only extraction in the same folder)
                manifest = None
        if manifest is None:
            shutil.rmtree(out, ignore_errors=True)
            manifest = extract_exefs(update, keys, out / 'exefs', update=True)
            print('ExeFS extracted:', ', '.join(f['name'] for f in manifest['files']))
        try:
            expected = identify_update(out / 'exefs', versions)
            ok, problems = exefs_ok(out / 'exefs', expected)
            require(ok, f'Update {expected["display_version"]}: {", ".join(problems)} differ')
        except ValueError:
            shutil.rmtree(out, ignore_errors=True)  # (a stale or unsupported ExeFS must not look extracted)
            raise
        version = expected['display_version']
        print(f'  ExeFS {version}: main, sdk, rtld match')
        if sha256(out / 'exefs' / 'main.npdm') != expected['exefs_sha256']['main.npdm']:
            print('  main.npdm differs from the reference dump; only the code modules are used')
        report = romfs_step(update, keys, manifest, out, args.flat, base=base)
        check_expected(version, manifest, report)
        if args.flat:
            flatten_romfs(out)
        print(f'OK: ExeFS and RomFS verified for Skyward Sword HD {version} ({GAME_TITLE_ID})')
        return

    base_expected = versions.get('1.0.0')
    print(f'Base game: {base.name}')
    out = extracted if args.flat else extracted / 'v0'
    if base_expected and out.exists() and not exefs_ok(out / 'exefs', base_expected)[0]:
        shutil.rmtree(out)
    if not (out / 'exefs').is_dir():
        manifest = extract_exefs(base, keys, out / 'exefs')
        print('ExeFS extracted:', ', '.join(f['name'] for f in manifest['files']))
    else:
        manifest = json.loads((out / 'extraction-manifest.json').read_text(encoding='utf-8'))
    if base_expected:
        ok, problems = exefs_ok(out / 'exefs', base_expected)
        require(ok, f'Base game is not Skyward Sword HD 1.0.0: {", ".join(problems)} differ. '
                    f'If this is an update, give it as the update and the 1.0.0 game as the base.')
        if sha256(out / 'exefs' / 'main.npdm') != base_expected['exefs_sha256']['main.npdm']:
            print('  main.npdm differs from the reference dump; only the code modules are used')
    else:
        print('  no expected hashes for this base game yet; measuring this dump')
    report = romfs_step(base, keys, manifest, out, args.flat)
    check_expected('1.0.0', manifest, report)
    if args.flat:
        flatten_romfs(out)
    print(f'OK: ExeFS and RomFS verified for Skyward Sword HD 1.0.0 ({GAME_TITLE_ID})')


if __name__ == '__main__':
    try:
        main()
    except ValueError as error:
        sys.exit(f'error: {error}')
