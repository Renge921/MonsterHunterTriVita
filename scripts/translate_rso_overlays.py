#!/usr/bin/env python3
"""Compile verified RSO overlays separately from address-overlapping boot code."""
import argparse
import hashlib
import json
import os
import re
import struct
import subprocess
from pathlib import Path
from link_rso import ROOT, Rso, static_exports, u32
from prepare_rso_translation import discover
from tick_sites_filter import write_filtered

PROFILES = {
    'map_village': ('map_village', 0x80B18920,
                    '7ae9f922a0cc72940973c4ad20017c44fdd281915738a83ba272b3321bde6563', 'MapVillageBinding'),
    'quest_field': ('quest_data', 0x809E3900,
                    '12c38014952a4f209bab856e7e060991e1199e0957166d4152e547ea23f7a8c5', 'QuestFieldBinding'),
    'enemy_field': ('em_data', 0x80A80220,
                    '1f586a5780811955f31d133aa25d92fffddea8261667f9eb0a0ded4a39c0745c', 'EnemyFieldBinding'),
    'map01_field': ('map01', 0x80BC0000,
                    'a4286ca086fb39239a34841100db2c16815d4e7ce9bd8590eb7c7b05e18753aa', 'Map01FieldBinding'),
    'map00_field': ('map00', 0x80BC0000,
                    'a9e3d84b522b7829dadbe8e4307a3f6ee624857877148aab5db3e3e49ed6023b', 'Map00FieldBinding'),
    'map02_field': ('map02', 0x80BC0000,
                    'ba5f3b2f109779236c387f2a8312c6e47cf092eadfbc5b8bf88193cc922ae1c1', 'Map02FieldBinding'),
    'map03_field': ('map03', 0x80BC0000,
                    '6a3584132f457969ad58d46186abdaaea2ab99cb2f1fb0c122421dddc3d71f3e', 'Map03FieldBinding'),
    'map04_field': ('map04', 0x80BC0000,
                    'b335caba6ff594347080937a8e8c6adc030e6a047c86b7b06ce680dfa18380ce', 'Map04FieldBinding'),
    'map05_field': ('map05', 0x80BC0000,
                    '4e363b786a43a668dbf9906ab70c2c8c15383459f653bff6adc77dffc0e6a92d', 'Map05FieldBinding'),
    'map06_field': ('map06', 0x80BC0000,
                    '1739f76cb5145545aaed4818a2657ecb0964243a9c0e291093619a4b689f1b1d', 'Map06FieldBinding'),
    'map07_field': ('map07', 0x80BC0000,
                    '3b9033e8c232a43f8a5e332dac76468ca62682bbe86b589f9425c1b458814faf', 'Map07FieldBinding'),
    'map08_field': ('map08', 0x80BC0000,
                    '1bb7a6506ed6798307fee002fae112e511e576ef0692966f58084358801d3255', 'Map08FieldBinding'),
    'map09_field': ('map09', 0x80BC0000,
                    '769f75375fe2641c33da7d02483a88aff19971f5dda373c6aa08aeafb9f3a29b', 'Map09FieldBinding'),
    'map10_field': ('map10', 0x80BC0000,
                    'c3d7fe06c91d8e5ca62ca5d5a6e5fa0e2fe2e57e2b371717780f98ac2169dab5', 'Map10FieldBinding'),
    'map11_field': ('map11', 0x80BC0000,
                    '44c22268a4aa3d32eecdc2f1ff6efc7c6039a2d427ab23de953de5b6c1af8130', 'Map11FieldBinding'),
    # The online menus (connection, terms, CAPCOM ID), loaded at the demo
    # module's base when the mode menu picks City.
    'net_data': ('net_data', 0x80B18920,
                 'a63eab6d3b9b31217148d47f68823163b68f30f74c03df1f49835c6d2780dc63', 'NetDataBinding'),
    # Loc Lac (the online city's gate and cities), right after net_data.
    'map_town_online': ('map_town', 0x80B36120,
                        'f0a3b9b70add9e2fed175b6826cfdab04afef4c34253cb5c7c916b0d90d37e7c', 'MapTownOnlineBinding'),
    # Loc Lac again after a quest: the city loads before net_data, at its base.
    'map_town_quest': ('map_town', 0x80B18920,
                       'f0a3b9b70add9e2fed175b6826cfdab04afef4c34253cb5c7c916b0d90d37e7c', 'MapTownQuestBinding'),
}
DOL_DIGEST = '994d489161983c438987fb59abe1d9127ed25e4892088a0a61e12e3b47244972'

# Native ports of overlay functions: their dispatch records enter the native
# function instead of the translation. An overlay shares its addresses with
# other modules, so these are not registered by address.
NATIVE_PORTS = {}


def write_changed(path, text):
    if not path.exists() or path.read_text() != text:
        path.write_text(text)


def isolate_profile_source(source, profile):
    source = re.sub(r'\bfunc_([A-Fa-f0-9]{8}\w*)', rf'mh3_{profile}_\1', source)
    source = re.sub(r'InvokeDirectCpu<(0x[0-9A-Fa-f]+u)>\(ctx\)', r'InvokeIndirectCpu(\1, ctx)', source)
    if 'InvokeDirectCpu<' in source:
        raise ValueError('Unrecognized direct-call form in isolated overlay')
    return source


def translate_profile(profile):
    name, base, digest, binding = PROFILES[profile]
    work = ROOT / 'build/rso-overlays' / profile
    work.mkdir(parents=True, exist_ok=True)
    rso = Rso((ROOT / f'Assets/DATA/files/rel/{name}.rso').read_bytes())
    if hashlib.sha256(rso.data).hexdigest() != digest:
        raise ValueError(f'Unverified {name} module')
    image, addresses = rso.link(base, static_exports())
    offset, size = rso.sections[1]
    text = addresses[1]
    prolog = addresses[rso.data[0x20]] + u32(rso.data, 0x24)
    seeds = discover(name, base, rso, image, addresses)
    if profile == 'quest_field':
        # Preserve verified tail-call boundaries from the boot-address profile.
        for line in (ROOT / 'symbols.txt').read_text().splitlines():
            fields = line.split('#', 1)[0].split()
            if len(fields) == 2 and 0x80B57850 <= int(fields[0], 16) < 0x80B87E88:
                seeds[int(fields[0], 16) - 0x80B57740 + base] = fields[1]
    dol = bytearray((ROOT / 'Assets/DATA/sys/main.dol').read_bytes())
    if hashlib.sha256(dol).hexdigest() != DOL_DIGEST:
        raise ValueError('Unverified DOL')
    file_offset = len(dol)
    dol.extend(image)
    for slot, address, length, position in ((2, text, size, file_offset + offset),
                                          (15, base, len(image), file_offset)):
        if u32(dol, 0x90 + slot * 4):
            raise ValueError('Overlay analysis DOL slot occupied')
        for table, value in ((0, position), (0x48, address), (0x90, length)):
            struct.pack_into('>I', dol, table + slot * 4, value)
    (work / 'analysis.dol').write_bytes(dol)
    # Base DOL seeds only: the boot RSOs have overlapping and unrelated code.
    lines = [line for line in (ROOT / 'build/MAP.txt').read_text().splitlines()
             if int(line.split()[0], 16) < 0x80800000]
    lines += [f'{address:08x} {name}' for address, name in sorted(seeds.items())]
    write_changed(work / 'MAP.txt', '\n'.join(lines) + '\n')
    config = (ROOT / 'recomp.yml').read_text().replace('workspace_root: .', 'workspace_root: ../../..')
    config = config.replace('Assets/DATA/sys/main.dol', f'build/rso-overlays/{profile}/analysis.dol')
    config = config.replace('build/MAP.txt', f'build/rso-overlays/{profile}/MAP.txt')
    config = config.replace(DOL_DIGEST, hashlib.sha256(dol).hexdigest())
    config = re.sub(r'0x8000(?:403C|6310)', f'0x{prolog:08X}', config)
    # Only this overlay's tick sites where it shares addresses with a boot module.
    write_filtered(work / 'tick_sites.inc', {profile})
    config = config.replace('runtime/src/tick_sites.inc', f'build/rso-overlays/{profile}/tick_sites.inc')
    write_changed(work / 'recomp.yml', config)
    toolchain = Path(os.environ.get('WIICOMPILED_ROOT', str(ROOT / 'toolchain')))
    translator = toolchain / 'translator/src/Translator.Cli/bin/Release/net8.0/Translator.Cli.dll'
    subprocess.run([os.environ.get('DOTNET_BIN', '/usr/local/bin/dotnet'), str(translator),
                    'translate-recursive', f'0x{prolog:08X}', '--project', str(work / 'recomp.yml'),
                    '--outdir', str(work / 'functions'), '--output-metadata', str(work / 'translation.json'),
                    '--threads', '8', '--max-instructions', '32768',
                    '--max-bytes', '262144', '--prune-stale',
                    # Only the overlay and its typed callees are used below.
                    '--emit-range', f'0x{text:08X}-0x{text + size:08X}'], cwd=ROOT, check=True)
    metadata = json.loads((work / 'translation.json').read_text())
    if any(metadata['quality'][key] for key in ('unsupportedInstructionCount', 'invalidSsaFunctionCount')):
        raise ValueError('Overlay translation quality check failed')
    functions = [entry for entry in metadata['functions'] if text <= entry['entryPoint'] < text + size]
    if not set(seeds).issubset({entry['entryPoint'] for entry in functions}):
        raise ValueError('Unexpected overlay function discovery')
    # Include same-translation typed callees rather than assuming their ABI
    # matches the independently translated boot image. CPU-context calls use
    # runtime dispatch, which selects the active overlay at overlapping addresses.
    entries = {entry['entryPoint']: entry for entry in metadata['functions']}
    selected = {entry['entryPoint'] for entry in functions}
    pending = list(selected)
    while pending:
        address = pending.pop()
        source = (work / 'functions' / entries[address]['relativePath']).read_text()
        for symbol in re.findall(r'\bfunc_([A-Fa-f0-9]{8})\w*', source):
            dependency = int(symbol, 16)
            if dependency not in selected:
                if dependency not in entries:
                    raise ValueError(f'Missing typed dependency {dependency:08X}')
                selected.add(dependency)
                pending.append(dependency)
    sources, records = [], []
    ports = NATIVE_PORTS.get(profile, {})
    for entry in sorted((entries[a] for a in selected), key=lambda item: item['entryPoint']):
        source = (work / 'functions' / entry['relativePath']).read_text()
        if hashlib.sha256(source.encode()).hexdigest() != entry['sha256']:
            raise ValueError('Generated overlay source hash mismatch')
        address = entry['entryPoint']
        sources.append(isolate_profile_source(source, profile))
        mask = entry['build']['nonvolatileFprWriteMask']
        if address in ports:
            # A native port keeps r14-r31 and f14-f31 (REGISTER_NATIVE_FUNCTION): no guard.
            records.append(f'    {{0x{address:08X}u, &{ports[address]}, 0x00000000u}},')
        elif text <= address < text + size:
            records.append(f'    {{0x{address:08X}u, &mh3_{profile}_{address:08X}, 0x{mask:08X}u}},')
    checksum = 0xCBF29CE484222325
    for byte in image[offset:offset + size]:
        checksum = ((checksum ^ byte) * 0x100000001B3) & 0xFFFFFFFFFFFFFFFF
    header = f'''// Generated static overlay; no runtime instruction translation.
#pragma once
#include <cstdint>
struct RawDispatchRecord;
namespace {binding} {{
constexpr uint32_t Base = 0x{base:08X}, Text = 0x{text:08X}, Size = 0x{size:X}, Prolog = 0x{prolog:08X};
constexpr uint64_t Checksum = 0x{checksum:016X}ULL;
constexpr unsigned Count = {len(records)};
extern const RawDispatchRecord Records[Count];
}}
'''
    output = ROOT / 'generated/rso_overlays'
    output.mkdir(parents=True, exist_ok=True)
    write_changed(output / f'{profile}.h', header)
    if set(ports) - {entry['entryPoint'] for entry in functions}:
        raise ValueError('A native port replaces no overlay function')
    sources += [''.join(f'extern "C" void {name}(CpuContext*);\n' for name in ports.values())]
    sources += [f'namespace {binding} {{\nconst RawDispatchRecord Records[]{{\n' +
                '\n'.join(records) + '\n};\n}\n']
    # Include the declaration first so the const table has external linkage.
    write_changed(output / f'{profile}.cpp', f'#include "{profile}.h"\n' + '\n'.join(sources))
    print(f'Emitted {len(records)} isolated {name} functions, linked text {text:08X}+{size:X}')


def is_up_to_date(profile):
    name, base, digest, binding = PROFILES[profile]
    header_path = ROOT / 'generated/rso_overlays' / f'{profile}.h'
    cpp_path = ROOT / 'generated/rso_overlays' / f'{profile}.cpp'
    if not header_path.exists() or not cpp_path.exists():
        return False
    rso = Rso((ROOT / f'Assets/DATA/files/rel/{name}.rso').read_bytes())
    image, addresses = rso.link(base, static_exports())
    offset, size = rso.sections[1]
    checksum = 0xCBF29CE484222325
    for byte in image[offset:offset + size]:
        checksum = ((checksum ^ byte) * 0x100000001B3) & 0xFFFFFFFFFFFFFFFF
    text = header_path.read_text()
    return f'Base = 0x{base:08X}' in text and f'Checksum = 0x{checksum:016X}ULL' in text


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--profile', choices=PROFILES, default='map_village')
    parser.add_argument('--all', action='store_true', help='Translate all overlay profiles')
    parser.add_argument('--force', action='store_true', help='Re-translate even if header matches')
    args = parser.parse_args()
    targets = list(PROFILES) if args.all else [args.profile]
    for profile in targets:
        if args.all and not args.force and is_up_to_date(profile):
            print(f'Skipping up-to-date {profile}')
            continue
        print(f'=== Translating {profile} ===', flush=True)
        translate_profile(profile)


if __name__ == '__main__':
    main()
