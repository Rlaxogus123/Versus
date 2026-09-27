"""Offline packaging regressions; all fixtures stay in a temporary directory."""
import contextlib
import importlib.util
import io
import json
from pathlib import Path
import struct
import tempfile
import unittest
from zipfile import ZipFile

spec = importlib.util.spec_from_file_location(
    'packager', Path(__file__).resolve().parents[1] / 'scripts/package_editions.py')
packager = importlib.util.module_from_spec(spec)
spec.loader.exec_module(packager)


def binary(name, edition):
    data = bytearray(256)
    if name.endswith('.dll'):
        data[:2] = b'MZ'
        struct.pack_into('<I', data, 0x3c, 64)
        data[64:68] = b'PE\0\0'
        struct.pack_into('<H', data, 68, 0x8664)
    else:
        data[:4] = b'\x7fELF'
        data[4] = 1 if 'android32' in name else 2
        data[5] = 1
        struct.pack_into('<H', data, 18, 40 if 'android32' in name else 183)
    return bytes(data) + f'versus-edition:{edition}'.encode()


class Packaging(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.artifacts = self.root / 'artifacts'
        for edition in packager.EDITIONS:
            for platform in packager.BINARIES:
                self.fixture(platform, edition)

    def fixture(self, platform, edition, *, actual_edition=None, sound=b'sound',
                version='v0.5.3', apple=False, corrupt_arch=False):
        folder = self.artifacts / f'geode-{platform}-{edition}'
        folder.mkdir(parents=True, exist_ok=True)
        name = packager.BINARIES[platform]
        data = bytearray(binary(name, actual_edition or edition))
        if corrupt_arch:
            data[4] = 0
            data[68] = 0
        with ZipFile(folder / 'mod.geode', 'w') as archive:
            archive.writestr('mod.json', json.dumps({
                'id': 'tipp7.versus', 'version': version, 'name': 'Versus'}))
            archive.writestr(name, data)
            if sound is not None:
                archive.writestr('resources/ready.ogg', sound)
            if apple:
                archive.writestr('tipp7.versus.dylib', b'not allowed')

    def combine(self):
        with contextlib.redirect_stdout(io.StringIO()):
            packager.combine(self.artifacts, self.root / 'output', 'v0.5.3')

    def test_two_complete_editions(self):
        self.combine()
        for edition in packager.EDITIONS:
            data = (self.root / 'output' / f'tipp7.versus-AllPlatform-{edition}.geode').read_bytes()
            packager.validate(data, edition, 'v0.5.3')
            with ZipFile(io.BytesIO(data)) as archive:
                self.assertEqual(archive.read('resources/ready.ogg'), b'sound')

    def test_rejects_wrong_edition(self):
        self.fixture('Android32', 'membership', actual_edition='standard')
        with self.assertRaises(AssertionError):
            self.combine()

    def test_rejects_wrong_architecture(self):
        self.fixture('Windows', 'standard', corrupt_arch=True)
        with self.assertRaises(AssertionError):
            self.combine()

    def test_rejects_resource_mismatch(self):
        self.fixture('Android64', 'standard', sound=b'wrong')
        with self.assertRaises(AssertionError):
            self.combine()

    def test_rejects_apple_binary(self):
        self.fixture('Android64', 'standard', apple=True)
        with self.assertRaises(AssertionError):
            self.combine()

    def test_rejects_old_version(self):
        self.fixture('Android64', 'standard', version='v0.5.2')
        with self.assertRaises(AssertionError):
            self.combine()

    def test_rejects_missing_platform(self):
        (self.artifacts / 'geode-Android32-standard' / 'mod.geode').unlink()
        with self.assertRaises(AssertionError):
            self.combine()

    def test_rejects_missing_sounds(self):
        for platform in packager.BINARIES:
            self.fixture(platform, 'standard', sound=None)
        with self.assertRaises(AssertionError):
            self.combine()


if __name__ == '__main__':
    unittest.main()
