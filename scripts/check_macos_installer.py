#!/usr/bin/env python3
"""Build and inspect the native preview installer without installing it."""
import argparse
import os
from pathlib import Path
import json
import subprocess
import sys
import tempfile

from build import product_manifest, project_version


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('distribution', type=Path)
    parser.add_argument('--arch', required=True, choices=('arm64', 'x86_64'))
    parser.add_argument('--install-on-runner', action='store_true')
    args = parser.parse_args()
    if args.install_on_runner and (
        os.environ.get('GITHUB_ACTIONS') != 'true'
        or os.environ.get('RUNNER_OS') != 'macOS'
        or os.environ.get('RUNNER_ENVIRONMENT') != 'github-hosted'
    ):
        parser.error('Installation testing requires a GitHub-hosted macOS runner')
    distribution = args.distribution.resolve()
    archives = list(distribution.glob('*.zip'))
    if len(archives) != 1:
        raise RuntimeError('Expected exactly one verified development ZIP')
    with tempfile.TemporaryDirectory(prefix='juicy-installer-check-') as temporary:
        folder = Path(temporary)
        subprocess.run(['ditto', '-x', '-k', str(archives[0]), str(folder)], check=True)
        source = folder / 'Fluid-Juicy-SF2'
        record = json.loads((source / 'build-record.json').read_text(encoding='utf-8'))
        version = project_version()
        if record.get('version') != version:
            raise RuntimeError('macOS ZIP version differs from the source version')
        output = distribution / f'Fluid-Juicy-SF2-macos-{args.arch}-preview.pkg'
        subprocess.run([sys.executable, str(Path(__file__).with_name('build_macos_installer.py')),
                        str(source), '--version', version, '--output', str(output)], check=True)
        payload = subprocess.check_output(['pkgutil', '--payload-files', str(output)], text=True)
        expanded = folder / 'expanded'
        subprocess.run(['pkgutil', '--expand-full', str(output), str(expanded)], check=True)
        payload_root = expanded / 'Payload'
        for name, install_dir in (
            ('Fluid_Juicy_SF2.app', 'Applications'),
            ('Fluid_Juicy_SF2.vst3', 'Library/Audio/Plug-Ins/VST3'),
        ):
            installed = payload_root / install_dir / name
            if product_manifest(installed) != product_manifest(source / name):
                raise RuntimeError(f'Packaged bundle differs from verified ZIP: {name}')
            subprocess.run(['codesign', '--verify', '--strict', str(installed)], check=True)
            binary = installed / 'Contents/MacOS/Fluid_Juicy_SF2'
            actual = subprocess.check_output(['lipo', '-archs', str(binary)], text=True).strip()
            if actual != args.arch:
                raise RuntimeError(f'Expected {args.arch}, got {actual}')

        support = payload_root / 'Library/Application Support/Fluid Juicy SF2'
        for name in ('LICENSE.txt', 'build-record.json', 'binary-verification.json',
                     'Fluid-Juicy-SF2-Installation-EN.pdf',
                     'Fluid-Juicy-SF2-Installation-HU.pdf'):
            if (support / name).read_bytes() != (source / name).read_bytes():
                raise RuntimeError(f'Packaged documentation or build record differs: {name}')
        output.with_suffix('.payload.txt').write_text(payload)
        if args.install_on_runner:
            install_roots = (
                Path('/Applications/Fluid_Juicy_SF2.app'),
                Path('/Library/Audio/Plug-Ins/VST3/Fluid_Juicy_SF2.vst3'),
                Path('/Library/Application Support/Fluid Juicy SF2'),
            )
            if any(path.exists() or path.is_symlink() for path in install_roots):
                raise RuntimeError('Existing installation found on the runner')
            with output.with_suffix('.install.log').open('w') as log:
                for phase in ('install', 'reinstall'):
                    print(phase, file=log, flush=True)
                    subprocess.run(['sudo', '-n', '/usr/sbin/installer', '-pkg',
                                    str(output), '-target', '/'], stdout=log,
                                   stderr=subprocess.STDOUT, check=True)
                    for path in install_roots:
                        expected = payload_root / path.relative_to('/')
                        if product_manifest(path) != product_manifest(expected):
                            raise RuntimeError(f'{phase}: installed files differ at {path}')
                    for bundle in install_roots[:2]:
                        subprocess.run(['codesign', '--verify', '--strict', str(bundle)], check=True)
                    print(f'{phase}: installed files and signatures verified', file=log, flush=True)
    print('Installer checks passed.')


if __name__ == '__main__':
    main()
