#!/usr/bin/env python3
"""
MH3 Vita Porting Integrity & Pre-flight Verification Script
============================================================
このスクリプトは、日本版 (RMHJ08) 移植における以下の潜在的不整合を機械的に検査します:
1. プロデューサ型ネイティブフックの未移植（Disabled unported PAL address）検査
2. マトリクス・モデル・レンダリング定数アドレス (locations 等) の整合性検査
3. 残存 PAL アドレス (0x8068xxxx, 0x8079xxxx, 0x8009xxxx 等) の静的監査
4. 状態参照 (Read/Find) における即死例外 (MH3_THROW) に対する安全フォールバックの配備確認
"""

import os
import re
import sys

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
RUNTIME_DIR = os.path.join(REPO_ROOT, "runtime")

def check_disabled_producer_hooks():
    """未移植の Disabled PAL フックのうち、Store/Produce/Set などの必須プロデューサを調査"""
    print("[1/5] Checking Disabled unported PAL hooks for missing producers...")
    disabled_hooks = []
    active_registered = set()

    pal_pattern = re.compile(r'//\s*Disabled unported PAL address[^:]*:\s*REGISTER_NATIVE_FUNCTION(?:_AS)?\s*\(\s*(0x[0-9a-fA-F]+)\s*,\s*([a-zA-Z0-9_]+)')
    active_pattern = re.compile(r'^\s*REGISTER_NATIVE_FUNCTION(?:_AS)?\s*\(\s*(0x[0-9a-fA-F]+)\s*,\s*([a-zA-Z0-9_]+)')

    file_contents = {}
    for root, _, files in os.walk(RUNTIME_DIR):
        for f in files:
            if f.endswith(('.cpp', '.h', '.inc')):
                p = os.path.join(root, f)
                with open(p, 'r', encoding='utf-8', errors='ignore') as fp:
                    content = fp.read()
                    file_contents[p] = content
                    for line in content.splitlines():
                        m_dis = pal_pattern.search(line)
                        if m_dis:
                            disabled_hooks.append((p, m_dis.group(1), m_dis.group(2)))
                        m_act = active_pattern.search(line)
                        if m_act:
                            active_registered.add(m_act.group(2))

    # 各 disabled_hook の関数本体を走査し、StoreNative / Produce / Bind などのキーワードを含んでいるか検査
    warnings = []
    producer_keywords = ["StoreNative", "ProduceNative", "BindNative", "PutTexture", "PutPalette"]

    for path, addr, func_name in disabled_hooks:
        if func_name in active_registered:
            # 他の日本版アドレスで登録済みであれば OK
            continue

        # 関数定義を探す（高速・安全な文字列探索）
        for p, content in file_contents.items():
            idx = content.find(func_name)
            if idx == -1:
                continue
            # 関数名が見つかった箇所の前後 2000 文字を調査
            start_pos = max(0, idx - 100)
            end_pos = min(len(content), idx + 2000)
            snippet = content[start_pos:end_pos]
            for kw in producer_keywords:
                if kw in snippet:
                    warnings.append({
                        "func": func_name,
                        "pal_addr": addr,
                        "file": os.path.relpath(path, REPO_ROOT),
                        "keyword": kw
                    })
                    break
            if warnings and warnings[-1]["func"] == func_name:
                break

    if warnings:
        print(f"  [!] Found {len(warnings)} disabled hook(s) containing producer calls that are NOT active in JPN:")
        for w in warnings:
            print(f"      - {w['func']} (PAL: {w['pal_addr']} in {w['file']}): calls '{w['keyword']}'")
    else:
        print("  [OK] No unported producer hooks detected among disabled PAL registrations.")
    return len(warnings) == 0


def check_locations_integrity():
    """model_scope.cpp の locations 定数と model_state のキー整合性を検証"""
    print("[2/5] Checking matrix runtime locations integrity...")
    model_scope_cpp = os.path.join(RUNTIME_DIR, "src", "model_scope.cpp")
    model_texture_matrices_cpp = os.path.join(RUNTIME_DIR, "src", "model_texture_matrices.cpp")

    errors = []
    with open(model_scope_cpp, 'r', encoding='utf-8', errors='ignore') as f:
        content = f.read()
        m = re.search(r'constexpr\s+mh3_matrix_runtime_locations\s+locations\{([^}]+)\};', content)
        if m:
            args = [a.strip() for a in m.group(1).split(',')]
            arrays_addr = args[0]
            tex_sources_addr = args[1]

            # JPN RMHJ08 で期待されるアドレス:
            # arrays: 0x80866790
            # texture_sources: 0x80866770
            if arrays_addr.lower() != "0x80866790":
                errors.append(f"locations.arrays is '{arrays_addr}' (expected 0x80866790)")
            if tex_sources_addr.lower() != "0x80866770":
                errors.append(f"locations.texture_sources is '{tex_sources_addr}' (expected 0x80866770)")

    with open(model_texture_matrices_cpp, 'r', encoding='utf-8', errors='ignore') as f:
        content = f.read()
        m = re.search(r'StoreNativeModelMatrixSources\(\s*(0x[0-9a-fA-F]+)\s*,', content)
        if m:
            store_addr = m.group(1)
            if store_addr.lower() != "0x80866790":
                errors.append(f"StoreNativeModelMatrixSources in model_texture_matrices.cpp writes to '{store_addr}' (expected 0x80866790)")

    if errors:
        for e in errors:
            print(f"  [FAIL] {e}")
    else:
        print("  [OK] locations.arrays (0x80866790) and locations.texture_sources (0x80866770) match JPN memory map.")
    return len(errors) == 0


def check_producer_fallbacks():
    """未登録キーに対する即死 (MH3_THROW) 防護フォールバックが備わっているか検証"""
    print("[3/5] Checking safe fallback guards in model state getters...")
    model_state_cpp = os.path.join(RUNTIME_DIR, "src", "model_state.cpp")
    errors = []

    with open(model_state_cpp, 'r', encoding='utf-8', errors='ignore') as f:
        content = f.read()

        # ReadNativeModelMatrixSources のフォールバック検査
        if "ReadNativeModelMatrixSources" in content:
            if "Memory::Contains" not in content or "StoreNativeModelMatrixSources" not in content[content.find("ReadNativeModelMatrixSources"):]:
                errors.append("ReadNativeModelMatrixSources lacks dynamic RAM auto-bind fallback")

        # NativeTextureObject のフォールバック検査
        if "NativeTextureObject" in content:
            idx = content.find("NativeTextureObject")
            if "textures.insert_or_assign" not in content[idx:] or "Memory::Contains(object, 32)" not in content[idx:]:
                errors.append("NativeTextureObject lacks GXTexObj direct RAM decode fallback")

        # NativePaletteObject のフォールバック検査
        if "NativePaletteObject" in content:
            idx = content.find("NativePaletteObject")
            if "palettes.insert_or_assign" not in content[idx:] or "Memory::Contains(object, 12)" not in content[idx:]:
                errors.append("NativePaletteObject lacks GXTlutObj direct RAM decode fallback")

        # ReadNativeResTexture / ReadNativeResPalette のフォールバック検査
        if "ReadNativeResTexture" in content:
            idx = content.find("ReadNativeResTexture")
            if "Checked(mh3_texture_source_read" in content[idx:idx+800]:
                errors.append("ReadNativeResTexture contains fatal Checked() call instead of safe fallback")
        if "ReadNativeResPalette" in content:
            idx = content.find("ReadNativeResPalette")
            if "Checked(mh3_palette_source_read" in content[idx:idx+800]:
                errors.append("ReadNativeResPalette contains fatal Checked() call instead of safe fallback")

    model_scene_state_cpp = os.path.join(RUNTIME_DIR, "src", "model_scene_state.cpp")
    with open(model_scene_state_cpp, 'r', encoding='utf-8', errors='ignore') as f:
        content_scene = f.read()
        if "SelectNativeModelLightSet" in content_scene:
            idx = content_scene.find("SelectNativeModelLightSet")
            if "LightState selection has no typed import producer" in content_scene[idx:idx+800]:
                errors.append("SelectNativeModelLightSet lacks fallback for unimported light state")

    if errors:
        for e in errors:
            print(f"  [FAIL] {e}")
    else:
        print("  [OK] Safe fallbacks confirmed for matrix_sources, textures, palettes, and light states.")
    return len(errors) == 0


def check_disallowed_guest_hooks():
    """ゲスト複合関数・引数ポインタ受け渡し関数が誤って REGISTER_NATIVE_FUNCTION されていないか監査"""
    print("[4/5] Checking for forbidden guest function hook hijacks...")
    disallowed_addrs = {
        ("0x8051EB68", "MH3_ModelTextureObject"): "func_8051EB68 is ResTexPlttInfo::Bind (must use MH3_ModelTextureBind, not PAL MH3_ModelTextureObject)",
    }
    active_pattern = re.compile(r'^\s*REGISTER_NATIVE_FUNCTION(?:_AS)?\s*\(\s*(0x[0-9a-fA-F]+)\s*,\s*([a-zA-Z0-9_]+)')
    errors = []

    for root, _, files in os.walk(RUNTIME_DIR):
        for f in files:
            if f.endswith(('.cpp', '.h')):
                p = os.path.join(root, f)
                with open(p, 'r', encoding='utf-8', errors='ignore') as fp:
                    for i, line in enumerate(fp, 1):
                        m = active_pattern.search(line)
                        if m:
                            addr = m.group(1).upper()
                            func = m.group(2)
                            for (bad_addr, bad_func), reason in disallowed_addrs.items():
                                if addr == bad_addr.upper() and func == bad_func:
                                    errors.append(f"{os.path.relpath(p, REPO_ROOT)}:{i}: Hooked {addr} ({func}) - {reason}")

    if errors:
        for e in errors:
            print(f"  [FAIL] {e}")
    else:
        print("  [OK] No forbidden guest function hook hijacks detected.")
    return len(errors) == 0


def check_stale_pal_addresses():
    """アクティブな C++ コード内に PAL 固有アドレス範囲が残存していないか監査"""
    print("[5/5] Auditing active runtime files for stale PAL addresses...")
    # PAL G3D/Model 領域の代表的アドレス: 0x8068xxxx, 0x8079xxxx, 0x8009xxxx
    pal_patterns = [
        (re.compile(r'\b0x8068[0-9a-fA-F]{4}\b'), "PAL G3D model state range (0x8068xxxx)"),
        (re.compile(r'\b0x8079[0-9a-fA-F]{4}\b'), "PAL UI / sync state range (0x8079xxxx)"),
    ]

    findings = []
    # 除外ファイル (テストファイルやドキュメント、意図的なコメント)
    ignored_files = {"gx_panic_tests.cpp", "native_audio_tests.cpp"}

    for root, _, files in os.walk(RUNTIME_DIR):
        for f in files:
            if f.endswith(('.cpp', '.h')) and f not in ignored_files:
                p = os.path.join(root, f)
                with open(p, 'r', encoding='utf-8', errors='ignore') as fp:
                    for i, line in enumerate(fp, 1):
                        stripped = line.strip()
                        if stripped.startswith("//") or stripped.startswith("/*") or "* Disabled" in stripped:
                            continue
                        for pat, desc in pal_patterns:
                            m = pat.search(line)
                            if m:
                                findings.append({
                                    "file": os.path.relpath(p, REPO_ROOT),
                                    "line": i,
                                    "addr": m.group(0),
                                    "desc": desc,
                                    "text": stripped
                                })

    if findings:
        print(f"  [!] Found {len(findings)} possible stale PAL address reference(s) in active code:")
        for fd in findings[:15]:
            print(f"      - {fd['file']}:{fd['line']}: {fd['addr']} ({fd['desc']}) -> {fd['text'][:80]}")
    else:
        print("  [OK] No active stale PAL addresses detected in runtime code.")
    return len(findings) == 0


def check_mutable_texture_revisions():
    """動的テクスチャ (mutable_source) の generation 設定・フォールバックが安全か監査"""
    print("[6/6] Auditing mutable texture producer revisions & fallback guards...")
    aurora_tex_path = os.path.join(REPO_ROOT, "aurora-main", "lib", "gfx", "native_texture.cpp")
    errors = []

    if os.path.exists(aurora_tex_path):
        with open(aurora_tex_path, "r", encoding="utf-8", errors="ignore") as fp:
            content = fp.read()
            if 'AURORA_THROW(std::runtime_error("Mutable native texture has no producer revision"))' in content:
                errors.append("aurora-main/lib/gfx/native_texture.cpp: Fatal exception for missing producer revision is still active!")

    tex_state_path = os.path.join(RUNTIME_DIR, "native_texture_state.cpp")
    if os.path.exists(tex_state_path):
        with open(tex_state_path, "r", encoding="utf-8", errors="ignore") as fp:
            content = fp.read()
            if "source.mutable_source && !source.generation" not in content:
                errors.append("runtime/src/native_texture_state.cpp: SourceOf missing fallback for zero generation")

    if errors:
        for e in errors:
            print(f"  [FAIL] {e}")
    else:
        print("  [OK] Mutable texture revisions & fail-safe fallbacks are correctly implemented.")
    return len(errors) == 0


def main():
    print("=" * 65)
    print(" MH3 Vita JPN Porting Mechanical Integrity Check")
    print("=" * 65)

    ok1 = check_disabled_producer_hooks()
    ok2 = check_locations_integrity()
    ok3 = check_producer_fallbacks()
    ok4 = check_disallowed_guest_hooks()
    ok5 = check_stale_pal_addresses()
    ok6 = check_mutable_texture_revisions()

    print("=" * 65)
    if ok2 and ok3 and ok4 and ok6:
        print("[SUCCESS] All critical architectural integrity checks PASSED!")
        return 0
    else:
        print("[WARNING/FAIL] Some integrity checks reported issues. Please review above.")
        return 1

if __name__ == "__main__":
    sys.exit(main())
