"""List the code that loads the 16:9 aspect constant in Skyward Sword HD's main (ultrawide research).

Usage (repo root):  tools/venv/Scripts/python.exe tools/ss_find_aspect.py [build/extracted/exefs/main]
Finds every aligned float32 16/9 (and its 1-ulp neighbours) and float64 16/9 in rodata/data, then every
`adrp xN` + `ldr s/d/q/w, [xN, #imm]` / `add xN, xN, #imm` (+ later `ldr [xN]`) / pc-relative `ldr s/d` whose
effective address is one of them, and prints each site with the instructions after it (how the value is used).
"""
import struct
import sys

sys.path.insert(0, 'tools')
from nso import Nso  # noqa: E402

BASE = 0x7000000000
F32 = {0x3FE38E39: 'f32 16/9', 0x3FE38E38: 'f32 16/9 -1ulp', 0x3FE38E3A: 'f32 16/9 +1ulp'}
F64 = {0x3FFC71C71C71C71C: 'f64 16/9'}
WINDOW = 16  # instructions an adrp result is followed for


def sext(v, bits):
    return v - (1 << bits) if v & (1 << (bits - 1)) else v


def find_constants(n):
    found = {}
    for seg in (n.rodata, n.data):
        off, size = seg
        for a in range(off, off + size - 3, 4):
            w = struct.unpack_from('<I', n.image, a)[0]
            if w in F32:
                found[a] = F32[w]
            if a % 8 == 0 and a + 8 <= off + size:
                q = struct.unpack_from('<Q', n.image, a)[0]
                if q in F64:
                    found[a] = F64[q]
    return found


def scan(n, consts):
    hits = []
    toff, tsize = n.text
    words = struct.unpack_from('<%dI' % (tsize // 4), n.image, toff)
    page = {}   # reg -> (address value, index set)
    for i, w in enumerate(words):
        pc = toff + i * 4
        # drop stale adrp results
        for r in [r for r, (_, at) in page.items() if i - at > WINDOW]:
            del page[r]
        if (w & 0x9F000000) == 0x90000000:  # adrp
            imm = sext((((w >> 5) & 0x7FFFF) << 2) | ((w >> 29) & 3), 21) << 12
            page[w & 31] = ((pc & ~0xFFF) + imm, i)
            continue
        if (w & 0xFF800000) in (0x91000000,) and ((w >> 5) & 31) in page:  # add xd, xn, #imm (no shift)
            rn, rd = (w >> 5) & 31, w & 31
            page[rd] = (page[rn][0] + ((w >> 10) & 0xFFF), i)
            continue
        ea, kind = None, None
        rn = (w >> 5) & 31
        for mask, val, scale, k in ((0xFFC00000, 0xBD400000, 4, 'ldr s'), (0xFFC00000, 0xFD400000, 8, 'ldr d'),
                                    (0xFFC00000, 0x3DC00000, 16, 'ldr q'), (0xFFC00000, 0xB9400000, 4, 'ldr w')):
            if (w & mask) == val and rn in page:
                ea, kind = page[rn][0] + ((w >> 10) & 0xFFF) * scale, k
                break
        if ea is None:
            for mask, val, k in ((0xFF000000, 0x1C000000, 'ldr s lit'), (0xFF000000, 0x5C000000, 'ldr d lit')):
                if (w & mask) == val:
                    ea, kind = pc + (sext((w >> 5) & 0x7FFFF, 19) << 2), k
                    break
        if ea is not None and ea in consts:
            hits.append((pc, w, kind, w & 31, ea))
        elif ea is not None and kind in ('ldr q',) and any(ea <= c < ea + 16 for c in consts):
            hits.append((pc, w, kind + ' (vector covers it)', w & 31, ea))
    return hits


# 16/9 built in a register: movz/movk wN halves of 0x3FE38E39 (any order, within a few instructions), then fmov
MOV_HI, MOV_LO = 0x3FE3, 0x8E39
# screen sizes the aspect may be divided from (float32 bits, as movz/movk upper half: low half is 0)
SIZES = {0x44F0: '1920.0f', 0x4487: '1080.0f', 0x44A0: '1280.0f', 0x4434: '720.0f'}


def scan_mov(n):
    hits = []
    toff, tsize = n.text
    words = struct.unpack_from('<%dI' % (tsize // 4), n.image, toff)
    for i, w in enumerate(words):
        # movz/movk Wd (32-bit), hw=1 (lsl 16): 0x52A00000 movz, 0x72A00000 movk
        if (w & 0xFFE00000) in (0x52A00000, 0x72A00000):
            imm, rd = (w >> 5) & 0xFFFF, w & 31
            if imm == MOV_HI:
                win = words[max(0, i - 4):i + 5]
                if any((x & 0xFFE00000) in (0x52800000, 0x72800000) and (x >> 5) & 0xFFFF == MOV_LO and x & 31 == rd
                       for x in win):
                    hits.append((toff + i * 4, w, 'mov 16/9 bits', rd))
            elif imm in SIZES and (w & 0xFFE00000) == 0x52A00000:
                hits.append((toff + i * 4, w, f'movz {SIZES[imm]}', rd))
    return hits


def main():
    path = sys.argv[1] if len(sys.argv) > 1 else 'build/extracted/exefs/main'
    n = Nso(path)
    consts = find_constants(n)
    print(f'module {path} build {n.build_id[:16]} text 0x{n.text[0]:x}+0x{n.text[1]:x}')
    print(f'{len(consts)} constant(s):')
    for a, k in sorted(consts.items()):
        print(f'  {BASE + a:#x}  {k}')
    hits = scan(n, consts) + [(pc, w, k, rd, 0) for pc, w, k, rd in scan_mov(n)]
    hits.sort(key=lambda h: (not h[2].startswith(('ldr', 'mov 16')), h[0]))
    print(f'\n{len(hits)} site(s) (16/9 loads first, then screen-size movz):')
    try:
        import capstone
        md = capstone.Cs(capstone.CS_ARCH_ARM64, capstone.CS_MODE_ARM)
    except ImportError:
        md = None
    syms = sorted((s[3], s[0]) for s in n.syms if s[0] and s[2] and (s[1] & 0xF) == 2)
    for pc, w, kind, rt, ea in hits:
        near = [nm for v, nm in syms if v <= pc]
        src = f'<- {BASE + ea:#x} ({consts.get(ea, "vector")})' if ea else ''
        print(f'\n== {BASE + pc:#x}  word {w:#010x}  {kind} reg {rt} {src}  near sym: {near[-1][:60] if near else "-"}')
        if md:
            for ins in md.disasm(bytes(n.image[pc - 16:pc + 4 * 21]), BASE + pc - 16):
                mark = '>>' if ins.address == BASE + pc else '  '
                print(f'   {mark} {ins.address:#x}  {ins.mnemonic:<7} {ins.op_str}')


if __name__ == '__main__':
    main()
