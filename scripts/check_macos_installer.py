#!/usr/bin/env python3
"""Build and inspect the native preview installer without installing it."""
import argparse
from pathlib import Path
import plistlib
import subprocess
import sys
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('distribution', type=Path)
    parser.add_argument('--arch', required=True, choices=('arm64', 'x86_64'))
    args = parser.parse_args()
    distribution = args.distribution.resolve()
    archives = list(distribution.glob('*.zip'))
    if len(archives) != 1:
        raise RuntimeError('Expected exactly one verified development ZIP')
    with tempfile.TemporaryDirectory(prefix='juicy-installer-check-') as temporary:
        folder = Path(temporary)
        subprocess.run(['ditto', '-x', '-k', str(archives[0]), str(folder)], check=True)
        source = folder / 'Fluid-Juicy-SF2'
        with (source / 'Fluid_Juicy_SF2.app/Contents/Info.plist').open('rb') as stream:
            version = plistlib.load(stream)['CFBundleShortVersionString']
        output = distribution / f'Fluid-Juicy-SF2-macos-{args.arch}-preview.pkg'
        subprocess.run([sys.executable, str(Path(__file__).with_name('build_macos_installer.py')),
                        str(source), '--version', version, '--output', str(output)], check=True)
        payload = subprocess.check_output(['pkgutil', '--payload-files', str(output)], text=True)
        required = ['Applications/Fluid_Juicy_SF2.app/Contents/MacOS/Fluid_Juicy_SF2',
                    'Library/Audio/Plug-Ins/VST3/Fluid_Juicy_SF2.vst3/Contents/MacOS/Fluid_Juicy_SF2',
                    'Library/Application Support/Fluid Juicy SF2/LICENSE.txt']
        paths = {line.removeprefix('./') for line in payload.splitlines()}
        if any(path not in paths for path in required):
            raise RuntimeError('Installer payload is missing a required product or license')
        # Confirm the artifact label matches the actual input architecture too.
        for name in ('Fluid_Juicy_SF2.app', 'Fluid_Juicy_SF2.vst3'):
            binary = source / name / 'Contents/MacOS/Fluid_Juicy_SF2'
            actual = subprocess.check_output(['lipo', '-archs', str(binary)], text=True).strip()
            if actual != args.arch:
                raise RuntimeError(f'Expected {args.arch}, got {actual}')
        output.with_suffix('.payload.txt').write_text(payload)
    print('Installer build and payload checks passed; installation acceptance is still pending.')


if __name__ == '__main__':
    main()
