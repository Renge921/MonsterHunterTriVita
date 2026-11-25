#!/usr/bin/env python3
"""Find the guest reads of MH3's step counters (system_w+0xC, incremented once
per logic step by RunTasks 0x802A5C38, and the scene counters in COUNTERS)
that test them periodically:
`counter % N == k` ("every N steps, at phase k"). Writes
runtime/src/step_counter_sites.inc: one {pc, N, k, kind} per site, where kind
is 1 for an equality test (an event on the steps the phase comes round), 2 for
one on a player's step counter (MANUAL) and 0 for any other use of the phase
(read as the tick count). The translator hooks
the listed loads (translation.load_hooks in recomp.yml) and the runtime
(step_counter.cpp) answers them in ticks of game time.

usage: step_counter_sites.py [--jpn-binary | main.s [overlay.s ...]]
  Without arguments (or with --jpn-binary), decodes Assets/DATA/sys/main.dol
  and the RMHJ08 RSOs directly via scripts/link_rso.py.
"""
import argparse
import re
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
if str(ROOT / 'scripts') not in sys.path:
    sys.path.insert(0, str(ROOT / 'scripts'))

# Counters the runtime advances by the ticks of each step (step_counter.cpp), RMHJ08.
COUNTERS = {
    0x80768AB4,  # system_w+0xC: steps (RunTasks 0x802A5C38)
    0x8076933C,  # system_w+0x894: play-time steps (PlayTimeTick 0x802BFC2C)
    0x80750150,  # village work+0x30: steps (VillageStepCounters 0x8025B058)
    0x8074FFF0,  # village talk work+0x150: steps (VillageTalkStep 0x80267FD0)
    0x8073B340,  # area steps 0 (inlined AreaStepCounters)
    0x8073B130,  # area steps 1 (inlined AreaStepCounters)
    0x8073B2C4,  # area steps 2 (inlined AreaStepCounters)
}
# Quest clock steps (quest work+0x28) in JPN QuestStep (0x801FD0E8 / 0x801FD1D4)
# are fused lwz/addi/clrlwi./stw/bne handled by tick_sites.inc (TickSite::CountupEvery, 32).
POINTER_READS = {}
# enemy_timers.cpp is not ported in this phase, so enemy periodic checks are covered here.
PORTED = {}
# Periodic tests given by hand (PLW+0x20 / fused `steps & 4`), RMHJ08: pc -> (N, k, kind).
MANUAL = {
    # PAL 0x801514B4 (`steps & 4` after `steps % 4 == 0`), inlined 3 times in JPN:
    0x80083224: (8, 4, 1),
    0x800848C8: (8, 4, 1),
    0x80084974: (8, 4, 1),
    # DOL PLW+0x20 periodic checks (PAL 0x80264334 -> JPN 0x80331BC0..):
    0x80331E5C: (30, 0, 2),
    0x80331EB0: (32, 0, 2),
    0x80331F1C: (60, 0, 2),
    0x80332170: (8, 0, 2),
    0x803321C0: (16, 0, 2),
    # quest_field pl_effect_sub PLW+0x20 checks (JPN quest_data @ 0x809E3900;
    # verified lobby_data has no lwz at these 7 PCs):
    0x809ED604: (32, 0, 2),
    0x809ED668: (16, 0, 2),
    0x809ED69C: (16, 0, 2),
    0x809ED6CC: (16, 0, 2),
    0x809ED700: (16, 0, 2),
    0x809ED73C: (32, 0, 2),
    0x809ED780: (36, 0, 2),
}
# Their own increments (lwz; addi 1; stw), not reads.
INCREMENTS = {
    0x802A5C88,  # RunTasks
    0x802BFC38,  # PlayTimeTick
    0x8025B108,  # VillageStepCounters
    0x80268058,  # VillageTalkStep
    0x8018E3FC, 0x8018E404, 0x8018E408,  # inlined AreaStepCounters site 1
    0x801999D0, 0x801999D8, 0x801999E4,  # inlined AreaStepCounters site 2
}


def parse_asm(path):
    lines = []
    for l in open(path):
        m = re.match(r'^([0-9a-f]{8}):\s+(\S+)\s*(.*)$', l.rstrip())
        if m:
            lines.append((int(m[1], 16), m[2], [o.strip() for o in m[3].split(',')] if m[3] else []))
        elif re.match(r'^[0-9a-f]{8} <', l):
            lines.append(None)
    return lines


def site_kind_asm(lines, i, reg):
    """Returns (N, k, eq) for the periodic test on the counter loaded into reg at i, or None."""
    n = k = None
    val = reg
    j = i + 1
    end = min(i + 14, len(lines))
    while j < end and lines[j] is not None:
        pc, op, ops = lines[j]
        if op in ('b', 'blr', 'bctr'):
            return None
        if op in ('clrlwi', 'clrlwi.') and ops[1] == val:
            n = 1 << (32 - int(ops[2])); val = ops[0]
            if op == 'clrlwi.':
                k = 0
                for jj in range(j + 1, min(j + 4, end)):
                    if lines[jj] is None:
                        break
                    bop, bops = lines[jj][1], lines[jj][2]
                    if bop in ('beq', 'bne') or (bop in ('bt', 'bf') and bops and bops[0].isdigit()):
                        return n, 0, True
        elif op == 'andi.' and ops[1] == val:
            uimm = int(ops[2], 0)
            if uimm > 0 and (uimm & (uimm + 1)) == 0:
                n = uimm + 1; val = ops[0]; k = 0
                return n, 0, True
        elif op in ('divwu', 'divw') and ops[1] == val and n is None:
            n = 0
        elif op == 'mulli' and n is None:
            n = int(ops[2]) if ops[1] != val else None
        elif op in ('sub', 'subf', 'subf.') and n is not None and (ops[1] == val or ops[2] == val):
            val = ops[0]
            if op == 'subf.':
                return n, 0, True
        elif op in ('cmplwi', 'cmpwi') and len(ops) >= 2 and ops[-2] == val:
            k = int(ops[-1])
            cr = ops[0] if len(ops) == 3 else '0'
            for jj in range(j + 1, min(j + 4, end)):
                if lines[jj] is None:
                    break
                bop, bops = lines[jj][1], lines[jj][2]
                if bop == 'bc' and len(bops) >= 2 and bops[0].isdigit() and int(bops[0]) & 0x14 == 0x04:
                    bop, bops = 'bt', bops[1:]
                if bop in ('bt', 'bf') and bops and bops[0].isdigit():
                    bit = int(bops[0])
                    eq = bit % 4 == 2 and bit // 4 == int(cr.replace('cr', '') or 0)
                    return n, k, eq
            return n, k, False
        elif op == 'cmplw' or op == 'cmpw':
            return None
        j += 1
    return None


def analyze_binary_window(words, i, reg_val):
    """Analyze binary PPC instructions after lwz reg_val, ... at index i.
    Returns (N, k, eq) or None.
    """
    n = k = None
    val = reg_val
    rc_zero = False
    slwi_n = None
    end = min(i + 22, len(words))
    for j in range(i + 1, end):
        w = words[j]
        op = (w >> 26) & 0x3F
        rD = (w >> 21) & 31
        rA = (w >> 16) & 31
        rB = (w >> 11) & 31
        simm = w & 0xFFFF
        if simm & 0x8000:
            simm -= 0x10000
        rc = w & 1
        if op == 18 and (w & 1) == 0:
            return None
        if w in (0x4E800020, 0x4E800420):  # blr / bctr
            return None
        if op == 21:  # rlwinm / clrlwi / slwi / rotlwi
            rS = rD
            dst = rA
            sh = rB
            mb = (w >> 6) & 31
            me = (w >> 1) & 31
            if rS == val and sh == 0 and me == 31 and mb > 0:
                n = 1 << (32 - mb)
                val = dst
                if rc:
                    k = 0
                    rc_zero = True
            elif rS == val and mb == 0 and me == 31 - sh and sh > 0 and n is None:
                slwi_n = (dst, 1 << (32 - sh))
            elif slwi_n and rS == slwi_n[0] and mb == 0 and me == 31 and sh == (32 - (slwi_n[1].bit_length() - 1)):
                n = slwi_n[1]
                val = dst
        elif op == 28:  # andi.
            rS = rD
            dst = rA
            uimm = w & 0xFFFF
            if rS == val and uimm > 0 and (uimm & (uimm + 1)) == 0:
                n = uimm + 1
                val = dst
                k = 0
                rc_zero = True
        elif op == 7 and n is None:  # mulli
            if rA != val and simm > 1:
                n = simm
        elif op == 31:
            xo = (w >> 1) & 0x3FF
            if xo in (459, 491) and rA == val and n is None:  # divwu / divw
                n = 0
            elif xo in (40, 266) and n is not None:
                if xo == 40 and (rA == val or rB == val):  # subf / subf.
                    val = rD
                    if rc:
                        k = 0
                        rc_zero = True
                elif xo == 266 and slwi_n and (rA == val or rB == val) and rc:  # add.
                    val = rD
                    k = 0
                    rc_zero = True
            elif xo in (0, 32) and (rA == val or rB == val):  # cmpw / cmplw
                return None
        elif op in (10, 11):  # cmplwi / cmpwi
            cr = (rD >> 2) & 7
            if rA == val and n is not None:
                k = simm if op == 11 else (w & 0xFFFF)
                for jj in range(j + 1, min(j + 5, end)):
                    bw = words[jj]
                    if (bw >> 26) == 16:
                        bo = (bw >> 21) & 31
                        bi = (bw >> 16) & 31
                        if (bo & 0x14) == 0x04 and bi // 4 == cr:
                            return n, k, (bi % 4) == 2
                return n, k, False
        elif op == 16 and rc_zero and n is not None:
            bo = (w >> 21) & 31
            bi = (w >> 16) & 31
            if (bo & 0x14) == 0x04 and bi // 4 == 0:
                return n, k, (bi % 4) == 2
    return None


def scan_binary_region(va_base, words, label, owner, shared):
    reg = {}
    sites, skipped = [], []
    for i, w in enumerate(words):
        pc = va_base + i * 4
        if va_base >= 0x80800000 and owner.setdefault(pc, label) != label:
            shared.add(pc)
        op = (w >> 26) & 0x3F
        rD = (w >> 21) & 31
        rA = (w >> 16) & 31
        simm = w & 0xFFFF
        if simm & 0x8000:
            simm -= 0x10000
        if op == 37 and rA == 1 and rD == 1 and simm < 0:
            reg = {}
        if op == 15 and rA == 0:
            reg[rD] = ((w & 0xFFFF) << 16) & 0xFFFFFFFF
            continue
        if op == 14 and rA in reg:
            reg[rD] = (reg[rA] + simm) & 0xFFFFFFFF
            continue
        if op == 32 and rA in reg and pc not in INCREMENTS and pc not in PORTED and pc not in MANUAL:
            addr = (reg[rA] + simm) & 0xFFFFFFFF
            if addr in COUNTERS:
                got = analyze_binary_window(words, i, rD)
                if got and (got[0] > 0 or (got[0] == 0 and got[1] == 0 and got[2])):
                    sites.append((pc, got[0], got[1], 1 if got[2] else 0))
                else:
                    skipped.append(pc)
        if op in (14, 32, 33, 34, 35, 40, 41, 42, 43) and not (op == 14 and rA in reg):
            reg.pop(rD, None)
        elif op == 31:
            xo = (w >> 1) & 0x3FF
            if xo not in (0, 32):
                dest = rA if xo in (24, 26, 28, 60, 124, 284, 316, 412, 444, 476, 536, 792, 824, 922, 954) else rD
                reg.pop(dest, None)
    return sites, skipped


def run_jpn_binary():
    from link_rso import Rso, static_exports
    dol = (ROOT / 'Assets/DATA/sys/main.dol').read_bytes()
    imports = static_exports()
    owner, shared = {}, set()
    sites, skipped = [], []

    # DOL .text sections (slots 0, 1)
    for sec in range(2):
        off = struct.unpack_from('>I', dol, sec * 4)[0]
        va = struct.unpack_from('>I', dol, 0x48 + sec * 4)[0]
        sz = struct.unpack_from('>I', dol, 0x90 + sec * 4)[0]
        if sz:
            ws = struct.unpack_from(f'>{sz // 4}I', dol, off)
            s, sk = scan_binary_region(va, ws, f'dol_{sec}', owner, shared)
            sites += s
            skipped += sk

    # Boot RSOs and overlay profiles at RMHJ08 load addresses
    rso_targets = [
        ('hbm', 'hbm_data', 0x808D9520),
        ('com', 'com_data', 0x8092AF60),
        ('lobby', 'lobby_data', 0x809E3900),
        ('demo', 'demo_data', 0x80B18920),
        ('quest', 'quest_data', 0x80B57740),
        ('map_village', 'map_village', 0x80B18920),
        ('quest_field', 'quest_data', 0x809E3900),
        ('enemy_field', 'em_data', 0x80A80220),
        ('net_data', 'net_data', 0x80B18920),
        ('map_town_online', 'map_town', 0x80B36120),
        ('map_town_quest', 'map_town', 0x80B18920),
    ] + [(f'map{m:02d}_field', f'map{m:02d}', 0x80BC0000) for m in range(12)]

    # Track non-lwz sites for MANUAL lobby_data collision checks
    rso_words = {}
    for prof, name, base in rso_targets:
        rso = Rso((ROOT / f'Assets/DATA/files/rel/{name}.rso').read_bytes())
        linked, addrs = rso.link(base, imports)
        toff, tsz = rso.sections[1]
        ws = struct.unpack_from(f'>{tsz // 4}I', linked, toff)
        rso_words[prof] = (addrs[1], tsz, ws)
        # Note: lobby_data shares 0x809E3900 with quest_field; only scan distinct modules per PC,
        # and verify no MANUAL kind=2 PC collides with an lwz in the overlapping module.
        if prof in ('lobby', 'map_village', 'net_data', 'map_town_quest') or prof.startswith('map0') or prof.startswith('map1'):
            # Check reads in other overlays too just in case
            s, sk = scan_binary_region(addrs[1], ws, prof, {}, set())
        else:
            s, sk = scan_binary_region(addrs[1], ws, prof, owner, shared)
        sites += s
        skipped += sk

    # Verify no MANUAL kind=2 PC collides with an lwz in lobby_data
    ltva, ltsz, lws = rso_words['lobby']
    for pc, (_, _, kind) in MANUAL.items():
        if kind == 2 and ltva <= pc < ltva + ltsz:
            lw = lws[(pc - ltva) // 4]
            if (lw >> 26) == 32:
                raise SystemExit(f'MANUAL kind=2 site {pc:08x} collides with lwz in lobby_data')

    return sites, skipped, shared


def run_asm(paths):
    lines = parse_asm(paths[0])
    owner, shared = {}, set()
    for path in paths[1:]:
        for e in parse_asm(path):
            if e is not None and e[0] < 0x80800000:
                continue
            if e is not None and owner.setdefault(e[0], path) != path:
                shared.add(e[0])
            lines.append(e)
        lines.append(None)
    sites, skipped = [], []
    reg = {}
    for i, e in enumerate(lines):
        if e is None:
            reg = {}
            continue
        pc, op, ops = e
        if op == 'lis':
            reg[ops[0]] = (int(ops[1]) << 16) & 0xFFFFFFFF
            continue
        if op == 'addi' and len(ops) == 3 and ops[1] in reg:
            reg[ops[0]] = (reg[ops[1]] + int(ops[2])) & 0xFFFFFFFF
            continue
        m = re.match(r'(-?\d+)\((\d+)\)$', ops[-1]) if ops else None
        if op == 'lwz' and m and (m[2] in reg or pc in POINTER_READS) and pc not in INCREMENTS and pc not in PORTED and pc not in MANUAL:
            if pc in POINTER_READS or (reg[m[2]] + int(m[1])) & 0xFFFFFFFF in COUNTERS:
                got = site_kind_asm(lines, i, ops[0])
                if got and (got[0] or (got[0] == 0 and got[1] == 0 and got[2])):
                    sites.append((pc, got[0], got[1], 1 if got[2] else 0))
                else:
                    skipped.append(pc)
        if ops and ops[0] in reg and op not in ('stw', 'stb', 'sth', 'stfs', 'stfd', 'cmpwi', 'cmplwi', 'cmpw', 'cmplw'):
            reg.pop(ops[0], None)
    return sites, skipped, shared


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--jpn-binary', action='store_true', help='decode JPN DOL and RSOs directly (default when no files given)')
    parser.add_argument('files', nargs='*')
    args = parser.parse_args()

    if args.jpn_binary or not args.files:
        sites, skipped, shared = run_jpn_binary()
    else:
        sites, skipped, shared = run_asm(args.files)

    sites += [(pc, n, k, kind) for pc, (n, k, kind) in MANUAL.items()]
    sites = sorted(set(sites))

    # Validate invariants
    seen_pcs = set()
    for pc, n, k, eq in sites:
        assert pc not in seen_pcs, f'duplicate pc {pc:08x}'
        seen_pcs.add(pc)
        assert k >= 0, f'negative k at {pc:08x}: {k}'
        if n > 0:
            assert k < n, f'k >= n at {pc:08x}: k={k} n={n}'
        if pc in shared:
            raise SystemExit(f'site {pc:08x} is in more than one overlay')

    out = [
        '// Generated by scripts/step_counter_sites.py for RMHJ08; do not edit.',
        '// {pc, N, k, kind}: the step counter read at pc is tested as counter % N (== k: kind 1,',
        "// or 2 on a player's step counter);",
        '// N = 0: N is only known at run time and the test is == 0.',
    ]
    out += [f'{{0x{pc:08X}u, {n}u, {k}u, {eq}}},' for pc, n, k, eq in sites]
    (ROOT / 'runtime/src/step_counter_sites.inc').write_text('\n'.join(out) + '\n')
    print(f'{len(sites)} periodic sites ({sum(s[3] > 0 for s in sites)} equality, {sum(s[3] == 2 for s in sites)} kind2), {len(skipped)} other reads', file=sys.stderr)


if __name__ == '__main__':
    main()
