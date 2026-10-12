import os
import tempfile
import unittest
from pathlib import Path
from scripts.watch_match import copy_runtime


class RuntimeCopyTests(unittest.TestCase):
    def test_static_archives_shared_but_settings_and_modules_independent(self):
        with tempfile.TemporaryDirectory() as root:
            source = Path(root) / 'source'
            source.mkdir()
            for name in ('STARDAT.MPQ', 'bwapi.ini', 'PersonaBot.dll', 'old.rep', 'old.log'):
                (source / name).write_bytes(b'original')
            target = Path(root) / 'target'
            copy_runtime(source, target)
            self.assertTrue(os.path.samefile(source / 'STARDAT.MPQ', target / 'STARDAT.MPQ'))
            for name in ('bwapi.ini', 'PersonaBot.dll'):
                self.assertFalse(os.path.samefile(source / name, target / name))
                (target / name).write_bytes(b'changed')
                self.assertEqual((source / name).read_bytes(), b'original')
            self.assertFalse((target / 'old.rep').exists())
            self.assertFalse((target / 'old.log').exists())


if __name__ == '__main__':
    unittest.main()
