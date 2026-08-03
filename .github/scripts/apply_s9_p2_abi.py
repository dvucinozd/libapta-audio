#!/usr/bin/env python3
from __future__ import annotations

import re
import shutil
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


def write(path: str, content: str) -> None:
    target = ROOT / path
    target.parent.mkdir(parents=True, exist_ok=True)
    if not content.endswith("\n"):
        content += "\n"
    target.write_text(content, encoding="utf-8")


def replace_once(path: str, old: str, new: str) -> None:
    text = read(path)
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{path}: expected one anchor, found {count}: {old[:100]!r}")
    write(path, text.replace(old, new, 1))


# ---------------------------------------------------------------------------
# Shared-library visibility and SONAME policy.
# ---------------------------------------------------------------------------
replace_once(
    "include/apta/apta_types.h",
    "#if defined(_WIN32) && defined(APTA_SHARED)\n",
    "#if defined(APTA_INTERNAL_LAYER)\n#  define APTA_API\n#elif defined(_WIN32) && defined(APTA_SHARED)\n")

replace_once(
    "CMakeLists.txt",
    "add_library(\n    apta_core\n    STATIC\n",
    "add_library(\n    apta_core\n")

replace_once(
    "CMakeLists.txt",
    """set_target_properties(
    apta_core
    PROPERTIES
        OUTPUT_NAME apta
        C_STANDARD 11
        C_STANDARD_REQUIRED YES
        C_EXTENSIONS NO)
""",
    """set_target_properties(
    apta_core
    PROPERTIES
        OUTPUT_NAME apta
        VERSION ${PROJECT_VERSION}
        SOVERSION ${PROJECT_VERSION_MAJOR}
        C_STANDARD 11
        C_STANDARD_REQUIRED YES
        C_EXTENSIONS NO
        C_VISIBILITY_PRESET hidden
        VISIBILITY_INLINES_HIDDEN YES)
""")

replace_once(
    "CMakeLists.txt",
    "target_compile_definitions(apta_core PRIVATE APTA_BUILDING_LIBRARY=1)\n",
    """target_compile_definitions(apta_core PRIVATE APTA_BUILDING_LIBRARY=1)
if(BUILD_SHARED_LIBS)
    target_compile_definitions(apta_core PUBLIC APTA_SHARED=1)
endif()
""")

# Every source-specific symbol rename creates an implementation layer rather
# than a public ABI entry. Suppress APTA_API while compiling those translation
# units so renamed `_base`, `_wovr`, `_s4`, and `_s6` symbols remain hidden.
cmake_path = ROOT / "CMakeLists.txt"
cmake = cmake_path.read_text(encoding="utf-8")
pattern = re.compile(
    r'(set_source_files_properties\(\n(?:.|\n)*?PROPERTIES\n\s+COMPILE_DEFINITIONS ")([^"]+)("\))',
    re.MULTILINE)

def mark_internal(match: re.Match[str]) -> str:
    definitions = match.group(2)
    if "=apta_" not in definitions:
        return match.group(0)
    if definitions.startswith("APTA_INTERNAL_LAYER=1;"):
        return match.group(0)
    return match.group(1) + "APTA_INTERNAL_LAYER=1;" + definitions + match.group(3)

cmake, changed = pattern.subn(mark_internal, cmake)
if changed == 0:
    raise SystemExit("CMakeLists.txt: no renamed implementation layers found")
cmake_path.write_text(cmake, encoding="utf-8")

# ---------------------------------------------------------------------------
# Frozen 1.0 public headers and old-header/new-library client.
# ---------------------------------------------------------------------------
snapshot_root = ROOT / "tests/compat/1.0.0/include/apta"
if snapshot_root.exists():
    shutil.rmtree(snapshot_root)
snapshot_root.mkdir(parents=True)
for header in sorted((ROOT / "include/apta").glob("*.h")):
    shutil.copy2(header, snapshot_root / header.name)

write(
    "tests/compat/1.0.0/README.md",
    """# APTA API 1.0 frozen header snapshot

These headers are the public API 1.0 baseline used by the old-header/new-library
compatibility client. They are intentionally copied, not symlinked. A live
header change therefore fails the snapshot gate until compatibility impact and
versioning are reviewed explicitly.
""")

write(
    "tests/compat/1.0.0/client.c",
    r'''// SPDX-License-Identifier: Apache-2.0
#include <stdint.h>
#include <string.h>

#include <apta/apta.h>

#define REQUIRE(condition) do { if (!(condition)) return __LINE__; } while (0)

int main(void)
{
    apta_context_config_t context_config;
    apta_session_config_t session_config;
    apta_context_t *context = NULL;
    apta_session_t *session = NULL;
    const apta_result_t *result = NULL;
    apta_result_info_t result_info;
    apta_source_info_t source_info;

    apta_context_config_init(&context_config);
    context_config.api_version = APTA_API_VERSION_ENCODE(1u, 0u, 123u);
    REQUIRE(apta_context_create(&context_config, &context) == APTA_STATUS_OK);

    apta_session_config_init(&session_config);
    session_config.source_sample_rate = 48000u;
    session_config.channel_count = 1u;
    session_config.sample_format = APTA_SAMPLE_S16_NATIVE_INTERLEAVED;
    session_config.channel_layout = APTA_CHANNEL_LAYOUT_MONO;
    session_config.total_frames = 1024u;
    session_config.source_fingerprint_kind =
        APTA_SOURCE_FINGERPRINT_APPLICATION_OPAQUE_256;
    session_config.source_fingerprint[0] = 0x10u;
    session_config.source_fingerprint[31] = 0x01u;
    REQUIRE(apta_session_create(context, &session_config, &session) ==
            APTA_STATUS_OK);

    result = apta_session_acquire_result(session);
    REQUIRE(result != NULL);

    apta_result_info_init(&result_info);
    result_info.api_version = APTA_API_VERSION_ENCODE(1u, 0u, 999u);
    REQUIRE(apta_result_get_info(result, &result_info) == APTA_STATUS_OK);
    REQUIRE(result_info.producer_api_version == APTA_API_VERSION);

    apta_source_info_init(&source_info);
    source_info.api_version = APTA_API_VERSION_ENCODE(1u, 0u, 7u);
    REQUIRE(apta_result_get_source_info(result, &source_info) == APTA_STATUS_OK);
    REQUIRE(source_info.sample_rate == 48000u);
    REQUIRE(source_info.channel_count == 1u);
    REQUIRE(source_info.fingerprint_kind ==
            APTA_SOURCE_FINGERPRINT_APPLICATION_OPAQUE_256);
    REQUIRE(source_info.fingerprint[0] == 0x10u);
    REQUIRE(source_info.fingerprint[31] == 0x01u);

    apta_result_release(result);
    REQUIRE(apta_session_destroy(session) == APTA_STATUS_OK);
    REQUIRE(apta_context_destroy(context) == APTA_STATUS_OK);
    return 0;
}
''')

write(
    "tests/abi/check_header_snapshot.py",
    r'''#!/usr/bin/env python3
from __future__ import annotations

import argparse
from pathlib import Path


def files(root: Path) -> dict[str, bytes]:
    return {
        str(path.relative_to(root)): path.read_bytes()
        for path in sorted(root.rglob("*.h"))
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--live", type=Path, required=True)
    parser.add_argument("--snapshot", type=Path, required=True)
    args = parser.parse_args()

    live = files(args.live)
    snapshot = files(args.snapshot)
    if live == snapshot:
        return 0

    for name in sorted(set(live) | set(snapshot)):
        if live.get(name) != snapshot.get(name):
            print(f"public header snapshot drift: {name}")
    return 1


if __name__ == "__main__":
    raise SystemExit(main())
''')

# ---------------------------------------------------------------------------
# Public symbol manifest generated from public declarations.
# ---------------------------------------------------------------------------
headers_text = "\n".join(
    path.read_text(encoding="utf-8")
    for path in sorted((ROOT / "include/apta").glob("*.h")))
public_symbols: set[str] = set()
for statement in headers_text.split(";"):
    if "APTA_API" not in statement:
        continue
    names = re.findall(r"\b(apta_[A-Za-z0-9_]+)\s*\(", statement)
    if names:
        public_symbols.add(names[-1])
if not public_symbols:
    raise SystemExit("no public APTA_API symbols discovered")
write("abi/public-symbols-1.0.txt", "\n".join(sorted(public_symbols)))

write(
    "tests/abi/check_symbols.py",
    r'''#!/usr/bin/env python3
from __future__ import annotations

import argparse
import re
import shutil
import subprocess
from pathlib import Path


def run(command: list[str]) -> str:
    return subprocess.check_output(command, text=True, errors="replace")


def elf_symbols(library: Path) -> set[str]:
    output = run(["nm", "-D", "--defined-only", str(library)])
    symbols = set()
    for line in output.splitlines():
        fields = line.split()
        if fields:
            symbols.add(fields[-1].split("@", 1)[0])
    return symbols


def pe_symbols(library: Path) -> set[str]:
    dumpbin = shutil.which("dumpbin") or shutil.which("dumpbin.exe")
    if dumpbin:
        output = run([dumpbin, "/exports", str(library)])
        symbols = set()
        for line in output.splitlines():
            match = re.match(r"\s*\d+\s+[0-9A-Fa-f]+\s+[0-9A-Fa-f]+\s+(\S+)", line)
            if match:
                symbols.add(match.group(1))
        return symbols

    llvm = shutil.which("llvm-readobj") or shutil.which("llvm-readobj.exe")
    if llvm:
        output = run([llvm, "--coff-exports", str(library)])
        return set(re.findall(r"Name: (apta_[A-Za-z0-9_]+)", output))
    raise RuntimeError("neither dumpbin nor llvm-readobj is available")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--library", type=Path, required=True)
    parser.add_argument("--manifest", type=Path, required=True)
    args = parser.parse_args()

    expected = {
        line.strip()
        for line in args.manifest.read_text(encoding="utf-8").splitlines()
        if line.strip() and not line.startswith("#")
    }
    discovered = pe_symbols(args.library) if args.library.suffix.lower() == ".dll" else elf_symbols(args.library)
    actual = {symbol for symbol in discovered if symbol.startswith("apta_")}

    missing = sorted(expected - actual)
    unexpected = sorted(actual - expected)
    if not missing and not unexpected:
        return 0
    for symbol in missing:
        print(f"missing public symbol: {symbol}")
    for symbol in unexpected:
        print(f"unexpected exported symbol: {symbol}")
    return 1


if __name__ == "__main__":
    raise SystemExit(main())
''')

# ---------------------------------------------------------------------------
# Public structure layout probe generated from all public typedef structs.
# ---------------------------------------------------------------------------
def strip_comments(text: str) -> str:
    text = re.sub(r"/\*.*?\*/", "", text, flags=re.DOTALL)
    text = re.sub(r"//.*", "", text)
    return text


def field_names(body: str) -> list[str]:
    names: list[str] = []
    for statement in body.split(";"):
        statement = " ".join(statement.split())
        if not statement or statement.startswith("#"):
            continue
        function_pointer = re.search(
            r"\(\s*(?:APTA_CALL\s*)?\*\s*([A-Za-z_]\w*)\s*\)",
            statement)
        if function_pointer:
            names.append(function_pointer.group(1))
            continue
        regular = re.search(
            r"([A-Za-z_]\w*)\s*(?:\[[^\]]+\])?\s*$",
            statement)
        if regular:
            names.append(regular.group(1))
            continue
        raise SystemExit(f"cannot parse public field declaration: {statement!r}")
    return names

structs: list[tuple[str, list[str]]] = []
for header in sorted((ROOT / "include/apta").glob("*.h")):
    text = strip_comments(header.read_text(encoding="utf-8"))
    for match in re.finditer(
        r"typedef\s+struct\s*\{(.*?)\}\s*(apta_[A-Za-z0-9_]+_t)\s*;",
        text,
        flags=re.DOTALL):
        name = match.group(2)
        fields = field_names(match.group(1))
        structs.append((name, fields))
if not structs:
    raise SystemExit("no public struct layouts discovered")

lines = [
    "// SPDX-License-Identifier: Apache-2.0",
    "#include <stddef.h>",
    "#include <stdio.h>",
    "#include <apta/apta.h>",
    "",
    "#define PRINT_TYPE(type_name) \\",
    "    printf(\"type\\t%s\\t%zu\\t%zu\\n\", #type_name, sizeof(type_name), _Alignof(type_name))",
    "#define PRINT_FIELD(type_name, field_name) \\",
    "    printf(\"field\\t%s\\t%s\\t%zu\\n\", #type_name, #field_name, offsetof(type_name, field_name))",
    "",
    "int main(void)",
    "{",
]
for name, fields in structs:
    lines.append(f"    PRINT_TYPE({name});")
    for field in fields:
        lines.append(f"    PRINT_FIELD({name}, {field});")
lines += ["    return 0;", "}", ""]
write("tests/abi/layout_manifest.c", "\n".join(lines))

write(
    "tests/abi/check_layout_manifest.py",
    r'''#!/usr/bin/env python3
from __future__ import annotations

import argparse
import subprocess
from pathlib import Path


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--probe", type=Path, required=True)
    parser.add_argument("--manifest", type=Path, required=True)
    args = parser.parse_args()

    actual = subprocess.check_output([str(args.probe)], text=True).replace("\r\n", "\n")
    expected = args.manifest.read_text(encoding="utf-8").replace("\r\n", "\n")
    if actual == expected:
        return 0

    print(f"layout manifest mismatch: {args.manifest}")
    print("--- ACTUAL LAYOUT BEGIN ---")
    print(actual, end="" if actual.endswith("\n") else "\n")
    print("--- ACTUAL LAYOUT END ---")
    return 1


if __name__ == "__main__":
    raise SystemExit(main())
''')

# Generate authoritative Linux data-model manifests directly with the host C
# compiler. The bootstrap job installs multilib before invoking this script.
probe_source = ROOT / "tests/abi/layout_manifest.c"
for model, flags in (("lp64", []), ("ilp32", ["-m32"])):
    executable = ROOT / f".p2-layout-{model}"
    command = [
        "cc",
        "-std=c11",
        "-I",
        str(ROOT / "include"),
        *flags,
        str(probe_source),
        "-o",
        str(executable),
    ]
    subprocess.run(command, check=True)
    output = subprocess.check_output([str(executable)], text=True)
    write(f"abi/public-layout-{model}.txt", output)
    executable.unlink()
write(
    "abi/public-layout-llp64.txt",
    "# PENDING LLP64 MANIFEST -- Windows CI prints the authoritative layout.\n")

# ---------------------------------------------------------------------------
# CMake/CTest integration.
# ---------------------------------------------------------------------------
append = r'''

# Stage S9 P2 public API/ABI freeze gates.
find_package(Python3 COMPONENTS Interpreter REQUIRED)

add_executable(apta_abi_layout_manifest abi/layout_manifest.c)
target_link_libraries(apta_abi_layout_manifest PRIVATE apta::headers)
set_target_properties(
    apta_abi_layout_manifest
    PROPERTIES
        C_STANDARD 11
        C_STANDARD_REQUIRED YES
        C_EXTENSIONS NO)

if(WIN32)
    set(APTA_ABI_LAYOUT_MODEL llp64)
elseif(CMAKE_SIZEOF_VOID_P EQUAL 4)
    set(APTA_ABI_LAYOUT_MODEL ilp32)
else()
    set(APTA_ABI_LAYOUT_MODEL lp64)
endif()

add_test(
    NAME apta.abi.public_layout
    COMMAND
        ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/abi/check_layout_manifest.py
        --probe $<TARGET_FILE:apta_abi_layout_manifest>
        --manifest
        ${CMAKE_SOURCE_DIR}/abi/public-layout-${APTA_ABI_LAYOUT_MODEL}.txt)

add_test(
    NAME apta.abi.header_snapshot
    COMMAND
        ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/abi/check_header_snapshot.py
        --live ${CMAKE_SOURCE_DIR}/include/apta
        --snapshot ${CMAKE_CURRENT_SOURCE_DIR}/compat/1.0.0/include/apta)

add_executable(apta_compat_1_0_client compat/1.0.0/client.c)
target_include_directories(
    apta_compat_1_0_client
    BEFORE
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/compat/1.0.0/include)
target_link_libraries(apta_compat_1_0_client PRIVATE apta::core)
set_target_properties(
    apta_compat_1_0_client
    PROPERTIES
        C_STANDARD 11
        C_STANDARD_REQUIRED YES
        C_EXTENSIONS NO)
add_test(NAME apta.abi.old_header_new_library COMMAND apta_compat_1_0_client)

if(BUILD_SHARED_LIBS)
    add_test(
        NAME apta.abi.public_symbols
        COMMAND
            ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/abi/check_symbols.py
            --library $<TARGET_FILE:apta_core>
            --manifest ${CMAKE_SOURCE_DIR}/abi/public-symbols-1.0.txt)
endif()
'''
tests_cmake = ROOT / "tests/CMakeLists.txt"
text = tests_cmake.read_text(encoding="utf-8")
if "# Stage S9 P2 public API/ABI freeze gates." in text:
    raise SystemExit("tests/CMakeLists.txt already contains P2 ABI block")
tests_cmake.write_text(text.rstrip() + append + "\n", encoding="utf-8")

# Update status document without claiming LLP64 completion yet.
status = read("docs/status/S9-P2-API-ABI-STATUS.md")
status = status.replace(
    "- checked-in 1.0 public-header snapshot and old-header/new-library client;\n- LP64, ILP32 and LLP64 public layout manifests;\n- public symbol manifest and shared-library export checks;",
    "- checked-in 1.0 public-header snapshot and old-header/new-library client (implemented);\n- LP64 and ILP32 public layout manifests (implemented); LLP64 capture pending Windows CI;\n- public symbol manifest and ELF/PE shared-library export checks (implemented, validation pending);")
write("docs/status/S9-P2-API-ABI-STATUS.md", status)

# Restore normal CI and remove the bootstrap source. The running job has already
# loaded this workflow definition.
ci_path = ROOT / ".github/workflows/ci.yml"
ci = ci_path.read_text(encoding="utf-8")
ci = ci.replace("permissions:\n  contents: write\n", "permissions:\n  contents: read\n", 1)
start = ci.find("\n  p2-abi-bootstrap:\n")
end_marker = "\n  core-build:\n"
if start < 0:
    raise SystemExit("ci.yml: P2 ABI bootstrap block not found")
end = ci.find(end_marker, start)
if end < 0:
    raise SystemExit("ci.yml: core-build anchor missing")
ci = ci[:start] + end_marker + ci[end + len(end_marker):]
ci_path.write_text(ci, encoding="utf-8")

Path(__file__).unlink()
