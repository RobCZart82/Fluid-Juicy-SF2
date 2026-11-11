#!/usr/bin/env python3
"""Verify that the packaged source entry point works outside a Git checkout."""
import argparse
import os
import shutil
from pathlib import Path
import subprocess
import sys
import tarfile
import tempfile


def extract_sources(archive, root):
    """Extract files before links, so no write can traverse an archive symlink."""
    root = Path(root).resolve()
    members = archive.getmembers()
    seen = set()
    for member in members:
        name = Path(member.name)
        if name.is_absolute() or '..' in name.parts or not name.parts:
            raise RuntimeError(f'Unsafe source bundle member: {member.name}')
        destination = root / name
        if not destination.resolve().is_relative_to(root):
            raise RuntimeError(f'Unsafe source bundle member: {member.name}')
        if name in seen and not member.isdir():
            raise RuntimeError(f'Duplicate source bundle member: {member.name}')
        seen.add(name)
        if not (member.isfile() or member.isdir() or member.issym() or member.islnk()):
            raise RuntimeError(f'Unsupported source bundle member: {member.name}')
    # Materialize regular data using exclusive creation. Archive links are still
    # absent, and a duplicate file/directory collision fails rather than replacing.
    for member in members:
        destination = root / member.name
        if member.isdir():
            destination.mkdir(parents=True, exist_ok=True)
        elif member.isfile():
            destination.parent.mkdir(parents=True, exist_ok=True)
            with archive.extractfile(member) as source, destination.open('xb') as target:
                shutil.copyfileobj(source, target)
            destination.chmod(member.mode & 0o777)
    # The two FluidSynth Unicode aliases in our pinned export are legitimate
    # relative links to regular files. Link chains/directories are unnecessary.
    for member in members:
        if not (member.issym() or member.islnk()):
            continue
        destination = root / member.name
        name = Path(member.linkname)
        target = destination.parent / name if member.issym() else root / name
        if (name.is_absolute() or not target.resolve().is_relative_to(root)
                or target.is_symlink() or not target.is_file()):
            raise RuntimeError(f'Unsafe source bundle link: {member.name}')
        destination.parent.mkdir(parents=True, exist_ok=True)
        if member.issym():
            destination.symlink_to(member.linkname)
        else:
            os.link(target, destination)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('archive', type=Path)
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='fj-src-') as directory:
        root = Path(directory).resolve()
        with tarfile.open(args.archive, 'r:gz') as archive:
            extract_sources(archive, root)
        if (root / 'fluid-juicy-sf2/.git').exists():
            raise RuntimeError('Archive verification must run without Git metadata')
        subprocess.run([sys.executable, str(root / 'fluid-juicy-sf2/scripts/build.py'),
                        '--verify-sources-only'], cwd=root, check=True)


if __name__ == '__main__':
    main()
