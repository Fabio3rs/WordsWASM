#!/usr/bin/env python3
"""Read-only WWDB inspection and isolated C++ layout simulations (no engine changes)."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct
import subprocess
import tempfile
import zlib

ROOT = Path(__file__).resolve().parents[1]


def inspect(path):
    data = path.read_bytes()
    assert data[:8] == b"WWDB\r\n\x1a\n"
    major, minor, header, count, profile, size, crc, reserved = struct.unpack_from(
        "<HHIIIQII", data, 8)
    assert header == 40 and size == len(data) and reserved == 0
    start = header + count * 32
    assert zlib.crc32(data[start:]) == crc
    sections = {}
    cursor = start
    for i in range(count):
        kind, flags, offset, length, rows, stride = struct.unpack_from(
            "<IIQQII", data, header + i * 32)
        assert offset == cursor, "unexpected inter-section padding"
        cursor += length
        sections[kind] = (flags, offset, length, rows, stride)
    assert cursor == len(data)
    flags, offset, length, rows, stride = sections[4]
    assert flags in (1, 2) and stride in (14, 16), "requires dense/search layout"
    assert length == rows * stride
    metadata_offset = stride - 6
    verbs = []
    for i in range(rows):
        positions = [offset + (i * stride + field if flags == 1 else field * rows + i)
                     for field in range(metadata_offset, stride)]
        meta = int.from_bytes(bytes(data[p] for p in positions), "little")
        assert meta >> 47 == 0
        if meta & 15 == 7:
            assert (meta >> 38) == 0
            verbs.append(positions)
    # Stress all verb records: consume all nine unused class bits, preserving
    # the low nibble (VerbKind) and the global reserved bit 47.
    changed = bytearray(data)
    mask = ((1 << 9) - 1) << 38
    for positions in verbs:
        old = int.from_bytes(bytes(data[p] for p in positions), "little")
        new = old | mask
        assert new >> 47 == 0 and (new & ~mask) == old
        for pos, byte in zip(positions, new.to_bytes(6, "little")):
            changed[pos] = byte
    struct.pack_into("<I", changed, 32, zlib.crc32(changed[start:]))
    assert len(changed) == len(data)
    # This is a byte-layout simulation, deliberately NOT an image accepted
    # by the existing loader: those nine bits are currently reserved.
    notices = sections.get(24, (0, 0, 0, 0, 3))
    return dict(path=str(path.relative_to(ROOT)), sha256=hashlib.sha256(data).hexdigest(),
                version=f"{major}.{minor}", profile=profile, bytes=len(data),
                sections=count, inter_section_padding=0, lexemes=rows,
                lexeme_stride=stride, verbs=len(verbs),
                notice_rows=notices[3], notice_bytes=notices[2],
                nine_flags_simulated_growth=len(changed) - len(data),
                extra_byte_per_lexeme_growth=rows,
                extra_bit_column_growth=(rows + 7) // 8,
                extra_two_bit_column_growth=(rows * 2 + 7) // 8)


def cpp_layout():
    header = (ROOT / "include/words/database.hpp").read_text()
    original = re.search(r"struct LexemeRecord final \{.*?\n\};", header, re.S)[0]
    variants = []
    for name, field, where in [
        ("OneAtEnd", "std::uint8_t flags{};", "end"),
        ("TwoAtEnd", "std::uint16_t flags{};", "end"),
        ("OneByKind", "std::uint8_t flags{};", "kind"),
        ("TwoByKind", "std::uint16_t flags{};", "kind"),
        ("OneInGap", "std::uint8_t flags{};", "gap"),
        ("TwoInGap", "std::uint16_t flags{};", "gap"),
    ]:
        decl = original.replace("LexemeRecord", name)
        if where == "end":
            decl = decl.replace("\n};", f"\n    {field}\n}};")
        elif where == "kind":
            decl = decl.replace("VerbKind verb_kind{VerbKind::unknown};",
                                f"VerbKind verb_kind{{VerbKind::unknown}};\n    {field}")
        else:
            decl = decl.replace("DictionaryKind dictionary{DictionaryKind::general};",
                                f"DictionaryKind dictionary{{DictionaryKind::general}};\n    {field}")
        variants.append(decl)
    code = '''#include "words/database.hpp"
#include "whitakers-words/src/legacy_data_layout.h"
#include <iostream>
using namespace words;
''' + "\n".join(variants) + '''
template <int N> struct LegacyVerb {
    ww_legacy_decn_record con;
    std::uint8_t kind;
    std::uint8_t flags[N];
};
struct Notice { std::uint32_t key; MorphologicalNoticeSet notices; };
int main() {
#define S(T) std::cout << #T << "=" << sizeof(T) << "\\n";
#define O(T, F) std::cout << #T "." #F << "=" << offsetof(T,F) << "\\n";
S(VerbKind) S(WhitakerTrimReason) S(MorphologicalNoticeSet)
S(LexemeRecord) S(OneAtEnd) S(TwoAtEnd) S(OneByKind) S(TwoByKind)
S(OneInGap) S(TwoInGap) O(OneInGap, flags) O(TwoInGap, flags)
O(LexemeRecord, verb_kind) O(LexemeRecord, source)
O(OneAtEnd, flags) O(TwoAtEnd, flags) O(OneByKind, flags) O(TwoByKind, flags)
S(Notice) S(ww_legacy_dictionary_verb_entry)
S(ww_legacy_dictionary_part_payload) S(ww_legacy_dictionary_entry)
S(ww_legacy_dictionary_stem) S(LegacyVerb<1>) S(LegacyVerb<3>)
S(LegacyVerb<4>) S(LegacyVerb<7>) S(LegacyVerb<8>)
}
'''
    with tempfile.TemporaryDirectory(prefix="deponent-layout-") as tmp:
        source = Path(tmp) / "probe.cpp"
        binary = Path(tmp) / "probe"
        source.write_text(code)
        subprocess.run(["g++", "-std=c++23", "-I", str(ROOT / "include"),
                        "-I", str(ROOT), str(source), "-o", str(binary)], check=True)
        output = subprocess.check_output([str(binary)], text=True)
    return dict(line.split("=") for line in output.splitlines())


def ada_layout():
    directory = ROOT / "whitakers-words/src/latin_utils"
    dictionary = (directory / "latin_utils-dictionary_package.ads").read_text()
    inflections = (directory / "latin_utils-inflections_package.ads").read_text()
    result = {}
    for name, extra_values, flag_bytes in [("baseline", 0, 0), ("enum16", 4, 0),
                                          ("enum17", 5, 0), ("flags1", 0, 1),
                                          ("flags3", 0, 3), ("flags4", 0, 4)]:
        with tempfile.TemporaryDirectory(prefix="deponent-ada-") as tmp:
            spec = dictionary
            enums = inflections
            if extra_values:
                enums = enums.replace("Perfdef    --", "Perfdef, " + ", ".join(
                    f"Extra_{i}" for i in range(extra_values)) + "    --", 1)
            if flag_bytes:
                spec = spec.replace("   type Verb_Entry is", "   type Flag_Byte is mod 256;\n"
                                    f"   type Flag_Array is array (1 .. {flag_bytes}) of Flag_Byte;\n"
                                    "   type Verb_Entry is", 1)
                begin = spec.index("   type Verb_Entry is")
                end = spec.index("      end record;", begin)
                spec = spec[:end] + "         Flags : Flag_Array := (others => 0);\n" + spec[end:]
            path = Path(tmp) / "latin_utils-dictionary_package.ads"
            path.write_text(spec)
            (Path(tmp) / "latin_utils-inflections_package.ads").write_text(enums)
            output = subprocess.run(["gcc", "-c", "-gnatc", "-gnatR3", "-I" + str(directory),
                                     "-I" + str(directory.parent), str(path)], cwd=tmp,
                                    capture_output=True, text=True, check=True)
            report = output.stdout + output.stderr
            result[name] = {t: int(re.search(r"for " + t + r"'(?:Object_Size|Size) use (\d+);",
                                             report)[1]) // 8
                            for t in ("Verb_Entry", "Part_Entry", "Dictionary_Entry")}
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("images", nargs="*", type=Path)
    parser.add_argument("--ada", action="store_true", help="also probe actual Ada specs with GNAT")
    args = parser.parse_args()
    paths = args.images or [ROOT / "web/engine/words-full.wwdb",
                            ROOT / "web/engine/words-search.wwdb"]
    result = dict(images=[inspect(p.resolve()) for p in paths], cpp_sizes_and_offsets=cpp_layout())
    if args.ada:
        result["ada_object_bytes"] = ada_layout()
    print(json.dumps(result, indent=2))


if __name__ == "__main__":
    main()
