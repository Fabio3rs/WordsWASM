[Project Website](http://mk270.github.io/whitakers-words/)

WORDS
=====

This is a cleaned-up version of the port of William Whitaker's WORDS
programme, a Latin-English dictionary with inflectional morphology
support; the original author passed away in 2010, so any and all help
maintaining the software as development and execution environments evolve
would be greatly appreciated.

Effectively, this is an exercise in digital preservation.

Contributing
============

Help is needed maintaining the code for future users; in particular, it
does not currently support vowel length, so it may be necessary to gather
a group of Latin experts to adjust its lexicon of several thousand words.

If you contribute, please be sure to indicate your assent to redistributing
your contributions under the same terms as the existing software; this
will minimise copyright hassles in the future.

Usage
=====

    $ make
    $ bin/words

To emit the canonical JSON analysis for one Latin word:

    $ make words_json
    $ bin/words_json rosae

The machine-readable result is written to standard output. Diagnostics and
usage errors are written to standard error.

Documentation
=============

See the HOWTO.txt file included,
and documentation on the [Project Website](http://mk270.github.io/whitakers-words/operational.html)

The morphological-analysis algorithm used to identify dictionary forms,
cases, and declensions is documented in Portuguese in
[docs/algoritmo-analise-morfologica.md](docs/algoritmo-analise-morfologica.md).
The high-level Ada package architecture, runtime data flow, and the one-suffix
derivation boundary are mapped in
[docs/arquitetura-ada-fluxo-dados.md](docs/arquitetura-ada-fluxo-dados.md).
The proposed C++23 modules, classes, ownership model, query pipeline, CMake
targets, and C/WASM boundary are described in
[docs/arquitetura-engine-cpp23.md](docs/arquitetura-engine-cpp23.md).
The pre-implementation plan for UTF-8 normalization, macrons, breves, and
future vowel-quantity enrichment is in
[docs/plano-unicode-quantidade-vocalica.md](docs/plano-unicode-quantidade-vocalica.md).

A proposed WebAssembly architecture, including a possible C++ port and direct
loading of the lexical database into WASM linear memory, is documented in
[docs/proposta-webassembly.md](docs/proposta-webassembly.md).

The canonical JSON contract and its mapping from the Ada engine are documented
in [docs/formato-json-canonico.md](docs/formato-json-canonico.md). Its JSON
Schema is in [../schemas/analysis-v1.schema.json](../schemas/analysis-v1.schema.json).
The compact, dataset-local search response is a separate contract documented in
[docs/formato-json-busca.md](docs/formato-json-busca.md), with schema in
[../schemas/search-v1.schema.json](../schemas/search-v1.schema.json).


Build-time Dependencies
=======================

* GPRBuild
* gnat

On a Debian-like system, you can install these roughly as follows:

    $ apt-get install gprbuild gnat

GNAT versions before 4.9 are believed to link against a buggy runtime on
64-bit platforms, so should be avoided.

Licensing
=========

WORDS, a Latin dictionary, by Colonel William Whitaker (USAF, Retired)

Copyright William A. Whitaker (1936-2010)

This is a free program, which means it is proper to copy it and pass
it on to your friends. Consider it a developmental item for which
there is no charge. However, just for form, it is Copyrighted
(c). Permission is hereby freely given for any and all use of program
and data. You can sell it as your own, but at least tell me.

This version is distributed without obligation, but the developer
would appreciate comments and suggestions.

All parts of the WORDS system, source code and data files, are made freely
available to anyone who wishes to use them, for whatever purpose.
