# Contributing to WordsWASM

Thank you for your interest in WordsWASM.

WordsWASM accepts two broad kinds of contributions, both of which are important to the project:

1. **Editorial and linguistic contributions** — corrections and improvements to the Latin lexical, morphological, grammatical, and quantity data.
2. **Code contributions** — changes to the C++23 engine, WebAssembly bindings, CLI, database tooling, tests, build system, packaging, or web interface.

Documentation, test cases, source research, and reproducible bug reports are also valuable contributions.

You do **not** need to be a C++ developer to contribute corrections to the Latin data.

## Use of AI tools

If you use an LLM or another AI tool to prepare an issue or pull request, please disclose that use and briefly describe what the tool contributed.

When an AI tool suggests a grammatical or linguistic claim, check it against identifiable sources outside the tool, such as a grammar, dictionary, edition, or corpus. Cite the relevant entry, section, or passage before presenting the claim as evidence for a correction. If it remains unverified, label it as a hypothesis.

---

## Editorial contributions

WordsWASM inherits a large amount of lexical and morphological data from William Whitaker's WORDS, but the inherited data is not treated as infallible.

The project already contains local corrections, additional provenance, morphological assessments, vowel-quantity evidence, and documented intentional differences from the historical implementation.

Editorial contributions may include:

* correcting an existing dictionary entry;
* reporting an incorrect stem, conjugation, declension, gender, or part of speech;
* adding or correcting vowel quantity;
* documenting historical or exceptional forms;
* identifying false analyses produced by inherited data;
* documenting a disagreement between Whitaker and another grammatical or lexicographical source;
* proposing a new lexeme or lexical variant;
* improving a definition or lexical classification;
* supplying attestations or grammatical evidence;
* identifying a case where two entries should be related but currently are not;
* improving the project's editorial documentation.

### You can contribute without editing data files

If you find an editorial problem but do not know which internal file should be changed, open an issue.

A useful editorial report contains:

```text
Lemma or form:
Current WordsWASM result:
Expected result:

Type of change:
- lexical entry
- morphology
- inflection
- vowel quantity
- historical/exceptional form
- definition/meaning
- other

Evidence:
- author / work / dictionary
- edition, if relevant
- entry, section, page, paragraph, or other stable locator
- public URL, if available

Notes:
Why the current result appears incorrect and what should change.
```

A minimal reproducible query is especially useful.

For example:

```text
Query: hiscere

Current:
no analysis corresponding to the expected infinitive of hisco

Expected:
present active infinitive of hisco

Evidence:
Lewis & Short, entry "hisco, ĕre", n20796
```

Maintainers can translate a source-backed report into the appropriate internal representation.

### Evidence and provenance

Editorial changes should be traceable.

Whenever practical, include:

* the full name of the source;
* the relevant dictionary entry or grammatical section;
* an edition or version when that distinction matters;
* a stable locator;
* the exact form or grammatical claim being supported.

Prefer established grammars, dictionaries, editions, corpora, or other identifiable scholarly sources.

A source citation supports a specific claim. It should not be interpreted more broadly than the evidence allows.

For example, evidence that a related passive form occurs somewhere does not automatically prove that every mechanically generated form of that subparadigm is attested.

Likewise, absence of a form in one dictionary is not by itself evidence that the form is impossible.

### Keep observations separate from decisions

WordsWASM tries to preserve the distinction between:

1. what the engine mechanically generates;
2. what Whitaker historically accepted or filtered;
3. what external sources attest;
4. what the WordsWASM editorial layer concludes;
5. what a client chooses to display.

When possible, editorial contributions should preserve this distinction instead of deleting ambiguous or disputed analyses.

If sources disagree, recording the disagreement can be preferable to forcing a single answer.

### Human-edited data and generated data

The repository contains both editorial source material and generated runtime representations.

Examples of editorial/provenance sources include:

* `whitakers-words/GRAMMAR_SPECIALIZATIONS.jsonl`
* `whitakers-words/MORPHOLOGICAL_NOTICES.LAT`
* `whitakers-words/QUANTITY_EVIDENCE.jsonl`
* `whitakers-words/ADDON_POLICIES.LAT`
* the editorial and morphology documents under `docs/` and `whitakers-words/docs/`

Choose the source according to the claim being corrected:

| Change | Source to review | Derived data |
| --- | --- | --- |
| Vowel quantity | `QUANTITY_EVIDENCE.jsonl` | `QUANTITIES.LAT`, then WWDB |
| Addon rule policy | `ADDON_POLICIES.LAT`, alongside the rule in `ADDONS.LAT` | WWDB rule attributes |
| Grammar specialization | `GRAMMAR_SPECIALIZATIONS.jsonl` | Validated provenance and regression cases; behavior may also need code or data changes |
| Morphological notice | `MORPHOLOGICAL_NOTICES.LAT` | WWDB notice metadata |
| Existing inherited dictionary entry | `DICTLINE.GEN`, until a versioned correction operation exists | `DICTFILE.GEN`, then WWDB |

These paths are under `whitakers-words/`. The [compact database pipeline](whitakers-words/poc/compact-db/README.md) and [lexeme review guide](whitakers-words/docs/revisao-editorial-lexemas.md) describe the current source and generation steps. Check those documents before editing a data file; the appropriate source depends on the kind of claim.

Some compact files are deterministic projections of richer editorial data.

Do not manually edit generated binary databases or build outputs.

In particular, `.wwdb` images are outputs of the data pipeline rather than the authoritative location for an editorial correction.

Other generated files may also have a human-maintained source of truth. Check the relevant documentation or ask in the issue before editing them directly.

### Corrections to inherited Whitaker data

Some corrections necessarily affect inherited dictionary or morphology data.

When changing an existing inherited record:

* identify the exact entry being changed;
* show the old and proposed values;
* provide evidence for the changed field;
* add a focused regression test whenever the change affects analysis;
* avoid unrelated cleanup in the same change.

The project is gradually moving toward explicit, versioned editorial operations for modifications of inherited entries. Until every class of correction has such a ledger, a direct correction to an inherited source such as `whitakers-words/DICTLINE.GEN` may still be appropriate when accompanied by provenance and tests.

Do not manually modify derived `DICTFILE.GEN` data or a WWDB image to make a correction appear in the runtime.

### Intentional differences from Whitaker

Whitaker's Ada implementation is used as an important compatibility oracle, but compatibility is not an absolute requirement when there is a documented reason to differ.

If a contribution intentionally changes historical Whitaker behavior:

* explain the difference;
* provide grammatical, lexical, or implementation evidence;
* include discriminating input examples;
* add regression coverage;
* record the specialization in the appropriate provenance/documentation layer.

`whitakers-words/GRAMMAR_SPECIALIZATIONS.jsonl` is currently used for several grammar-level specializations and assessments.

A deliberate difference should be reproducible and visible rather than hidden inside an implementation special case.

### Copyright and third-party dictionaries

Only contribute material that can legally be redistributed.

Facts such as grammatical classifications, headwords, identifiers, bibliographic references, and independently written summaries are different from copying substantial copyrighted dictionary text.

Do not copy long definitions or entries from a copyrighted dictionary unless its license permits redistribution.

When third-party material is incorporated under its own license, preserve the required attribution and licensing information.

See `THIRD_PARTY_NOTICES.md` for the project's existing third-party lineage.

---

## Code contributions

WordsWASM's core engine is written in C++23 and targets both native platforms and WebAssembly.

Changes may affect several layers:

* `include/` — public and internal C++ interfaces;
* `src/` — native engine implementation;
* `wasmsrc/` — WebAssembly/Embind integration;
* `tests/` — unit, differential, data-pipeline, and regression tests;
* `schemas/` — serialized contract schemas;
* `whitakers-words/` — inherited Ada oracle and the extended data pipeline;
* `scripts/` — release, packaging, profiling, and development tooling;
* `npm/` — npm packages;
* `web/` — browser demo.

Try to keep changes scoped to the problem being solved.

Large unrelated refactors make both code review and differential testing harder.

---

## Building

The commands below use a POSIX shell, CMake, Ninja, and a C++23 compiler (GCC 14+ or Clang 19+). The test build also needs GoogleTest, either installed locally or fetched during configuration with `FETCH_GTEST=ON` (which requires network access). Generating the test databases needs `make`, GNAT, and GPRbuild. Python 3 and `jsonschema` are needed for the complete differential and data-pipeline suite; Node.js enables the browser-wrapper tests.

Initialize the repository dependencies first:

```sh
git submodule update --init --recursive
```

A native C++ build that does not require the Ada oracle can be created with:

```sh
cmake -S . -B build/native -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DENABLE_TESTS=OFF

cmake --build build/native --target words_cli -j"$(nproc)"
```

This compiles the CLI but does not create a WWDB. To run it, pass a compatible database with `--database`.

For development with the C++ tests, first build the Ada source data and oracle, then compile the test targets and generate both WWDB fixtures:

```sh
make -C whitakers-words -j"$(nproc)" \
  GPRBUILD_OPTIONS="-j$(nproc)" all

cmake -S . -B build/test -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug \
  -DENABLE_TESTS=ON \
  -DFETCH_GTEST=ON \
  -DENABLE_SANITIZERS=OFF

cmake --build build/test --target words_tests -j"$(nproc)"

mkdir -p whitakers-words/poc/compact-db/output
build/test/wwdb_poc_pack \
  whitakers-words \
  whitakers-words/poc/compact-db/output/words-poc-dense.wwdb \
  dense
build/test/wwdb_poc_pack \
  whitakers-words \
  whitakers-words/poc/compact-db/output/words-poc-search-only.wwdb \
  search-only

ctest --test-dir build/test -L unit --output-on-failure
```

The `unit` label includes database, engine, CLI, and regression checks that load these fixtures. Building `words_tests` also builds the packer, but does not generate its WWDB outputs. For the complete suite, run `ctest --test-dir build/test --output-on-failure` after installing its Python dependencies. The main [README](README.md#complete-native-build-and-test-suite) describes the full test prerequisites. This example disables sanitizers explicitly because `Debug` enables them by default; use a separate build tree when testing with sanitizers.

If you cannot run the complete suite, that does not prevent you from opening a pull request. Clearly state which tests you were able to run.

---

## Formatting and static analysis

C++ formatting is defined by `.clang-format`. Apply `clang-format` only to the C++ files you changed, for example:

```sh
clang-format -i src/engine.cpp
```

The repository's `format-code.sh` formats every changed C++ file and removes trailing whitespace from text files throughout the tree. Use it only when those wider edits are intended.

After configuring `build/test`, run static analysis against that build tree:

```sh
./run-clang-tidy.sh build/test
```

Keep unrelated formatting out of a small functional change.

---

## Tests

Bug fixes should normally include a regression test.

For morphology or dictionary corrections, prefer a focused witness that demonstrates the specific distinction being corrected.

For example, an editorial change may need to demonstrate that:

```text
form A -> expected analysis exists
form B -> incorrect analysis no longer occurs
form C -> unrelated historical behavior is preserved
```

Changes to morphology should be tested against nearby or ambiguous cases whenever the rule could affect more than one lexeme.

A test that only proves that one desired result appears may be insufficient if the same change accidentally introduces additional false analyses.

### Differential tests

Whitaker's Ada implementation remains an oracle for compatibility testing.

A differential failure can mean either:

* a regression in WordsWASM; or
* an intentional and justified difference from Whitaker.

Intentional differences must be documented and tested as such.

Do not weaken a differential test merely to make a new implementation pass.

---

## Public contracts and compatibility

WordsWASM exposes versioned CLI, WebAssembly, schema, and WWDB contracts.

Before changing a public structure, serialized field, database format, or observable CLI behavior, read:

* `docs/versioning.md`

Avoid changing public contracts as an accidental side effect of an internal refactor.

---

## Pull requests

A good pull request explains:

* what problem is being solved;
* why the change is needed;
* which parts of the repository are affected;
* which tests were run;
* whether public behavior or compatibility changes.

For editorial changes, also include:

* the affected lemma, entry, rule, or form;
* the source supporting the change;
* the relevant locator;
* before/after behavior.

Small focused pull requests are preferred.

A pull request can contain both code and editorial changes when they are inseparable — for example, adding a new editorial concept together with the engine support needed to expose it.

---

## Research and incomplete findings are welcome

Not every useful contribution needs to arrive as a finished correction.

Latin morphology contains disputed, historical, rare, and source-dependent cases. If you find something interesting but are not yet certain how it should be represented, open an issue with the evidence you have.

A well-documented unresolved case is more useful than silently forcing uncertain data into the engine.

The repository already contains research notes under `docs/` for cases where the correct representation required investigation before implementation.

---

## Questions

If you are unsure whether something belongs in the engine, the inherited Whitaker data, an editorial ledger, or documentation, open an issue describing the problem first.

You do not need to understand the internal WWDB representation before contributing linguistic evidence.

The important part is making the claim reproducible and traceable.
