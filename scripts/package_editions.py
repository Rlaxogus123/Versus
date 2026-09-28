"""Combine and verify same-edition Windows/Android artifacts (never Apple)."""
import argparse
import hashlib
import io
import json
from pathlib import Path
import struct
from zipfile import ZIP_DEFLATED, ZipFile

BINARIES = {'Windows': 'tipp7.versus.dll', 'Android32': 'tipp7.versus.android32.so',
            'Android64': 'tipp7.versus.android64.so'}
EDITIONS = ('standard', 'membership')


def check_binary(name, data, edition):
    assert f'versus-edition:{edition}'.encode() in data, f'Wrong edition in {name}'
    opposite = 'membership' if edition == 'standard' else 'standard'
    assert f'versus-edition:{opposite}'.encode() not in data, f'Mixed edition in {name}'
    if name.endswith('.dll'):
        assert data[:2] == b'MZ'
        offset = struct.unpack_from('<I', data, 0x3c)[0]
        assert data[offset:offset+4] == b'PE\0\0'
        assert struct.unpack_from('<H', data, offset+4)[0] == 0x8664
    else:
        assert data[:4] == b'\x7fELF' and data[5] == 1
        bits, machine = (1, 40) if 'android32' in name else (2, 183)
        assert data[4] == bits and struct.unpack_from('<H', data, 18)[0] == machine


def validate(package, edition, version):
    with ZipFile(io.BytesIO(package)) as archive:
        assert archive.testzip() is None
        manifest = json.loads(archive.read('mod.json'))
        assert manifest['id'] == 'tipp7.versus' and manifest['version'] == version
        names = archive.namelist()
        assert len(names) == len(set(names)), 'Duplicate ZIP entries'
        assert {n for n in names if n.endswith(('.dll', '.so', '.dylib'))} == set(BINARIES.values())
        for name in BINARIES.values():
            check_binary(name, archive.read(name), edition)
        assert any(n.endswith(('.ogg', '.wav', '.mp3')) for n in names), 'Missing sounds'
        assert manifest['name'] == f'Versus ({edition.title()})'


def combine(root, destination, version):
    destination.mkdir(parents=True, exist_ok=True)
    for edition in EDITIONS:
        contents = {}
        for platform, binary in BINARIES.items():
            packages = list((root / f'geode-{platform}-{edition}').glob('*.geode'))
            assert len(packages) == 1, f'Missing or ambiguous {platform} {edition}'
            with ZipFile(packages[0]) as archive:
                assert archive.testzip() is None
                assert {n for n in archive.namelist() if n.endswith(('.dll', '.so', '.dylib'))} == {binary}
                check_binary(binary, archive.read(binary), edition)
                for name in archive.namelist():
                    if name.endswith('/'):
                        continue
                    assert not name.startswith('/') and '..' not in Path(name).parts
                    value = archive.read(name)
                    if name == 'mod.json':
                        value = json.dumps(json.loads(value), sort_keys=True, indent=2).encode()
                    # Checkout uses CRLF on Windows and LF on Linux. Only
                    # normalize bundled documentation; never alter binaries
                    # or audio when comparing platform resources.
                    if name.endswith('.md'):
                        value = value.replace(b'\r\n', b'\n')
                    if name in contents:
                        assert contents[name] == value, f'Platform resource mismatch: {name}'
                    contents[name] = value
        manifest = json.loads(contents['mod.json'])
        assert manifest['version'] == version
        manifest['name'] = f'Versus ({edition.title()})'
        contents['mod.json'] = json.dumps(manifest, indent=2).encode()
        output = io.BytesIO()
        with ZipFile(output, 'w', compression=ZIP_DEFLATED, compresslevel=9) as archive:
            for name, value in sorted(contents.items()):
                archive.writestr(name, value)
        package = output.getvalue()
        validate(package, edition, version)
        path = destination / f'tipp7.versus-AllPlatform-{edition}.geode'
        path.write_bytes(package)
        print(path.name, version, hashlib.sha256(package).hexdigest())


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('artifacts', type=Path)
    parser.add_argument('destination', type=Path)
    parser.add_argument('--manifest', type=Path, default=Path('mod.json'))
    args = parser.parse_args()
    combine(args.artifacts, args.destination, json.loads(args.manifest.read_text())['version'])
