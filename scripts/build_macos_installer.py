#!/usr/bin/env python3
"""Build an unsigned preview PKG from a verified development package directory."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import plistlib
import shutil
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('package', type=Path, help='Extracted Fluid-Juicy-SF2 directory')
    parser.add_argument('--version', required=True, help='Numeric package version, e.g. 1.0.0')
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    if not re.fullmatch(r'[0-9]+\.[0-9]+\.[0-9]+', args.version):
        parser.error('version must have three numeric components')
    source = args.package.resolve()
    record = json.loads((source / 'binary-verification.json').read_text())
    products = [('Fluid_Juicy_SF2.app', 'Applications'),
                ('Fluid_Juicy_SF2.vst3', 'Library/Audio/Plug-Ins/VST3')]
    arches = set()
    for name, _ in products:
        bundle = source / name
        if not bundle.is_dir() or bundle.is_symlink():
            raise RuntimeError(f'Missing real bundle directory: {bundle}')
        binary = bundle / 'Contents/MacOS/Fluid_Juicy_SF2'
        arch = subprocess.check_output(['lipo', '-archs', str(binary)], text=True).strip()
        if arch not in ('arm64', 'x86_64') or record[name]['architecture'] != arch:
            raise RuntimeError(f'Architecture verification failed: {bundle}')
        arches.add(arch)
        subprocess.run(['codesign', '--verify', '--strict', str(bundle)], check=True)
    if len(arches) != 1:
        raise RuntimeError('Both products must use the same architecture')
    output = args.output.resolve()
    if output.exists():
        raise RuntimeError(f'Refusing to overwrite existing output: {output}')
    output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='juicy-pkg-') as temporary:
        root = Path(temporary) / 'root'
        for name, destination in products:
            target = root / destination / name
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copytree(source / name, target, symlinks=True)
        notices = root / 'Library/Application Support/Fluid Juicy SF2'
        notices.mkdir(parents=True)
        for name in ('LICENSE.txt', 'build-record.json', 'binary-verification.json'):
            shutil.copy2(source / name, notices / name)
        for guide in source.glob('Fluid-Juicy-SF2-Installation-*.pdf'):
            shutil.copy2(guide, notices / guide.name)
        components = Path(temporary) / 'components.plist'
        subprocess.run(['pkgbuild', '--analyze', '--root', str(root), str(components)], check=True)
        with components.open('rb') as stream:
            entries = plistlib.load(stream)
        for entry in entries:
            entry['BundleIsRelocatable'] = False
        with components.open('wb') as stream:
            plistlib.dump(entries, stream)
        subprocess.run(['pkgbuild', '--root', str(root), '--component-plist', str(components), '--install-location', '/',
                        '--identifier', 'com.robczart82.fluidjuicysf2.preview',
                        '--version', args.version, '--ownership', 'recommended', str(output)], check=True)
    digest = hashlib.sha256(output.read_bytes()).hexdigest()
    output.with_suffix('.pkg.sha256').write_text(f'{digest}  {output.name}\n')
    print(f'Unsigned preview installer: {output}')
    print('Not notarized or release-approved. Distribute the corresponding source archive alongside it.')


if __name__ == '__main__':
    main()
