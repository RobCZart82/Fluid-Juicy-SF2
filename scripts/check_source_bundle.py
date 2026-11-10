#!/usr/bin/env python3
"""Verify that the packaged source entry point works outside a Git checkout."""
import argparse
from pathlib import Path
import subprocess
import sys
import tarfile
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('archive', type=Path)
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='fj-src-') as directory:
        root = Path(directory).resolve()
        # This is the archive just produced by our own packager. Still reject
        # traversal/link members before extraction into the temporary directory.
        with tarfile.open(args.archive, 'r:gz') as archive:
            for member in archive.getmembers():
                destination = (root / member.name).resolve()
                if not destination.is_relative_to(root):
                    raise RuntimeError(f'Unsafe source bundle member: {member.name}')
                if member.issym() or member.islnk():
                    target = destination.parent / member.linkname if member.issym() else root / member.linkname
                    if not target.resolve().is_relative_to(root):
                        raise RuntimeError(f'Unsafe source bundle link: {member.name}')
                elif not (member.isfile() or member.isdir()):
                    raise RuntimeError(f'Unsupported source bundle member: {member.name}')
            archive.extractall(root)
        if (root / 'fluid-juicy-sf2/.git').exists():
            raise RuntimeError('Archive verification must run without Git metadata')
        subprocess.run([sys.executable, str(root / 'fluid-juicy-sf2/scripts/build.py'),
                        '--verify-sources-only'], cwd=root, check=True)


if __name__ == '__main__':
    main()
