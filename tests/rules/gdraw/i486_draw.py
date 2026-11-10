"""sr_group_draw's i486 object (the Mod's cobj flags) under Unicorn: per draw the instructions, the deepest stack (a
painted stack), and the groups against the native build's (bench d).  Needs the unicorn module (the Mod's
internal/hosttest/.venv). Engine P2 / P3 (motore-sr-stage.md, T2):
    cc -O2 -Irules -o bench tests/rules/gdraw/bench.c rules/sr_stage.c && ./bench SET.txt d > SET.d
    python3 tests/rules/gdraw/i486_draw.py rules/sr_stage.c -Os SET.txt SET.d [k,k,..|all]
SET.txt: one draw a line, seed n g and n association bytes (gen.py makes UEFA-like / hard ones; inst.txt = the
fuzzer's slow shapes)."""
import struct, sys, os, subprocess, tempfile

CFLAGS = ['-target', 'i686-unknown-none-elf', '-march=i486', '-ffreestanding', '-fno-pic', '-fno-pie',
          '-fno-stack-protector', '-mno-sse', '-mno-mmx', '-fno-builtin', '-fno-asynchronous-unwind-tables',
          '-fno-unwind-tables']


def _elf(o):
    shoff, = struct.unpack_from('<I', o, 32)
    shentsize, shnum, shstrndx = struct.unpack_from('<HHH', o, 46)
    secs = [dict(zip(('name', 'type', 'flags', 'addr', 'offset', 'size', 'link', 'info', 'align', 'entsize'),
                     struct.unpack_from('<10I', o, shoff + i * shentsize))) for i in range(shnum)]
    return secs

from unicorn import Uc, UC_ARCH_X86, UC_MODE_32, UC_HOOK_CODE
from unicorn.x86_const import UC_X86_REG_ESP, UC_X86_REG_EAX, UC_X86_REG_EIP
src, opt, setf, natf = sys.argv[1:5]
sel = sys.argv[5] if len(sys.argv) > 5 else 'all'
with tempfile.TemporaryDirectory() as t:
    o = os.path.join(t, 'o.o')
    fl = CFLAGS + [opt]
    subprocess.run(['clang', *fl, '-I' + os.path.dirname(src), '-c', src, '-o', o], check=True)
    obj = open(o, 'rb').read()
secs = _elf(obj)
BASE, STACK_TOP, STACK = 0x100000, 0x800000, 0x100000
addr, at = {}, BASE
for i, s in enumerate(secs):
    if s['flags'] & 2 and s['size']:
        al = max(s['align'], 1); at = (at + al - 1) // al * al; addr[i] = at; at += s['size']
img = bytearray(at - BASE)
for i, a in addr.items():
    if secs[i]['type'] == 1: img[a - BASE:a - BASE + secs[i]['size']] = obj[secs[i]['offset']:secs[i]['offset'] + secs[i]['size']]
symtab = next(s for s in secs if s['type'] == 2); strtab = secs[symtab['link']]
syms = []
for k in range(symtab['size'] // 16):
    name, value, size, info, other, shndx = struct.unpack_from('<IIIBBH', obj, symtab['offset'] + 16 * k)
    a = strtab['offset'] + name; nm = obj[a:obj.index(b'\0', a)].decode()
    syms.append((nm, addr[shndx] + value if shndx in addr else None))
for s in secs:
    if s['type'] == 9 and s['info'] in addr:
        for k in range(s['size'] // 8):
            off, info = struct.unpack_from('<II', obj, s['offset'] + 8 * k)
            S_ = syms[info >> 8][1]; assert S_ is not None, syms[info >> 8]
            P = addr[s['info']] + off; A, = struct.unpack_from('<i', img, P - BASE)
            v = S_ + A if info & 0xFF == 1 else S_ + A - P
            struct.pack_into('<I', img, P - BASE, v & 0xFFFFFFFF)
fn = dict(syms)['sr_group_draw']
text = sum(secs[i]['size'] for i in addr if secs[i]['type'] == 1)
uc = Uc(UC_ARCH_X86, UC_MODE_32)
uc.mem_map(BASE, (len(img) + 0xFFFF) & ~0xFFFF); uc.mem_write(BASE, bytes(img))
uc.mem_map(STACK_TOP - STACK, STACK); uc.mem_map(0x900000, 0x10000)
cnt = [0]
uc.hook_add(UC_HOOK_CODE, lambda u, a, s, d: cnt.__setitem__(0, cnt[0] + 1))
toks = open(setf).read().split()
shapes, i = [], 0
while i < len(toks):
    seed, n, g = int(toks[i]), int(toks[i + 1]), int(toks[i + 2]); a = [int(x) for x in toks[i + 3:i + 3 + n]]; i += 3 + n
    shapes.append((seed, n, g, a))
nat = {int(l.split()[0]): l.split()[1:] for l in open(natf) if 'draws' not in l}
ks = range(len(shapes)) if sel == 'all' else [int(x) for x in sel.split(',') if x]
RET = 0x900F00
worst_i, worst_st, same = 0, 0, 0
print(f'{os.path.basename(src)} {opt}: .text etc {text} B, sr_group_draw at {fn:#x}')
for k in ks:
    seed, n, g, a = shapes[k]
    uc.mem_write(STACK_TOP - STACK, b'\xA5' * STACK)
    uc.mem_write(0x900000, bytes(a) + b'\0' * (64 - n)); uc.mem_write(0x900100, b'\0' * 64)
    esp = STACK_TOP - 0x100
    uc.mem_write(esp, struct.pack('<6I', RET, n, g, 0x900000, seed & 0xFFFFFFFF, 0x900100))
    uc.reg_write(UC_X86_REG_ESP, esp); cnt[0] = 0
    uc.emu_start(fn, RET)
    r = struct.unpack('<i', struct.pack('<I', uc.reg_read(UC_X86_REG_EAX)))[0]
    m = uc.mem_read(STACK_TOP - STACK, STACK); low = next(j for j in range(len(m)) if m[j] != 0xA5)
    depth = esp - (STACK_TOP - STACK + low)
    grp = list(uc.mem_read(0x900100, n))
    want = nat[k]; ok = int(want[0]) == r and (r != g or [int(x) for x in want[1:]] == grp)
    same += ok
    worst_i, worst_st = max(worst_i, cnt[0]), max(worst_st, depth)
    print(f'{k}: n {n} g {g} -> {r} insns {cnt[0]} stack {depth} {"same" if ok else "DIFFERENT"}', flush=True)
print(f'SUMMARY {len(ks)} draws, {same} same as native; worst {worst_i} instructions, deepest stack {worst_st} B')
