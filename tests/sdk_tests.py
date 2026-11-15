"""Exercise pinned SDK extraction, provenance and architecture without network."""
import hashlib
import io
import json
from pathlib import Path
import sys
import tarfile
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
import fetch_core_sdk


class SdkDownload(unittest.TestCase):
    def exercise(self, *, version_ok=True, architecture_ok=True, checksum_ok=True, source_ok=True):
        lock = json.loads((ROOT / 'ci/core-sdk.json').read_text())
        machine = 183 if architecture_ok else 62
        library = b'\x7fELF\x02\x01' + bytes(12) + machine.to_bytes(2, 'little') + bytes(12)
        info = dict(source_commit=lock['source_commit'] if source_ok else 'stale',
                    dtype_selective_build=False, sha256=hashlib.sha256(library).hexdigest())
        files = {
            'share/phono-core/VERSION': (lock['version'] if version_ok else '999.0.0').encode(),
            'share/phono-core/build-info.json': json.dumps(info).encode(),
            'lib/libphono_core.so': library,
            'include/phono-core/phono_api.h': b'header',
            'lib/cmake/phono-core/phono-coreConfig.cmake': b'config',
            'share/licenses/phono-core/LICENSE': b'license',
        }
        content = io.BytesIO()
        with tarfile.open(fileobj=content, mode='w:gz') as archive:
            for name, value in files.items():
                entry = tarfile.TarInfo(name)
                entry.size = len(value)
                archive.addfile(entry, io.BytesIO(value))
        payload = content.getvalue()
        lock['assets']['linux-arm64']['sha256'] = (hashlib.sha256(payload).hexdigest()
                                                   if checksum_ok else '0' * 64)
        with tempfile.TemporaryDirectory(dir=Path.cwd()) as temp:
            root = Path(temp)
            sdk = root / 'sdk'
            (sdk / 'share/phono-core').mkdir(parents=True)
            (sdk / 'share/phono-core/VERSION').write_text('stale')
            def download(url, target):
                self.assertIn('linux-arm64', url)
                Path(target).write_bytes(payload)
            with patch.object(fetch_core_sdk.urllib.request, 'urlretrieve', side_effect=download):
                fetch_core_sdk.fetch(sdk, root / 'downloads', 'linux-arm64', lock=lock)
            self.assertEqual((sdk / 'share/phono-core/VERSION').read_text(), lock['version'])

    def test_release_replaces_stale_sdk(self):
        self.exercise()

    def test_invalid_sdk_is_rejected(self):
        for option, message in [('version_ok', 'version differs'),
                                ('architecture_ok', 'architecture differs'),
                                ('checksum_ok', 'checksum mismatch'),
                                ('source_ok', 'provenance differs')]:
            with self.subTest(option=option), self.assertRaisesRegex(ValueError, message):
                self.exercise(**{option: False})


if __name__ == '__main__':
    unittest.main()
