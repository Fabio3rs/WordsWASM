# WordsWASM

WordsWASM is a C++23 Latin morphological analysis engine derived from the
data and behaviour of William Whitaker's WORDS. It targets both native
applications and WebAssembly while preserving the Ada implementation as an
oracle for compatibility testing.

The bundled Ada source tree is based on
[Fabio3rs/whitakers-words](https://github.com/Fabio3rs/whitakers-words), the
project fork, and
[mk270/whitakers-words](https://github.com/mk270/whitakers-words), the upstream
repository from which it was forked. These origins are also recorded in
[`whitakers-words/remotes.txt`](whitakers-words/remotes.txt).

The project currently provides:

- UTF-8 input with macron and breve support;
- typed morphological analysis and resolved search results;
- compact, validated WWDB datasets;
- separate full and search-only database profiles;
- a native command-line interface and an Embind browser API;
- differential tests against the original Ada engine.

## Building

Clone the repository with its submodules, then build the native engine with
CMake:

```sh
git submodule update --init --recursive
cmake -S . -B build/native -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build/native -j"$(nproc)"
```

The original Ada tools and source datasets can be built separately:

```sh
make -C whitakers-words -j"$(nproc)" \
  GPRBUILD_OPTIONS="-j$(nproc)" all
```

GNAT/GPRbuild are required for the Ada build. Emscripten is required for the
browser build. See
[the browser documentation](whitakers-words/docs/webassembly-browser.md) for
the WebAssembly commands and deployment layout.

## Databases

The two deployable WWDB profiles share the same lexeme and rule IDs:

- `words-full.wwdb` includes morphology, metadata, and meanings;
- `words-search.wwdb` is intended for applications that already provide
  definitions from external dictionaries. It omits Whitaker's meanings but
  still resolves lemmas, parts of speech, morphology, flags, and stable IDs
  within the dataset that can be used by the application's search layer.

The format and data pipeline are described in
[the implementation overview](whitakers-words/docs/estado-implementacao.md).

## Documentation

The detailed design notes are in
[`whitakers-words/docs/`](whitakers-words/docs/). They cover the Ada oracle,
the C++23 architecture, Unicode and vowel quantity, compact storage, browser
integration, and lexical enrichment.

## Development disclosure

The architectural analyses, proofs of concept, documentation, and portions of
the proposed engine implementation in this project were developed with the
assistance of an LLM, OpenAI Codex. The original WORDS program and its lexical
data are the work of William Whitaker and their subsequent contributors.

## License

The original WordsWASM code is available under the
[MIT License](LICENSE), copyright 2026 Fabio R. Sluzala.

William Whitaker's WORDS source code and data under `whitakers-words/` retain
their original permissive license and attribution request. See
[`whitakers-words/LICENCE.txt`](whitakers-words/LICENCE.txt) for the complete
terms.

Third-party components in [`vendor/`](vendor/) retain their respective
licenses.
