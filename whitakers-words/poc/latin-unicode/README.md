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
- `unicode_backend_compact_poc` e `unicode_backend_full_poc`: fachadas com os
  mesmos tipos pequenos e símbolos separados para comparação no mesmo link;
- `unicode_backend_poc_tests`: equivalência exaustiva de `words_category`,
  `iterate` e `encode_char` contra utf8proc;
- `unicode_backend_{compact,full,selected}_size`: microexecutáveis conjuntos
  de decoding, categoria e normalização;
- `unicode_backend_poc_benchmark`: workload conjunto para tempo, Callgrind,
  DHAT e LLVM fprofile;
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
cmake --build build --target \
  latin_unicode_poc_tests unicode_backend_poc_tests
build/whitakers-words/poc/latin-unicode/latin_unicode_poc_tests
build/whitakers-words/poc/latin-unicode/unicode_backend_poc_tests
```

O teste executa, entre outras matrizes, todos os escalares Unicode válidos, as
444.828 sequências de até três escalares do alfabeto aceito, todas as strings
de um a três bytes e 8.388.608 casos estruturais de quatro bytes com starters
`F0`–`F7`. Ambas as APIs são comparadas campo a campo ao oracle.

A fachada adicional fixa utf8proc 2.11.3/Unicode 17.0.0, compara
`words_category` em todos os escalares, compara precisamente retorno, escalar e
largura de `iterate` nas enumerações de bytes e compara `encode_char` em todo
escalar válido. Os bits internos gerados têm `static_assert` contra
`words::BoundaryFlag`.

## Fachada selecionável e integração interna

`unicode_backend_selected.hpp` resolve somente por compile time. A opção
experimental `WORDS_POC_UNICODE_BACKEND=AUTO|FULL|COMPACT` controla o target
`unicode_backend_selected_size`; `AUTO` significa FULL nativo e COMPACT no
Emscripten. Os targets com nomes `compact` e `full` sempre forçam cada lado.

A fachada promovida em `src/unicode_backend.*` é a fronteira privada de
`words_core`. A opção de produção `WORDS_UNICODE_BACKEND=AUTO|FULL|COMPACT`
tem a mesma resolução: FULL nativo e COMPACT no Emscripten em `AUTO`, com os
dois modos explícitos disponíveis em qualquer plataforma. `TextTokenCursor`
usa `iterate` e `words_category`; o caminho não-ASCII de `LatinLexer` usa o
normalizador finito somente em COMPACT. O fast path ASCII não foi alterado.
FULL preserva a implementação utf8proc anterior.

`words_category(codepoint)` já devolve a projeção de quatro `BoundaryFlag`. O
backend full contém a sequência atual de testes sobre `utf8proc_category`; o
gerado grava os quatro bits diretamente. `iterate` usa ponteiro, comprimento e
inteiros fixos. O backend compacto não oferece um `map` Unicode geral: o menor
bloco futuro de `LatinLexer::lex` deve selecionar o
`LatinSurfaceNormalizer` inteiro.

A fonte de categoria integrável é reproduzida pela tool nativa, sem `main`,
`std::print`, exports Emscripten ou keepalive:

```sh
cmake --build build --target utf8proc_destilation
build/tools/utf8proc_destilation --library-output-dir \
  whitakers-words/poc/latin-unicode/generated
```

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

cmake --build build/latin-unicode-minsize --target \
  unicode_backend_compact_size \
  unicode_backend_full_size \
  unicode_backend_selected_size \
  unicode_backend_poc_benchmark

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

Para o driver conjunto, uma execução percorre todos os 1.112.064 escalares em
decode e categoria e normaliza 6.877 palavras/40.321 bytes. Os símbolos
`run_{compact,full}_{decode,category,normalize}` mantêm as etapas separáveis:

```sh
valgrind --tool=callgrind --collect-atstart=no \
  '--toggle-collect=*run_compact_backend*' \
  build/latin-unicode-minsize/whitakers-words/poc/latin-unicode/unicode_backend_poc_benchmark \
  compact 1

valgrind --tool=dhat \
  build/latin-unicode-minsize/whitakers-words/poc/latin-unicode/unicode_backend_poc_benchmark \
  full 1
```

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

cmake --build build/latin-unicode-wasm --target \
  unicode_backend_compact_size \
  unicode_backend_full_size \
  unicode_backend_selected_size

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

for variant in \
  unicode_table \
  unicode_switch \
  unicode_boundary_switch \
  unicode_boundary_fallback_switch
do
  /mnt/projects/Projects/emsdk/upstream/emscripten/em++ \
    "build/tools/${variant}.cpp" \
    -Iinclude -std=c++23 -Oz -flto \
    -sWASM=1 -sFILESYSTEM=0 -sENVIRONMENT=node \
    -o "build/tools/${variant}.mjs"
done

node whitakers-words/poc/latin-unicode/measure_compression.mjs \
  build/latin-unicode-wasm/whitakers-words/poc/latin-unicode/latin_unicode_poc_size.wasm \
  build/latin-unicode-wasm/whitakers-words/poc/latin-unicode/latin_unicode_poc_into_size.wasm \
  build/latin-unicode-wasm/whitakers-words/poc/latin-unicode/latin_unicode_utf8proc_size.wasm \
  build/tools/unicode_table.wasm \
  build/tools/unicode_switch.wasm \
  build/tools/unicode_boundary_switch.wasm \
  build/tools/unicode_boundary_fallback_switch.wasm
```

Tabela e switch preservam as sete categorias utf8proc como controles. O switch
de fronteira colapsa-as nas quatro `BoundaryFlag` consumidas pelo lexer; a
variante de fallback também omite os 22 codepoints já interceptados pelo switch
especializado anterior em `boundary_flag`. Esta última só representa o fallback,
não a função completa quando chamada isoladamente.

As fontes geradas de microbenchmark marcam seus lookups com
`EMSCRIPTEN_KEEPALIVE`, impedindo que
o LTO elimine o código ou seus dados. O gerador permanece nativo; fontes e
artefatos isolados continuam somente nos diretórios de build. A fonte de
biblioteca `generated/compact_words_category.cpp`, sem exports nem I/O,
participa do link somente quando o backend COMPACT é selecionado.

`measure_compression.mjs` não grava cópias: imprime tamanho bruto, gzip nível 9,
Brotli qualidade 11 e SHA-256.

## Primeiro A/B no WASM principal

Medição de 2026-09-12 com Emscripten 5.0.6, `Release`, LTO do target principal,
testes e geração automática de arquivos comprimidos desabilitados. FULL e
COMPACT foram configurados em árvores separadas; os números comprimidos foram
calculados sobre o mesmo `.wasm` por `measure_compression.mjs`.

| backend | `.wasm` bruto | gzip-9 | Brotli-11 | `.mjs` bruto |
| --- | ---: | ---: | ---: | ---: |
| FULL | 896.863 B | 251.558 B | 172.667 B | 56.721 B |
| COMPACT | 581.317 B | 170.431 B | 122.126 B | 56.721 B |
| redução | 315.546 B (35,18%) | 81.127 B (32,25%) | 50.541 B (29,27%) | 0 B |

`AUTO` no Emscripten gerou um `.wasm` byte a byte idêntico ao COMPACT
(SHA-256 `26431712921f31bf7cc787f6101b47b096ee689b460f6c94cb2f6d518a547f82`).
O fixture de contrato principal produziu exatamente os mesmos 191.786 bytes de
JSON nos três builds (FULL, COMPACT e AUTO), e o smoke test completo passou nos
dois backends explícitos. No executável nativo COMPACT, a inspeção de símbolos
e dependências também não encontrou `utf8proc`; o build FULL continua contendo
e expondo a dependência anterior.

O cursor mantém a interface raw de `iterate` (ponteiro, comprimento e inteiro),
e o decoder compacto subjacente é `always_inline`. Forçar também a fachada
interna a `always_inline` aumentou o WASM COMPACT para 581.414 B bruto e
122.366 B em Brotli (+97 B e +240 B); por isso a fachada permanece `inline`
normal e deixa a decisão de duplicação para o otimizador.
