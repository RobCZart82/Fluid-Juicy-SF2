#!/usr/bin/env python3
"""Compile an unsigned Windows preview installer from the verified ZIP."""
import argparse
import hashlib
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import zipfile
from package_desktop import verify_pe


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('distribution', type=Path)
    parser.add_argument('--compiler', type=Path, required=True, help='Installed Inno Setup 6 ISCC.exe')
    args = parser.parse_args()
    distribution = args.distribution.resolve()
    archives = list(distribution.glob('*.zip'))
    if len(archives) != 1:
        raise RuntimeError('Expected one verified development ZIP')
    version_match = re.search(r'project\(JUICY_SF_PLUGIN VERSION ([0-9.]+)',
                              (Path(__file__).resolve().parents[1] / 'CMakeLists.txt').read_text())
    if not version_match:
        raise RuntimeError('Missing project version')
    output = distribution / 'Fluid-Juicy-SF2-windows-x64-preview-setup.exe'
    if output.exists():
        raise RuntimeError(f'Refusing to overwrite: {output}')
    with tempfile.TemporaryDirectory(prefix='juicy-win-installer-') as temporary:
        root = Path(temporary)
        with zipfile.ZipFile(archives[0]) as archive:
            for name in archive.namelist():
                if not (root / name).resolve().is_relative_to(root.resolve()):
                    raise RuntimeError('Unsafe ZIP entry')
            archive.extractall(root)
        source = root / 'Fluid-Juicy-SF2'
        verify_pe(source / 'Fluid_Juicy_SF2.exe')
        verify_pe(source / 'Fluid_Juicy_SF2.vst3/Contents/x86_64-win/Fluid_Juicy_SF2.vst3')
        script = Path(__file__).resolve().parents[1] / 'installers/windows/FluidJuicySF2.iss'
        subprocess.run([str(args.compiler.resolve()), f'/DInputDir={source}',
                        f'/DOutputDir={distribution}', f'/DProductVersion={version_match[1]}', str(script)], check=True)
    if not output.is_file():
        raise RuntimeError('Compiler did not produce the expected installer')
    output.with_suffix('.exe.sha256').write_text(hashlib.sha256(output.read_bytes()).hexdigest() + '  ' + output.name + '\n')
    print('Unsigned preview EXE built; installation and runtime acceptance remain pending.')


if __name__ == '__main__':
    main()
