# Quantity-resolved returned forms

The engine compares marked spelling with confirmed stem, inflection, and
productive suffix evidence when selecting analyses. Presentation enrichment is
performed after that selection. The local native JSON v4 and browser/WASM
schema v6 include suffix quantity evidence in `form.display` and
`form.quantity`. Native v3 retains its prior projection.

`form.recognized` is the normalized spelling accepted by the analysis.
`form.display` is the NFC, presentation-ready spelling: confirmed database
evidence takes precedence at known positions, while an explicit macron or breve
typed by the user survives wherever the database has no evidence.

`form.quantity.annotated` contains database evidence only and is `null` when
there is none. `coverage` is `none`, `partial`, or `complete`. Each entry of
`positions` has a zero-based logical Latin-letter `index`, `quantity` (`short`
or `long`), and `origin` (`stem`, `suffix`, or `ending`). Combining marks do not increment
the logical index. Rule-less UNIQUES rows, artificial Roman numerals, and
compound analyses initially report `none` rather than borrowing uncertain
evidence.

The WWDB 1.11 development format rejects a conflicting marked reading (for
example `sanctĕ` as a derived adverb), while unmarked `sancte` retains all
matching analyses. In v4/v6, its adverbial reading displays `sanctē` and
identifies the last vowel as suffix evidence. The adjective reading remains
independent. Native v3 continues to expose only stem and ending origins.

The evidence compiled into `QUANTITIES.LAT` is traceable through
`whitakers-words/QUANTITY_EVIDENCE.jsonl`. Grammar-derived inflection evidence
names the worksheet and paradigm; lexical evidence names the dictionary,
native entry identifier, reviewed witness, POS, and gender where applicable.
The local audit inputs under `.study` are intentionally not required by CI or
by a release build.

Relevant source descriptions recorded in that evidence ledger include:

- *LatinaeTabulae — paradigmas latinos*, workspace worksheet snapshot.
- Charlton T. Lewis and Charles Short, *A Latin Dictionary*, searchable source
  represented by the local Lewis–Short database.
- Félix Gaffiot, *Dictionnaire latin-français*, searchable source represented
  by the local Gaffiot database.
- Ernesto Faria, *Dicionário Escolar Latino-Português*, ISBN not recorded in
  the available OCR snapshot.
- Napoleão Mendes de Almeida, *Gramática Latina: curso único e completo*, ISBN
  not recorded in the available OCR snapshot.
- Charles E. Bennett, *New Latin Grammar*, Project Gutenberg ebook 15665,
  <https://www.gutenberg.org/ebooks/15665>.

These names and locators are citations, not runtime dependencies. The generated
microdata remains the sole release input consumed by the packer.
