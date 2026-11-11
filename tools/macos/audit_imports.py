#!/usr/bin/env python3
"""Static import-name census, not proof of API behavior or runtime binding.

Requires pefile. Keep output under work/: it is derived from the user's disc.
"""
import argparse
import hashlib
import json
import re
from pathlib import Path

import pefile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--toolkit", type=Path, default=Path("work/pcrecomp"))
    parser.add_argument("--host-source", type=Path, default=Path("src/portable"))
    parser.add_argument("--game", type=Path, default=Path("game"))
    parser.add_argument("--out", type=Path,
                        default=Path("work/macos-assessment/imports.json"))
    args = parser.parse_args()
    sources = args.toolkit / "runtime/win32hle"
    if not sources.is_dir():
        parser.error(f"missing HLE sources: {sources}")
    def scan(directory):
        names = {}
        for source in directory.glob("*.c"):
            if source.name == "selftest.c" or source.name.endswith("_selftest.c"):
                continue
            text = source.read_text()
            found = set(re.findall(r'\{\s*"([^"\n]+)"\s*,', text))
            found.update(re.findall(r'\.name\s*=\s*"([^"\n]+)"', text))
            for name in found:
                names.setdefault(name, []).append(str(source))
        return names
    registered, host = scan(sources), scan(args.host_source)
    result = {"method": "static name/alias scan; candidates may be stubs or unrelated tables; no runtime guarantee",
              "runtime_sources": str(sources), "host_sources": str(args.host_source),
              "modules": {}}
    for name in ("PODMMX.EXE", "INSTALL/IR41_32.DLL"):
        path = args.game / name
        pe = pefile.PE(str(path))
        imports = []
        for dll in pe.DIRECTORY_ENTRY_IMPORT:
            for entry in dll.imports:
                symbol = (entry.name.decode() if entry.name else
                          dll.dll.decode().lower() + "#" + str(entry.ordinal))
                imports.append({"dll": dll.dll.decode(), "name": symbol,
                                "name_found_in_hle": symbol in registered,
                                "name_found_in_host": symbol in host,
                                "candidate_sources": registered.get(symbol, []) + host.get(symbol, [])})
        absent = [entry["name"] for entry in imports if not entry["name_found_in_hle"] and not entry["name_found_in_host"]]
        result["modules"][name] = {
            "size": path.stat().st_size,
            "sha256": hashlib.sha256(path.read_bytes()).hexdigest(),
            "timestamp": hex(pe.FILE_HEADER.TimeDateStamp),
            "imports": imports,
            "names_absent_from_scanned_sources": absent,
            "sections": [{"name": s.Name.rstrip(b"\0").decode(),
                          "rva": hex(s.VirtualAddress),
                          "virtual_size": s.Misc_VirtualSize,
                          "raw_size": s.SizeOfRawData,
                          "raw_offset": s.PointerToRawData} for s in pe.sections],
        }
        print(f"{name}: {len(imports)} imports; {len(absent)} names absent from scanned runtime/host")
        print(", ".join(absent))
        pe.close()
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps(result, indent=2) + "\n")


if __name__ == "__main__":
    main()
