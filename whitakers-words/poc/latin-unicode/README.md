# Biblioteca experimental de UTF-8 latino

Esta pasta contém uma biblioteca C++23 isolada para investigar o domínio
finito aceito pelo `LatinLexer`. Ela não é uma implementação de Unicode geral,
não substitui o lexer de produção e não participa do link de `words_core` nem
do `WordsWASM` publicado.

O relatório completo, incluindo contrato, tabela de codepoints, testes e
medições, está em
[`../../docs/investigacao-latin-unicode.md`](../../docs/investigacao-latin-unicode.md).

## Componentes

- `latin_unicode_poc`: biblioteca estática que depende somente da STL;
- `latin_unicode_poc_tests`: testes diferenciais contra `LatinLexer` e o
  utf8proc vendorizado;
- `latin_unicode_poc_fuzz`: libFuzzer diferencial, disponível com Clang e
  `EXCLUDE_FROM_ALL`;
- `latin_unicode_poc_benchmark`: microbenchmark nativo, também
  `EXCLUDE_FROM_ALL`;
- `latin_unicode_poc_size`, `latin_unicode_poc_into_size` e
  `latin_unicode_utf8proc_size`: microexecutáveis comparáveis para inspeção de
  seções e compressibilidade;
- `latin_library_corpus.tsv`: amostra congelada, auditável e independente em
  runtime do SQLite da Latin Library;
- `latin_utf8_fuzz.dict`: dicionário pequeno para o harness diferencial.

`LatinSurfaceNormalizer::normalize` devolve ownership. Para investigar zero
alocações, `requirements` calcula tamanhos exatos e `normalize_into` escreve em
cinco spans do chamador, devolvendo `LatinSurfaceView`. Entrada e buffers devem
viver mais que a view e não podem se sobrepor. Toda validação de entrada e
capacidade acontece antes da primeira escrita.

Somente o teste é incluído no build normal quando `ENABLE_TESTS=ON`. Os targets
de pesquisa precisam ser solicitados explicitamente.

## Reproduzir os testes

```sh
cmake -S . -B build -DENABLE_TESTS=ON
cmake --build build --target latin_unicode_poc_tests
build/whitakers-words/poc/latin-unicode/latin_unicode_poc_tests
```

O teste executa, entre outras matrizes, todos os escalares Unicode válidos, as
444.828 sequências de até três escalares do alfabeto aceito, todas as strings
de um a três bytes e 8.388.608 casos estruturais de quatro bytes com starters
`F0`–`F7`. Ambas as APIs são comparadas campo a campo ao oracle.

Fuzz diferencial curto:

```sh
cmake --build build --target latin_unicode_poc_fuzz
build/whitakers-words/poc/latin-unicode/latin_unicode_poc_fuzz \
  -runs=200000 -max_len=64 -seed=20260910 \
  -dict=whitakers-words/poc/latin-unicode/latin_utf8_fuzz.dict
```

## Benchmark e tamanho nativo

```sh
cmake -S . -B build/latin-unicode-minsize \
  -DCMAKE_BUILD_TYPE=MinSizeRel -DENABLE_TESTS=OFF
cmake --build build/latin-unicode-minsize --target \
  latin_unicode_poc_benchmark \
  latin_unicode_poc_size \
  latin_unicode_poc_into_size \
  latin_unicode_utf8proc_size

build/latin-unicode-minsize/whitakers-words/poc/latin-unicode/latin_unicode_poc_benchmark

llvm-size -A \
  build/latin-unicode-minsize/whitakers-words/poc/latin-unicode/latin_unicode_poc_size \
  build/latin-unicode-minsize/whitakers-words/poc/latin-unicode/latin_unicode_poc_into_size \
  build/latin-unicode-minsize/whitakers-words/poc/latin-unicode/latin_unicode_utf8proc_size
```

O benchmark aceita opcionalmente `CAMINHO_CORPUS ITERAÇÕES`. Preparação do
corpus, warmup e estruturas pré-decodificadas ficam fora das regiões medidas.

Perfis exploratórios também podem ser produzidos sem tocar em produção:

```sh
valgrind --tool=callgrind \
  --collect-atstart=no \
  '--toggle-collect=words::poc::latin_unicode::LatinSurfaceNormalizer::normalize*' \
  build/latin-unicode-minsize/whitakers-words/poc/latin-unicode/latin_unicode_poc_benchmark \
  whitakers-words/test/01_aeneid/input.txt 1

valgrind --tool=dhat \
  build/latin-unicode-minsize/whitakers-words/poc/latin-unicode/latin_unicode_poc_benchmark \
  whitakers-words/test/01_aeneid/input.txt 1
```

No DHAT, a ausência de qualquer stack contendo `normalize_into` verifica o
caminho sem heap. Para separar instruções, consolide as chamadas vindas de
`main` para `normalize`, `normalize_into` e, em uma execução à parte,
`words::LatinLexer::lex`; o toggle de `normalize*` também coleta a preparação
proprietária do corpus se ela não for excluída na análise.

O relatório registra também uma execução instrumentada com
`-fprofile-instr-generate -fcoverage-mapping`, consolidada por
`llvm-profdata`/`llvm-cov` 21.

## Micro-WASM

Use a toolchain Emscripten suportada pelo projeto, não altere o build de
release:

```sh
/mnt/projects/Projects/emsdk/upstream/emscripten/emcmake \
  cmake -S . -B build/latin-unicode-wasm \
  -DCMAKE_BUILD_TYPE=MinSizeRel \
  -DENABLE_TESTS=OFF \
  -DENABLE_WEB_COMPRESSION=OFF

cmake --build build/latin-unicode-wasm --target \
  latin_unicode_poc_size latin_unicode_poc_into_size \
  latin_unicode_utf8proc_size

node whitakers-words/poc/latin-unicode/measure_compression.mjs \
  build/latin-unicode-wasm/whitakers-words/poc/latin-unicode/latin_unicode_poc_size.wasm \
  build/latin-unicode-wasm/whitakers-words/poc/latin-unicode/latin_unicode_poc_into_size.wasm \
  build/latin-unicode-wasm/whitakers-words/poc/latin-unicode/latin_unicode_utf8proc_size.wasm
```

Para comparar também as representações destiladas de
`utf8proc_category`, gere as fontes com a tool nativa e compile-as diretamente
com os mesmos objetivos de tamanho dos microtargets:

```sh
cmake --build build --target utf8proc_destilation
(cd build/tools && ./utf8proc_destilation)

/mnt/projects/Projects/emsdk/upstream/emscripten/em++ \
  build/tools/unicode_table.cpp \
  -std=c++23 -Oz -flto \
  -sWASM=1 -sFILESYSTEM=0 -sENVIRONMENT=node \
  -o build/tools/unicode_table.mjs

/mnt/projects/Projects/emsdk/upstream/emscripten/em++ \
  build/tools/unicode_switch.cpp \
  -std=c++23 -Oz -flto \
  -sWASM=1 -sFILESYSTEM=0 -sENVIRONMENT=node \
  -o build/tools/unicode_switch.mjs

node whitakers-words/poc/latin-unicode/measure_compression.mjs \
  build/latin-unicode-wasm/whitakers-words/poc/latin-unicode/latin_unicode_poc_size.wasm \
  build/latin-unicode-wasm/whitakers-words/poc/latin-unicode/latin_unicode_poc_into_size.wasm \
  build/latin-unicode-wasm/whitakers-words/poc/latin-unicode/latin_unicode_utf8proc_size.wasm \
  build/tools/unicode_table.wasm \
  build/tools/unicode_switch.wasm
```

As fontes geradas marcam `get_unicode_category` com `EMSCRIPTEN_KEEPALIVE`,
disponibilizando `_get_unicode_category` pelo módulo Emscripten e impedindo que
o LTO elimine o lookup ou seus dados. O gerador permanece nativo; fontes e
artefatos destilados continuam somente nos diretórios de build.

`measure_compression.mjs` não grava cópias: imprime tamanho bruto, gzip nível 9,
Brotli qualidade 11 e SHA-256.
