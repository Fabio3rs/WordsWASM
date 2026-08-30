#!/usr/bin/env python3

from __future__ import annotations

import importlib.util
import json
import sqlite3
import sys
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SCRIPT = ROOT / "whitakers-words/poc/compact-db/suggest_quantity_evidence.py"
SPEC = importlib.util.spec_from_file_location("suggest_quantity_evidence", SCRIPT)
if SPEC is None or SPEC.loader is None:
    raise RuntimeError("cannot load quantity suggestion generator")
SUGGESTER = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = SUGGESTER
SPEC.loader.exec_module(SUGGESTER)


def dictionary_record(stems: list[str], part: int, gender: int, meaning: str) -> bytes:
    record = bytearray(b" " * 180)
    for index, stem in enumerate(stems):
        encoded = stem.encode("ascii")
        record[index * 18 : index * 18 + len(encoded)] = encoded
    record[72] = part
    record[84] = gender
    encoded_meaning = meaning.encode("ascii")[:80]
    record[97 : 97 + len(encoded_meaning)] = encoded_meaning
    return bytes(record)


def create_superdb(path: Path) -> None:
    connection = sqlite3.connect(path)
    connection.executescript(
        """
        create table source(id integer primary key, name text not null);
        create table entry(
            id integer primary key,
            source_id integer not null,
            source_entry_id text not null,
            lemma text not null,
            pos_std text,
            gender_std text,
            head_raw text
        );
        insert into source values(1, 'ls_dict');
        insert into entry values(1, 1, 'apple', 'mālum', 'NOUN', 'n',
                                 'mālum, i, n., an apple or fleshy fruit');
        insert into entry values(2, 1, 'bad', 'mălus', 'ADJ', null,
                                 'mălus, a, um, bad or evil');
        insert into entry values(3, 1, 'plain', 'rosa', 'NOUN', 'f',
                                 'rosa, ae, f., a rose');
        """
    )
    connection.commit()
    connection.close()


def create_collatinus_data(path: Path) -> None:
    path.mkdir()
    (path / "modeles.la").write_text(
        """! minimal model fixture
modele:lupus
pos:n
modele:templum
pere:lupus
modele:ager
pere:lupus
modele:doctus
pos:a
modele:fortis
pere:doctus
modele:miser
pere:doctus
""",
        encoding="utf-8",
    )
    (path / "lemmes.la").write_text(
        """! quantity homographs
lēvis2|fortis|lēv||e|73
lĕvis=lĕvĭs|fortis|lĕv||e|732
lĭber2=lĭbĕr|ager|||bri, m.|300
līber=lībĕr|miser|||era, erum|566
mālum2=mālŭm|templum|||i, n.|1
mălum=mălŭm|templum|||i, n.|1
pālus2|lupus|||i, m.|15
pălus=pălūs|lupus|pălūd||udis, f.|134
pōpŭlus2|lupus|||i, f.|23
pŏpŭlus=pŏpŭlus,pō̆plus|lupus|||i, m.|2558
ho!|lupus|||i, n.|1
""",
        encoding="utf-8",
    )
    (path / "lemmes.en").write_text(
        """levis:light, slight
levis2:smooth, not rough
liber:free, unimpeded
liber2:book, volume
malum:evil, misfortune
malum2:apple, fruit
palus:swamp, marsh
palus2:stake, pole
populus:people, nation
populus2:poplar tree
ho:an exclamation
""",
        encoding="utf-8",
    )


def create_latin_german(path: Path) -> None:
    connection = sqlite3.connect(path)
    connection.execute(
        """
        create table VOC(
            id integer primary key,
            vok_id text,
            latin text,
            desc text,
            grammar text,
            typnr integer
        )
        """
    )
    connection.executemany(
        "insert into VOC values(?, ?, ?, ?, ?, ?)",
        (
            (1, "smooth", "lēvis lēve", "glatt, poliert", "a", 305),
            (2, "light", "levis leve", "leicht", "a", 305),
            (3, "apple", "mālum -ī, n", "Apfel", "s", 205),
            (4, "love", "amāre, amō, amāvī, amātum", "lieben", "v", 102),
            (5, "phrase", "Liber adest", "Liber naht", "-", None),
        ),
    )
    connection.commit()
    connection.close()


class QuantitySuggestionTest(unittest.TestCase):
    def fixture(self, directory: Path) -> tuple[Path, Path]:
        dictionary = directory / "DICTFILE.GEN"
        dictionary.write_bytes(
            dictionary_record(["mal"], 1, 3, "apple; fruit;")
            + dictionary_record(["mal"], 1, 3, "evil; harm;")
            + dictionary_record(["mal"], 4, 0, "bad; evil;")
        )
        database = directory / "superdb.sqlite"
        create_superdb(database)
        return dictionary, database

    def test_keeps_homographs_separate_and_review_only(self) -> None:
        with tempfile.TemporaryDirectory() as name:
            dictionary, database = self.fixture(Path(name))
            stems = SUGGESTER.read_whitaker_stems(dictionary)
            entries = SUGGESTER.read_dictionary_entries(database, ["ls_dict"])
            candidates = SUGGESTER.suggest(stems, entries)

        self.assertEqual(len(candidates), 3)
        apple = [item for item in candidates if item.dictionary.source_entry_id == "apple"]
        self.assertEqual(len(apple), 2)
        self.assertTrue(all(item.alternatives == 2 for item in apple))
        self.assertEqual({item.evidence_record()["marked"] for item in apple}, {"māl"})
        self.assertTrue(
            all(item.evidence_record()["confidence"] == "needs_review" for item in candidates)
        )
        bad = [item for item in candidates if item.dictionary.source_entry_id == "bad"]
        self.assertEqual([item.whitaker.dictionary_entry for item in bad], [3])
        self.assertEqual(bad[0].semantic_overlap, ("bad", "evil"))

    def test_ignores_unmarked_lemma_and_quantity_outside_stem(self) -> None:
        word = SUGGESTER.extract_first_word("rosā")
        self.assertIsNotNone(word)
        assert word is not None
        stem = SUGGESTER.WhitakerStem(1, 1, "ros", False, "NOUN", "f", "rose")
        entry = SUGGESTER.DictionaryEntry("ls_dict", "rose", "rosā", "NOUN", "f", "rose")
        candidates = SUGGESTER.suggest({"ros": (stem,)}, [entry])
        self.assertEqual(candidates, ())
        self.assertIsNone(SUGGESTER.extract_first_word("rosa"))

    def test_extracts_gaffiot_numbered_headword(self) -> None:
        word = SUGGESTER.extract_first_word("17 ăbactĭo")
        self.assertIsNotNone(word)
        assert word is not None
        self.assertEqual(word.source, "abactio")
        self.assertEqual(word.marked_prefix(6), "ăbactĭ")

    def test_reads_collatinus_quantities_homographs_and_model_metadata(self) -> None:
        with tempfile.TemporaryDirectory() as name:
            data = Path(name) / "data"
            create_collatinus_data(data)
            entries = tuple(SUGGESTER.read_collatinus_entries(data))

        self.assertEqual(len(entries), 11)
        by_id = {entry.source_entry_id: entry for entry in entries}
        smooth = by_id["lemmes.la:levis2"]
        self.assertEqual(smooth.lemma, "lēvis")
        self.assertEqual(smooth.part_of_speech, "ADJ")
        self.assertIn("smooth, not rough", smooth.head)
        self.assertEqual(smooth.source_family, "collatinus-derived")
        self.assertFalse(smooth.independent_quantity_authority)
        self.assertTrue(smooth.morphology_hint.startswith("fortis|"))

        book = by_id["lemmes.la:liber2"]
        self.assertEqual(book.lemma, "lĭbĕr")
        self.assertEqual(book.part_of_speech, "NOUN")
        self.assertEqual(book.gender, "m")
        self.assertEqual(by_id["lemmes.la:malum2"].lemma, "mālŭm")
        self.assertEqual(by_id["lemmes.la:malum"].lemma, "mălŭm")
        self.assertEqual(by_id["lemmes.la:populus"].lemma, "pŏpŭlus")

    def test_collatinus_candidates_keep_derived_provenance(self) -> None:
        with tempfile.TemporaryDirectory() as name:
            data = Path(name) / "data"
            create_collatinus_data(data)
            entries = tuple(SUGGESTER.read_collatinus_entries(data))

        light_stem = SUGGESTER.WhitakerStem(
            1, 1, "lev", False, "ADJ", None, "light, slight"
        )
        smooth_stem = SUGGESTER.WhitakerStem(
            2, 1, "lev", False, "ADJ", None, "smooth, polished"
        )
        candidates = SUGGESTER.suggest(
            {"lev": (light_stem, smooth_stem)},
            (entry for entry in entries if entry.source_entry_id.startswith("lemmes.la:levis")),
        )
        self.assertEqual(len(candidates), 4)
        report = candidates[0].report_record()
        self.assertEqual(report["source_family"], "collatinus-derived")
        self.assertFalse(report["independent_quantity_authority"])
        self.assertEqual(candidates[0].evidence_record()["confidence"], "needs_review")
        rendered = json.loads(SUGGESTER.render_report(candidates))
        self.assertEqual(rendered["counts"]["derived_authority_candidates"], 4)
        self.assertEqual(rendered["counts"]["independent_authority_candidates"], 0)
        self.assertFalse(rendered["sources"][0]["independent_quantity_authority"])

    def test_reads_latin_german_citation_forms_and_ignores_phrases(self) -> None:
        with tempfile.TemporaryDirectory() as name:
            database = Path(name) / "latin-german.sqlite"
            create_latin_german(database)
            entries = tuple(SUGGESTER.read_latin_german_entries(database))

        self.assertEqual(len(entries), 4)
        by_id = {entry.source_entry_id: entry for entry in entries}
        self.assertEqual(by_id["smooth"].lemma, "lēvis")
        self.assertEqual(by_id["smooth"].part_of_speech, "ADJ")
        self.assertEqual(by_id["apple"].lemma, "mālum")
        self.assertEqual(by_id["apple"].gender, "n")
        self.assertEqual(by_id["love"].lemma, "amō")
        self.assertEqual(by_id["love"].part_of_speech, "VERB")
        self.assertEqual(
            by_id["love"].morphology_hint,
            "typnr:102;citation:amāre, amō, amāvī, amātum",
        )
        self.assertEqual(by_id["apple"].source_family, "latin-german")
        self.assertTrue(by_id["apple"].independent_quantity_authority)

    def test_consensus_counts_independent_families_not_derived_sources(self) -> None:
        def candidate(
            source_name: str,
            family: str,
            marked: str,
            entry: int,
            *,
            alternatives: int = 1,
            independent: bool = True,
        ) -> SUGGESTER.Candidate:
            word = SUGGESTER.extract_first_word(marked)
            assert word is not None
            stem = SUGGESTER.WhitakerStem(
                entry, 1, "mal", False, "NOUN", "n", "apple"
            )
            dictionary = SUGGESTER.DictionaryEntry(
                source_name,
                f"{source_name}-{entry}-{marked}",
                marked,
                "NOUN",
                "n",
                "apple",
                family,
                independent,
            )
            return SUGGESTER.Candidate(dictionary, stem, word, ("apple",), alternatives)

        candidates = (
            candidate("ls_dict", "lewis", "mālum", 1),
            candidate("latin_german", "latin-german", "mālum", 1),
            candidate(
                "collatinus", "collatinus-derived", "mălum", 1, independent=False
            ),
            candidate("ls_dict", "lewis", "mālum", 2),
            candidate("gaffiot", "gaffiot", "mălum", 2),
            candidate("ls_dict", "lewis", "mālum", 3),
            candidate(
                "collatinus", "collatinus-derived", "mālum", 3, independent=False
            ),
            candidate("latin_german", "latin-german", "mālum", 4, alternatives=2),
        )
        report = SUGGESTER.quantity_consensus(candidates)
        by_target = {
            (item["target"]["dictionary_entry"], item["position"]): item
            for item in report["positions"]
        }
        self.assertEqual(by_target[(1, 1)]["decision"], "consensus_2_of_3")
        self.assertEqual(by_target[(1, 1)]["independent_support"], 2)
        self.assertTrue(by_target[(1, 1)]["derived_conflict"])
        self.assertEqual(by_target[(2, 1)]["decision"], "conflict")
        self.assertEqual(by_target[(3, 1)]["decision"], "single_source")
        self.assertEqual(report["counts"]["consensus_2_of_3"], 1)
        self.assertEqual(report["counts"]["excluded_ambiguous_candidate_positions"], 1)

    def test_existing_confirmed_evidence_participates_after_queue_suppression(self) -> None:
        word = SUGGESTER.extract_first_word("mālum")
        assert word is not None
        stem = SUGGESTER.WhitakerStem(7, 1, "mal", False, "NOUN", "n", "apple")
        external = SUGGESTER.DictionaryEntry(
            "latin_german",
            "apple",
            "mālum",
            "NOUN",
            "n",
            "Apfel",
            "latin-german",
            True,
        )
        candidate = SUGGESTER.Candidate(external, stem, word, (), 1)
        with tempfile.TemporaryDirectory() as name:
            evidence_path = Path(name) / "evidence.jsonl"
            evidence_path.write_text(
                json.dumps(
                    {
                        "record": "source",
                        "id": "lewis-short-ls-dict",
                        "family": "lewis",
                        "independent_quantity_authority": True,
                    }
                )
                + "\n"
                + json.dumps(
                    {
                        "record": "evidence",
                        "id": "reviewed-ls-apple",
                        "target": {
                            "kind": "stem",
                            "dictionary_entry": 7,
                            "slot": 1,
                        },
                        "base": "mal",
                        "marked": "māl",
                        "source": "lewis-short-ls-dict",
                        "confidence": "confirmed",
                    },
                    ensure_ascii=False,
                )
                + "\n",
                encoding="utf-8",
            )
            votes = SUGGESTER.existing_consensus_votes(evidence_path)

        report = SUGGESTER.quantity_consensus((candidate,), votes)
        self.assertEqual(report["counts"]["existing_confirmed_vote_positions"], 1)
        self.assertEqual(report["counts"]["consensus_2_of_3"], 1)

    def test_rejects_malformed_collatinus_record(self) -> None:
        with tempfile.TemporaryDirectory() as name:
            root = Path(name)
            models_path = root / "modeles.la"
            models_path.write_text("modele:lupus\npos:n\n", encoding="utf-8")
            models = SUGGESTER.read_collatinus_models(models_path)
            lemmas = root / "lemmes.la"
            lemmas.write_text("mālum|lupus|missing-fields\n", encoding="utf-8")
            with self.assertRaisesRegex(SUGGESTER.SuggestionError, "expected six"):
                tuple(SUGGESTER.read_collatinus_lemma_file(lemmas, models, {}))

    def test_resolves_collatinus_radical_rule_inheritance(self) -> None:
        with tempfile.TemporaryDirectory() as name:
            models_path = Path(name) / "modeles.la"
            models_path.write_text(
                """modele:root
R:0:1,0
pos:v
modele:child
pere:root
R:1:1,āv
""",
                encoding="utf-8",
            )
            model = SUGGESTER.read_collatinus_models(models_path)["child"]

        self.assertEqual(model.part_of_speech, "VERB")
        self.assertEqual(model.radical_rules, ((0, "1,0"), (1, "1,āv")))
        self.assertEqual(
            SUGGESTER.collatinus_radicals("amō", model, "", ""),
            ((0, "am"), (1, "amav")),
        )

    def test_rejects_proper_name_collision_and_internal_editorial_cut(self) -> None:
        common = SUGGESTER.WhitakerStem(1, 1, "con", False, "NOUN", "m", "cone")
        proper = SUGGESTER.DictionaryEntry(
            "ls_dict", "proper", "Cŏnōn", "NOUN", "m", "Conon, a proper name"
        )
        self.assertEqual(SUGGESTER.suggest({"con": (common,)}, [proper]), ())
        self.assertIsNone(SUGGESTER.extract_first_word("cŏrō^na"))
        self.assertIsNone(SUGGESTER.extract_first_word("eurŏ-nŏtus"))
        self.assertIsNone(SUGGESTER.extract_first_word("super indictum"))

    def test_existing_source_target_pair_is_suppressed(self) -> None:
        with tempfile.TemporaryDirectory() as name:
            root = Path(name)
            dictionary, database = self.fixture(root)
            candidates = SUGGESTER.suggest(
                SUGGESTER.read_whitaker_stems(dictionary),
                SUGGESTER.read_dictionary_entries(database, ["ls_dict"]),
            )
            existing = root / "existing.jsonl"
            existing.write_text(
                json.dumps(candidates[0].evidence_record(), ensure_ascii=False) + "\n",
                encoding="utf-8",
            )
            filtered = SUGGESTER.remove_existing(candidates, SUGGESTER.existing_keys(existing))
        self.assertEqual(len(filtered), len(candidates) - 1)
        apple = [item for item in filtered if item.dictionary.source_entry_id == "apple"]
        self.assertTrue(all(item.alternatives == 1 for item in apple))

    def test_rendering_is_deterministic_and_import_compatible(self) -> None:
        with tempfile.TemporaryDirectory() as name:
            dictionary, database = self.fixture(Path(name))
            candidates = SUGGESTER.suggest(
                SUGGESTER.read_whitaker_stems(dictionary),
                SUGGESTER.read_dictionary_entries(database, ["ls_dict"]),
            )
        first = SUGGESTER.render_jsonl(candidates)
        second = SUGGESTER.render_jsonl(candidates)
        self.assertEqual(first, second)
        records = [json.loads(line) for line in first.splitlines()]
        self.assertTrue(all(record["record"] == "evidence" for record in records))
        report = json.loads(SUGGESTER.render_report(candidates))
        self.assertEqual(report["policy"], "review_only")
        self.assertEqual(report["counts"]["candidates"], 3)

    def test_prioritizes_only_explicit_quantity_opposition_in_same_word(self) -> None:
        light_word = SUGGESTER.extract_first_word("lĕvis")
        smooth_word = SUGGESTER.extract_first_word("lēvis")
        fuller_light_word = SUGGESTER.extract_first_word("lĕvĭs")
        assert light_word is not None
        assert smooth_word is not None
        assert fuller_light_word is not None
        light_stem = SUGGESTER.WhitakerStem(
            1, 1, "lev", False, "ADJ", None, "light; slight"
        )
        smooth_stem = SUGGESTER.WhitakerStem(
            2, 1, "lev", False, "ADJ", None, "smooth; polished"
        )
        light = SUGGESTER.Candidate(
            SUGGESTER.DictionaryEntry(
                "ls_dict", "light", "lĕvis", "ADJ", None, "light"
            ),
            light_stem,
            light_word,
            ("light",),
            1,
        )
        smooth = SUGGESTER.Candidate(
            SUGGESTER.DictionaryEntry(
                "ls_dict", "smooth", "lēvis", "ADJ", None, "smooth"
            ),
            smooth_stem,
            smooth_word,
            ("smooth",),
            1,
        )
        partial_only = SUGGESTER.Candidate(
            SUGGESTER.DictionaryEntry(
                "ls_dict", "fuller-light", "lĕvĭs", "ADJ", None, "light"
            ),
            light_stem,
            fuller_light_word,
            ("light",),
            1,
        )

        prioritized = SUGGESTER.meaning_distinguishing_homographs(
            (light, smooth, partial_only)
        )
        self.assertEqual(
            prioritized,
            {("ls_dict", "levis"): {(1, 1): frozenset({1}), (2, 1): frozenset({1})}},
        )
        report = json.loads(SUGGESTER.render_report((light, smooth, partial_only)))
        self.assertEqual(
            report["counts"]["meaning_distinguishing_homograph_groups"], 1
        )
        self.assertEqual(report["counts"]["direct_homograph_groups"], 1)
        self.assertEqual(report["counts"]["direct_homograph_targets"], 2)
        self.assertEqual(report["counts"]["independent_authority_candidates"], 3)
        self.assertEqual(report["counts"]["derived_authority_candidates"], 0)
        self.assertEqual(report["sources"][0]["family"], "lewis")
        self.assertTrue(report["candidates"][0]["meaning_distinguishing_homograph"])
        self.assertEqual(report["candidates"][0]["opposed_quantity_positions"], [1])
        self.assertEqual(report["candidates"][0]["homograph_priority"], "direct")


if __name__ == "__main__":
    unittest.main()
