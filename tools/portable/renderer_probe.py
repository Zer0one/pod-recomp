#!/usr/bin/env python3
"""Audit/lift alternate POD builds into ignored work/renderers/<variant>.

This does not supply a GPU renderer. The supported MMX lift and its catalogs
are never replaced. Binary-derived reports and generated C stay under work/.
"""
import argparse
import hashlib
import json
from pathlib import Path
import sys
import uuid

ROOT = Path(__file__).resolve().parents[2]
BUILDS = {
    'glide': ('PODX3DFX.EXE', 'c3c2243d919f4ca1508031699f4c47914c573d360aa99de133954458b339c08f'),
    'd3d': ('PODD3DX.EXE', '51750191616b6c26fa6816160db997368a43b3fc7f4185fbb30195dac030498b'),
}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('variant', choices=BUILDS)
    parser.add_argument('--stage', choices=('audit', 'catalog', 'lift'), default='audit')
    parser.add_argument('--seed', action='append', type=lambda value: int(value, 0), default=[],
                        help='observed indirect target (repeatable; saved per variant)')
    args = parser.parse_args()
    image_name, expected = BUILDS[args.variant]
    image = ROOT / 'game' / image_name
    if not image.is_file():
        parser.error(f'provide your own disc executable at {image}')
    digest = hashlib.sha256(image.read_bytes()).hexdigest()
    if digest != expected:
        parser.error(f'unsupported {image_name}: SHA256 {digest}; expected {expected}')
    import pefile
    pe = pefile.PE(str(image))
    destination = ROOT / 'work' / 'renderers' / args.variant
    destination.mkdir(parents=True, exist_ok=True)
    # Public COM interface identifiers, not a disassembly or proof that the
    # interface is reached. Reference definitions: Wine include/d3d.h.
    interface_ids = {
        'IDirect3D': '3bba0080-2421-11cf-a31a-00aa00b93356',
        'IDirect3D2': '6aae1ec1-662a-11d0-889d-00aa00bbb76a',
        'IDirect3D3': 'bb223240-e72b-11d0-a9b4-00aa00c0993e',
    }
    raw = image.read_bytes()
    report = dict(variant=args.variant, image=image_name, sha256=digest,
                  timestamp=pe.FILE_HEADER.TimeDateStamp,
                  entry=pe.OPTIONAL_HEADER.ImageBase + pe.OPTIONAL_HEADER.AddressOfEntryPoint,
                  imports={entry.dll.decode(): [i.name.decode() if i.name else '#'+str(i.ordinal)
                                               for i in entry.imports]
                           for entry in pe.DIRECTORY_ENTRY_IMPORT},
                  interface_guid_bytes_present=[name for name, value in interface_ids.items()
                                                if uuid.UUID(value).bytes_le in raw],
                  scope='static audit; GUID byte presence is not an observed API call; no GPU implementation or runtime compatibility established')
    pe.close()
    (destination / 'image.json').write_text(json.dumps(report, indent=2)+'\n')
    print(f'{args.variant}: {image_name}; {sum(map(len, report["imports"].values()))} imports', flush=True)
    if args.stage == 'audit':
        return
    sys.path.insert(0, str(ROOT / 'tools'))
    import catalog
    catalog_path = destination / 'functions.json'
    seeds_path = destination / 'seeds.json'
    seeds = set()
    if seeds_path.exists():
        seeds = {e['address'] if isinstance(e, dict) else int(e)
                 for e in json.loads(seeds_path.read_text())}
    for address in args.seed:
        section = pefile.PE(str(image))
        rva = address - section.OPTIONAL_HEADER.ImageBase
        owner = section.get_section_by_rva(rva)
        valid = owner is not None and owner.Characteristics & 0x20000000
        section.close()
        if not valid:
            parser.error(f'seed {address:#x} is outside executable sections')
        seeds.add(address)
    if args.seed:
        seeds_path.write_text(json.dumps(sorted(seeds), indent=2)+'\n')
    catalog.catalog(str(image), str(catalog_path), seeds)
    if args.stage == 'catalog':
        return
    codec = ROOT / 'game' / 'INSTALL' / 'IR41_32.DLL'
    codec_catalog = ROOT / 'work' / 'indeo' / 'functions.json'
    if not codec_catalog.is_file():
        parser.error('run tools/catalog.py for the supported codec catalog first')
    sys.path.insert(0, str(ROOT))
    import run_lift
    if run_lift.exe_stamp(str(codec)) != 0x326D51E5:
        parser.error('the codec is not the supported Indeo 4.1 disc build')
    from capstone import Cs, CS_ARCH_X86, CS_MODE_32
    md = Cs(CS_ARCH_X86, CS_MODE_32)
    md.detail = True
    generated = destination / 'gen'
    temporary = destination / 'gen.new'
    if temporary.exists():
        parser.error(f'leftover staging tree {temporary}; inspect/remove it before retrying')
    temporary.mkdir()
    output = run_lift.Out(str(temporary), 200)
    entries, statistics = [], {}
    for binary, functions in ((image, catalog_path), (codec, codec_catalog)):
        entry, stats = run_lift.lift(str(binary), str(functions), md, output, patches={})
        entries.append(entry)
        for key, value in stats.items():
            statistics[key] = statistics.get(key, 0) + value
    output.flush()
    names = ''.join(f'void {name}(void);\n' for _, name in output.entries)
    (temporary / 'recomp_funcs.h').write_text(
        '/* Auto-generated alternate POD build; never redistribute. */\n#pragma once\n#include <stdint.h>\n'+names)
    table = ''.join(f'    {{ 0x{address:08X}u, {name} }},\n'
                    for address, name in sorted(output.entries))
    (temporary / 'recomp_dispatch.c').write_text(
        '#include "recomp_types.h"\n#include "recomp_funcs.h"\n'
        'const recomp_dispatch_entry_t recomp_dispatch_table[] = {\n'+table+'};\n'
        f'const uint32_t recomp_dispatch_count = {len(output.entries)};\n'
        f'const uint32_t pod_entry_va = 0x{entries[0]:08X}u;\n'
        f'const uint32_t pod_ir41_entry_va = 0x{entries[1]:08X}u;\n')
    statistics['changed'] = run_lift.sync(str(temporary), str(generated))
    statistics['generated_files'] = output.files
    (destination / 'lift_stats.json').write_text(json.dumps(statistics, indent=2)+'\n')
    print(json.dumps(statistics), flush=True)
    if statistics.get('errors'):
        raise SystemExit('lift errors: this output is not ready to build')


if __name__ == '__main__':
    main()
