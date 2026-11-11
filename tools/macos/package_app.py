#!/usr/bin/env python3
"""Create a local macOS app with its runtime libraries, excluding game assets.

Uses the installed build's actual Mach-O dependency graph. The resulting
app still requires explicit --game/--state-dir paths outside the dev checkout;
this is local packaging, not an installer or a release publisher.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]


def run(*args):
    return subprocess.check_output([str(a) for a in args], text=True).strip()


def system(name):
    return name.startswith(('/System/Library/', '/usr/lib/'))


def dependencies(path):
    return [line.strip().split(' (compatibility version')[0]
            for line in run('otool', '-L', path).splitlines()[1:]]


def rpaths(path):
    return re.findall(r'cmd LC_RPATH\n\s+cmdsize \d+\n\s+path (.*?) \(offset',
                      run('otool', '-l', path))


def expanded(name, loader, executable):
    return Path(name.replace('@loader_path', str(loader.parent))
                .replace('@executable_path', str(executable.parent)))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build', type=Path, default=ROOT / 'build/native')
    parser.add_argument('--output', type=Path, default=ROOT / 'work/macos-package/POD.app')
    args = parser.parse_args()
    if sys.platform != 'darwin':
        parser.error('this packaging step requires macOS Mach-O tools')
    build, output = args.build.resolve(), args.output.absolute()
    executable = build / 'pod'
    if not executable.is_file():
        parser.error(f'build the standalone first: {executable}')
    if output.suffix != '.app' or output.exists() or output.is_symlink():
        parser.error('--output must name a new .app; existing bundles are never replaced')
    executable = executable.resolve()
    architectures = run('lipo', '-archs', executable).split()
    main_rpaths = [expanded(p, executable, executable) for p in rpaths(executable)]
    nodes, names, edges = {}, {}, {}

    def visit(source, name=None, inherited=()):
        source = source.resolve()
        if source in nodes:
            return nodes[source]
        name = name or source.name
        if name in names and names[name] != source:
            raise RuntimeError(f'library name collision: {name}')
        for arch in architectures:
            run('lipo', source, '-verify_arch', arch)
        nodes[source], names[name] = name, source
        paths = [expanded(p, source, executable) for p in rpaths(source)] + list(inherited) + main_rpaths
        links = []
        identifiers = run('otool', '-D', source).splitlines()[1:] if source != executable else []
        for dependency in dependencies(source):
            if dependency in identifiers or system(dependency):
                continue
            if dependency.startswith('@rpath/'):
                relative = dependency[len('@rpath/'):]
                target = next((p / relative for p in paths if (p / relative).is_file()), None)
            else:
                target = expanded(dependency, source, executable)
            if target is None or not target.is_file():
                raise RuntimeError(f'unresolved dependency {dependency} in {source}')
            links.append((dependency, visit(target, inherited=paths)))
        edges[source] = links
        return name

    visit(executable)
    # sdl2-compat opens SDL3 dynamically, so otool cannot discover this edge.
    compat = [p for p in nodes if p.name.startswith('libSDL2-') and
              b'@loader_path/libSDL3.dylib' in p.read_bytes()]
    if compat:
        sdl3 = Path(run('pkg-config', '--variable=libdir', 'sdl3')) / 'libSDL3.dylib'
        if not sdl3.is_file():
            raise RuntimeError(f'sdl2-compat requires a local SDL3 library: {sdl3}')
        visit(sdl3, 'libSDL3.dylib')

    output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='pod-package-', dir=output.parent) as temporary:
        app = Path(temporary) / output.name
        macos, frameworks, resources = [app / 'Contents' / p for p in ('MacOS', 'Frameworks', 'Resources')]
        for directory in (macos, frameworks, resources):
            directory.mkdir(parents=True, exist_ok=True)
        shutil.copy2(ROOT / 'src/portable/Info.plist', app / 'Contents/Info.plist')
        manifest = []
        for source, name in nodes.items():
            destination = macos / 'pod' if source == executable else frameworks / name
            shutil.copy2(source, destination)
            destination.chmod(destination.stat().st_mode | 0o200)
            for old, new in edges[source]:
                run('install_name_tool', '-change', old, '@rpath/' + new, destination)
            for old in rpaths(destination):
                run('install_name_tool', '-delete_rpath', old, destination)
            run('install_name_tool', '-add_rpath',
                '@executable_path/../Frameworks' if source == executable else '@loader_path', destination)
            if source != executable:
                run('install_name_tool', '-id', '@rpath/' + name, destination)
            for dependency in dependencies(destination):
                if not system(dependency) and not dependency.startswith('@rpath/'):
                    raise RuntimeError(f'non-relocatable load path: {dependency}')
            manifest.append(dict(file=str(destination.relative_to(app)),
                                 source_sha256=hashlib.sha256(source.read_bytes()).hexdigest()))
        (resources / 'runtime-manifest.json').write_text(json.dumps(dict(
            architectures=architectures, dynamic_sdl3=bool(compat), files=manifest), indent=2) + '\n')
        for library in frameworks.iterdir():
            run('codesign', '--force', '--sign', '-', library)
        run('codesign', '--force', '--sign', '-', app)
        run('codesign', '--verify', '--deep', '--strict', app)
        app.rename(output)
    print(f'Created {output}: {len(nodes)-1} bundled libraries; no game assets copied')


if __name__ == '__main__':
    main()
