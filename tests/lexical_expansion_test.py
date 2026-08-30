#!/usr/bin/env python3

from __future__ import annotations

import importlib.util
import sqlite3
import sys
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
COMPACT_DB = ROOT / "whitakers-words/poc/compact-db"
sys.path.insert(0, str(COMPACT_DB))
SCRIPT = COMPACT_DB / "audit_lexical_expansion.py"
SPEC = importlib.util.spec_from_file_location("audit_lexical_expansion", SCRIPT)
if SPEC is None or SPEC.loader is None:
    raise RuntimeError("cannot load lexical expansion auditor")
AUDIT = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = AUDIT
SPEC.loader.exec_module(AUDIT)
QUANTITY = AUDIT.quantity


def dictionary_record(
    stems: list[str],
    part: int,
    gender: int,
    meaning: str,
    declension: int = 0,
    variant: int = 0,
) -> bytes:
    record = bytearray(b" " * 180)
    for index, stem in enumerate(stems):
        encoded = stem.encode("ascii")
        record[index * 18 : index * 18 + len(encoded)] = encoded
    record[72] = part
    record[76:80] = declension.to_bytes(4, "little")
    record[80:84] = variant.to_bytes(4, "little")
    record[84] = gender
    encoded_meaning = meaning.encode("ascii")[:80]
    record[97 : 97 + len(encoded_meaning)] = encoded_meaning
    return bytes(record)


def entry(
    source: str,
    identifier: str,
    lemma: str,
    part: str | None,
    family: str,
    independent: bool,
) -> QUANTITY.DictionaryEntry:
    return QUANTITY.DictionaryEntry(
        source,
        identifier,
        lemma,
        part,
        "n" if part == "NOUN" else None,
        lemma,
        family,
        independent,
    )


class LexicalExpansionTest(unittest.TestCase):
    def test_builds_only_reviewable_structural_drafts(self) -> None:
        candidate = {
            "ascii_lemma": "novus",
            "marked_lemmas": ["nŏvus"],
            "part_of_speech": "ADJ",
            "proper": False,
            "support": "corroborated_independent",
            "typed_independent_families": ["latin-german", "lewis"],
            "independent_families": ["latin-german", "lewis"],
            "proposed_words_paradigm": {
                "part_of_speech": "ADJ",
                "declension_or_conjugation": 1,
                "variant": 1,
            },
            "proposed_words_stems": [
                {"slot": 1, "stem": "nov"},
                {"slot": 2, "stem": "nov"},
            ],
            "stem_readiness": "generated_single",
            "genders": [],
            "witnesses": [],
            "latin_german_form_validation": {"status": "all_slots_attested"},
        }

        counts = AUDIT.attach_structural_drafts([candidate])

        self.assertEqual(counts["cross_validated"], 1)
        self.assertEqual(candidate["structural_draft_status"], "cross_validated")
        self.assertEqual(
            candidate["structural_draft"]["lexical"]["stems"][0]["stem"],
            "nov",
        )
        self.assertIn(
            "canonical_meaning", candidate["structural_draft"]["unresolved_fields"]
        )

    def test_reads_legacy_inflection_endings_and_applies_wildcard_variant(self) -> None:
        with tempfile.TemporaryDirectory() as name:
            path = Path(name) / "INFLECTS.SEC"
            record = bytearray(40)
            record[0] = 4  # adjective
            record[4:8] = (1).to_bytes(4, "little")
            record[8:12] = (0).to_bytes(4, "little")
            record[20:24] = (1).to_bytes(4, "little")
            record[24:28] = (2).to_bytes(4, "little")
            record[28:30] = b"us"
            path.write_bytes(record)
            rules = AUDIT.read_inflection_endings(path)

        endings = AUDIT.compatible_endings(
            rules,
            {
                "part_of_speech": "ADJ",
                "declension_or_conjugation": 1,
                "variant": 3,
            },
            1,
        )
        self.assertEqual(endings, frozenset({"us"}))

    def test_crosswalk_requires_repeated_unanimous_words_paradigm(self) -> None:
        exact = ("NOUN", 2, 1)
        other = ("NOUN", 3, 1)
        report, resolved = AUDIT.build_morphology_crosswalk(
            {
                "latin-german:205": AUDIT.Counter({exact: 4}),
                "collatinus:lupus": AUDIT.Counter({exact: 8, other: 1}),
                "collatinus:rare": AUDIT.Counter({exact: 2}),
            },
            AUDIT.Counter(),
            AUDIT.Counter(),
        )

        self.assertEqual(resolved, {"latin-german:205": exact})
        by_key = {item["morphology_key"]: item for item in report["mappings"]}
        self.assertEqual(by_key["latin-german:205"]["status"], "exact_empirical")
        self.assertEqual(by_key["collatinus:lupus"]["status"], "one_to_many")
        self.assertEqual(by_key["collatinus:rare"]["status"], "insufficient_support")

    def test_infers_a_complete_words_slot_template_from_external_radicals(self) -> None:
        template = AUDIT.infer_stem_template(
            {1: "ager", 2: "agr"}, ((1, "agr"), (2, "ager"))
        )
        self.assertEqual(template, ((1, 2), (2, 1)))
        report, resolved = AUDIT.build_stem_template_crosswalk(
            {"collatinus:ager": AUDIT.Counter({template: 5})}
        )
        self.assertEqual(resolved["collatinus:ager"], template)
        self.assertEqual(report["counts"]["exact_empirical"], 1)

    def test_reads_only_publishable_simple_faria_lexical_classes(self) -> None:
        with tempfile.TemporaryDirectory() as name:
            database = Path(name) / "faria.sqlite"
            connection = sqlite3.connect(database)
            connection.executescript(
                """
                create table entry(
                    entry_id text, entry_kind text, editorial_status text,
                    headword text, morphology_raw text, lexical_pos_norm text,
                    definition_raw text, sort_order integer
                );
                insert into entry values(
                    'noun', 'lexical_entry', 'publishable', 'mālum',
                    'subs. n.', 'NOUN', 'maçã', 1
                );
                insert into entry values(
                    'proper', 'lexical_entry', 'publishable', 'Rōma',
                    'subs. pr. f.', 'PROPER_NOUN', 'Roma', 2
                );
                insert into entry values(
                    'participle', 'lexical_entry', 'publishable', 'amātus',
                    'part. pass.', 'PARTICIPLE|VERBAL_FORM', 'amado', 3
                );
                insert into entry values(
                    'review', 'lexical_entry', 'needs_review', 'dūbium',
                    'subs. n.', 'NOUN', 'duvidoso', 4
                );
                """
            )
            connection.commit()
            connection.close()
            entries = tuple(AUDIT.read_faria_v3_entries(database))

        self.assertEqual(
            [item.source_entry_id for item in entries],
            ["noun", "proper", "participle"],
        )
        self.assertEqual(entries[0].part_of_speech, "NOUN")
        self.assertEqual(entries[0].gender, "n")
        self.assertEqual(entries[1].part_of_speech, "NOUN")
        self.assertIsNone(entries[2].part_of_speech)
        self.assertEqual(entries[0].source_family, "faria")

    def test_groups_coverage_and_independent_support_conservatively(self) -> None:
        with tempfile.TemporaryDirectory() as name:
            dictionary = Path(name) / "DICTFILE.GEN"
            dictionary.write_bytes(dictionary_record(["mal"], 1, 3, "apple"))
            entries = (
                entry("ls_dict", "apple", "mālum", "NOUN", "lewis", True),
                entry("ls_dict", "new", "nŏvum", "NOUN", "lewis", True),
                entry("gaffiot", "new-fr", "nŏvum", None, "gaffiot", True),
                entry(
                    "collatinus",
                    "new-derived",
                    "nŏvum",
                    "NOUN",
                    "collatinus-derived",
                    False,
                ),
                entry(
                    "latin_german",
                    "rare",
                    "rārum",
                    "NOUN",
                    "latin-german",
                    True,
                ),
                entry(
                    "collatinus",
                    "derived",
                    "dērīvum",
                    "NOUN",
                    "collatinus-derived",
                    False,
                ),
                entry("gaffiot", "orphan", "orphānum", None, "gaffiot", True),
            )
            report, candidates = AUDIT.audit(dictionary, entries)

        self.assertEqual(report["groups"]["total_typed"], 4)
        self.assertEqual(report["groups"]["covered"], 1)
        self.assertEqual(report["groups"]["structurally_unmatched"], 3)
        self.assertEqual(report["groups"]["structural_drafts"]["not_ready"], 3)
        self.assertEqual(
            report["groups"]["priority_queue"]["corroborated_independent_common"],
            1,
        )
        self.assertEqual(report["input"]["unknown_pos_attached_unambiguously"], 1)
        self.assertEqual(report["input"]["unknown_pos_unresolved"], 1)
        by_lemma = {candidate["ascii_lemma"]: candidate for candidate in candidates}
        self.assertEqual(by_lemma["novum"]["support"], "corroborated_independent")
        self.assertEqual(by_lemma["novum"]["independent_family_count"], 2)
        self.assertEqual(by_lemma["rarum"]["support"], "single_independent")
        self.assertEqual(by_lemma["derivum"]["support"], "derived_only")

    def test_shared_stem_reader_preserves_short_words_for_coverage(self) -> None:
        with tempfile.TemporaryDirectory() as name:
            dictionary = Path(name) / "DICTFILE.GEN"
            dictionary.write_bytes(
                dictionary_record(["a"], 10, 0, "from", declension=4, variant=2)
            )
            stems = QUANTITY.read_whitaker_stems(dictionary)
            external = entry("ls_dict", "a", "ā", "PREP", "lewis", True)
            word = QUANTITY.extract_first_word("ā", require_quantity=False)
            assert word is not None
            matches = AUDIT.find_matches(stems, AUDIT.NormalizedEntry(external, word))

        self.assertEqual(len(matches), 1)
        self.assertEqual(matches[0].stem, "a")
        self.assertEqual((matches[0].declension, matches[0].variant), (4, 2))


if __name__ == "__main__":
    unittest.main()
