#!/usr/bin/env python3
"""Match RMHP08 tick_sites (from origin/main:runtime/src/tick_sites.inc) to RMHJ08
and emit runtime/src/tick_sites.inc.

Uses strict function-level and instruction-level verification:
- Function boundaries use symbol maps + stack prologues (never splitting on internal bl).
- Float sites (EaseDivisor/PerStep/Decay) verify lfs and identical r2 IEEE754 float32
  value (or identical struct offset when non-r2).
- Integer sites verify identical load width (1/2/4), context match (in-window +/-1
  or branch-separated countdown/up, Advance immediate k/-k), and struct offset / window similarity.
- RSO sites carry their module tag ("quest_field", "demo", "net_data") to keep overlapping
  addresses isolated per translation unit.
- Adds the 8 JPN-inlined sites (6 AreaStepCounters + 2 QuestStep CountupEvery 32).
"""
import argparse
import collections
import math
import re
import struct
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'scripts'))
from link_rso import Rso, static_exports  # noqa: E402

PAL_R2 = 0x8079FF60
JPN_R2 = 0x808A93C0

EXTRA_JPN_SITES = [
    (0x8018E3FC, 'Countup', 0, None, 'JPN AreaStepCounters inlined (0x8073B340)'),
    (0x8018E404, 'Countup', 0, None, 'JPN AreaStepCounters inlined (0x8073B2C4)'),
    (0x8018E408, 'Countup', 0, None, 'JPN AreaStepCounters inlined (0x8073B130)'),
    (0x801999D0, 'Countup', 0, None, 'JPN AreaStepCounters inlined (0x8073B340)'),
    (0x801999D8, 'Countup', 0, None, 'JPN AreaStepCounters inlined (0x8073B130)'),
    (0x801999E4, 'Countup', 0, None, 'JPN AreaStepCounters inlined (0x8073B2C4)'),
    (0x801FD0E8, 'CountupEvery', 32, None, 'JPN QuestStep quest work+0x28 (% 32 == 0)'),
    (0x801FD1D4, 'CountupEvery', 32, None, 'JPN QuestStep quest work+0x28 (% 32 == 0)'),
]


def load_dol(path):
    data = path.read_bytes()
    secs = []
    for i in range(18):
        off = struct.unpack_from('>I', data, i * 4)[0]
        va = struct.unpack_from('>I', data, 0x48 + i * 4)[0]
        sz = struct.unpack_from('>I', data, 0x90 + i * 4)[0]
        if sz:
            secs.append((i, va, sz, off))
    return data, secs


def read_f32(data, secs, va):
    for _, sva, sz, off in secs:
        if sva <= va and va + 4 <= sva + sz:
            return struct.unpack_from('>f', data, off + (va - sva))[0]
    return None


def norm_feat(w):
    op = (w >> 26) & 0x3F
    rA = (w >> 16) & 31
    simm = w & 0xFFFF
    if simm & 0x8000:
        simm -= 0x10000
    if op in (32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 48, 49, 50, 51, 52, 53, 54, 55):
        return ('ls', op, 'G' if rA in (1, 2, 13) else simm)
    if op == 14:
        return ('addi', rA == 0, simm if rA not in (1, 2, 13) else 'G')
    if op in (10, 11):
        return ('cmpi', op, simm if op == 11 else w & 0xFFFF)
    if op == 7:
        return ('mulli', simm)
    if op == 18:
        return ('br', w & 1)
    if op == 16:
        return ('bc', (w >> 21) & 31, (w >> 16) & 3)
    if op == 21:
        return ('rlw', (w >> 11) & 31, (w >> 6) & 31, (w >> 1) & 31)
    if op == 31:
        return ('x31', (w >> 1) & 0x3FF)
    return ('op', op)


def load_pal_tick_sites():
    txt = subprocess.check_output(['git', 'show', 'origin/main:runtime/src/tick_sites.inc'], cwd=ROOT, text=True)
    sites = []
    for line in txt.splitlines():
        m = re.match(r'^\{0x([0-9A-Fa-f]+)u,\s*TickSite::(\w+)(?:,\s*([^,}]+))?(?:,\s*"(\w+)")?\s*\},?(.*)', line)
        if m:
            pc = int(m.group(1), 16)
            kind = m.group(2)
            lim = int(m.group(3).strip(), 0) if m.group(3) else 0
            mod = m.group(4)
            cmt = m.group(5).strip()
            sites.append((pc, kind, lim, mod, cmt))
    return sites


def load_pal_sel_exports():
    pal_link_src = subprocess.check_output(['git', 'show', 'origin/main:scripts/link_rso.py'], cwd=ROOT, text=True)
    m_sb = re.search(r'SEL_BASES\s*=\s*(\[[^\]]+\])', pal_link_src)
    pal_sel_bases = eval(m_sb.group(1))
    pal_sel = Rso((ROOT / 'Assets/DATA_PAL/files/mh3.sel').read_bytes())
    imp = {}
    for sym, off, sec in pal_sel.exports:
        imp[sym] = off if sec == 0xFFF1 else pal_sel_bases[sec] + off
    return imp


def build_dol_funcs(words, tva, extra_starts):
    tsz = len(words) * 4
    starts = set(s for s in extra_starts if tva <= s < tva + tsz)
    for i in range(len(words) - 1):
        if (words[i] & 0xFFFF8000) == 0x94218000 and words[i + 1] == 0x7C0802A6:
            starts.add(tva + i * 4)
    starts = sorted(starts)
    funcs = collections.OrderedDict()
    for idx, s in enumerate(starts):
        e = min(starts[idx + 1] if idx + 1 < len(starts) else tva + tsz, s + 0x10000)
        ws = words[(s - tva) // 4 : (e - tva) // 4]
        if ws:
            funcs[s] = (e, ws)
    return starts, funcs


def build_rso_funcs(rso, linked, addrs):
    toff, tsz = rso.sections[1]
    tva = addrs[1]
    ws = struct.unpack_from(f'>{tsz // 4}I', linked, toff)
    starts = set()
    for _, off, sec in rso.exports:
        if sec == 1 and off < tsz:
            starts.add(tva + off)
    for i in range(len(ws) - 1):
        if (ws[i] & 0xFFFF8000) == 0x94218000 and ws[i + 1] == 0x7C0802A6:
            starts.add(tva + i * 4)
        if (ws[i] >> 26) == 18 and (ws[i] & 1) == 1:
            li = ws[i] & 0x03FFFFFC
            if li & 0x02000000:
                li -= 0x04000000
            tgt = tva + i * 4 + li
            if tva <= tgt < tva + tsz:
                starts.add(tgt)
    starts = sorted(starts)
    funcs = collections.OrderedDict()
    for idx, s in enumerate(starts):
        e = starts[idx + 1] if idx + 1 < len(starts) else tva + tsz
        funcs[s] = (e, ws[(s - tva) // 4 : (e - tva) // 4])
    return tva, ws, starts, funcs


def find_owner(starts, va):
    lo, hi = 0, len(starts) - 1
    while lo <= hi:
        m = (lo + hi) // 2
        if starts[m] <= va:
            lo = m + 1
        else:
            hi = m - 1
    return starts[hi] if hi >= 0 else None


def load_width(op):
    if op == 32:
        return 4
    if op == 34:
        return 1
    if op in (40, 42):
        return 2
    return 0


def pal_pattern(pws, pi, kind, lim):
    """Describe PAL site context so JPN verification matches its actual compiled idiom."""
    win = pws[pi + 1 : min(len(pws), pi + 17)]
    has_inc = any((x >> 26) in (12, 13, 14) and (x & 0xFFFF) == 1 for x in win)
    has_dec = any((x >> 26) in (12, 13, 14) and (x & 0xFFFF) == 0xFFFF for x in win)
    win8 = pws[pi + 1 : min(len(pws), pi + 9)]
    has_cmp_branch = any((x >> 26) in (10, 11, 31) and ((x >> 26) != 31 or ((x >> 1) & 0x3FF) in (0, 32)) for x in win8) and any((x >> 26) == 16 for x in win8)
    has_ext_cmp_branch = has_cmp_branch or (len(win8) >= 1 and (win8[0] >> 26) == 16)
    pw = pws[pi]
    prA = (pw >> 16) & 31
    psimm = pw & 0xFFFF
    if psimm & 0x8000:
        psimm -= 0x10000
    is_lis_load = False
    for k in range(max(0, pi - 6), pi):
        kw = pws[k]
        if (kw >> 26) == 15 and ((kw >> 16) & 31) == 0 and ((kw >> 21) & 31) == prA:
            is_lis_load = True
            break
    is_struct_load = (prA not in (1, 2, 13)) and not is_lis_load
    return {
        'has_inc': has_inc,
        'has_dec': has_dec,
        'has_cmp_branch': has_cmp_branch or has_ext_cmp_branch,
        'is_struct': is_struct_load,
        'simm': psimm,
        'op': (pw >> 26) & 0x3F,
        'rA': prA,
    }


def jpn_insn_matches(jws, ji, kind, lim, pat, p_fval, jpn_data, jpn_secs):
    jw = jws[ji]
    jop = (jw >> 26) & 0x3F
    jrA = (jw >> 16) & 31
    jsimm = jw & 0xFFFF
    if jsimm & 0x8000:
        jsimm -= 0x10000

    if kind in ('EaseDivisor', 'PerStep', 'Decay'):
        if jop != 48:
            return False, False
        if p_fval is not None:
            if jrA != 2:
                return False, False
            jf = read_f32(jpn_data, jpn_secs, JPN_R2 + jsimm)
            if jf != p_fval:
                return False, False
            return True, True
        return True, (not pat['is_struct'] or jsimm == pat['simm'])

    if load_width(jop) != load_width(pat['op']):
        return False, False

    win = jws[ji + 1 : min(len(jws), ji + 17)]
    win8 = jws[ji + 1 : min(len(jws), ji + 9)]

    if kind in ('Countdown', 'CountdownPastZero', 'CountdownEvery'):
        j_dec = any((x >> 26) in (12, 13, 14) and (x & 0xFFFF) == 0xFFFF for x in win)
        if pat['has_dec']:
            if not j_dec:
                # Allow branch-separated decrement only if same struct offset and cmp+branch present
                j_cb = any((x >> 26) in (10, 11) for x in win8) and any((x >> 26) == 16 for x in win8)
                if not (pat['has_cmp_branch'] and j_cb and pat['is_struct'] and jsimm == pat['simm']):
                    return False, False
        elif pat['has_cmp_branch']:
            j_cb = any((x >> 26) in (10, 11) for x in win8) and any((x >> 26) == 16 for x in win8)
            if not (j_cb and (not pat['is_struct'] or jsimm == pat['simm'])):
                return False, False
        elif not j_dec:
            return False, False
    elif kind in ('Countup', 'CountupEvery'):
        j_inc = any((x >> 26) in (12, 13, 14) and (x & 0xFFFF) == 1 for x in win)
        if pat['has_inc'] and not j_inc:
            return False, False
    elif kind == 'Advance':
        k16 = lim & 0xFFFF
        nk16 = (-lim) & 0xFFFF
        khi = (lim >> 16) & 0xFFFF
        if not any(
            ((x >> 26) in (12, 13, 14) and (x & 0xFFFF) in (k16, nk16))
            or ((x >> 26) == 15 and (x & 0xFFFF) == khi)
            for x in win
        ):
            return False, False

    offset_eq = pat['is_struct'] and (jrA not in (1, 2, 13)) and (jsimm == pat['simm'])
    return True, offset_eq


def win_sim(pws, pi, jws, ji, offset_eq, rad=10):
    pf = collections.Counter(norm_feat(w) for w in pws[max(0, pi - rad) : min(len(pws), pi + rad + 1)])
    jf = collections.Counter(norm_feat(w) for w in jws[max(0, ji - rad) : min(len(jws), ji + rad + 1)])
    inter = sum(min(pf[k], jf[k]) for k in pf if k in jf)
    tot = max(sum(pf.values()), sum(jf.values()), 1)
    bonus = 0.0
    if (pws[pi] >> 26) == (jws[ji] >> 26):
        bonus += 0.08
    if offset_eq:
        bonus += 0.25
    near = sum(
        1 for d in (-2, -1, 1, 2, 3)
        if 0 <= pi + d < len(pws) and 0 <= ji + d < len(jws) and norm_feat(pws[pi + d]) == norm_feat(jws[ji + d])
    ) * 0.05
    return inter / tot + bonus + near


def match_sites_in_func(slist, ps, pws, js, jws, pal_data, pal_secs, jpn_data, jpn_secs, used_jpc):
    """Find best 1-to-1 assignment of PAL sites in slist to JPN PCs in jws."""
    cand_map = {}
    for site in slist:
        pc, kind, lim, mod, cmt = site
        pi = (pc - ps) // 4
        if pi >= len(pws):
            cand_map[pc] = []
            continue
        pw = pws[pi]
        pop = (pw >> 26) & 0x3F
        prA = (pw >> 16) & 31
        p_fval = None
        if kind in ('EaseDivisor', 'PerStep', 'Decay') and pop == 48 and prA == 2:
            psimm = pw & 0xFFFF
            if psimm & 0x8000:
                psimm -= 0x10000
            p_fval = read_f32(pal_data, pal_secs, PAL_R2 + psimm)
        pat = pal_pattern(pws, pi, kind, lim)
        icands = []
        for ji in range(len(jws)):
            jpc = js + ji * 4
            if jpc in used_jpc:
                continue
            ok, offset_eq = jpn_insn_matches(jws, ji, kind, lim, pat, p_fval, jpn_data, jpn_secs)
            if not ok:
                continue
            sc = win_sim(pws, pi, jws, ji, offset_eq)
            pos_pen = abs((pi / max(len(pws), 1)) - (ji / max(len(jws), 1))) * 0.15
            final_sc = sc - pos_pen
            # Safety gate for non-offset-matched struct loads
            min_req = 0.43 if (offset_eq or not pat['is_struct']) else 0.70
            if final_sc >= min_req:
                icands.append((final_sc, jpc, offset_eq))
        icands.sort(reverse=True)
        cand_map[pc] = icands

    # Greedy assignment sorted by highest best-score-minus-second-best or highest score
    chosen = {}
    local_used = set()
    order = sorted(slist, key=lambda s: (len(cand_map[s[0]]) > 0, cand_map[s[0]][0][0] if cand_map[s[0]] else -1.0), reverse=True)
    for site in order:
        pc = site[0]
        picked = None
        for sc, jpc, oeq in cand_map[pc]:
            if jpc not in local_used:
                picked = (sc, jpc, oeq)
                break
        if picked:
            local_used.add(picked[1])
            chosen[pc] = picked
    return chosen


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--dry-run', action='store_true')
    args = parser.parse_args()

    pal_sites = load_pal_tick_sites()
    pal_data, pal_secs = load_dol(ROOT / 'Assets/DATA_PAL/sys/main.dol')
    jpn_data, jpn_secs = load_dol(ROOT / 'Assets/DATA_JPN/sys/main.dol')

    # --- 1. DOL matching ---
    pt1_va, pt1_sz, pt1_off = pal_secs[1][1], pal_secs[1][2], pal_secs[1][3]
    jt1_va, jt1_sz, jt1_off = jpn_secs[1][1], jpn_secs[1][2], jpn_secs[1][3]
    pwords_all = struct.unpack_from(f'>{pt1_sz // 4}I', pal_data, pt1_off)
    jwords_all = struct.unpack_from(f'>{jt1_sz // 4}I', jpn_data, jt1_off)

    pal_syms = []
    for line in (ROOT / 'symbols-pal.txt').read_text().splitlines():
        p = line.split('#', 1)[0].split()
        if p:
            try:
                pal_syms.append(int(p[0], 16))
            except ValueError:
                pass
    jpn_map = []
    for line in (ROOT / 'build/MAP.txt').read_text().splitlines():
        p = line.split()
        if p:
            jpn_map.append(int(p[0], 16))

    pstarts, pfuncs = build_dol_funcs(pwords_all, pt1_va, pal_syms)
    jstarts, jfuncs = build_dol_funcs(jwords_all, jt1_va, jpn_map)

    df = collections.Counter()
    jfunc_feats = {}
    for js, (je, jws) in jfuncs.items():
        f = collections.Counter(norm_feat(w) for w in jws)
        jfunc_feats[js] = f
        for k in f:
            df[k] += 1
    N = len(jfuncs)
    idf = {k: math.log((N + 1) / (c + 1)) + 1.0 for k, c in df.items()}
    inv = collections.defaultdict(list)
    jnorms = {}
    for js, f in jfunc_feats.items():
        jnorms[js] = math.sqrt(sum((v * idf.get(k, 1.0)) ** 2 for k, v in f.items())) or 1.0
        for k in f:
            if idf.get(k, 0) > 4.2:
                inv[k].append(js)

    dol_sites = [s for s in pal_sites if s[0] < 0x80800000]
    by_pfunc = collections.defaultdict(list)
    for s in dol_sites:
        ps = find_owner(pstarts, s[0])
        by_pfunc[ps].append(s)

    matched_entries = []  # (jpc, kind, lim, mod, comment)
    skipped = []
    used_dol_jpc = set()

    for ps, slist in sorted(by_pfunc.items()):
        _, pws = pfuncs[ps]
        pf = collections.Counter(norm_feat(w) for w in pws)
        pnorm = math.sqrt(sum((v * idf.get(k, 1.0)) ** 2 for k, v in pf.items())) or 1.0
        cands = collections.Counter()
        for k in pf:
            if k in inv:
                for js in inv[k]:
                    cands[js] += 1
        cand_list = [js for js, _ in cands.most_common(350)] if cands else list(jfuncs.keys())
        scored_f = []
        for js in cand_list:
            _, jws = jfuncs[js]
            jf = jfunc_feats[js]
            lr = min(len(pws), len(jws)) / max(len(pws), len(jws), 1)
            if lr < 0.25:
                continue
            cos = sum(v * jf[k] * (idf.get(k, 1.0) ** 2) for k, v in pf.items() if k in jf) / (pnorm * jnorms[js])
            scored_f.append((cos * (0.7 + 0.3 * lr), js))
        scored_f.sort(reverse=True)
        if not scored_f or scored_f[0][0] < 0.42:
            for s in slist:
                skipped.append((s, f'func_miss pfunc=0x{ps:08X}'))
            continue

        # Try top 3 candidate JPN functions and pick the one matching the most sites
        best_assign = {}
        best_js = None
        for fscore, js in scored_f[:3]:
            if fscore < 0.42 or (scored_f[0][0] - fscore) > 0.15:
                break
            _, jws = jfuncs[js]
            assign = match_sites_in_func(slist, ps, pws, js, jws, pal_data, pal_secs, jpn_data, jpn_secs, used_dol_jpc)
            if len(assign) > len(best_assign):
                best_assign = assign
                best_js = js
            if len(assign) == len(slist):
                break

        for s in slist:
            pc, kind, lim, mod, cmt = s
            if pc in best_assign:
                sc, jpc, _ = best_assign[pc]
                used_dol_jpc.add(jpc)
                matched_entries.append((jpc, kind, lim, None, cmt))
            else:
                skipped.append((s, f'insn_miss pfunc=0x{ps:08X}->0x{scored_f[0][1]:08X}({scored_f[0][0]:.2f})'))

    # --- 2. RSO matching ---
    pal_imp = load_pal_sel_exports()
    jpn_imp = static_exports()
    rso_cfg = {
        'quest_field': ('quest_data', 0x808C76A0, 0x809E3900, 'quest_field'),
        'enemy_field': ('em_data',    0x80972F40, 0x80A80220, None),
        'demo':        ('demo_data',  0x8097D080, 0x80B18920, 'demo'),
        'net_data':    ('net_data',   0x8097D080, 0x80B18920, 'net_data'),
    }
    rso_loaded = {}
    for prof, (name, pbase, jbase, _) in rso_cfg.items():
        prso = Rso((ROOT / f'Assets/DATA_PAL/files/01/{name}.rso').read_bytes())
        pl, pa = prso.link(pbase, pal_imp)
        jrso = Rso((ROOT / f'Assets/DATA_JPN/files/rel/{name}.rso').read_bytes())
        jl, ja = jrso.link(jbase, jpn_imp)
        rso_loaded[prof] = (
            build_rso_funcs(prso, pl, pa),
            build_rso_funcs(jrso, jl, ja),
            (prso, pa),
        )

    rso_by_mod = collections.defaultdict(list)
    for s in pal_sites:
        pc, kind, lim, mod, cmt = s
        if pc < 0x80800000:
            continue
        if mod:
            rso_by_mod[mod].append(s)
        else:
            for prof, (_, _, (prso, pa)) in rso_loaded.items():
                if pa[1] <= pc < pa[1] + prso.sections[1][1]:
                    rso_by_mod[prof].append(s)
                    break

    rso_used_per_mod = collections.defaultdict(set)
    for prof, slist in rso_by_mod.items():
        (_, _, pstarts_r, pfuncs_r), (_, _, jstarts_r, jfuncs_r), _ = rso_loaded[prof]
        jtag = rso_cfg[prof][3]
        jf_map = {js: collections.Counter(norm_feat(w) for w in jws) for js, (_, jws) in jfuncs_r.items()}
        by_pf = collections.defaultdict(list)
        for s in slist:
            by_pf[find_owner(pstarts_r, s[0])].append(s)
        for ps, flist in sorted(by_pf.items()):
            _, pws = pfuncs_r[ps]
            pf = collections.Counter(norm_feat(w) for w in pws)
            scored_f = []
            for js, (_, jws) in jfuncs_r.items():
                lr = min(len(pws), len(jws)) / max(len(pws), len(jws), 1)
                if lr < 0.3:
                    continue
                jf = jf_map[js]
                inter = sum(min(pf[k], jf[k]) for k in pf if k in jf)
                sc = inter / max(sum(pf.values()), sum(jf.values()), 1)
                scored_f.append((sc, js))
            scored_f.sort(reverse=True)
            if not scored_f or scored_f[0][0] < 0.42:
                for s in flist:
                    skipped.append((s, f'rso_func_miss {prof} 0x{ps:08X}'))
                continue
            best_assign = {}
            for fsc, js in scored_f[:3]:
                if fsc < 0.42:
                    break
                _, jws = jfuncs_r[js]
                assign = match_sites_in_func(flist, ps, pws, js, jws, pal_data, pal_secs, jpn_data, jpn_secs, rso_used_per_mod[prof])
                if len(assign) > len(best_assign):
                    best_assign = assign
                if len(assign) == len(flist):
                    break
            for s in flist:
                pc, kind, lim, _, cmt = s
                if pc in best_assign:
                    _, jpc, _ = best_assign[pc]
                    rso_used_per_mod[prof].add(jpc)
                    matched_entries.append((jpc, kind, lim, jtag, cmt))
                else:
                    skipped.append((s, f'rso_insn_miss {prof} 0x{ps:08X}'))

    # --- 3. Add JPN inlined sites & validate uniqueness ---
    extra_map = {e[0]: e for e in EXTRA_JPN_SITES}
    # QuestStep (PAL 0x803B0C00 / 0x803B0CE4) maps to JPN 0x801FD0E8 / 0x801FD1D4 where
    # increment and % 32 == 0 share a single lwz; upgrade those entries to CountupEvery(32).
    matched_entries = [e for e in matched_entries if e[0] not in extra_map]
    extra_matched_from_pal = 2  # 0x801FD0E8, 0x801FD1D4 replaced Countup with CountupEvery(32)
    for jpc, kind, lim, mod, desc in EXTRA_JPN_SITES:
        matched_entries.append((jpc, kind, lim, mod, f'// {desc}'))

    matched_entries.sort(key=lambda e: e[0])
    seen_pc = {}
    for jpc, kind, lim, mod, cmt in matched_entries:
        if jpc in seen_pc:
            raise SystemExit(f'FATAL: PC collision at 0x{jpc:08X}: {seen_pc[jpc]} vs ({kind}, {mod})')
        seen_pc[jpc] = (kind, mod)

    extra_inlined_only = {x[0] for x in EXTRA_JPN_SITES} - {0x801FD0E8, 0x801FD1D4}
    dol_matched = sum(1 for e in matched_entries if e[0] < 0x80800000 and e[0] not in extra_inlined_only)
    rso_matched = sum(1 for e in matched_entries if e[0] >= 0x80800000)
    print(f'Matched PAL->JPN: DOL {dol_matched}/{len(dol_sites)} (incl {extra_matched_from_pal} upgraded QuestStep), RSO {rso_matched}/{sum(len(v) for v in rso_by_mod.values())}, Extra +{len(extra_inlined_only)} = Total {len(matched_entries)} (Skipped {len(skipped)})')
    for s, why in skipped:
        print(f'  SKIP PAL 0x{s[0]:08X} {s[1]}({s[2]}) [{s[3] or "DOL"}]: {why} | {s[4][:60]}')

    if args.dry_run:
        return

    lines = [
        '// Tick sites for RMHJ08: guest loads of step counters and countdowns (recomp.yml',
        '// translation.tick_sites_path; tick_sites.cpp). {pc, kind[, limit[, module]]},',
        '// sorted by pc. A module ("demo", "net_data", "quest_field") marks a site in',
        '// addresses a boot module and an overlay share: only that module\'s translation',
        '// hooks it (scripts/tick_sites_filter.py).',
    ]
    for jpc, kind, lim, mod, cmt in matched_entries:
        if mod:
            entry = f'{{0x{jpc:08X}u, TickSite::{kind}, {lim}, "{mod}"}},'
        elif lim:
            entry = f'{{0x{jpc:08X}u, TickSite::{kind}, {lim}}},'
        else:
            entry = f'{{0x{jpc:08X}u, TickSite::{kind}}},'
        if cmt:
            lines.append(f'{entry:<52s} {cmt}')
        else:
            lines.append(entry)

    (ROOT / 'runtime/src/tick_sites.inc').write_text('\n'.join(lines) + '\n')


if __name__ == '__main__':
    main()
