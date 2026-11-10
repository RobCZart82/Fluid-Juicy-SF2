"""Exercise a Git-free source bundle and rejection of modified exported files."""
import hashlib
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'scripts'))
import build


class ExportedSources(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.bundle = Path(self.temporary.name).resolve()
        self.plugin = self.bundle / 'fluid-juicy-sf2'
        self.plugin.mkdir()
        self.dependency = self.bundle / 'dependencies/stub'
        self.dependency.mkdir(parents=True)
        self.revision = 'a' * 40
        self.manifest = {'format': 1, 'repositories': {}}
        for repository in [self.plugin, self.dependency]:
            (repository / 'source.txt').write_bytes(b'original source\n')
            self.manifest['repositories'][repository.relative_to(self.bundle).as_posix()] = {
                'revision': self.revision,
                'files': {'source.txt': {'sha256': hashlib.sha256(b'original source\n').hexdigest()}}}
        self.saveManifest()
        self.rootPatch = patch.object(build, 'ROOT', self.plugin)
        self.rootPatch.start()
        self.addCleanup(self.rootPatch.stop)

    def saveManifest(self):
        (self.bundle / 'source-manifest.json').write_text(json.dumps(self.manifest))

    def test_export_verifies_without_parent_git(self):
        self.assertEqual(build.clean_revision(self.plugin), self.revision)
        self.assertEqual(build.source('stub', self.bundle / 'dependencies'), self.dependency)

    def test_changed_plugin_is_not_a_clean_export(self):
        (self.plugin / 'source.txt').write_text('changed')
        self.assertIsNone(build.clean_revision(self.plugin))

    def test_changed_dependency_is_rejected(self):
        (self.dependency / 'source.txt').write_text('changed')
        with self.assertRaises(RuntimeError):
            build.source('stub', self.bundle / 'dependencies')

    def test_missing_file_is_rejected(self):
        (self.plugin / 'source.txt').unlink()
        self.assertIsNone(build.clean_revision(self.plugin))

    def test_manifest_path_cannot_escape_repository(self):
        self.manifest['repositories']['fluid-juicy-sf2']['files']['../outside'] = {'sha256': '0' * 64}
        self.saveManifest()
        with self.assertRaisesRegex(RuntimeError, 'Unsafe path'):
            build.clean_revision(self.plugin)

    def test_build_entry_point_accepts_export(self):
        with patch.object(sys, 'argv', ['build.py', '--verify-sources-only']):
            build.main()


if __name__ == '__main__':
    with patch.object(build, 'PINS', {'stub': ('https://invalid.example/stub', 'a' * 40)}):
        unittest.main()
