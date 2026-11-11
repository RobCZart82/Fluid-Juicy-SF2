"""Source packaging completes Git output before reading tar members."""
import io
from pathlib import Path
import subprocess
import sys
import tarfile
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'scripts'))
from package_desktop import git_source_archive


class SourcePackaging(unittest.TestCase):
    def test_git_archive_content_is_preserved(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            def git(*args):
                return subprocess.run(['git', '-C', directory, *args], check=True,
                                      stdout=subprocess.PIPE, stderr=subprocess.PIPE)
            git('init')
            # Keep this byte-preservation fixture independent of Windows/global EOL policy.
            git('config', 'core.autocrlf', 'false')
            git('config', 'core.eol', 'lf')
            (root / 'source.txt').write_bytes(b'exact source\n')
            git('add', 'source.txt')
            git('-c', 'user.name=Test', '-c', 'user.email=test@example.invalid',
                'commit', '-m', 'fixture')
            with git_source_archive(root, 'HEAD') as archive:
                self.assertEqual(archive.extractfile('source.txt').read(), b'exact source\n')

    def test_delayed_trailing_output_does_not_break_writer(self):
        # The tar iterator finishes before this producer writes its last 2 MiB.
        # A live pipe closed at that point gives the old exporter BrokenPipe.
        data = io.BytesIO()
        with tarfile.open(fileobj=data, mode='w') as archive:
            entry = tarfile.TarInfo('source.txt')
            entry.size = 7
            archive.addfile(entry, io.BytesIO(b'fixture'))
        writer = ('import sys,time; from pathlib import Path; '
                  'sys.stdout.buffer.write(Path(sys.argv[1]).read_bytes()); '
                  'sys.stdout.buffer.flush(); time.sleep(0.1); '
                  'sys.stdout.buffer.write(bytes(2 * 1024 * 1024)); sys.stdout.buffer.flush()')
        real_run = subprocess.run
        with tempfile.TemporaryDirectory() as directory:
            fixture = Path(directory) / 'fixture.tar'
            fixture.write_bytes(data.getvalue())
            def produce(_command, **kwargs):
                return real_run([sys.executable, '-c', writer, str(fixture)], **kwargs)
            with patch('package_desktop.subprocess.run', side_effect=produce):
                with git_source_archive(Path('fixture'), 'HEAD') as archive:
                    self.assertEqual(archive.extractfile('source.txt').read(), b'fixture')

    def test_git_failure_keeps_exit_code_and_diagnostic(self):
        with tempfile.TemporaryDirectory() as directory:
            with self.assertRaisesRegex(RuntimeError, r'Source archive failed:.*exit [1-9].*fatal:'):
                with git_source_archive(Path(directory), 'HEAD'):
                    self.fail('failed producer must not return an archive')


if __name__ == '__main__':
    unittest.main()
