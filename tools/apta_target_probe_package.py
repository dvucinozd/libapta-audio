#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Offline Linux BUILD_ONLY_LINKCHECK packages. No flash, serial or device runner."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys

import apta_dense_runner_support as support

ROOT = Path(__file__).resolve().parents[1]
CRATE = ROOT / 'rust/probes/target-adapter'
TARGET = 'riscv32imafc-unknown-none-elf'
VARIANTS = dict(copy=131072, projection32=109200, projection64=218400, activation=114688, logs=229376)
MAX_PACKAGE = 64 * 1048576


def require(condition, message):
    if not condition:
        raise ValueError(message)


def strict_json(raw):
    def pairs(items):
        out = {}
        for k, v in items:
            require(k not in out, 'duplicate JSON key')
            out[k] = v
        return out
    def constant(_):
        raise ValueError('nonfinite JSON')
    return json.loads(raw, object_pairs_hook=pairs, parse_constant=constant)


def sha(path):
    return support.sha(path)


def tool(name):
    path = shutil.which(name)
    require(path is not None, 'Missing installed tool: ' + name)
    return Path(path).absolute()


def elf_sections(raw):
    require(len(raw) >= 52 and raw[:7] == b'\x7fELF\x01\x01\x01', 'Expected little-endian ELF32')
    require(struct.unpack_from('<H', raw, 16)[0] == 2 and struct.unpack_from('<H', raw, 18)[0] == 243, 'Expected RISC-V executable')
    offset = struct.unpack_from('<I', raw, 32)[0]
    size, count, strings = struct.unpack_from('<HHH', raw, 46)
    require(size == 40 and 1 <= count <= 8192 and strings < count and offset + count*size <= len(raw), 'Invalid section table')
    rows = [struct.unpack_from('<10I', raw, offset+i*size) for i in range(count)]
    table = rows[strings]
    require(table[4]+table[5] <= len(raw), 'Invalid section-name table')
    names = raw[table[4]:table[4]+table[5]]
    result = {}
    for row in rows:
        require(row[0] < len(names), 'Invalid section name')
        end = names.find(b'\0', row[0])
        require(end >= row[0], 'Unterminated section name')
        name = names[row[0]:end].decode('ascii', errors='strict')
        if name:
            require(name not in result, 'Duplicate section name')
            if row[1] != 8:
                require(row[4]+row[5] <= len(raw), 'Section exceeds ELF')
            result[name] = dict(address=row[3], bytes=row[5], type=row[1])
    return result


def snapshot():
    names = subprocess.check_output(['git', '-C', str(ROOT), 'ls-files', '--cached', '--others', '--exclude-standard', '-z', '--',
        'rust/probes/resource-core', 'rust/probes/target-adapter', 'tools/apta_target_probe_package.py',
        'tools/apta_target_probe_telemetry.py', 'docs/rust/PRODUCT-TARGET-ADAPTER-PROTOCOL.json'], text=True).split('\0')
    return {n: sha(ROOT/n) for n in sorted(set(filter(None, names)))}


def dependencies(cargo):
    raw = subprocess.check_output([str(cargo), 'metadata', '--locked', '--offline', '--format-version', '1',
        '--manifest-path', str(CRATE/'Cargo.toml'), '--features', 'esp32p4,copy', '--filter-platform', TARGET], cwd=CRATE, timeout=60)
    metadata = strict_json(raw)
    bound = {}
    for package in metadata['packages']:
        if package['source'] is not None:
            folder = Path(package['manifest_path']).parent
            for path in sorted(folder.rglob('*')):
                if path.is_file():
                    require(not path.is_symlink(), 'Dependency source symlink')
                    bound[str(path)] = sha(path)
    return metadata, bound


def inventory(folder):
    return {str(p.relative_to(folder)): dict(bytes=p.stat().st_size, sha256=sha(p))
            for p in sorted(folder.rglob('*')) if p.is_file()}


def verify(folder):
    manifest_path = folder/'package.json'
    require(manifest_path.stat().st_size <= 1048576, 'Package manifest too large')
    m = strict_json(manifest_path.read_text())
    require(type(m) is dict and m.get('format') == 'libapta-target-probe-package-1' and m.get('status') == 'BUILD_ONLY_COMPLETE', 'Invalid/incomplete package format')
    require(m['kind'] == 'BUILD_ONLY_LINKCHECK' and m['execution_ready'] is False and m['qualification'] == 'NOT_QUALIFIED', 'Unsafe package readiness claim')
    require(m['target'] == TARGET and m['minimum_chip_policy'] == 302, 'Target/policy mismatch')
    require(list(m['variants']) == sorted(VARIANTS), 'Missing/unexpected variants')
    actual = inventory(folder)
    require(set(actual) == set(m['files']) | {'package.json'}, 'Missing/unexpected package files')
    for name, identity in m['files'].items():
        require(not Path(name).is_absolute() and '..' not in Path(name).parts, 'Nonlocal artifact path')
        path = folder/name
        require(not path.is_symlink() and actual[name] == identity, 'Artifact hash/size changed: ' + name)
    require(sum(v['bytes'] for v in actual.values()) <= MAX_PACKAGE, 'Package byte cap exceeded')
    for name, info in m['variants'].items():
        require(info['features'] == 'esp32p4,'+name and info['buffer_payload_bytes'] == VARIANTS[name], 'Variant feature/payload mismatch')
        raw = (folder/name/'firmware.elf').read_bytes()
        sections = elf_sections(raw)
        require(info['sections'] == sections, 'Section metadata mismatch')
        require(info['image_id'].encode() in raw, 'ELF image label missing')
        require(m['source_revision'].encode() in raw and m['protocol_sha256'].encode() in raw, 'ELF source/protocol identity missing')
        require(sum(s['bytes'] for n, s in sections.items() if '.bss' in n) >= VARIANTS[name], 'Static caller buffers not retained')
        require('retained_trial' in (folder/name/'symbols.txt').read_text(), 'Probe link anchor absent')
        template = strict_json((folder/name/'capture-template.json').read_text())
        require(template['execution_ready'] is False and template['unit_id'] is None and template['chip_revision'] is None, 'Template must retain unknown unit/revision')
        require(template['elf_sha256'] == sha(folder/name/'firmware.elf'), 'Template image mismatch')
        require(template['source_revision'] == m['source_revision'] and template['protocol_sha256'] == m['protocol_sha256'], 'Template source/protocol mismatch')
    return m


def build(output, target_dir, cargo='cargo', rustc='rustc', development=False):
    require(sys.platform == 'linux', 'Build requires Linux/WSL')
    support.external(output, ROOT)
    support.external(target_dir, ROOT)
    require(not output.exists() and not target_dir.exists(), 'Output/build directory must be new')
    require(not output.resolve().is_relative_to(target_dir.resolve()) and not target_dir.resolve().is_relative_to(output.resolve()), 'Build/output directories overlap')
    revision = support.git(ROOT, 'rev-parse', 'HEAD')
    dirty = support.git(ROOT, 'status', '--porcelain')
    if not development:
        support.check_source(ROOT, revision)
    for name in ['RUSTFLAGS', 'CARGO_ENCODED_RUSTFLAGS', 'RUSTC_WRAPPER', 'RUSTC_WORKSPACE_WRAPPER']:
        require(not os.environ.get(name), 'Inherited build override not allowed: '+name)
    require(os.environ.get('ESP_HAL_CONFIG_MIN_CHIP_REVISION', '302') == '302', 'Inherited revision policy differs')
    tools = dict(cargo=tool(cargo), rustc=tool(rustc), readelf=tool('readelf'), objdump=tool('llvm-objdump-18'))
    compiler = subprocess.check_output([str(tools['rustc']), '-vV'], text=True)
    require('\nrelease: 1.95.0\n' in compiler, 'Pinned Rust1.95.0 required')
    sources = snapshot()
    metadata, dependency_bindings = dependencies(tools['cargo'])
    require(any(n.endswith('target-adapter/Cargo.lock') for n in sources), 'Committed/resolved adapter lockfile required')
    output.mkdir(parents=True)
    target_dir.mkdir(parents=True)
    support.write_new(output/'cargo-metadata.json', metadata)
    support.write_new(output/'dependency-bindings.json', dependency_bindings)
    sysroot = Path(subprocess.check_output([str(tools['rustc']), '--print', 'sysroot'], text=True).strip())
    package = dict(format='libapta-target-probe-package-1', kind='BUILD_ONLY_LINKCHECK', execution_ready=False,
                   qualification='NOT_QUALIFIED', status='BUILDING', source_revision=revision, development=development,
                   dirty_status=dirty, source_sha256=sources, compiler=compiler, target=TARGET, minimum_chip_policy=302,
                   protocol_sha256=sha(ROOT/'docs/rust/PRODUCT-TARGET-ADAPTER-PROTOCOL.json'),
                   tools={n: dict(path=str(p), sha256=sha(p.resolve())) for n, p in tools.items()}, variants={},
                   resolved_toolchain={n: dict(path=str(sysroot/'bin'/n),sha256=sha(sysroot/'bin'/n)) for n in ['rustc','cargo']},
                   dependency_source_files=len(dependency_bindings),
                   missing_capabilities=['exact unit/recovery/programming layout', 'qualified producer/control transport and operator arming',
                       'reusable physical warmup/trial scheduler', 'numeric output verification/digests',
                       'resident BSP/allocator/stack telemetry', 'asynchronous cancel requester and physical timing/placement'])
    support.write_new(output/'build-start.json', package)
    original_env = dict(os.environ)
    try:
        seed = hashlib.sha256(json.dumps([revision, sources, compiler], sort_keys=True).encode()).hexdigest()
        for name, payload in VARIANTS.items():
            folder = output/name
            folder.mkdir()
            image_id = 'apta-'+revision[:12]+'-'+seed[:12]+'-'+name
            flags = ['-C', 'link-arg=-Tlinkall.x', '-C', 'force-frame-pointers', '-C', 'link-arg=-Map='+str(target_dir/'link.map')]
            os.environ['CARGO_ENCODED_RUSTFLAGS'] = '\x1f'.join(flags)
            os.environ['ESP_HAL_CONFIG_MIN_CHIP_REVISION'] = '302'
            os.environ['APTA_PROBE_IMAGE_ID'] = image_id
            os.environ['APTA_PROBE_SOURCE_REVISION'] = revision
            os.environ['APTA_PROBE_PROTOCOL_SHA256'] = sha(ROOT/'docs/rust/PRODUCT-TARGET-ADAPTER-PROTOCOL.json')
            command = [str(tools['cargo']), 'build', '--locked', '--offline', '--release', '--target', TARGET,
                       '--manifest-path', str(CRATE/'Cargo.toml'), '--target-dir', str(target_dir.resolve()),
                       '--features', 'esp32p4,'+name, '--bin', 'apta-probe-linkcheck']
            # The package tool runs from CRATE so its pinned .cargo configuration
            # participates. Source identity is checked after each bounded build.
            previous_cwd = Path.cwd()
            try:
                os.chdir(CRATE)
                resource = support.supervise(command, 180, 2097152, MAX_PACKAGE, [output], folder/'build')
            finally:
                os.chdir(previous_cwd)
            require(resource['status'] == 'PASS' and resource['failure'] is None, 'Build supervision refused')
            require(snapshot() == sources and support.git(ROOT, 'rev-parse', 'HEAD') == revision, 'Build source changed')
            support.recheck(dependency_bindings)
            elf = target_dir/TARGET/'release/apta-probe-linkcheck'
            with elf.open('rb') as src, (folder/'firmware.elf').open('xb') as dst:
                shutil.copyfileobj(src, dst)
            with (target_dir/'link.map').open('rb') as src, (folder/'link.map').open('xb') as dst:
                shutil.copyfileobj(src, dst)
            for file, command in [('headers.txt', [str(tools['readelf']), '--wide', '--file-header', '--program-headers', '--section-headers', str(elf)]),
                                  ('symbols.txt', [str(tools['readelf']), '--wide', '--symbols', str(elf)]),
                                  ('disassembly.txt', [str(tools['objdump']), '-d', '--demangle', str(elf)])]:
                with (folder/file).open('xb') as log:
                    subprocess.run(command, stdout=log, stderr=subprocess.STDOUT, check=True, timeout=60)
            sections = elf_sections((folder/'firmware.elf').read_bytes())
            require(image_id.encode() in (folder/'firmware.elf').read_bytes(), 'Missing embedded image identity')
            info = dict(image_id=image_id, features='esp32p4,'+name, buffer_payload_bytes=payload, sections=sections,
                        command=command, build_environment=dict(encoded_rustflag_arguments=flags, ESP_HAL_CONFIG_MIN_CHIP_REVISION='302', APTA_PROBE_IMAGE_ID=image_id, APTA_PROBE_SOURCE_REVISION=revision, APTA_PROBE_PROTOCOL_SHA256=os.environ['APTA_PROBE_PROTOCOL_SHA256']))
            package['variants'][name] = info
            support.write_new(folder/'capture-template.json', dict(format='libapta-target-probe-capture-template-1', execution_ready=False,
                image_id=image_id, source_revision=revision, protocol_sha256=package['protocol_sha256'], elf_sha256=sha(folder/'firmware.elf'), unit_id=None, chip_revision=None,
                clock_hz_nominal=1000000, counter_bits_interface=64, trial_schedule=None, operator_arming_transport=None,
                warning='Template is not an ingest manifest or physical observation;resolve all missing capabilities before execution'))
            require(support.tree_bytes([output]) <= MAX_PACKAGE, 'Package byte cap exceeded')
            print('LINKCHECK '+name+' PASS', flush=True)
        if not development:
            support.check_source(ROOT, revision)
        package['status'] = 'BUILD_ONLY_COMPLETE'
        package['files'] = inventory(output)
        support.write_new(output/'package.json', package)
        verify(output)
    except BaseException as exc:
        support.write_new(output/'build-failure.json', dict(status='BUILD_FAILED_OR_INCOMPLETE', error=str(exc), completed_variants=list(package['variants']), execution_ready=False, qualification='NOT_QUALIFIED'))
        raise
    finally:
        os.environ.clear()
        os.environ.update(original_env)
    return package


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest='command', required=True)
    cmd = sub.add_parser('build')
    cmd.add_argument('--output', type=Path, required=True)
    cmd.add_argument('--target-dir', type=Path, required=True)
    cmd.add_argument('--cargo', default='cargo')
    cmd.add_argument('--rustc', default='rustc')
    cmd.add_argument('--development', action='store_true')
    cmd = sub.add_parser('verify')
    cmd.add_argument('--package', type=Path, required=True)
    args = parser.parse_args()
    try:
        result = verify(args.package.resolve()) if args.command == 'verify' else build(args.output.resolve(), args.target_dir.resolve(), args.cargo, args.rustc, args.development)
        print(json.dumps(dict(status=result['status'], kind=result['kind'], execution_ready=False, qualification='NOT_QUALIFIED')))
        return 0
    except (ValueError, KeyError, TypeError, RecursionError, OSError, subprocess.SubprocessError) as exc:
        parser.exit(1, 'ERROR: '+str(exc)+'\n')


if __name__ == '__main__':
    sys.exit(main())
