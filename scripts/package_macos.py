#!/usr/bin/env python3
"""Verify and package an arm64 development build with corresponding sources."""
import copy
import json
from pathlib import Path
import platform
import shutil
import subprocess
import tarfile

from build import ROOT, PINS, run

def main():
    if platform.system() != 'Darwin' or platform.machine() != 'arm64':
        raise RuntimeError('Packaging requires a native Apple Silicon Mac')
    build = ROOT / 'build' / 'arm64'
    distribution = build / 'distribution'
    stage = build / 'package' / 'Fluid-Juicy-SF2'
    stage.mkdir(parents=True, exist_ok=False)
    distribution.mkdir(parents=True, exist_ok=True)
    artefacts = build / 'plugin' / 'JuicySFPlugin_artefacts' / 'Release'
    evidence = {}
    for suffix, directory in [('.vst3', 'VST3'), ('.app', 'Standalone')]:
        bundle = artefacts / directory / ('Fluid Juicy SF2' + suffix)
        binary = bundle / 'Contents' / 'MacOS' / 'Fluid Juicy SF2'
        architecture = subprocess.check_output(['lipo', '-archs', str(binary)], text=True).strip()
        if architecture != 'arm64':
            raise RuntimeError(f'Unexpected architecture: {binary}: {architecture}')
        dependencies = subprocess.check_output(['otool', '-L', str(binary)], text=True)
        for line in dependencies.splitlines()[1:]:
            dependency = line.strip().split(' (', 1)[0]
            if not dependency.startswith(('/usr/lib/', '/System/Library/')):
                raise RuntimeError(f'Non-system dependency in portable build: {dependency}')
        target = stage / bundle.name
        shutil.copytree(bundle, target, symlinks=True)
        run('codesign', '--force', '--sign', '-', '--timestamp=none', target)
        run('codesign', '--verify', '--strict', target)
        evidence[bundle.name] = {'architecture': architecture, 'dependencies': dependencies}

    shutil.copy2(build / 'build-record.json', stage)
    (stage / 'binary-verification.json').write_text(json.dumps(evidence, indent=2), encoding='utf-8')
    (stage / 'README.txt').write_text(
        'Fluid Juicy SF2 - development preview, native Apple Silicon.\n'
        'Ad-hoc signed only; not Developer ID signed or notarized.\n'
        'CI compilation and processor tests do not prove REAPER/manual acceptance.\n'
        'Fluid Juicy SF2 by RobCZart82. No SoundFonts included.\n'
        'See corresponding-source.tar.gz for complete build sources and license notices.\n', encoding='utf-8')

    # Include exact corresponding source, not only links to external repositories.
    repositories = [('fluid-juicy-sf2', ROOT)]
    repositories += [(f'dependencies/{name}', ROOT / 'build' / 'sources' / name) for name in PINS]
    repositories += [(f'dependencies/fluidsynth/{name}', ROOT / 'build' / 'sources' / 'fluidsynth' / name)
                     for name in ['gcem', 'signalsmith-audio-basics']]
    with tarfile.open(distribution / 'corresponding-source.tar.gz', 'w:gz') as destination:
        for prefix, repository in repositories:
            process = subprocess.Popen(['git', '-C', str(repository), 'archive', 'HEAD'], stdout=subprocess.PIPE)
            with tarfile.open(fileobj=process.stdout, mode='r|') as archive:
                for entry in archive:
                    member = copy.copy(entry)
                    member.name = prefix + '/' + entry.name
                    destination.addfile(member, archive.extractfile(entry) if entry.isfile() else None)
            process.stdout.close()
            if process.wait() != 0:
                raise RuntimeError(f'Source archive failed: {repository}')
    run('ditto', '-c', '-k', '--sequesterRsrc', '--keepParent', stage,
        distribution / 'fluid-juicy-sf2-macos-arm64-development.zip')

if __name__ == '__main__':
    main()
