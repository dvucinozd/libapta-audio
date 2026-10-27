# SPDX-License-Identifier: Apache-2.0
"""Package identity/refusal tests, using synthetic ELF containers only."""
import json
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import apta_target_probe_package as pack


def fake_elf(label, payload):
    raw = bytearray(200)
    raw[:7] = b'\x7fELF\x01\x01\x01'
    struct.pack_into('<HH', raw, 16, 2, 243)
    struct.pack_into('<I', raw, 32, 52)
    struct.pack_into('<HHH', raw, 46, 40, 3, 1)
    names = b'\0.shstrtab\0.bss\0'
    struct.pack_into('<10I', raw, 92, 1, 3, 0, 0, 172, len(names), 0, 0, 1, 0)
    struct.pack_into('<10I', raw, 132, 11, 8, 3, 0x4ff00000, 0, payload+8, 0, 0, 8, 0)
    raw[172:172+len(names)] = names
    return bytes(raw)+label.encode()


def make_package(folder):
    variants = {}
    for name, payload in pack.VARIANTS.items():
        d = folder/name
        d.mkdir()
        label = 'synthetic-'+name
        (d/'firmware.elf').write_bytes(fake_elf(label, payload)+('b'*40+'c'*64).encode())
        (d/'symbols.txt').write_text('synthetic retained_trial\n')
        (d/'link.map').write_text('synthetic link map, no code\n')
        (d/'capture-template.json').write_text(json.dumps(dict(execution_ready=False, unit_id=None, chip_revision=None, source_revision='b'*40,protocol_sha256='c'*64,elf_sha256=pack.sha(d/'firmware.elf'))))
        variants[name] = dict(features='esp32p4,'+name, buffer_payload_bytes=payload, image_id=label, sections=pack.elf_sections((d/'firmware.elf').read_bytes()))
    m = dict(format='libapta-target-probe-package-1',kind='BUILD_ONLY_LINKCHECK',status='BUILD_ONLY_COMPLETE',execution_ready=False,qualification='NOT_QUALIFIED',target=pack.TARGET,minimum_chip_policy=302,source_revision='b'*40,protocol_sha256='c'*64,variants=variants,files=pack.inventory(folder))
    (folder/'package.json').write_text(json.dumps(m, sort_keys=True))
    return m


class PackageTests(unittest.TestCase):
    def test_complete_synthetic_container_verifies_but_never_qualifies(self):
        with tempfile.TemporaryDirectory() as tmp:
            folder=Path(tmp);make_package(folder)
            m=pack.verify(folder)
            self.assertEqual(m['kind'],'BUILD_ONLY_LINKCHECK')
            self.assertFalse(m['execution_ready'])
            self.assertEqual(m['qualification'],'NOT_QUALIFIED')

    def test_modified_missing_extra_and_symlink_artifacts_refused(self):
        for mode in ['modify','missing','extra']:
            with self.subTest(mode=mode), tempfile.TemporaryDirectory() as tmp:
                folder=Path(tmp);make_package(folder)
                if mode=='modify':(folder/'copy/firmware.elf').write_bytes(b'not ELF')
                if mode=='missing':(folder/'copy/link.map').unlink()
                if mode=='extra':(folder/'unexpected').write_text('x')
                with self.assertRaises(ValueError):pack.verify(folder)

    def test_readiness_policy_variants_and_template_unknowns_are_guarded(self):
        for change in [dict(status='BUILDING'),dict(execution_ready=True),dict(qualification='PASS'),dict(minimum_chip_policy=301),dict(target='x86_64')]:
            with self.subTest(change=change),tempfile.TemporaryDirectory() as tmp:
                folder=Path(tmp);m=make_package(folder);m.update(change)
                (folder/'package.json').write_text(json.dumps(m,sort_keys=True))
                with self.assertRaises(ValueError):pack.verify(folder)
        with tempfile.TemporaryDirectory() as tmp:
            folder=Path(tmp);m=make_package(folder);del m['variants']['logs']
            (folder/'package.json').write_text(json.dumps(m,sort_keys=True))
            with self.assertRaises(ValueError):pack.verify(folder)
        with tempfile.TemporaryDirectory() as tmp:
            folder=Path(tmp);m=make_package(folder)
            p=folder/'copy/capture-template.json';t=json.loads(p.read_text());t['unit_id']='invented';p.write_text(json.dumps(t))
            m['files']['copy/capture-template.json']=dict(bytes=p.stat().st_size,sha256=pack.sha(p))
            (folder/'package.json').write_text(json.dumps(m,sort_keys=True))
            with self.assertRaises(ValueError):pack.verify(folder)

    def test_malformed_foreign_and_truncated_elf_refused(self):
        raw=fake_elf('synthetic',131072)
        for data in [b'',raw[:100],b'\0'+raw[1:],raw[:4]+b'\x02'+raw[5:],raw[:18]+b'\x3e\0'+raw[20:]]:
            with self.subTest(length=len(data)),self.assertRaises(ValueError):pack.elf_sections(data)

    def test_duplicate_json_and_unknown_cli_action_refused(self):
        with self.assertRaises(ValueError):pack.strict_json('{"x":1,"x":2}')
        proc=subprocess.run([sys.executable,pack.__file__,'flash'],capture_output=True,timeout=10)
        self.assertNotEqual(proc.returncode,0)
        self.assertEqual(proc.stdout,b'')

    @unittest.skipUnless(sys.platform=='linux','Build refusal before compiler dispatch is Linux-specific')
    def test_existing_output_or_build_directory_cannot_be_overwritten(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp);out=root/'out';out.mkdir();marker=out/'keep';marker.write_text('original')
            with self.assertRaises(ValueError):pack.build(out,root/'new-target')
            self.assertEqual(marker.read_text(),'original')
        with tempfile.TemporaryDirectory() as tmp:
            folder=Path(tmp);m=make_package(folder);artifact=folder/'copy/link.map'
            artifact.unlink();artifact.symlink_to(folder/'copy/symbols.txt')
            m['files']['copy/link.map']=dict(bytes=artifact.stat().st_size,sha256=pack.sha(artifact))
            (folder/'package.json').write_text(json.dumps(m,sort_keys=True))
            with self.assertRaises(ValueError):pack.verify(folder)


if __name__=='__main__':unittest.main()
