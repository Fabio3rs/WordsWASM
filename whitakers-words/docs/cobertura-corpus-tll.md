# Cobertura lexical do corpus da The Latin Library

> **Estado deste trabalho:** análise exploratória inicial. Os relatórios e as
> listas abaixo são peças de evidência destinadas a ajudar na futura construção
> de um possível roadmap de adição de palavras ao WORDS. Eles não constituem um
> roadmap concluído, decisões editoriais aprovadas, verbetes prontos ou dados
> autorizados para importação/publicação.

Esta auditoria ordena por frequência as formas do corpus que a engine atual não
analisa. A ausência efetiva exige `status: unknown` tanto na consulta comum
quanto a falta de uma análise completa com `--two-words=legacy`. Assim,
composições clássicas sem espaço como `respublica`, analisadas como `res` +
`publica`, recebem `coverage_status: analyzed_two_words`. O resultado bruto é
preservado em `words_status`. Comparações com dicionários externos servem
somente para explicar e agrupar a fila; elas não substituem esses testes.

## Recorte do corpus

O extrator abre `thelatinlibrary.sqlite3` com
`mode=ro&immutable=1`, ativa `PRAGMA query_only=ON` e descobre o único run
`nlp_prepare` completo e ativo. Apenas unidades canônicas cuja decisão seja
`include` participam da contagem. Assim, índices, traduções detectadas, material
em quarentena e o piloto CLTK não são confundidos com o corpus oficial.

A unidade factual é a forma normalizada em NFC depois de `casefold`. O artefato
preserva as grafias observadas, frequência total, número de arquivos-fonte,
capitalização e ocorrência antes de ponto. A lista por lexema é deliberadamente
uma estimativa editorial: formas ambíguas produzem limites superior e exclusivo
e nunca são atribuídas silenciosamente a todos os homógrafos.

## Reprodução

Na raiz deste repositório:

```sh
python3 whitakers-words/poc/compact-db/extract_tll_word_frequencies.py \
  /mnt/projects/Projects/thelatinlibrary/var/thelatinlibrary.sqlite3 \
  --output /tmp/tll-word-frequencies.tsv.gz \
  --report /tmp/tll-word-frequencies-report.json

python3 whitakers-words/poc/compact-db/rank_tll_unknown_words.py \
  /tmp/tll-word-frequencies.tsv.gz \
  --frequency-report /tmp/tll-word-frequencies-report.json \
  whitakers-words/DICTFILE.GEN \
  /mnt/projects/Projects/Dicionarios/dicionarios/superdb.sqlite \
  --words-cli build/words_cli \
  --words-database dist/words-web/words-full.wwdb \
  --source ls_dict --source gaffiot \
  --collatinus-data /mnt/projects/Projects/collatinus/bin/data \
  --collatinus-extended \
  --latin-german /mnt/projects/Projects/Dicionarios/dicionarios/token_latim_german.sqlite \
  --faria-v3 /mnt/projects/Projects/Dicionarios/resultados/faria-v3/faria-v3-quality.sqlite \
  --output-directory whitakers-words/data/tll-corpus-coverage
```

O segundo comando conserva os defaults públicos do WORDS: ortografia clássica
e medieval e todos os mecanismos produtivos habilitados. O SHA-256 do WWDB é
enviado como `dataset-id` e registrado junto dos hashes das demais entradas.

## Saídas

- `tll-word-coverage.tsv.gz`: todas as formas, status bruto e cobertura efetiva;
- `tll-unknown-forms.tsv.gz`: formas cuja cobertura efetiva é `unknown`;
- `tll-engine-errors.tsv.gz`: entradas que não pertencem ao domínio aceito;
- `tll-lexeme-priorities.jsonl.gz`: grupos lexicais externos relacionados às
  formas desconhecidas;
- `tll-top-priorities.tsv`: topo legível de cada fila editorial;
- `tll-coverage-report.json`: estatísticas, política e manifesto completo.

As filas de lacunas são `common_lexeme`, `proper_name`,
`quantity_or_orthography`, `abbreviation_or_editorial` e `unresolved`. A segunda
passagem usa `--two-words=legacy`: uma sugestão completa conta como cobertura,
fica na fila informativa `covered_two_words` do arquivo completo e não entra nas
listas de lacunas. Nada é descartado das filas de lacunas por pertencer a uma
categoria secundária e nenhum registro permite promoção automática ao WWDB.

Todos os arquivos comprimidos usam `mtime=0`; ordenação, JSON compacto e escrita
atômica tornam duas execuções sobre os mesmos snapshots byte a byte idênticas.

## Segunda etapa exploratória: lemas CLTK/Stanza

O enriquecimento por lemas é executado separadamente para não transformar a
saída parcial de um modelo em verdade lexical. O snapshot atual contém somente
o piloto CLTK/Stanza, run `13`, com 745 documentos e 142.761 tokens. O run é
informado explicitamente e precisa ter como entrada o mesmo `nlp_prepare` usado
na contagem do corpus.

```sh
python3 whitakers-words/poc/compact-db/enrich_tll_unknowns_with_cltk.py \
  whitakers-words/data/tll-corpus-coverage/tll-unknown-forms.tsv.gz \
  --stage-one-report \
    whitakers-words/data/tll-corpus-coverage/tll-coverage-report.json \
  --structural-candidates \
    whitakers-words/data/tll-corpus-coverage/tll-lexeme-priorities.jsonl.gz \
  --tll-database \
    /mnt/projects/Projects/thelatinlibrary/var/thelatinlibrary.sqlite3 \
  --cltk-run-id 13 \
  --superdb \
    /mnt/projects/Projects/Dicionarios/dicionarios/superdb.sqlite \
  --source ls_dict --source gaffiot \
  --latin-german \
    /mnt/projects/Projects/Dicionarios/dicionarios/token_latim_german.sqlite \
  --retificado-v2 \
    /mnt/projects/Projects/Dicionarios/dicionarios/retificado_v2.db \
  --output-directory whitakers-words/data/tll-corpus-coverage
```

O `retificado_v2.db` é consultado diretamente porque é a fonte canônica e tem
mais entradas que seu snapshot parcial no SuperDB. A fonte `retificado_v2` do
SuperDB é deliberadamente excluída; o Faria release 6 não participa desta
etapa. Todos os bancos permanecem em `mode=ro&immutable=1` com
`PRAGMA query_only=ON`.

As saídas são:

- `tll-cltk-lemma-evidence.tsv.gz`, ledger completo por forma, lema e UPOS;
- `tll-cltk-lexeme-priorities.jsonl.gz`, grupos completos para processamento;
- `tll-cltk-roadmap.tsv`, resumo humano ordenado por frequência do corpus;
- `tll-cltk-coverage-report.json`, manifesto, política e cobertura do piloto.

`corpus_occurrences_upper_bound` soma a frequência global das formas associadas
ao lema no piloto; não afirma que todas as ocorrências globais têm esse lema.
`pilot_lemma_occurrences` conta somente as atribuições realmente observadas.
Lemas concorrentes, homógrafos, conflitos de POS e flags de revisão do Faria
são preservados. Esta é apenas mais uma peça inicial da análise para um futuro
roadmap; nenhuma saída representa decisão editorial final ou autoriza promoção
automática ao banco WORDS.
