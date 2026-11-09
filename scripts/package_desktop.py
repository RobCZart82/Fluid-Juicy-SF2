#!/usr/bin/env python3
"""Package verified desktop builds and their pinned corresponding sources."""
import argparse
import copy
import hashlib
import json
from pathlib import Path
import platform
import shutil
import struct
import subprocess
import tarfile
import tempfile

from build import ROOT, PINS, run


def verify_pe(path):
    with path.open('rb') as stream:
        if stream.read(2) != b'MZ':
            raise RuntimeError(f'Not a PE executable: {path}')
        stream.seek(0x3c)
        offset = struct.unpack('<I', stream.read(4))[0]
        stream.seek(offset)
        signature, machine = struct.unpack('<4sH', stream.read(6))
        stream.seek(offset + 24)
        magic = struct.unpack('<H', stream.read(2))[0]
    if signature != b'PE\0\0' or machine != 0x8664 or magic != 0x20b:
        raise RuntimeError(f'Expected Windows x64 PE32+ binary: {path}')
    return {'architecture': 'x86_64', 'format': 'PE32+'}


def archive_sources(target, sources):
    repositories = [('fluid-juicy-sf2', ROOT)]
    for name, (_, expected) in PINS.items():
        repository = sources / name
        actual = subprocess.check_output(['git', '-C', str(repository), 'rev-parse', 'HEAD'], text=True).strip()
        if actual != expected:
            raise RuntimeError(f'Wrong source revision for {name}: {actual}')
        repositories.append((f'dependencies/{name}', repository))
    for name in ['gcem', 'signalsmith-audio-basics']:
        parent = sources / 'fluidsynth'
        expected = subprocess.check_output(['git', '-C', str(parent), 'rev-parse', f'HEAD:{name}'], text=True).strip()
        repository = parent / name
        actual = subprocess.check_output(['git', '-C', str(repository), 'rev-parse', 'HEAD'], text=True).strip()
        if actual != expected:
            raise RuntimeError(f'Wrong submodule revision for {name}')
        repositories.append((f'dependencies/fluidsynth/{name}', repository))
    for _, repository in repositories:
        if subprocess.check_output(['git', '-C', str(repository), 'status', '--porcelain', '--untracked-files=no'], text=True).strip():
            raise RuntimeError(f'Commit source changes before packaging: {repository}')
    with tarfile.open(target, 'w:gz') as destination:
        for prefix, repository in repositories:
            process = subprocess.Popen(['git', '-C', str(repository), 'archive', 'HEAD'], stdout=subprocess.PIPE)
            try:
                with tarfile.open(fileobj=process.stdout, mode='r|') as archive:
                    for entry in archive:
                        member = copy.copy(entry)
                        member.name = prefix + '/' + entry.name
                        destination.addfile(member, archive.extractfile(entry) if entry.isfile() else None)
            finally:
                process.stdout.close()
                code = process.wait()
            if code:
                raise RuntimeError(f'Source archive failed: {repository}')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--arch', choices=['arm64', 'x86_64'], default='arm64')
    parser.add_argument('--build-dir', type=Path, default=ROOT / 'build')
    parser.add_argument('--source-dir', type=Path)
    args = parser.parse_args()
    system = platform.system()
    if system not in ('Darwin', 'Windows') or (system == 'Windows' and args.arch != 'x86_64'):
        parser.error('Supported packages: macOS arm64/x86_64 and Windows x86_64')
    build = args.build_dir.resolve() / args.arch
    sources = (args.source_dir or args.build_dir.resolve() / 'sources').resolve()
    record = json.loads((build / 'build-record.json').read_text())
    if record['arch'] != args.arch or record['pins'] != {k: list(v) for k, v in PINS.items()}:
        raise RuntimeError('Build record does not match the requested architecture/source pins')
    distribution = build / 'distribution'
    distribution.mkdir(parents=True, exist_ok=True)
    artifacts = build / 'plugin' / 'JuicySFPlugin_artefacts' / 'Release'
    os_name = 'macos' if system == 'Darwin' else 'windows'
    package_name = f'fluid-juicy-sf2-{os_name}-{args.arch}-development'
    with tempfile.TemporaryDirectory(prefix='package-', dir=build) as temporary:
        stage = Path(temporary) / 'Fluid-Juicy-SF2'
        stage.mkdir()
        evidence = {}
        for suffix, directory in [('.vst3', 'VST3'), ('.app' if system == 'Darwin' else '.exe', 'Standalone')]:
            bundle = artifacts / directory / ('Fluid_Juicy_SF2' + suffix)
            target = stage / bundle.name
            if system == 'Darwin':
                binary = bundle / 'Contents/MacOS/Fluid_Juicy_SF2'
                arch = subprocess.check_output(['lipo', '-archs', str(binary)], text=True).strip()
                if arch != args.arch:
                    raise RuntimeError(f'Unexpected architecture: {binary}: {arch}')
                dependencies = subprocess.check_output(['otool', '-L', str(binary)], text=True)
                for line in dependencies.splitlines()[1:]:
                    dependency = line.strip().split(' (', 1)[0]
                    if not dependency.startswith(('/usr/lib/', '/System/Library/')):
                        raise RuntimeError(f'Non-system dependency: {dependency}')
                shutil.copytree(bundle, target, symlinks=True)
                run('codesign', '--force', '--sign', '-', '--timestamp=none', target)
                run('codesign', '--verify', '--strict', target)
                evidence[bundle.name] = {'architecture': arch, 'dependencies': dependencies}
            else:
                binary = bundle / 'Contents/x86_64-win/Fluid_Juicy_SF2.vst3' if suffix == '.vst3' else bundle
                evidence[bundle.name] = verify_pe(binary)
                if bundle.is_dir():
                    shutil.copytree(bundle, target)
                else:
                    shutil.copy2(bundle, target)
        shutil.copy2(build / 'build-record.json', stage)
        shutil.copy2(ROOT / 'LICENSE.txt', stage)
        (stage / 'binary-verification.json').write_text(json.dumps(evidence, indent=2), encoding='utf-8')
        (stage / 'README.txt').write_text(
            f'Fluid Juicy SF2 by RobCZart82 - development preview ({os_name}, {args.arch}).\n'
            'VST3: copy the complete Fluid_Juicy_SF2.vst3 bundle to your host plugin folder.\n'
            'macOS: ad-hoc signed, not Developer ID signed or notarized. Windows: unsigned.\n'
            'Windows may require the Microsoft Visual C++ 2015-2022 x64 runtime.\n'
            'DAW, minimum-OS and clean-machine acceptance remain separate from CI.\n'
            'No SoundFonts included. Keep corresponding-source.tar.gz with this package;\n'
            'it contains complete pinned build sources and dependency license notices.\n', encoding='utf-8')
        archive_sources(distribution / 'corresponding-source.tar.gz', sources)
        zip_path = distribution / (package_name + '.zip')
        if system == 'Darwin':
            run('ditto', '-c', '-k', '--sequesterRsrc', '--keepParent', stage, zip_path)
        else:
            shutil.make_archive(str(zip_path.with_suffix('')), 'zip', stage.parent, stage.name)
        files = [zip_path, distribution / 'corresponding-source.tar.gz']
        (distribution / 'SHA256SUMS.txt').write_text(''.join(
            hashlib.sha256(p.read_bytes()).hexdigest() + '  ' + p.name + '\n' for p in files), encoding='utf-8')
    print(f'Development package ready: {zip_path}')


if __name__ == '__main__':
    main()
