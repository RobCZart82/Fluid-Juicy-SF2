"""Regression checks for safe extraction on Python 3.9 and newer."""
import io
import os
from pathlib import Path
import sys
import tarfile
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'scripts'))
from check_source_bundle import extract_sources


class SourceExtraction(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.outer = Path(self.temporary.name).resolve()
        self.root = self.outer / 'unpacked'
        self.root.mkdir()

    def extract(self, entries, **options):
        data = io.BytesIO()
        with tarfile.open(fileobj=data, mode='w') as archive:
            for name, kind, payload in entries:
                member = tarfile.TarInfo(name)
                if kind == 'file':
                    member.mode = 0o755
                    member.size = len(payload)
                    archive.addfile(member, io.BytesIO(payload))
                else:
                    member.type = tarfile.SYMTYPE if kind == 'symlink' else tarfile.DIRTYPE if kind == 'dir' else tarfile.LNKTYPE
                    member.linkname = payload
                    archive.addfile(member)
        data.seek(0)
        with tarfile.open(fileobj=data, mode='r') as archive:
            extract_sources(archive, self.root, **options)

    def test_regular_file_and_unicode_alias(self):
        self.extract([('sf2/café.sf2', 'symlink', 'original.sf2'),
                      ('sf2/original.sf2', 'file', b'fixture'),
                      ('tool.py', 'file', b'build script')])
        self.assertEqual((self.root / 'sf2/café.sf2').read_bytes(), b'fixture')
        if os.name != 'nt':
            self.assertTrue((self.root / 'sf2/café.sf2').is_symlink())
            self.assertTrue(os.access(self.root / 'tool.py', os.X_OK))

    def test_pivot_cannot_write_outside_root(self):
        with self.assertRaises(RuntimeError):
            self.extract([('sub', 'dir', ''), ('pivot', 'symlink', 'sub/..'),
                          ('pivot/../escaped.txt', 'file', b'audit fixture')])
        self.assertFalse((self.outer / 'escaped.txt').exists())

    def test_absolute_and_parent_paths_are_rejected(self):
        for name in ['../escaped.txt', str(self.outer / 'absolute.txt')]:
            with self.subTest(name=name), self.assertRaises(RuntimeError):
                self.extract([(name, 'file', b'fixture')])

    def test_links_cannot_escape_or_target_directories(self):
        for target in ['../outside', str(self.outer), 'sub']:
            with self.subTest(target=target), self.assertRaises(RuntimeError):
                self.extract([('sub', 'dir', ''), ('alias', 'symlink', target)])

    def test_duplicate_file_cannot_replace_existing_data(self):
        with self.assertRaises(RuntimeError):
            self.extract([('source', 'file', b'original'), ('source', 'file', b'overwrite')])

    def test_hardlink_to_regular_file_is_preserved(self):
        self.extract([('alias', 'hardlink', 'source'), ('source', 'file', b'fixture')])
        self.assertEqual((self.root / 'alias').read_bytes(), b'fixture')

    def test_materialized_aliases_do_not_require_link_privileges(self):
        with patch('os.symlink', side_effect=PermissionError('no symlink permission')), \
             patch('os.link', side_effect=PermissionError('no hardlink permission')):
            self.extract([('alias', 'symlink', 'source'), ('hard', 'hardlink', 'source'),
                          ('source', 'file', b'fixture')], materialize_links=True)
        for name in ['alias', 'hard']:
            self.assertFalse((self.root / name).is_symlink())
            self.assertEqual((self.root / name).read_bytes(), b'fixture')

    def test_materialization_does_not_enable_link_chains(self):
        with self.assertRaises(RuntimeError):
            self.extract([('source', 'file', b'fixture'), ('first', 'symlink', 'source'),
                          ('second', 'symlink', 'first')], materialize_links=True)

    def test_windows_permission_failure_materializes_the_alias(self):
        denied = PermissionError('symlink privilege unavailable')
        denied.winerror = 1314
        with patch('check_source_bundle.ON_WINDOWS', True), patch('os.symlink', side_effect=denied):
            self.extract([('alias', 'symlink', 'source'), ('source', 'file', b'fixture')])
        self.assertFalse((self.root / 'alias').is_symlink())
        self.assertEqual((self.root / 'alias').read_bytes(), b'fixture')


if __name__ == '__main__':
    unittest.main()
