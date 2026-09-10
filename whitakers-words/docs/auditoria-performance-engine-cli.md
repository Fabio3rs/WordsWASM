# Auditoria de performance da engine e do CLI C++23

Executada em 10 de setembro de 2026. A primeira coleta registrou somente a
investigação e o harness de profiling. Depois dela, F-04, F-05a e F-05b foram
implementadas isoladamente e medidas novamente contra o mesmo corpus,
executável e configuração. As demais propostas continuam sendo possibilidades
de estudo.

## Resumo executivo

Os perfis separam três domínios de custo:

1. no formato `analysis`, a construção e destruição do DOM JSON é o custo
   dominante do CLI;
2. dentro da engine, o scheduler de síncope/ortografia, o lexer Unicode usado
   também por formas internas e os custos restantes de consulta aos índices
   são os hotpaths mais promissores;
3. na inicialização, o loader reconstrói e ordena índices que poderiam vir
   prontos do WWDB.

As oportunidades com melhor relação entre impacto e risco são:

- escrever JSON diretamente em um buffer ou `ostream`, sem construir um DOM;
- manter um cache limitado por consulta no modo batch;
- canonizar uma vez somente as chaves não canônicas dos índices em uma arena
  privada pequena, permitindo comparação por bytes no steady-state
  (implementado e medido em F-05b);
- adicionar um fast path ASCII ao lexer, preservando o caminho Unicode;
- indexar regras de reescrita por tipo, estágio e prioridade;
- percorrer os spans de terminações sem materializar `CandidateIR`
  (implementado e medido em F-04);
- comparar a chave armazenada com a consulta já canônica sem normalizar os dois
  lados (implementado e medido em F-05a);
- experimentar tries compactos para stems e afixos;
- persistir os índices já ordenados no WWDB;
- substituir a normalização Unicode geral por um lexer de domínio latino com
  fast path ASCII e tabelas finitas para quantidade vocálica.

F-04 substituiu o vetor transitório de `CandidateIR` por grupos inline e
materialização por valor somente no consumo, removendo 18.790.000 bytes de
alocações. F-05a retirou a normalização do lado já canônico da consulta, e
F-05b passou a canonizar uma vez as chaves armazenadas. Em sequência, as três
mudanças levaram a região isolada de 788.641.095 para 503.316.489 instruções
(-36,18%), de 137.438.204 para 72.000.199 branches (-47,61%) e de 2.037.545
para 1.727.231 misses D1 (-15,23%), com checksum e semântica preservados. O CI
de F-05a e as validações nativa, WASM e sanitizada de F-05b passaram.

## Ambiente e metodologia

O executável de investigação foi compilado com Clang 21 em
`RelWithDebInfo`, usando:

```text
-O2 -g -DNDEBUG -fno-omit-frame-pointer
```

Ferramentas usadas dentro do NativeLab MCP:

- Callgrind, inclusive com simulação de caches no perfil `search`;
- DHAT para volume, quantidade e origem das alocações;
- GDB com `ptrace` para inspecionar tipos e cardinalidades em runtime;
- `strace -c` para separar custo de sistema operacional;
- `/usr/bin/time`, `llvm-size` e `llvm-nm`.

O `perf` por hardware não pôde ser usado porque a ferramenta correspondente ao
kernel `6.14.0-1020` não estava instalada no ambiente. Callgrind fornece
contagens instrumentais determinísticas, mas não substitui uma amostragem de
hardware para branch misses, IPC e comportamento real do último nível de cache.

### Corpora

Foram usados dois conjuntos:

- `test/01_aeneid/input.txt`: Eneida IV completa, 735 linhas, 4.573 tokens
  alfabéticos e 2.726 tokens distintos;
- uma amostra de 30.000 tokens alfabéticos da Latin Library, com 10.022 formas
  distintas.

O banco externo
`/mnt/projects/Projects/thelatinlibrary/var/thelatinlibrary.sqlite3` foi aberto
somente por:

```text
sqlite3 -readonly 'file:/mnt/projects/Projects/thelatinlibrary/var/thelatinlibrary.sqlite3?mode=ro&immutable=1'
```

Não houve escrita no banco da Latin Library.

O banco morfológico usado no perfil principal foi
`poc/compact-db/output/words-poc-dense.wwdb`, com 2.731.947 bytes.

## Harness dedicado da engine

O alvo `words_engine_benchmark` liga apenas `words_core`, sem `words_json`. Ele
é `EXCLUDE_FROM_ALL`, portanto só é compilado quando solicitado:

```sh
cmake --build build/perf-investigation-clang \
  --target words_engine_benchmark
```

Exemplo sobre a Eneida:

```sh
build/perf-investigation-clang/words_engine_benchmark \
  --database whitakers-words/poc/compact-db/output/words-poc-dense.wwdb \
  --corpus whitakers-words/test/01_aeneid/input.txt \
  --mode lines \
  --warmup 1 \
  --iterations 5
```

O banco e o corpus são carregados antes do warmup e da região cronometrada. Há
três modos:

- `queries`: cada linha física não vazia é passada separadamente a
  `Engine::analyze_text`; destina-se a corpora pré-tokenizados com uma consulta
  de um ou dois tokens por linha;
- `lines`: cada linha física não vazia é passada a `Engine::analyze_line` e o
  vetor resultante é destruído antes da linha seguinte;
- `corpus`: o arquivo inteiro é passado de uma vez a `Engine::analyze_line`,
  mantendo todos os `QueryResult` vivos até o fim da iteração.

Essa distinção é necessária porque a API atual de `analyze_text` aceita somente
um ou dois tokens. Textos arbitrários devem usar `analyze_line`.

`--iterations N` repete somente a operação selecionada. `--warmup N` executa o
mesmo caminho antes da medição e imprime um checksum separado. O checksum
medido é:

```text
quantidade de QueryResult + análises lexicais + compostas + artificiais
```

Ele torna o resultado observável sem percorrer a projeção JSON. O executável
também aceita os flags de ablação `--no-*` e os três modos de ortografia usados
pelo CLI.

Quando os headers de Valgrind estão disponíveis, client requests zeram e
iniciam Callgrind imediatamente antes das iterações medidas e interrompem a
coleta antes da impressão. Portanto, a seguinte execução exclui loader, leitura
do corpus, warmup e `println`:

```sh
valgrind \
  --tool=callgrind \
  --instr-atstart=no \
  --cache-sim=yes \
  --branch-sim=yes \
  --callgrind-out-file=/tmp/words-engine.callgrind \
  build/perf-investigation-clang/words_engine_benchmark \
  --database whitakers-words/poc/compact-db/output/words-poc-dense.wwdb \
  --corpus whitakers-words/test/01_aeneid/input.txt \
  --mode lines --warmup 1 --iterations 1
```

DHAT e `strace` podem usar o mesmo executável:

```sh
valgrind --tool=dhat \
  --dhat-out-file=/tmp/words-engine.dhat.json \
  build/perf-investigation-clang/words_engine_benchmark \
  --database whitakers-words/poc/compact-db/output/words-poc-dense.wwdb \
  --corpus whitakers-words/test/01_aeneid/input.txt \
  --mode lines --iterations 1

strace -c \
  build/perf-investigation-clang/words_engine_benchmark \
  --database whitakers-words/poc/compact-db/output/words-poc-dense.wwdb \
  --corpus whitakers-words/test/01_aeneid/input.txt \
  --mode lines --iterations 1
```

### Validação inicial do harness

Os modos `lines` e `corpus` produziram o mesmo resultado sobre o arquivo bruto:

```text
checksum=22723 units=4570 analyses=18153
```

O corpus tem 735 linhas físicas, das quais 705 são não vazias. O tokenizer da
engine produziu 4.570 unidades. O modo `queries`, executado sobre a extração
pré-tokenizada de 4.573 linhas, produziu:

```text
checksum=22768 units=4573 analyses=18195
```

Com três iterações, o checksum de `lines` foi exatamente `68169`, três vezes o
valor de uma iteração.

### Primeiro perfil isolado

Callgrind, no modo `lines`, uma iteração depois de um warmup:

| Evento | Contagem |
| --- | ---: |
| instruções | 788.641.095 |
| referências a dados | 223.088.957 |
| misses D1 | 2.037.545 |
| misses de dados no último nível | 22.432 |
| branches | 137.438.204 |
| mispredicts | 7.841.566, 5,7% |

Hotpaths inclusivos dentro dessa região sem JSON e sem loader:

- `analyze_lexical_surface`: 81,8%;
- `append_word_analyses`: 71,3%;
- `analyze_syncope`: 51,3%;
- `analyze_orthography`: 27,5%;
- `lookup_stem`: 12,4%;
- `lookup_prefix`: 12,4%;
- `enumerate_candidates`: 8,0%;
- `lookup_suffix`: 7,8%;
- `LatinLexer::lex`: 7,5%;
- `rewrite_attempts`: 6,1%.

Os valores inclusivos se sobrepõem. A principal utilidade deste resultado é
confirmar que os achados da engine não eram artefatos da projeção JSON.

O DHAT do harness, sem warmup, mediu 54.086.548 bytes em 122.562 blocos, pico
de 9.685.767 bytes e zero bytes vivos ao final. O allocation point de
`CandidateIR` permaneceu exatamente em 18.790.000 bytes e 9.211 blocos.

#### Decomposição das alocações da engine

As backtraces do DHAT permitem atribuir exatamente os 54.086.548 bytes:

| Origem | Bytes acumulados | Percentual | Blocos |
| --- | ---: | ---: | ---: |
| materialização de `CandidateIR` | 18.790.000 | 34,7% | 9.211 |
| crescimento de `vector<AnalysisIR>` | 13.946.400 | 25,8% | 12.570 |
| crescimento de `vector<QueryResult>` | 8.947.816 | 16,5% | 2.834 |
| loader e índices do WWDB | 6.869.024 | 12,7% | 113 |
| buffers de banco/corpus do harness | 2.782.593 | 5,1% | 6 |
| lexer e utf8proc | 1.057.358 | 2,0% | 56.161 |
| outros | 699.289 | 1,3% | 3.719 |
| vetores temporários de IDs de addons | 413.540 | 0,8% | 30.074 |
| cópia de `DatasetIdentity` | 329.256 | 0,6% | 4.573 |
| tentativas de reescrita | 251.272 | 0,5% | 3.301 |

Os três primeiros itens representam 77,0% de todos os bytes alocados. Se
loader e buffers de entrada forem excluídos, eles representam 93,8% do churn
da região de análise.

Os 54 MiB são volume acumulado de `malloc`/`new`, não memória simultaneamente
residente. O pico foi 9,69 MiB e ocorreu próximo da construção do banco; não há
vazamento.

##### `CandidateIR`: maior origem por bytes

O `reserve` da implementação de baseline de `enumerate_candidates` alocou
939.500 slots de 20 bytes. Foram 9.211 vetores, com média de aproximadamente
102 candidatos por materialização. O corpus terminou com somente 18.153
análises, cerca de 52 candidatos materializados para cada análise emitida.

Essa alocação não é crescimento geométrico: o código conta os candidatos e
reserva o tamanho exato. A origem é a decisão de copiar os oito spans de regras
para uma representação plana temporária. A enumeração lazy implementada em
F-04 removeu diretamente todos os 18,79 MB.

##### `AnalysisIR`: objetos grandes e resultados intermediários

Cada `AnalysisIR` possui 312 bytes. Os allocation points dos `push_back` em
[`append_regular_analyses`](../../src/engine.cpp#L439) e nos demais caminhos
somaram capacidade acumulada para 44.700 objetos, embora apenas 18.153 análises
tenham chegado ao checksum final.

Há duas causas combinadas:

- crescimento geométrico dos vetores;
- construção de análises completas em tentativas derivacionais que depois são
  descartadas ou deduplicadas.

Uma direção mais estrutural é acumular descritores compactos, ordenar e
deduplicar esses descritores, reservar o total conhecido e materializar cada
`AnalysisIR` grande apenas uma vez. Uma arena de scratch reutilizável pode ser
avaliada para intermediários, mas o resultado público continua precisando de
armazenamento proprietário.

##### `QueryResult`: custo da API `analyze_line`

Os `push_back` em [`Engine::analyze_line`](../../src/engine.cpp#L3029) alocaram
capacidade cumulativa para 10.859 objetos de 824 bytes, contra 4.570 unidades
finais. A diferença vem principalmente do crescimento 1, 2, 4, 8... repetido
para cada uma das 705 linhas não vazias.

No modo `queries`, que devolve um `QueryResult` por chamada e não constrói o
vetor externo, o DHAT caiu de 54,09 MiB para 45,25 MiB. Isso confirma a ordem de
grandeza dos 8,95 MB atribuídos ao vetor.

Possíveis APIs para consumidores de corpus:

- callback/sink `analyze_line(text, options, consumer)`, emitindo cada resultado
  sem formar um vetor;
- range/generator, caso o custo e o tamanho de código da corrotina sejam
  aceitáveis;
- como mudança menor, reservar uma estimativa baseada no número de tokens da
  linha, aceitando uma pré-passagem barata.

O sink é compatível com o lookahead verbal existente. A engine não pode emitir
o resultado corrente antes de analisar o token seguinte, pois ambos podem virar
um composto; contudo, precisa manter vivos somente `current` e `next`. Depois
da decisão, o resultado corrente pode ser movido ao consumidor. O uso de
memória do vetor externo cai de O(tokens da linha) para O(maior par de
resultados), sem alterar a semântica.

##### Sink e WebAssembly

O binding atual materializa a análise em etapas:

```text
Engine::analyze_line
  -> vector<QueryResult>
  -> vector<BrowserSearchResult>
  -> handle Embind
  -> Array e objetos JavaScript via size()/get()
```

[`BrowserAnalysisEngine::analyze_line`](../../wasmsrc/main.cpp#L848) conserva o
`vector<QueryResult>` inteiro enquanto projeta strings e hits para um segundo
vetor. Depois, [`copyOwnedVector`](../../wasmsrc/words-engine.mjs#L70) atravessa
o vetor retornado e seus vetores aninhados com chamadas Embind. Portanto, um
sink interno também serve ao WASM: ele pode projetar cada `QueryResult&&`
diretamente para o `vector<BrowserSearchResult>`, eliminando a primeira coleção
e mantendo uma única chamada pública C++ -> JavaScript.

Um callback C++ -> JavaScript por token não é a primeira opção. Na Eneida isso
criaria 4.570 travessias da fronteira Embind, além das travessias dos vetores de
hits, e introduziria tratamento de exceção/reentrância no meio da análise. É
provável que troque allocator churn nativo por overhead de binding.

Desenho compatível com os dois ambientes:

- manter o algoritmo de iteração uma única vez no `.cpp`;
- expor um consumidor síncrono não proprietário (`function_ref`, ou
  `void *` + função trampoline) que receba `QueryResult&&`, evitando
  `std::function` e sua possível alocação;
- implementar a API atual que retorna vetor como um sink que faz `push_back`,
  preservando compatibilidade;
- no adaptador WASM, usar outro sink que chama `browser_search_result` e move o
  resultado projetado ao vetor final;
- no benchmark/CLI streaming, consumir checksum ou escrever a apresentação sem
  reter a linha inteira.

Para entradas realmente grandes no navegador, uma segunda API compacta pode
usar lotes ou uma representação plana/colunar com arena de strings e uma única
cópia para JavaScript. Uma `typed_memory_view` persistente requer cuidado:
o build usa `ALLOW_MEMORY_GROWTH=1`, portanto crescimento da memória pode
invalidar views. Isso é uma evolução independente; o sink C++ não obriga a
mudar o contrato JS atual.

##### Lexer e addons: maior origem por quantidade de blocos

Os bytes do lexer são modestos, mas ele responde por 56.161 alocações, 45,8%
de todos os blocos. Foram 8.023 superfícies lexadas, incluindo formas geradas
por reescritas. Cada superfície provocou sete alocações:

- quatro allocation events sob os dois `utf8proc_map`, 32.092 blocos e 616.312
  bytes, em [`map_utf8`](../../src/lexer.cpp#L203);
- um `vector<Glyph>`, 8.023 blocos, em
  [`LatinLexer::lex`](../../src/lexer.cpp#L419);
- um vetor de offsets, 8.023 blocos, em
  [`build_logical_offsets`](../../src/lexer.cpp#L275);
- um vetor de quantidades, 8.023 blocos, em
  [`LatinLexer::lex`](../../src/lexer.cpp#L466).

Os quatro eventos sob `utf8proc_map` têm uma explicação determinística. Cada
uma das 16.046 chamadas a `map_utf8` executa
[`utf8proc_map_custom`](../../vendor/utf8proc/utf8proc.c#L776), que:

1. calcula o tamanho decomposto sem alocar;
2. faz `malloc` de um buffer temporário de `int32_t`;
3. o recompõe *in place* como bytes UTF-8;
4. chama `realloc` para encolher o bloco ao tamanho final.

O DHAT registra o `malloc` e o `realloc` como dois eventos, ambos atribuídos ao
allocation point do buffer. Assim, `16.046 x 2 = 32.092` blocos: não é leak nem
volume oculto, mas é churn real da API genérica. O lexer chama essa API uma vez
para `DECOMPOSE|CASEFOLD` e outra para `COMPOSE` em cada superfície.

O Callgrind separou as 8.023 invocações do lexer em:

| origem | chamadas |
| --- | ---: |
| entrada original em `Engine::analyze` | 4.573 |
| superfícies tentadas por síncope | 2.640 |
| superfícies tentadas por ortografia | 793 |
| base de ortografia + enclítico | 17 |

Logo, 3.450 chamadas, ou 43,0%, são formas internas produzidas por mecanismos
da engine. No corpus ASCII medido, todas as chamadas genéricas ao Unicode são
evitáveis. O contrato completo de caracteres e transformações necessário para
substituir a biblioteca está inventariado em
[`plano-unicode-quantidade-vocalica.md`](plano-unicode-quantidade-vocalica.md#inventário-para-retirar-utf8proc).

Os vetores de IDs de addons acrescentam outros 30.074 blocos, mas somente 414
KiB. Eles surgem ao agregar resultados de prefixos e sufixos em
[`add_matching_prefixes`](../../src/engine.cpp#L729) e
[`add_matching_suffixes`](../../src/engine.cpp#L740). Lexer e IDs de addons
juntos representam 70,4% de todas as chamadas ao allocator.

Portanto, há duas prioridades diferentes:

- para reduzir bytes copiados: candidatos e vetores de resultados;
- para reduzir chamadas ao allocator: fast path ASCII e enumeração de addons
  sem pequenos vetores temporários, ou com buffer inline limitado.

##### Identidade e reescritas

`DatasetIdentity` alocou aproximadamente 72 bytes em cada uma de 4.573
construções. Como o formato é sempre `sha256:` mais 64 dígitos, armazenar o
digest binário em 32 bytes inline eliminaria essa alocação e preservaria uma
identidade que pode sobreviver à engine. Um ponteiro internado seria menor,
mas mudaria a semântica de lifetime dos resultados.

As tentativas de reescrita alocaram apenas 251 KiB em 3.301 blocos. O problema
principal de F-03 é CPU por varreduras repetidas; em memória, elas são uma
prioridade menor que candidatos, resultados, lexer e addons.

##### Loader

O loader e os buffers de arquivo explicam aproximadamente 9,65 MiB da
inicialização e quase todo o pico de memória. Entre os maiores blocos estão:

- 2,21 MiB para os lexemas;
- 1,49 MiB para o índice temporário de 62.086 stems em
  [`database.cpp`](../../src/database.cpp#L1091);
- 1,32 MiB para views dos pools de strings;
- 1,17 MiB para grupos de stems em
  [`database.cpp`](../../src/database.cpp#L1135);
- 497 KiB para referências de stems.

Não é churn por consulta. Persistir os índices na ordem final, como proposto em
F-06, remove principalmente temporários e custo de startup.

O `strace -c` contou somente 92 syscalls, 1 chamada a `write` e cerca de 2,0 ms
totais em syscalls. Ao contrário do CLI JSONL, o harness não escreve uma linha
por token.

### LLVM instrumentation profile

Uma build independente pode gerar `.profraw` sem contaminar os flags da build
normal:

```sh
cmake -S . -B build/profile-instr -G Ninja \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DENABLE_TESTS=OFF \
  -DENABLE_SANITIZERS=OFF \
  -DCMAKE_C_COMPILER=clang \
  -DCMAKE_CXX_COMPILER=clang++ \
  -DCMAKE_CXX_FLAGS='-fprofile-instr-generate -fcoverage-mapping -fno-omit-frame-pointer' \
  -DCMAKE_EXE_LINKER_FLAGS='-fprofile-instr-generate'

cmake --build build/profile-instr --target words_engine_benchmark

LLVM_PROFILE_FILE=/tmp/words-engine-%p.profraw \
  build/profile-instr/words_engine_benchmark \
  --database whitakers-words/poc/compact-db/output/words-poc-dense.wwdb \
  --corpus whitakers-words/test/01_aeneid/input.txt \
  --mode lines --warmup 1 --iterations 3

llvm-profdata merge -sparse /tmp/words-engine-*.profraw \
  -o /tmp/words-engine.profdata
llvm-profdata show --counts --topn=20 /tmp/words-engine.profdata

llvm-cov show build/profile-instr/words_engine_benchmark \
  -instr-profile=/tmp/words-engine.profdata \
  -format=html \
  -output-dir=build/profile-instr/coverage-html \
  -show-line-counts-or-regions \
  -show-branches=count \
  -show-expansions
```

O índice navegável fica em `build/profile-instr/coverage-html/index.html`. Ele
é um heatmap de cobertura e frequência por linha/região, não um call graph nem
uma atribuição de tempo. Para árvore de chamadas e custos inclusivos, deve-se
abrir o arquivo Callgrind em KCachegrind; o perfil LLVM instrumentado não contém
pilhas de chamadas.

Na execução anterior a F-05a, que inclui um warmup e três iterações no perfil
LLVM, os maiores block counts foram:

- `normalized_char`: 70.183.235;
- `normalized_compare`: 35.091.563;
- `rewrite_attempts`: 17.816.000;
- `normalized_less`: 16.722.004;
- `append_suffix_analyses`: 7.363.072;
- `analyze_syncope`: 5.133.848.

Depois de F-05a, a mesma execução foi repetida com checksum 68.169. Os pontos
mais relevantes passaram a ser:

- `normalized_char`: 39.254.539;
- `stored_key_compare_canonical_query`: 30.928.696;
- `rewrite_attempts`: 17.816.000;
- `stored_key_less_canonical_query`: 16.722.004;
- `CandidateRange::EndingMatch::candidate`: 5.627.416;
- `analyze_syncope`: 5.133.848;
- `normalized_compare`: 4.162.867, agora restrito principalmente ao loader.

O contador de `rewrite_attempts` ficou exatamente igual, enquanto
`normalized_compare` caiu de 35,09 milhões para 4,16 milhões e
`normalized_char`, de 70,18 milhões para 39,25 milhões. Isso confirma que
F-05a retirou trabalho dos lookups sem mascarar a oportunidade independente de
F-03. O novo heatmap navegável está em
`build/profile-instr/coverage-html-post-f05/index.html`; ele mostra frequência
de blocos e branches, não tempo nem pilhas de chamada.

Depois de F-05b, o checksum continuou em 68.169 e o mesmo perfil passou a
mostrar:

- `canonical_compare`: 30.928.696 iterações;
- `canonical_less`: 16.722.004;
- `normalized_char`: 9.137.900, agora fora dos lookups de steady-state;
- `rewrite_attempts`: 17.816.000, novamente invariável;
- `CandidateRange::EndingMatch::candidate`: 5.627.416;
- `normalized_compare`: 4.162.867, restrito ao trabalho do loader.

O total de contagens do perfil caiu de 387.297.862 depois de F-05a para
312.182.249 depois de F-05b. O heatmap final está em
`build/profile-instr/coverage-html-f05b/index.html`. A permanência dos números
de rewrite e de construção lazy de candidatos reforça que o ganho veio da
comparação dos índices, não de uma alteração no trabalho semântico realizado.

### Ambiente de profiling WebAssembly

O `build/wasm` existente foi inspecionado antes de criar uma segunda árvore. A
configuração efetiva é:

| Componente | Caminho/versão |
| --- | --- |
| configuração | `Release`, `-O3 -DNDEBUG`, testes/sanitizers/compressão web desabilitados |
| Emscripten C++ | `/mnt/projects/Projects/emsdk/upstream/emscripten/em++`, 5.0.6 |
| LLVM/LLD | `/mnt/projects/Projects/emsdk/upstream/bin/{clang++,wasm-ld}`, 23.0.0, revisão `bbeae6932d653b8a71a3a985af0ccf97e13e2e08` |
| Binaryen | `/mnt/projects/Projects/emsdk/upstream/bin/wasm-opt`, 129 |
| CMake | `/home/fabio/.local/bin/cmake`, 3.30.1 |
| Ninja | `/home/fabio/.local/bin/ninja`, `1.11.1.git.kitware.jobserver-1` |
| Node usado manualmente | `/home/fabio/.npm-global/bin/node`, 20.18.0, V8 11.3.244.8 |
| artefatos | `build/wasm/words_wasm.{mjs,wasm}` e `build/wasm/words-engine.mjs` |

Há uma proveniência mista no cache antigo: `CMAKE_TOOLCHAIN_FILE` aponta para
`/usr/share/emscripten/cmake/Modules/Platform/Emscripten.cmake` e o emulator
para `/usr/bin/node --experimental-wasm-threads`, mas os compiladores realmente
registrados e usados pelo Ninja são os do SDK em
`/mnt/projects/Projects/emsdk`. Essa árvore continua válida, porém não deve ser
copiada como receita reprodutível.

A nova árvore `build/wasm-profile` foi configurada pelo `emcmake` do mesmo SDK,
que seleciona sem mistura:

```text
CMAKE_TOOLCHAIN_FILE=/mnt/projects/Projects/emsdk/upstream/emscripten/cmake/Modules/Platform/Emscripten.cmake
CMAKE_CROSSCOMPILING_EMULATOR=/mnt/projects/Projects/emsdk/node/22.16.0_64bit/bin/node
```

Esse Node embarcado é 22.16.0 com V8 12.4.254.21. O runner também passou no
Node 20.18.0 do ambiente. Perfis A/B devem fixar o mesmo executável Node: trocar
V8 entre as duas metades invalida a comparação, ainda que o checksum coincida.

Foram adicionadas duas opções CMake independentes:

- `WORDS_WASM_PROFILING=ON` acrescenta `--profiling-funcs` somente ao link e
  habilita o binding privado `benchmarkCorpus`;
- `WORDS_WASM_SOURCE_MAPS=ON` acrescenta `-gsource-map` à compilação de
  `words_core`/binding e ao link. É uma etapa opcional para navegação em fonte,
  não a configuração padrão do profiler amostral.

`--profiling-funcs` é adequado ao primeiro A/B: conserva a otimização e a
minificação normais, adicionando nomes de funções ao Wasm. O artefato de
profiling mediu 1.014.333 bytes contra 839.084 bytes do Release, enquanto o
glue `.mjs` permaneceu em 56.721 bytes. Esse crescimento inclui a name section
e o binding de benchmark; portanto, o tamanho dessa build não deve ser usado
como tamanho de entrega.

A opção de source map também foi compilada: produziu
`words_wasm.wasm.map` com 392.411 bytes e referências verificadas a
`src/engine.cpp`, `src/database.cpp` e `wasmsrc/main.cpp`. Ela fica desligada
no perfil de funções porque o `.cpuprofile` já mostrou nomes C++ suficientes;
deve ser ligada apenas quando uma amostra precisar ser levada até a linha.

Configuração reproduzível:

```sh
/mnt/projects/Projects/emsdk/upstream/emscripten/emcmake \
  /home/fabio/.local/bin/cmake \
  -S . -B build/wasm-profile -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DWORDS_WASM_PROFILING=ON \
  -DWORDS_WASM_SOURCE_MAPS=OFF \
  -DENABLE_TESTS=OFF \
  -DENABLE_SANITIZERS=OFF \
  -DENABLE_WEB_COMPRESSION=OFF

/home/fabio/.local/bin/cmake --build build/wasm-profile -j
```

#### Dois perfis delimitados depois do warmup

`scripts/profile-wasm.mjs` usa o Inspector do próprio Node para ligar o V8 CPU
profiler somente depois de carregar o módulo, validar o WWDB e aquecer a engine.
Ele oferece duas regiões com o mesmo corpus e checksum:

```text
core:
JS -> Embind (uma chamada) -> Engine::analyze_line × N -> checksum em C++

end-to-end:
JS -> Embind -> Engine::analyze_line -> BrowserSearchResult -> Embind
   -> copyOwnedVector/objetos JS, repetido N vezes
```

O binding `benchmarkCorpus` só existe quando `WORDS_WASM_PROFILING=ON`; não
altera a API pública nem o Release. Ele devolve apenas três números exatamente
representáveis por JavaScript no workload esperado. O modo end-to-end chama o
wrapper público sobre o corpus inteiro, que corresponde ao uso da caixa de
texto e mantém uma única travessia JS -> Wasm por iteração.

Exemplos:

```sh
/home/fabio/.npm-global/bin/node scripts/profile-wasm.mjs \
  --mode core --warmup 10 --iterations 50 --interval 250 \
  --output build/wasm-profile/profiles/baseline-core.cpuprofile

/home/fabio/.npm-global/bin/node scripts/profile-wasm.mjs \
  --mode end-to-end --warmup 5 --iterations 10 --interval 250 \
  --output build/wasm-profile/profiles/baseline-end-to-end.cpuprofile
```

O runner grava o `.cpuprofile` para Chrome DevTools e um
`.cpuprofile.summary.json` com checksum, tempos, corpus, tamanho dos artefatos e
SHA-256 do Wasm/glue, além das versões de Node/V8.
`scripts/compare-wasm-profiles.mjs` recusa comparar modos, runtime, corpus,
banco ou checksum incompatíveis:

```sh
node scripts/compare-wasm-profiles.mjs \
  build/wasm-profile/profiles/baseline-core.cpuprofile.summary.json \
  build/wasm-profile-candidate/profiles/candidate-core.cpuprofile.summary.json
```

Para comparar latência, repetir o mesmo comando com `--profiler none`; o
intervalo de 250 microssegundos tem overhead e serve principalmente para obter
o flame chart. As medianas temporais devem vir das coletas sem Inspector, e a
atribuição de hotpaths, das coletas com Inspector.

Um smoke test da baseline F-05b confirmou os nomes C++ dentro do `.cpuprofile`,
incluindo `Engine::analyze_line`, `analyze_lexical_surface`,
`enumerate_candidates` e os lookups. Ambos os modos produziram por iteração
`checksum=22723`, `units=4570` e `analyses=18153`. Em uma coleta curta no Node
20, sem pretensão de benchmark estável, `core` levou cerca de 101 ms e
end-to-end, 524 ms. A diferença grande confirma o valor de manter os dois
perfis, mas não é o ganho esperado do sink: ela inclui toda a projeção,
travessia de vetores, construção de objetos JS e garbage collection. O sink
proposto elimina somente a primeira coleção de `QueryResult` e sua retenção.

Para `perf`, o mesmo runner aceita `--profiler none`, deixando a delimitação ao
profiler externo:

```sh
perf record -F 999 -g -- \
  /home/fabio/.npm-global/bin/node \
  --perf-basic-prof-only-functions \
  scripts/profile-wasm.mjs --mode core --profiler none \
  --warmup 10 --iterations 100
```

No ambiente atual, inclusive dentro do NativeLab, esse comando ainda está
bloqueado por infraestrutura: `/usr/bin/perf` não possui as ferramentas para o
kernel `6.14.0-1020-oem` e `kernel.perf_event_paranoid=4`. `ptrace` habilitado
não concede automaticamente `perf_event_open`. O Inspector funciona agora e é
a baseline recomendada; o perfil de hardware fica preparado para quando o
pacote do kernel e a permissão forem disponibilizados.

O perfil manual no Chrome continua válido como confirmação final do consumidor
real e pode reutilizar `build/wasm-profile`; ele não substitui o A/B controlado
no mesmo V8. O `--profiling` completo também não traz vantagem nesta etapa:
preserva JS e whitespace adicionais para leitura, enquanto
`--profiling-funcs` já entregou os nomes necessários sem desligar otimizações.

Referências oficiais usadas para conferir esse desenho: documentação das
[flags de profiling e source map do Emscripten](https://emscripten.org/docs/tools_reference/emcc.html),
[profiling nos DevTools](https://emscripten.org/docs/porting/Debugging.html),
[Inspector do Node](https://nodejs.org/api/inspector.html) e
[flags `--cpu-prof`/`--perf-*` do Node](https://nodejs.org/api/cli.html).

Para o A/B do sink, devem ser preservadas quatro comparações separadas:

1. `core` baseline contra `core` candidato, medindo a engine Wasm;
2. `end-to-end` baseline contra `end-to-end` candidato, medindo o consumidor;
3. Release baseline contra Release candidato para `.wasm`, `.wasm.br` e glue;
4. Callgrind/DHAT nativos, que continuam sendo a fonte determinística de
   instruções e alocações internas.

Não se deve subtrair diretamente os percentuais de Callgrind dos samples V8,
nem atribuir toda a diferença end-to-end/core ao sink. Para cada modo, usar ao
menos cinco coletas alternadas A/B, comparar medianas e verificar o checksum
por iteração antes de abrir os flame charts.

## Tempos nativos

Os valores abaixo são tempos de parede representativos, com saída direcionada
para `/dev/null`. O cache de páginas do sistema operacional não foi descartado.

| Corpus | `search` | `analysis` | `search-v2` | `analysis-v2` |
| --- | ---: | ---: | ---: | ---: |
| Eneida IV, 4.573 tokens | 0,12 s | 0,30 s | 0,18 s | 0,36 s |
| Latin Library, 30.000 tokens | 0,66 s | 1,76 s | 1,03 s | 2,12 s |

No lote da Eneida, o `strace -c` atribuiu aproximadamente 2,8 ms a todas as
syscalls. Foram 4.573 chamadas a `write`, uma por resultado, mas elas não são
um gargalo neste ambiente.

## Fronteira entre engine e projeção

O Callgrind contou 1.580.372.282 instruções no `search` da Eneida:

- `Engine::analyze_text`: 49,98% inclusivos;
- `search_json`: 29,23% inclusivos;
- o restante inclui startup, parsing do CLI e bibliotecas de runtime.

No formato `analysis`, foram 4.225.772.138 instruções:

- caminho `analysis_json`/`write_result`: 72,6% inclusivos;
- `Engine::analyze_text`: 19,5% inclusivos;
- `full_lexeme_json`: 16,3% inclusivos, majoritariamente dentro do custo de
  construção do DOM;
- `dictionary_form`: somente 0,54% inclusivos.

Percentuais inclusivos possuem sobreposição e não devem ser somados.

## F-01 — DOM JSON domina alocações e o formato `analysis`

### Evidência

O DHAT mediu, para a Eneida completa:

| Formato | Bytes alocados | Blocos alocados | Bytes atribuídos ao JSON | Blocos atribuídos ao JSON |
| --- | ---: | ---: | ---: | ---: |
| `search` | 89.358.685 | 906.297 | 44.439.200 | 789.817 |
| `analysis` | 374.480.022 | 5.390.080 | 329.560.537 | 5.273.600 |

A classificação “JSON” foi feita pelas backtraces dos allocation points. Ao
término, o DHAT reportou zero bytes vivos: trata-se de churn, não de vazamento.

Os pontos principais estão em:

- [`full_lexeme_json` e `full_analysis`](../../src/json.cpp#L527);
- [`search_hits_json`](../../src/json.cpp#L738);
- construção do objeto raiz e `dump()` em [`search_json`](../../src/json.cpp#L1037).

`ordered_json` representa objetos como vetores de pares. Cada objeto aninhado
é construído, realocado, copiado ou movido, percorrido por `dump()` e finalmente
destruído.

### Hipótese

Introduzir um writer JSON que escreva diretamente no destino, preservando:

- schema e versão;
- ordem atual dos campos;
- escaping válido de strings;
- representação de `null`, números e arrays;
- uma linha atômica por consulta no modo batch.

Pode ser um writer pequeno e específico para os quatro formatos, ou uma API
SAX/output adapter que não materialize a árvore. Escrever diretamente em
`ostream` exige conservar buffering suficiente para não transformar cada campo
em uma syscall.

### Validação necessária

- comparar byte a byte a saída nova e antiga em todos os testes/corpora;
- fuzzar o escaping com controles, aspas, barras e UTF-8;
- repetir DHAT e Callgrind separadamente para os quatro formatos;
- medir também o tamanho final de `.text`.

## F-02 — O modo batch recalcula palavras repetidas

O loop em [`main.cpp`](../../src/main.cpp#L230) analisa e serializa cada linha sem
memoização.

### Evidência

- Eneida IV: 1.847 de 4.573 ocorrências repetem uma forma já presente, isto é,
  40,4%;
- amostra Latin Library: 19.978 de 30.000 ocorrências são repetições, 66,6%;
- `search` dos 30.000 tokens: 0,66 s; somente os 10.022 distintos: 0,32 s;
- `analysis` dos 30.000 tokens: 1,76 s; somente os distintos: 0,70 s.

Os tempos sobre formas distintas são um limite superior do ganho, pois uma
implementação real ainda precisa consultar o cache e reemitir cada linha.

### Hipótese

Adicionar ao CLI batch um LRU limitado cuja chave contenha, pelo menos:

- texto exato da consulta;
- opções morfológicas;
- formato e versão da projeção;
- identidade do dataset.

Guardar a linha JSON pronta elimina tanto a análise quanto a projeção nas
repetições. O cache deve ser limitado por bytes, não apenas por número de
entradas, porque uma linha `analysis` pode ser grande. Essa otimização pertence
ao CLI; a API geral da engine não precisa assumir política global de cache.

## F-03 — Scheduler de reescritas executa varreduras `O(P * R)`

O banco contém 170 regras de reescrita: 11 de síncope e 159 ortográficas.

[`analyze_syncope`](../../src/engine.cpp#L2207) percorre todas as regras para
descobrir as prioridades presentes. Para cada prioridade, chama
[`rewrite_attempts`](../../src/engine.cpp#L2118), que percorre novamente as 170
regras e descarta as que não coincidem com tipo, estágio e prioridade.
`analyze_orthography` repete a mesma estrutura.

### Evidência

No perfil `search` da Eneida:

- 25.199 chamadas a `rewrite_attempts` provenientes da síncope;
- 1.001 chamadas provenientes da ortografia;
- `analyze_syncope` chegou a 25,6% inclusivos das instruções do programa. Esse
  valor inclui análises lexicais chamadas a partir da síncope.

A ablação sobre 30.000 tokens, usando a mediana de tempo de usuário, deu:

| Configuração | Tempo | Variação aproximada |
| --- | ---: | ---: |
| padrão | 0,65 s | — |
| sem ortografia | 0,56 s | -14% |
| sem síncope | 0,46 s | -29% |
| sem sufixos | 0,53 s | -18% |
| somente análise lexical | 0,35 s | -46% |

Prefixos, tickons, tackons e packons isoladamente ficaram próximos do ruído
nessa amostra.

### Hipótese

No packer ou no loader, construir ranges contíguos por:

```text
(RewriteKind, RewriteStage, priority, medieval-mode)
```

O scheduler receberia um `span` de regras já relevantes e uma lista das
prioridades não vazias. O custo administrativo passa de `O(R + P*R)` para
`O(R_relevante)`, além do custo inevitável de procurar correspondências no
texto.

Uma segunda etapa pode indexar regras literais pelo primeiro caractere ou por
um trie. Com apenas 170 regras, os buckets contíguos são a primeira experiência
mais simples e de menor risco.

A ordenação das tentativas em [`rewrite_attempts`](../../src/engine.cpp#L2193)
também pode ser evitável se o packer e a enumeração já produzirem exatamente a
ordem normativa. Isso só pode ser feito depois de testes de equivalência,
porque a ordem afeta precedência e resultados.

## F-04 — Candidatos são materializados antes de serem consumidos — implementado

Na implementação de baseline, `enumerate_candidates` reunia no máximo oito
spans de grupos de terminações em uma estrutura na pilha. Depois contava todos
os itens, reservava um `std::vector<CandidateIR>` e copiava cada candidato para
ele. A implementação atual está descrita abaixo.

### Evidência

Na Eneida:

- 9.211 alocações no `reserve` da linha 1.214;
- 18.790.000 bytes alocados nesse ponto;
- `sizeof(CandidateIR) == 20` bytes;
- 939.500 candidatos materializados, aproximadamente 102 por enumeração;
- somente 18.033 hits chegaram à saída final: média 3,94 por token, mediana 3,
  p95 12, p99 20 e máximo 45.

O Callgrind atribuiu 62.685.894 instruções inclusivas a essas 9.211 chamadas,
7,95% das 788.641.095 instruções medidas. Destas, 39.116.016 instruções, ou
4,96% do programa, vieram somente de `Database::lookup_ending`; o restante
inclui a contagem, alocação e escrita dos 939.500 objetos.

### Implementação

`CandidateRange` conserva numa `std::array` os grupos não vazios encontrados
pelos mesmos `lookup_ending`. Cada `EndingMatch` contém os ranges do stem e da
terminação e um `span<const RuleId>` apontando para o armazenamento estável do
banco. `groups()` expõe esses descritores como span multipass; somente
`EndingMatch::candidate(rule_id)` sintetiza um `CandidateIR` por valor.

Os consumidores passaram a usar loops aninhados por grupo e regra. Além de
eliminar o vetor, isso torna explícito o ponto em que `lookup_stem` deve ocorrer
uma vez por comprimento e remove as comparações com `previous_stem`. Prefixos,
sufixos, packons e o caminho especial dos pronomes `qu-` preservam a ordem
anterior de comprimentos e `RuleId`.

Uma primeira versão experimental expôs diretamente um forward iterator
achatado. Embora não alocasse, a máquina de estado `(grupo, regra)` elevou o
perfil para 859.406.693 instruções (+8,97%). Uma versão com ponteiros e sentinel
caiu para 804.256.991, ainda +1,98%. Esses resultados motivaram a interface
orientada a grupos usada na implementação final: o lazy permanece no objeto
`CandidateIR`, sem cobrar um iterator achatado em todos os consumidores.

### Resultado A/B

Uma iteração `lines` da Eneida IV, depois de um warmup, produziu os seguintes
resultados no NativeLab:

Os artefatos locais estão em
`build/profile-instr/profiles/engine-lines{,-lazy}.callgrind` e
`build/profile-instr/profiles/engine-lines{,-lazy}.dhat.json`.

| Métrica | Vetor materializado | Range por grupos | Delta |
| --- | ---: | ---: | ---: |
| instruções | 788.641.095 | 692.809.841 | -12,15% |
| referências a dados | 223.088.957 | 196.037.719 | -12,13% |
| misses D1 | 2.037.545 | 1.696.357 | -16,75% |
| branches | 137.438.204 | 107.075.695 | -22,09% |
| mispredicts | 7.841.566 | 7.574.861 | -3,40% |
| bytes alocados pelo DHAT | 54.086.548 | 35.297.094 | -34,74% |
| blocos alocados pelo DHAT | 122.562 | 113.351 | -7,51% |
| pico de bytes vivos | 9.685.767 | 9.686.313 | +546 bytes |

O allocation point do vetor desapareceu exatamente: 18.790.000 bytes e 9.211
blocos a menos. O pico praticamente idêntico confirma que ele era churn
transitório, não memória simultaneamente residente. As 64.511 chamadas a
`lookup_ending` permaneceram iguais, isolando esta medição da proposta de índice
reverso. O custo inclusivo de `enumerate_candidates` caiu de 62.685.894 para
41.041.761 instruções (-34,52%).

O tamanho de código também não regrediu. A seção `.text` do benchmark otimizado
caiu de 689.443 para 688.439 bytes; em `engine.cpp.o`, de 136.007 para 135.003
bytes, 1.004 bytes a menos nos dois casos.

Os checksums continuaram em `22723` para `lines` e `22768` para as 4.573
consultas pré-tokenizadas. A comparação diferencial da Eneida preservou 2.630
resultados exatos, 41 diferenças somente de proveniência, 41 semanticamente
equivalentes e 14 resultados nativos aceitos, sem incompatibilidade nova.

Validação pós-implementação:

- builds Clang, GCC 14 e Emscripten/WASM concluídos;
- 112/112 testes do build nativo concluídos;
- 112/112 testes no NativeLab com ASan, UBSan e LeakSanitizer habilitados, sem
  diagnóstico;
- `git diff --check` limpo.

## F-05 — Lookups repetem buscas binárias e normalização de caracteres

Os métodos de [`Database::lookup_*`](../../src/database.cpp#L2061) usam
`std::ranges::lower_bound`. O comparador `normalized_less` aplica equivalência
latina caractere a caractere durante cada comparação.

Cardinalidades observadas no GDB:

- 39.415 lexemas;
- 1.785 regras flexionais;
- 48.463 grupos de stems e 62.086 referências;
- 459 grupos de terminações;
- 123 grupos de sufixos;
- 115 grupos de prefixos.

No Callgrind, os sete lookups de stem, ending e addons somaram 18,35% exclusivos
das instruções do `search`. Entre as chamadas observadas:

- `lookup_prefix`: 179.702;
- `lookup_suffix`: 116.922;
- `lookup_ending`: 64.511;
- `lookup_stem`: dezenas de milhares em diferentes caminhos derivacionais.

### Custo exato da enumeração de terminações

O banco medido contém 459 chaves canônicas de terminação e 1.785 `RuleId`. Para
cada palavra ou base derivada, `enumerate_candidates` consulta separadamente
os comprimentos `min(7, tamanho)` até zero. No perfil da Eneida:

| operação | contagem |
| --- | ---: |
| enumerações | 9.211 |
| buscas binárias | 64.511 |
| comparações do `lower_bound` | 578.308 |
| verificações finais de igualdade | 64.306 |
| buscas que encontraram um grupo | 25.011 |
| candidatos produzidos | 939.500 |

São 7,00 buscas e 62,78 comparações do `lower_bound` por enumeração. As
578.308 comparações equivalem a 8,96 por busca, coerente com
`log2(459) = 8,84`. Isso confirma que o `lower_bound` funciona conforme sua
complexidade; o custo vem de recomeçá-lo para cada tamanho e normalizar os dois
lados em cada comparação.

Cada enumeração encontra sempre a terminação vazia, cujo grupo possui 58
regras. Só esse grupo responde por `9.211 x 58 = 534.238` candidatos, 56,9% do
total materializado. Outros grupos largos são `a` (76 regras), `i` (72), `is`
(66), `um` (58), `e` (51) e `o` (48). Assim, inverter o índice pode remover o
custo das buscas, mas não elimina o fan-out normativo das regras; o range lazy
é o experimento complementar que elimina sua alocação, não sua avaliação.

Não há comportamento exponencial. Definindo `K = min(tamanho, 7) + 1`, `G`
como a quantidade de grupos, `E <= 7` como caracteres comparados e `C` como os
candidatos retornados, o limite é:

```text
O(K * log(G) * E + C)
```

Como `K` e `E` são limitados por oito e sete no schema, o custo de lookup é uma
constante alta por superfície; `C` é limitado pelas 1.785 regras. Se o limite
de terminação crescesse junto com a entrada, comparar todos os sufixos poderia
chegar a O(n^2 log G), mas ainda não seria exponencial.

O texto de consulta já chega em `lookup_ascii`: minúsculo, com `j -> i` e
`v -> u`. Entre as 460 terminações textuais de `INFLECTS.SEC`, somente `jus`
precisa de normalização e colapsa com `ius`, resultando nos 459 grupos. Portanto,
quase todas as chamadas a `normalized_char` feitas por essas comparações são
no-op. Canonizar as chaves no packer permitiria comparar bytes diretamente no
runtime.

### Possibilidade de estudo: trie reverso com dois iteradores

A proposta de percorrer a string ao contrário localizaria todos os finais numa
única descida:

```text
word.rbegin()  <->  nó atual do trie de terminações invertidas
       |                    |
       +-- consome letra ---+-- registra terminal, quando houver
```

O conjunto atual de 459 terminações gera somente 561 nós e 560 arestas depois
de canonizar `j/v`. A travessia consumiria no máximo 55.300 arestas nas 9.211
enumerações medidas, contra 578.308 comparações binárias, cada uma capaz de
examinar mais de um caractere. Cada nó terminal apontaria para o mesmo span
contíguo de `RuleId` usado hoje.

O iterador da palavra não exige `reverse`, cópia ou string temporária: pode ser
um índice decrescente ou `string_view::const_reverse_iterator`. O segundo
iterador é o estado/nó no trie. Ao faltar uma aresta, comprimentos maiores
também são impossíveis e a descida termina cedo.

O algoritmo atual emite comprimentos do maior para o menor, enquanto a descida
descobre terminais do menor para o maior. Para preservar precedência, inclusive
a terminação vazia, seria suficiente guardar até oito pares
`(ending_size, rules)` numa `std::array` e percorrê-los ao contrário. Sobre
esses spans, um range lazy poderia gerar `CandidateIR` por valor sempre que as
passagens da engine precisassem revisitá-los.

#### Mapa de consumidores e contrato do range lazy

Uma busca completa no código encontrou somente três pontos que criam esse conjunto:

- `append_word_analyses`, para o caminho regular e os fallbacks produtivos;
- `append_packon_analyses`, sobre a palavra-base sem o packon;
- `append_qu_pronoun_analyses`, para o tratamento dedicado de pronomes.

`CandidateIR` não escapa da engine. Quando há um hit, seus três campos (`RuleId`,
range do stem e range da terminação) são copiados para `AnalysisIR`. Portanto,
substituir o vetor transitório não exige mudar projeção, JSON, CLI ou bindings WASM.

O consumidor mais exigente é `append_word_analyses`. O caminho regular percorre os
candidatos uma vez; se não encontrar uma resposta que suprima derivações, passa o
mesmo conjunto para prefixos e sufixos. `append_prefix_analyses` faz uma passagem
para coletar prefixos possíveis e pode fazer outra para cada prefixo tentado.
`append_suffix_analyses` também faz uma passagem inicial, uma ou duas por sufixo e,
no fallback combinado, outras passagens por prefixo. Logo, o novo range precisa ser
**multipass** (`forward_range`), e não um gerador consumível ou `input_range`.

Esses consumidores dependem de duas propriedades de ordem do vetor atual:

1. terminações aparecem do maior comprimento para o menor, incluindo a vazia por
   último;
2. todas as regras de uma mesma terminação são adjacentes e mantêm a ordem de
   `RuleId` do índice.

O segundo ponto é usado para evitar repetir `lookup_stem`: os loops comparam o stem
atual com `previous_stem` e só consultam o banco quando muda o grupo. Uma interface
orientada a grupos tornaria essa dependência explícita e permitiria eliminar essas
comparações:

```text
WordEndingMatches
  word: SurfaceRange
  groups: array<EndingMatch, 8>

EndingMatch
  ending_size: inteiro pequeno
  rules: span<const RuleId>
```

O banco já mantém todos os 1.785 IDs em um único `ending_rule_ids_`; cada
`EndingMatch::rules` aponta para uma região contígua desse vetor. O conjunto de uma
palavra, porém, é uma sequência de até oito regiões distintas. Um único
`std::span<const RuleId>` não consegue representá-lo sem copiar ou duplicar regras
no banco. A terminação vazia, por exemplo, teria de ser repetida em praticamente
todo caminho se cada nó terminal armazenasse uma lista acumulada.

Foi escolhida uma única vista sem alocação:
`groups() -> span<const EndingMatch>`. Ela permite fazer `lookup_stem` uma vez
por comprimento e depois percorrer diretamente o span contíguo de regras. O
método `EndingMatch::candidate(rule_id)` sintetiza `CandidateIR` por valor
somente onde a API interna existente precisa dele. A cópia e o ajuste local nos
caminhos prefixados continuam válidos porque esse valor é independente.

Um iterator achatado *word-aware* chegou a ser medido, mas mesmo a variante com
ponteiros e sentinel acrescentou instruções ao hotpath. O span de grupos já é
multipass, usa o `end()` conhecido uma vez pelo range-for e deixa os dois loops
simples o bastante para o compilador otimizar.

Os spans permanecem válidos durante toda a análise porque apontam para armazenamento
imutável possuído por `Database`, que não é copiável nem movível. A vista deve ser
local à chamada e não escapar do tempo de vida do banco. Ela pode guardar apenas o
`SurfaceRange`, sem reter uma referência à string; `SurfaceForm` continua sendo
recebido separadamente pelos consumidores.

Alterar o índice de terminações também fica bem localizado. `lookup_ending` só é
usado por `enumerate_candidates` na produção e diretamente por testes do banco; sua
API de busca exata pode ser preservada sobre um índice futuro. `rules_`, os próprios
`RuleId`, `ending_strings_` e `inflection_quantities_` continuam necessários e não
precisam ser reordenados. O índice reverso pode continuar apontando para
`ending_rule_ids_`, preservando IDs e compatibilidade do restante do banco.

#### Duas otimizações independentes

O range lazy não depende da inversão nem do trie. A primeira experiência foi
concluída: `CandidateRange` é preenchido pelo laço e pelos `lookup_ending`
atuais. A mudança eliminou as 9.211 alocações e os 18.790.000 bytes de
`CandidateIR`, preservou o algoritmo de busca e limitou a comparação
diferencial a uma única variável.

Numa segunda experiência, a implementação que preenche os mesmos `EndingMatch`
pode trocar as até oito buscas binárias por uma descida reversa. Os consumidores
de `groups()` não mudam. Essa fronteira permite medir separadamente:

1. `lower_bound` atual + range lazy, agora implementado e medido;
2. índice reverso + vetor materializado, se desejado como controle cruzado;
3. índice reverso + range lazy.

Não é necessário executar `reverse()` nem persistir strings invertidas em nenhuma
das etapas. No primeiro caso, usam-se os `substr` atuais; no segundo, percorrem-se
os bytes da palavra com índice decrescente enquanto o índice do banco representa as
terminações do último caractere para o primeiro.

Esta ideia pode ser estudada em dois níveis independentes:

1. sem alterar o WWDB, construir o trie no loader a partir dos grupos atuais;
2. numa futura revisão do formato, persistir nós, arestas e spans já canônicos,
   removendo também a construção e ordenação desse índice no startup.

Uma variante intermediária seria ordenar as strings invertidas e usar
`lower_bound`/`upper_bound`. Inverter torna cada terminação candidata um prefixo
de `asor`, para a entrada `rosa`, mas há uma ressalva: todos os prefixos de uma
chave não formam necessariamente um intervalo lexicográfico limpo. Por exemplo,
`a`, `as`, `aso` e `asor` podem ter um ramo não correspondente como `ab` entre
eles. Um intervalo precisa filtrar falsos positivos ou de metadados adicionais;
o trie/radix é justamente a estrutura que representa sem ambiguidade o caminho
dos prefixos.

Um comparador “é prefixo de” também não pode ser passado diretamente ao
`lower_bound`: a relação não fornece a ordenação estrita fraca exigida pelo
algoritmo. Uma representação plana ainda pode funcionar com metadados de LCP ou
links para ancestrais, mas nesse ponto ela é uma serialização compacta de um
trie/radix, não apenas um vetor ordenado.

Buckets pelo primeiro caractere invertido continuam sendo um experimento menor
válido. No banco atual há apenas doze finais não vazios:
`a,c,d,e,i,m,n,o,r,s,t,u`; os buckets têm de 2 a 135 grupos, contra 459 no
índice global. Eles reduzem comparações e rejeitam imediatamente qualquer outro
caractere final, embora não eliminem por si sós a busca de todos os comprimentos.

Uma representação inicial simples usaria 26 transições `uint16_t` por nó,
cerca de 29 KiB antes dos metadados. Uma versão esparsa com bitmap de 26 bits,
offset de arestas e `popcount` pode ser menor e manter transição O(1). Ambas
precisam ser medidas no nativo e no WASM; por enquanto são somente hipóteses de
estudo, não uma proposta de mudança do formato.

Validação necessária antes de qualquer reescrita:

- comparar spans e ordem para todas as 459 chaves e para cada sufixo delas;
- preservar comprimento descendente e a ordem dos `RuleId` em cada span;
- testar terminação vazia, palavras menores, iguais e maiores que sete letras;
- testar a colisão canônica `jus`/`ius`;
- testar `SurfaceRange.begin != 0`, usado por packons e bases derivadas;
- comparar `QueryResult` nos corpora com mecanismos ligados e em ablações;
- medir isoladamente trie, range lazy de candidatos e ambos, acompanhando
  instruções, alocações, misses e tamanho dos binários nativo/WASM.

### Hipóteses

1. Normalizar definitivamente as chaves no packer, para que o hotpath compare
   bytes já canônicos, sem aplicar `i/j` e `u/v` a cada comparação.
2. Experimentar um trie/radix tree compacto para stems e prefixos, e uma
   versão reversa para sufixos e terminações.

O trie permite consumir uma palavra uma vez em cada direção e retornar todos
os terminais encontrados. Isso substitui buscas sobre vários cortes e reduz a
dependência de `O(log N)` comparações de strings por corte.

Não se recomenda trocar diretamente por `unordered_map`: os grupos de afixos
são pequenos, e o aumento de memória, indireções e código pode superar o ganho.
O experimento deve comparar tempo, misses simulados, RSS e tamanho do binário.

## F-06 — O loader reconstrói e ordena índices em todo processo

O loader cria vetores auxiliares e executa sorts distintos para stems, regras,
uniques, sufixos, prefixos, tickons e tackons. Os pontos começam em
[`database.cpp`](../../src/database.cpp#L1121) e reaparecem nas linhas 1.374,
1.577, 1.720, 1.944, 1.968 e 2.002.

### Evidência

- `Database::load_poc` possui 51.284 bytes de código no ELF;
- no `search` da Eneida, o corpo do loader representou 8,11% exclusivos das
  instruções;
- somente a ordenação do índice de stems representou 6,46% exclusivos;
- processo vazio com banco dense: 0,02–0,03 s e cerca de 12,5 MiB RSS;
- processo vazio com banco search-only: aproximadamente 0,02 s e 10,8 MiB RSS.

### Hipótese

Persistir no WWDB as referências na ordem final de consulta. O loader validaria
IDs, limites e monotonicidade em uma passagem `O(N)`, em vez de reconstruir e
ordenar em `O(N log N)`.

Além do ganho de startup, isso deve remover várias instanciações independentes
de introsort do binário. O packer paga o custo uma vez e deve emitir saída
determinística.

Uma evolução separada poderia oferecer backing por `mmap` no CLI nativo,
mantendo o caminho por `vector<byte>` para WebAssembly. Ela não deve preceder a
remoção dos sorts: o `strace` mostra que leitura física não é o custo principal.

## F-07 — O lexer Unicode não tem fast path para ASCII

[`LatinLexer::lex`](../../src/lexer.cpp#L398) faz, inclusive para uma palavra ASCII:

1. validação inicial com `utf8proc_iterate`;
2. `utf8proc_map` com decomposição e case folding;
3. alocação de `vector<Glyph>`;
4. preenchimento de strings, quantidades e offsets;
5. outro `utf8proc_map` para composição NFC;
6. uma nova passagem para construir offsets lógicos.

Os dois corpora medidos são predominantemente ASCII. O lexer não é o maior
percentual isolado de CPU, mas suas alocações se repetem em formas produzidas
pelas reescritas.

### Hipótese

Adicionar um caminho de uma passagem para `[A-Za-z]+`:

- validar e converter para minúsculas;
- preencher `orthography_ascii` e `lookup_ascii` diretamente;
- representar quantidades como desconhecidas;
- usar offsets identidade;
- não chamar `utf8proc_map`.

O caminho Unicode atual permanece como fallback e deve continuar produzindo a
mesma `SurfaceForm`.

## F-08 — Vetores de resultados movem objetos grandes

O GDB mediu:

```text
sizeof(AnalysisIR)  = 312 bytes
sizeof(SurfaceForm) = 176 bytes
sizeof(DerivationIR)= 248 bytes
sizeof(QueryResult) = 824 bytes
```

[`sort_and_deduplicate_analyses`](../../src/engine.cpp#L1702) ordena diretamente
objetos de 312 bytes e recalcula a chave semântica durante as comparações. O
vetor de resultados também cresce geometricamente em vários caminhos de
`append_*`.

Este é um alvo secundário porque o corpus tem mediana de apenas três hits.
Experiências possíveis:

- ordenar índices ou ponteiros com chave pré-calculada, movendo cada
  `AnalysisIR` apenas na compactação final;
- reservar uma capacidade pequena baseada no histograma real;
- avaliar small-vector somente depois de medir o custo de carregar storage
  inline em todos os resultados vazios ou pequenos.

Reservar 16 elementos cegamente não é indicado: seriam quase 5 KiB por query
antes de conhecer o número de análises.

## F-09 — Tamanho de código e tabelas Unicode

Seções alocáveis relevantes do ELF `RelWithDebInfo`:

| Seção | Bytes |
| --- | ---: |
| `.text` | 469.349 |
| `.rodata` | 355.448 |
| `.eh_frame` | 21.592 |
| `.gcc_except_table` | 24.356 |

Um novo snapshot depois de F-05a mediu 470.277 bytes de `.text` e 355.448 de
`.rodata` no CLI `RelWithDebInfo`. O browser build produzido pelo Emscripten
tem 838.304 bytes em `words_wasm.wasm` e 56.721 bytes no wrapper `.mjs`. Esses
valores passam a ser a baseline de tamanho para os próximos A/B; não constituem
por si sós um delta atribuível a F-05a, porque não foi preservado um artefato
WASM imediatamente anterior com a mesma toolchain.

Depois de F-05b, o CLI ficou com 468.725 bytes de `.text` (-1.552; -0,33%) e
355.800 bytes de `.rodata` (+352; +0,10%). O WASM ficou com 839.084 bytes
(+780; +0,09%) e o wrapper `.mjs` permaneceu em 56.721 bytes. Assim, a arena
privada não causou crescimento relevante do artefato e a unificação do código
dos grupos recuperou inclusive 1,5 KiB de `.text` nativo.

Maiores objetos de código medidos:

| Objeto | `.text` aproximado |
| --- | ---: |
| `json.cpp.o` | 196.129 bytes |
| `engine.cpp.o` | 136.007 bytes |
| `database.cpp.o` | 109.213 bytes |

Símbolos especialmente grandes incluem:

- `Database::load_poc`: 51.284 bytes;
- `dictionary_form`: 28.975 bytes;
- visitor de `morphology_order_key`: 12.990 bytes.

Os quatro blocos principais de dados do utf8proc somam 328.538 bytes:

- `utf8proc_properties`: 201.240;
- `utf8proc_stage2table`: 92.672;
- `utf8proc_sequences`: 25.922;
- `utf8proc_stage1table`: 8.704.

### Protótipo reaproveitável: `simple_utf8view.hpp`

O header de outro projeto em
`/mnt/projects/Projects/indexador/include/simple_utf8view.hpp` contém um
decoder UTF-8 pequeno, sem alocação e com checks explícitos para truncamento,
overlong encodings, surrogates e valores acima de `U+10FFFF`. Ele é uma boa
base para o fallback Unicode finito da engine, mas não pode ser incorporado
sem adaptação:

- a política atual converte sequências inválidas em `U+FFFD`; o lexer da
  engine deve retornar `DiagnosticCode::invalid_utf8`, e precisa distinguir
  um `U+FFFD` válido de um erro de decodificação;
- o `iterator` executa `decode` tanto em `operator*` quanto em `operator++`,
  duplicando a decodificação em um `range-for`; o lexer deve usar um cursor
  que devolva codepoint e tamanho uma única vez por iteração;
- `string_view_utf8` mantém um `vector<size_t>` lazy de offsets. Isso é útil
  para acesso aleatório genérico, mas não para o hotpath: `SurfaceForm` já
  possui `nfc_byte_offsets`, que deve continuar sendo preenchido durante a
  mesma passagem;
- `multisplit` materializa um vetor e, em uma variante, um `unordered_set`.
  Ele não deve substituir `TextTokenCursor`, que é lazy e precisa também
  preservar as classes de pontuação;
- o decoder resolve somente UTF-8. A retirada de utf8proc ainda exige a tabela
  latina finita de composição/decomposição, o encoder de codepoints aceitos e
  uma política explícita para as categorias de pontuação usadas pelo cursor.

A interface apropriada para reaproveitamento seria um resultado estrito:

```cpp
enum class Utf8DecodeError : std::uint8_t {
    none,
    invalid_lead,
    invalid_continuation,
    truncated,
    overlong,
    surrogate,
    out_of_range,
};

struct Utf8DecodeResult final {
    char32_t codepoint{};
    std::uint8_t byte_count{};
    Utf8DecodeError error{Utf8DecodeError::none};
};
```

O experimento deve copiar/adaptar apenas o núcleo do decoder, preservando sua
proveniência/licença, e não o container com cache nem os algoritmos de split.
No caminho Unicode da engine, cada chamada alimentaria diretamente o estado da
letra lógica, a quantidade vocálica, a saída NFC e os offsets, sem `Glyph` ou
buffer Unicode intermediário. O fast path `[A-Za-z]+` continua separado e não
precisa decodificar UTF-8.

O contrato do lexer aceita somente uma palavra latina com macrons ou breves,
mas usa tabelas Unicode gerais para chegar a esse conjunto restrito. Se esse
contrato permanecer formalmente restrito, pode-se gerar uma tabela compacta
com todos e somente os codepoints que decompõem/casefoldam para entradas
aceitas.

Essa mudança tem potencial para reduzir fortemente `.rodata`, mas é de risco
maior que um fast path ASCII. Deve ser precedida por geração exaustiva de casos
a partir da mesma versão do Unicode e por comparação contra utf8proc. Se o
produto passar a aceitar normalização Unicode geral, as tabelas devem ser
mantidas.

O tamanho de `dictionary_form` não é prioridade de performance: apesar dos
28,9 KiB de código, consumiu somente 0,54% inclusivos no perfil `analysis`.
Primeiro devem ser removidos o DOM JSON e os sorts do loader; só depois faz
sentido estudar dispatch mais compacto para os visitors de morfologia.

## Cache simulado

No `search` da Eneida, Callgrind registrou:

| Evento | Contagem |
| --- | ---: |
| instruções | 1.580.372.282 |
| misses I1 | 5.347.694 |
| misses de instrução no último nível | 4.874 |
| referências a dados | 452.264.308 |
| misses D1 | 3.839.974 |
| misses de dados no último nível | 159.232 |

As taxas do último nível são baixas. O perfil aponta mais para volume de
instruções, comparações repetidas e allocator churn do que para espera por RAM.
Como são caches simulados, a conclusão deve ser confirmada com contadores de
hardware quando `perf` estiver disponível.

## Reavaliação geral depois de F-04, F-05a e F-05b

Esta reavaliação usa somente `words_engine_benchmark`, ligado a `words_core`.
Loader, DOM JSON, serialização e escrita do CLI ficam fora da região medida.
Portanto, a ordem abaixo é a ordem da **engine pura**, não do produto completo.

### O que já foi resolvido

F-04 era a maior origem individual de bytes transitórios da engine e a única
que materializava quase um milhão de objetos que não escapavam da chamada. Ela
foi eliminada sem mudar API pública, WWDB ou WASM. Depois da mudança:

- `enumerate_candidates` representa 5,92% inclusivos, dos quais 5,65 pontos
  percentuais pertencem a `lookup_ending`; a materialização deixou de ser o
  problema;
- o total da região caiu para 692.809.841 instruções;
- `CandidateIR` deixou de aparecer como allocation point;
- não há sinal de complexidade exponencial no caminho de terminações.

Assim, o hotpath mais grave e óbvio de alocação interna foi resolvido, mas não
todos os hotpaths relevantes da engine.

### Hotpaths antes de F-05a

No Callgrind pós-F-04, os custos exclusivos dos quatro lookups principais são:

| Função | Instruções exclusivas |
| --- | ---: |
| `lookup_stem` | 12,70% |
| `lookup_prefix` | 11,91% |
| `lookup_suffix` | 7,43% |
| `lookup_ending` | 4,65% |

Eles somam 36,69% da região. Isso não significa que uma nova estrutura remova
todo esse custo, mas mostra que F-05 deve ser decomposto antes de investir num
trie. Todas as consultas feitas pela engine são substrings de `lookup_ascii` e
o parâmetro público já se chama `normalized_ascii`; apesar disso,
`normalized_compare` aplicava lowercase e `j/v` folding também ao lado da
consulta em cada comparação. O experimento F-05a passou a normalizar somente a
chave armazenada e a comparar o lado direito como byte canônico. Isso não exige
mudar o banco e deixa a canonização de chaves no packer como hipótese separada.

A inspeção de todos os call sites da engine confirmou a precondição: os oito
`lookup_*` recebem exclusivamente a própria `SurfaceForm::lookup_ascii` ou um
`substr` dela. O lexer constrói essa string depois de casefold, aceitando apenas
`a`–`z`, e `lookup_letter` já transforma `j -> i` e `v -> u`. A ressalva está na
API pública de `Database`: o nome `normalized_ascii` comunicava a precondição,
mas ela não estava documentada nem verificada, e a implementação anterior
tolerava acidentalmente maiúsculas, `j` e `v` na consulta. F-05a formaliza o
contrato no header e usa uma asserção somente em debug; ela é eliminada por
`NDEBUG` no binário instrumentado.

A cobertura de `i/j` e `u/v` existe, mas ainda não fecha esse contrato em todas
as camadas. `LatinLexerTest.KeepsDistinctSurfaceAndLookupRepresentations` fixa
o caso forte `JŪVĔNIS`: preserva `juvenis` em `orthography_ascii` e produz
`iuuenis` em `lookup_ascii`. `DatabaseTest.LoadsAndIndexesUniqueAnalyses`
confirma que a consulta canônica `mauis` alcança a entrada unique armazenada
com ortografia equivalente. Os diferenciais incluem `mavis` e `iusiurandum`,
mas não comparam pares de grafias entre si.

O teste
`EngineTest.FoldsIAndJAndUAndVWithoutChangingAnalysisSemantics` fecha essa
lacuna antes do A/B. Ele compara `juvenis`/`iuuenis`, `mavis`/`mauis` e
`mavisque`/`mauisque`, exige que as duas superfícies produzam o mesmo
`lookup_ascii` canônico não vazio e compara, como multiset, lexema, regra,
`stem_key`, ranges, morfologia, quantidade, derivação e assessment. Assim o
conjunto cobre os caminhos regular, unique e com addon sem tornar a grafia de
apresentação artificialmente igual.

As chaves dos grupos carregados também não podem ser comparadas como bytes sem
mais trabalho. O loader ordena e agrupa com `normalized_compare`, mas conserva
como `group.key` a grafia original do primeiro registro. Logo, a variante XS
correta normaliza somente o byte da chave armazenada; tornar ambos os lados um
`string_view::compare` exige antes canonizar as chaves no loader ou no WWDB.
Isso continua sendo custo linear no comprimento inspecionado dentro de cada
passo do `lower_bound`, não comportamento exponencial.

#### Resultado F-05a — comparador assimétrico canônico

O Callgrind foi repetido no NativeLab com a mesma configuração RelWithDebInfo,
Eneida IV, modo `lines`, um warmup e uma iteração. O artefato está em
`build/profile-instr/profiles/engine-lines-asymmetric.callgrind`.

| Métrica | Pós-F-04 | Comparador assimétrico | Delta |
| --- | ---: | ---: | ---: |
| instruções | 692.809.841 | 567.928.816 | -18,03% |
| referências a dados | 196.037.719 | 188.911.255 | -3,64% |
| misses D1 | 1.696.357 | 1.696.451 | +0,006% |
| branches | 107.075.695 | 91.611.347 | -14,44% |
| mispredicts | 7.574.861 | 6.834.882 | -9,77% |
| checksum | 22.723 | 22.723 | idêntico |

O custo exclusivo nomeado de `lookup_stem` caiu de 12,70% para 8,63% do
programa; os demais lookups foram incorporados em seus chamadores nessa build
otimizada e não aparecem como linhas de função independentes no
`callgrind_annotate`. O D1 praticamente invariável e a grande redução de
instruções e branches são coerentes com a mudança: ela remove decisões por
caractere, não acessos à estrutura do índice. `normalized_char` continua
visível como hotpath porque a chave armazenada ainda precisa ser normalizada;
eliminar também esse lado exigirá chaves canônicas no loader ou no WWDB.

Validação de F-05a:

- builds Debug e RelWithDebInfo com Clang, além do link Emscripten/WASM, todos
  com as flags estritas do projeto;
- 116/116 testes no build Debug;
- diferenciais selecionados, configured oracle e Eneida com checksum e
  semântica preservados;
- 116/116 testes com ASan/UBSan. LeakSanitizer foi desabilitado porque não
  funciona sob o `ptrace` deste ambiente, não por diagnóstico do programa;
- CI GitHub completo: engine e oráculo Ada, CLIs Linux/Windows/macOS x86-64 e
  macOS arm64, sanitizers GCC/Clang/MSVC e build/análise no navegador;
- `git diff --check` limpo.

#### Resultado F-05b — arena canônica somente para chaves não canônicas

O perfil LLVM pós-F-05a ainda contou 30.928.696 iterações de
`stored_key_compare_canonical_query` e 39.254.539 chamadas a
`normalized_char`. O lado da consulta já não é normalizado; o custo restante
vem da chave armazenada e, em menor parte, da construção dos índices no loader.

Não é seguro canonizar `image_` ou os pools de strings em memória. Os mesmos
bytes são retornados por `stem_string`, `ending_string` e pelos acessores de
addons, e participam de formas de dicionário, provenance e JSON. Alterá-los
converteria dados de apresentação como `Jupiter`/`Vulcan`, não apenas o índice.

Uma inspeção com GDB no NativeLab, no ponto final do loader, mediu as chaves dos
oito índices:

| Índice | Grupos | Bytes de chave | Grupos não canônicos | Bytes a copiar |
| --- | ---: | ---: | ---: | ---: |
| stems | 48.463 | 381.991 | 8.495 | 66.814 |
| endings | 459 | 1.931 | 0 | 0 |
| uniques | 58 | 379 | 7 | 37 |
| suffixes | 123 | 396 | 3 | 9 |
| prefixes | 115 | 424 | 4 | 6 |
| tickons | 6 | 18 | 0 | 0 |
| tackons | 15 | 55 | 2 | 5 |
| packons | 11 | 45 | 1 | 3 |
| **total** | **49.250** | **385.239** | **8.512** | **66.874** |

Depois de lowercase e `j -> i`, `v -> u`, todas as 49.250 chaves ficam em
`[a-z]*`; a terminação vazia continua válida. A implementação não mudou o
WWDB e passou a:

1. conservar a `string_view` original para os 40.738 grupos já canônicos;
2. somar previamente os 66.874 bytes das demais chaves;
3. reservar uma única arena de `char`, preenchê-la sem nova realocação e
   redirecionar somente essas views;
4. usar um comparador lexicográfico manual de bytes nos oito lookups.

O custo persistente observado é cerca de 65,3 KiB e uma alocação, não uma cópia
dos 385 KiB nem 8.512 alocações. A arena recebe um único `resize` com o tamanho
exato antes de qualquer `string_view` ser redirecionada e permanece imutável
durante a vida do `Database`. Os pools de apresentação não são alterados. O
loader rejeita bytes que não pertençam ao domínio canônico latino, e validações
de overflow e do tamanho final protegem a construção da arena.

Um primeiro A/B usou a comparação padrão de `std::string_view`, que nesta
toolchain chamou `memcmp`. O resultado foi rejeitado: 566.199.113 instruções
(-0,30% contra F-05a), mas 215.106.231 referências a dados (+13,87%),
96.022.277 branches (+4,82%) e 5,30 milhões de branches indiretos, contra 1,08
milhão na baseline. Também acrescentou 8.256 bytes de `.text` e 6.581 bytes ao
WASM. Para estas chaves curtas e divergências precoces, a chamada genérica era
pior que um laço byte a byte que retorna na primeira diferença.

A variante final usa esse laço manual e unifica os tipos de grupo de mesmo
layout em `LookupGroup`. Uma única rotina percorre um array de oito spans para
construir a arena, evitando oito instanciações do mesmo código. Esse ajuste
também eliminou o crescimento de `.text` observado no protótipo.

Callgrind final, com a mesma configuração, corpus, warmup e iteração:

| Métrica | F-05a | F-05b | Delta |
| --- | ---: | ---: | ---: |
| instruções | 567.928.816 | 503.316.489 | -11,38% |
| referências a dados | 188.911.255 | 187.088.295 | -0,96% |
| misses I1 | 2.318.801 | 2.110.675 | -8,98% |
| misses D1 | 1.696.451 | 1.727.231 | +1,81% |
| misses LL | 23.962 | 24.276 | +1,31% |
| branches | 91.611.347 | 72.000.199 | -21,41% |
| mispredicts | 6.834.882 | 4.685.523 | -31,45% |
| checksum | 22.723 | 22.723 | idêntico |

O DHAT confirmou o custo esperado da arena: 35.363.446 bytes em 113.352 blocos
e pico de 9.752.473 bytes, contra 35.297.094 bytes, 113.351 blocos e pico de
9.686.313 na baseline. Isso representa +66.352 bytes totais, uma alocação e
+66.160 bytes de pico. Não havia bytes vivos ao encerrar o processo.

Validação de F-05b:

- checksum 22.723 no benchmark de uma iteração e 68.169 no perfil LLVM de três;
- teste dedicado garante que consultas canônicas ainda retornam as grafias de
  origem `Jupiter` e `Vulcan`, sem mutar os pools públicos;
- guardrails de quantidade, `i/j`, `u/v` e consistência de todos os índices;
- 117/117 testes no build Debug e 117/117 com ASan/UBSan;
- builds RelWithDebInfo e Emscripten/WASM com as flags estritas do projeto.

No Callgrind pós-F-04, anterior ao comparador, outros custos já claros foram:

- `LatinLexer::lex`: 7,27% inclusivos; somente `map_utf8` representa 5,79%;
- `rewrite_attempts`: 6,92% inclusivos, além das análises lexicais disparadas
  pelas formas transformadas;
- `sort_and_deduplicate_analyses`: 2,57% inclusivos;
- vetores de `AnalysisIR` e `QueryResult` respondem juntos por cerca de 64,9%
  dos bytes alocados no DHAT pós-F-04;
- lexer/utf8proc ainda produz 56.161 blocos e os vetores temporários de addons,
  30.074; juntos são aproximadamente 76,1% de todos os blocos restantes.

Como a Eneida e todas as formas internas geradas nesse perfil são ASCII, um
fast path do lexer pode evitar 32.092 eventos de `malloc`/`realloc` do utf8proc
e 8.023 alocações de `vector<Glyph>`: limite superior de 40.115 blocos, ou
35,4% dos blocos atuais. `quantities` e offsets continuam proprietários nessa
primeira etapa; a retirada completa de utf8proc permanece um experimento
posterior e separado.

### Ablações pós-F-05a

As ablações abaixo foram executadas no NativeLab com Callgrind sobre o mesmo
corpus. Elas alteram resultados e caminhos de controle, portanto são limites
superiores do custo de cada mecanismo, não previsões de ganho de uma
otimização. Os percentuais se sobrepõem e não podem ser somados.

| Configuração | Instruções | Delta | Checksum |
| --- | ---: | ---: | ---: |
| padrão | 567.928.816 | — | 22.723 |
| sem síncope | 304.980.280 | -46,30% | 22.660 |
| sem ortografia | 424.987.383 | -25,17% | 22.695 |
| sem sufixos | 411.561.668 | -27,53% | 22.683 |
| sem derivações produtivas | 369.906.572 | -34,87% | 22.680 |

Síncope, ortografia e sufixos custam muito mais que a quantidade de análises
adicionais sugere, mas parte relevante é trabalho lexical necessário para
provar que uma tentativa falha. O primeiro corte seguro é reduzir varreduras e
tentativas redundantes, preservando todos os resultados, não simplesmente
desabilitar mecanismos.

Os quatro perfis foram gerados no NativeLab com simulação de cache e branches.
Os artefatos foram preservados em
`build/profile-instr/profiles/engine-lines-post-f05-*.callgrind`. O comparador
reduziu todos os caminhos que fazem lookups, mas não mudou a conclusão: rewrites
continuam com o maior teto de CPU. O valor de `rewrite_attempts` no perfil LLVM
também permaneceu em 17.816.000 execuções, indicando que a próxima redução deve
vir da enumeração/scheduling das regras, não de mais ajustes no comparador.

### Backlog por ondas

#### Onda E1 — experimentos pequenos da engine

1. **Concluído em F-05a:** comparador para chave armazenada versus consulta já
   canônica, removendo a normalização redundante do lado direito nos oito
   `lookup_*`; -18,03% de instruções no Callgrind.
2. **Concluído em F-05b:** arena única de cerca de 65 KiB para as chaves não
   canônicas e comparação manual por bytes; -11,38% de instruções e -21,41% de
   branches adicionais no Callgrind.
3. Adicionar o fast path ASCII de uma passagem ao lexer, mantendo utf8proc como
   fallback Unicode.
4. Indexar as 170 reescritas por tipo, estágio e prioridade; depois medir
   buckets pelo primeiro byte da sequência `before`.
5. Reperfilar após cada mudança. Só avançar se checksum, diferenciais, tamanho
   nativo/WASM e sanitizers permanecerem aceitáveis.

#### Onda E2 — escolher com os novos perfis

1. Avaliar buffer inline para os IDs de addons, priorizando quantidade de
   alocações; os bytes envolvidos são pequenos.
2. Comparar trie reverso de terminações com o `lower_bound` já simplificado. O
   teto histórico de `lookup_ending` era 5,65% inclusivos antes de F-05a;
   portanto o trie deve ser reperfilado depois da arena canônica.
3. Medir ordenação indireta/chaves pré-calculadas para `AnalysisIR`; hoje o
   sort representa 2,57%, logo não justifica ainda uma grande reestruturação.

#### Onda E3 — mudanças de representação ou API

1. Sink interno de `analyze_line`, preservando a API de vetor e projetando
   diretamente para o vetor final no adaptador WASM.
2. Descritores compactos antes de materializar `AnalysisIR`, caso o DHAT depois
   das ondas anteriores continue apontando os vetores como maior churn.
3. Chaves e índices já canônicos/ordenados no WWDB, loader linear e possível
   trie persistido somente depois de medir os protótipos em memória.
4. Substituição completa de utf8proc pelo normalizador latino finito e pela
   política de pontuação escolhida para o tokenizador.

### Priorização por custo versus benefício

As estimativas de código abaixo incluem implementação, testes específicos e
integração, mas são somente ordens de grandeza: **XS** até aproximadamente 100
linhas tocadas, **S** 100–250, **M** 250–600, **L** 600–1.200 e **XL** quando
há mudança de formato/API transversal. O “teto medido” é a fatia atualmente
atribuída ao subsistema; não é uma promessa de que toda a fatia desaparecerá.

#### Engine: ordem padrão para testar

| Ordem | Hipótese | Código | Risco | Benefício provável | Teto ou evidência atual |
| ---: | --- | --- | --- | --- | --- |
| 1 | **Concluído:** comparador chave armazenada × consulta canônica | XS | baixo | alto medido | -18,03% de instruções, -14,44% de branches e checksum idêntico |
| 2 | **Concluído:** arena canônica híbrida para chaves dos índices (F-05b) | S–M | baixo–médio | alto medido em CPU, pequeno custo de RSS | -11,38% de instruções, -21,41% de branches; +66.352 bytes alocados e uma alocação |
| 3 | Fast path `[A-Za-z]+` no lexer | S | baixo–médio | alto em corpus ASCII | lexer 7,27% inclusivos antes de F-05a; limite de 40.115 blocos, 35,4% das alocações atuais |
| 4 | Buckets de rewrite por tipo/estágio/prioridade | M | médio | médio–alto em CPU | 17.816.000 execuções de `rewrite_attempts`; ablações atuais dão tetos de 25,17% a 46,30%, mas incluem reanálise inevitável |
| 5 | Sink interno de `analyze_line` para WASM/line mode | M | médio | alto em textos, nulo no benchmark por palavra | vetor de `QueryResult`: 8,95 MB, 25,3% dos bytes no perfil `lines` |
| 6 | Buffer inline especializado para IDs de addons | S–M | baixo–médio | alto em contagem de alocações, baixo/incerto em CPU | 30.074 blocos, 26,5% do total, mas somente cerca de 414 KiB |
| 7 | Trie reverso de terminações construída em memória | M | médio | baixo–médio depois do comparador | `lookup_ending` tinha teto de 5,65% inclusivos antes de F-05a; deve ser reperfilado depois da arena canônica |
| 8 | Ordenação indireta/chaves pré-calculadas de `AnalysisIR` | S–M | médio | baixo na mediana, possível em alta ambiguidade | sort/dedup inteiro representava 2,57%; mediana de apenas três análises |
| 9 | Normalizador latino finito e retirada de utf8proc | L | alto semântico | baixo adicional em runtime ASCII; alto em tamanho WASM | até 328.538 bytes de tabelas `.rodata`; o fast path captura antes o caso runtime comum |
| 10 | Índices canônicos e pré-ordenados no WWDB | XL | alto de formato | alto no startup, nulo no steady-state medido | loader 8,11% exclusivos e sort de stems 6,46% no perfil de startup |
| 11 | Descritores compactos antes de `AnalysisIR` | L–XL | alto arquitetural | potencialmente alto em memória, CPU incerto | vetores de `AnalysisIR`: 13,95 MB, 39,5% dos bytes pós-F-04 |

A ordem começa agora no item 3; cada item continua sendo um A/B isolado e deve
ter novo Callgrind e DHAT. A ordem 5–11 é condicional ao novo perfil. Em
particular, se tamanho do WASM for o objetivo dominante, o
normalizador finito sobe da posição 9 para a posição 5. Se latência de startup
for o objetivo dominante, o WWDB pré-ordenado sobe, mas continua atrás de um
protótipo que prove o formato desejado.

O critério para promover uma hipótese é:

```text
prioridade prática = ganho reproduzível
                   / (código novo + risco semântico + custo no binário)
```

Por isso, quantidade de alocações sozinha não promove o buffer inline acima do
lexer: addons produzem muitos blocos pequenos, enquanto o lexer combina CPU,
alocações e uma oportunidade clara de simplificação.

### Cobertura de testes necessária por hipótese da engine

O checksum do benchmark (`units.size() + total_analyses()`) impede a eliminação
trivial do trabalho, mas não prova equivalência: IDs, morfologia, ordenação,
quantidades e provenance podem mudar mantendo as mesmas contagens. Cada A/B
deve conservar o checksum barato dentro da região instrumentada e executar uma
comparação semântica forte fora dela. A suíte atual oferece boa cobertura de
resultados finais, porém vários contratos internos novos precisam de testes
dedicados antes da implementação.

| Hipótese | Cobertura atual | Pitfall principal ainda não coberto | Guardrail a adicionar antes/do experimento |
| --- | --- | --- | --- |
| Comparador assimétrico canônico | concluído, cobertura forte | consulta não canônica ou ordenação incompatível nas bordas do `lower_bound` | implementados: asserção debug de `[a-z]*` sem `j/v`, consistência estrutural dos índices, pares `i/j` e `u/v`, quantidades exatas, diferenciais, corpus, sanitizers e CI multiplataforma; uma referência exaustiva sobre a seção privada de `stem_references` exigiria API de auditoria adicional |
| Arena canônica híbrida (F-05b) | concluído, cobertura forte | `string_view` pendente após realocação, mutação dos pools de apresentação ou colisões canônicas separadas | implementados: dimensionamento exato antes de publicar views, validação do domínio `[a-z]*`, consistência estrutural dos oito índices, preservação de `Jupiter`/`Vulcan`, guardrails de F-05a, DHAT, tamanho nativo/WASM e sanitizers; fixtures sintéticos de arena vazia e overflow continuariam úteis, mas o banco real cobre chaves canônicas e não canônicas |
| Fast path ASCII | parcial | aceitar vazio/pontuação/NUL, alterar `original_utf8`, offsets ou folding `J/V` | tabela direta para minúsculas, maiúsculas, `JjVv`, palavra de um byte e longa; confirmar todas as propriedades de `SurfaceForm`; provar fallback para qualquer byte não ASCII |
| Buckets de rewrite | funcional forte, estrutural parcial | perder/duplicar regra ou mudar ordem de prioridade, estágio, `scan_reverse` e ID | teste de integridade: os 170 IDs aparecem exatamente uma vez no bucket correto e na ordem original; buckets vazios; paridade de tentativas e provenance nos fixtures de síncope/ortografia |
| Sink de `analyze_line` | funcional forte | emitir lookahead cedo, duplicar/perder token, mover dados antes do consumo | executar wrapper e sink sobre os mesmos casos e comparar resultados completos; cobrir lookahead falho, composto aceito, erro entre tokens, Unicode, pontuação e two-words |
| Buffer inline de addons | funcional parcial | overflow, transição inline→heap, sort/unique atravessando os dois storages e invalidar iteradores | testar vazio, capacidade−1, capacidade, capacidade+1, múltiplos appends, duplicatas, move e overflow forçado com capacidade de teste pequena |
| Trie reversa de endings | parcial | esquecer ending vazio, parar cedo, inverter a ordem longest-first ou errar palavras menores que o máximo | comparar trie com `lookup_ending` de referência para todas as terminações do banco e mutações ausentes; testar ending vazio, prefixos compartilhados, profundidade máxima e ordem exata dos `RuleId` |
| Sort indireto de `AnalysisIR` | parcial | deduplicar análises distintas, produzir ordem não determinística sem necessidade ou manter `string_view` pendente após movimentos | paridade do multiset semântico, sem congelar ordem de apresentação; casos 0/1/duplicados e palavra de alta ambiguidade; ASan/UBSan para lifetime das chaves pré-calculadas |
| Normalizador latino finito | bom conjunto de exemplos, insuficiente para substituição | classificação errada de UTF-8 inválido, composição NFC divergente ou expansão de casefold indevida | todos os 22 precompostos; combinações decompostas; `y`+breve; marcas antes/duplicadas/conflitantes; matriz de overlong, truncados, surrogate, `>U+10FFFF`, `U+FFFD` válido e comparação exaustiva contra utf8proc |
| WWDB pré-ordenado/canônico | forte para formato atual | aceitar grupos fora de ordem, ranges sobrepostos/fora do arquivo, duplicatas ou chave não canônica | fixtures corrompidos específicos para cada novo invariante; geração determinística byte a byte; paridade de todos os lookups entre banco antigo e novo |
| Descritores compactos de análise | black-box ampla, estrutural fraca | perder ownership/provenance/quantidade durante materialização tardia | modo A/B temporário com comparação dos multisets semânticos de `QueryResult`; Eneida completa, casos de rewrite, addon, quantidade, homógrafos, compostos e alta ambiguidade; sanitizers |

Cobertura existente especialmente relevante:

- `words_aeneid_corpus` cobre 2.726 formas ASCII distintas, mas compara a
  semântica do corpus principalmente como conjunto e não protege toda ordem;
- `words_differential` cobre fixtures selecionados contra Ada e compara JSON
  completo em vários caminhos;
- os testes `AppliesDataDrivenPerfectSyncopeByPriority`,
  `AppliesDataDrivenOrthographicFamilies`,
  `BoundsOrthographyThenSyncopeToTwoTypedSteps` e
  `KeepsDirectEncliticAnalysisAheadOfSpellingRecovery` já protegem decisões
  importantes do scheduler de rewrites;
- os testes de `analyze_line` já cobrem lookahead falho, composto aceito,
  pontuação ASCII/Unicode, erros por token e normalização;
- os testes do lexer cobrem os principais casos latinos e rejeições, mas não
  constituem ainda uma suíte UTF-8 estrita/exaustiva;
- os testes de corrupção do WWDB protegem header, versão, truncamento e
  checksum do formato atual, não os futuros invariantes de índices persistidos.

Um fixture visual revelou uma lacuna concreta na cobertura de quantidade. O
comportamento atual é:

| Consulta | Entradas | Análises | Partição esperada |
| --- | ---: | ---: | --- |
| `puella` | 1 | 3 | nominativo, vocativo e ablativo singular |
| `puellă` | 1 | 2 | nominativo e vocativo singular |
| `puellā` | 1 | 1 | ablativo singular |
| `malum` | 6 | 17 | união não marcada dos grupos curto e longo |
| `mălum` | 2 | 8 | entradas 26.267 e 26.269 |
| `mālum` | 4 | 9 | entradas 26.263–26.266 |

Os binários Ada locais foram consultados como oráculo somente para as formas
ASCII. `bin/words puella` confirma NOM/VOC/ABL singular feminino, e
`bin/words_json puella` contém as mesmas três análises estruturadas.
`bin/words_json malum` contém as mesmas 17 análises da engine. A saída textual
de `bin/words` agrupa regras e sentidos na apresentação, portanto não deve ser
usada para contar objetos `AnalysisIR`; o JSON é o oráculo estrutural. As
formas com breve/macron não são enviadas ao programa histórico ASCII: elas são
validadas como partições da semântica da forma não marcada.

Há cobertura parcial: `AnalyzesNounOnlyFixtures` fixa somente as três análises
de `puella`; `InflectionQuantityDistinguishesFirstDeclensionA` prova a divisão
breve/macron usando `rosă` e `rosā`; e `LexicalQuantityPartitionsMalumHomographs`
prova os grupos de entry IDs e que suas contagens somam a consulta sem marca.
Os testes `QuantityPartitionsPuellaInflectionsExactly` e
`QuantityPartitionsMalumLexemesExactly` agora fixam as seis assinaturas acima,
incluindo caso, número, gênero, entry ID, `stem_key`, `quantity_match`, ranges
de stem/ending e multiplicidade. Eles comparam assinaturas ordenadas apenas
internamente como multisets e deliberadamente não tornam a ordem de
apresentação parte do contrato. Esse fixture protege os experimentos de lexer,
comparadores, trie ou materialização tardia, pois cruza todos esses caminhos.

Foi acrescentado também
`DatabaseTest.CanonicalLookupIndexesRemainInternallyConsistent`. Ele percorre
todos os endings, uniques e addons expostos pelo banco e confirma que a forma
canônica resolve para o ID correspondente; para stems, percorre todas as
grafias lexicais que de fato resolvem e verifica que cada referência devolvida
aponta novamente para a mesma chave canônica. Uma tentativa inicial de exigir
que todo slot não vazio de `LexemeRecord::stems` estivesse no índice revelou um
contrato interno importante: esses quatro slots são payload lexical e podem
conter alternativas ou sentinelas como `zzz`; somente a seção
`stem_references` define quais pares lexema/slot/stem-key são pesquisáveis.
Portanto, um teste de paridade absolutamente exaustivo do índice de stems
precisaria expor uma visão somente-leitura das referências ou decodificar essa
seção do WWDB; inferi-la dos slots do lexema produziria falsos positivos.

Antes do primeiro A/B, deve ser criado um fingerprint semântico de validação,
calculado fora da região medida, contendo ao menos status, `LexemeId`, `RuleId`,
morfologia, quantity match e derivation/addon/rewrite IDs. A comparação padrão
deve ser por multiset; ordem só entra no fingerprint onde “primeiro match” ou
prioridade do scheduler fizer parte da semântica. O checksum barato continua
dentro do loop para impedir dead-code elimination sem poluir o perfil com
hashing de strings e variantes.

#### CLI e produto: trilha independente

| Ordem | Hipótese | Código | Risco | Benefício provável | Evidência atual |
| ---: | --- | --- | --- | --- | --- |
| 1 | Writer JSON incremental | M–L | médio de compatibilidade textual | muito alto | DOM JSON: 329,6 MB e 5,27 milhões de blocos |
| 2 | Usar o sink de `analyze_line` na projeção WASM | M incremental | médio | alto para textos grandes | evita manter `QueryResult` e projeção do browser ao mesmo tempo |
| 3 | LRU limitado por bytes | M | médio de política/memória | alto quando há repetição, zero em consultas únicas | repetição observada de 40,4% na Eneida e 66,6% na amostra Latin Library |
| 4 | `mmap` do banco nativo | S–M | médio de plataforma | baixo enquanto sorts dominarem startup | leitura física não apareceu como custo principal no `strace` |

O LRU não deve preceder o writer: cache melhora workloads repetitivos, enquanto
o writer reduz custo também para todas as consultas distintas. Esses números
não devem ser usados para priorizar alterações da engine pura.

### Trilha separada do CLI

O writer JSON streaming continua sendo a maior oportunidade do formato
`analysis`, e o LRU do modo batch continua atraente para corpora repetitivos.
Eles devem usar o CLI nos benchmarks A/B, não `words_engine_benchmark`, pois não
pertencem ao custo interno da engine.

## Ordem sugerida dos experimentos

Para a engine, o próximo A/B de E1 é o fast path ASCII; depois dele, reperfilar
antes dos buckets de rewrite e então escolher E2 pelos novos números.
Para o CLI, medir writer streaming antes do cache, pois ele melhora também
consultas distintas. Mudanças de WWDB, API e retirada completa de utf8proc
ficam em E3 para não misturar estrutura, semântica e micro-otimização.

Cada experimento deve ser isolado em benchmark A/B e preservar os resultados
canônicos da engine. Tempos devem ser acompanhados por instruções, alocações,
RSS e tamanho de `.text`/`.rodata`; uma melhora em apenas uma dessas dimensões
não justifica regressão desproporcional nas demais.

## Limitações

- Os números caracterizam este snapshot, compilador e WWDB; mudanças no banco
  alteram cardinalidades e distribuição dos caminhos.
- A amostra da Latin Library contém os primeiros 30.000 tokens alfabéticos
  selecionados, não uma amostra estratificada de todos os autores e períodos.
- Os tempos incluem projeção e escrita em `/dev/null`, salvo quando a seção
  explicitamente atribui custo à engine via Callgrind.
- O page cache estava quente e não foi limpo.
- Callgrind e DHAT perturbam tempo de parede; seus resultados foram usados para
  proporções, contagens e attribution, não como latência de produção.
- Nenhuma estimativa deste documento substitui benchmark no WebAssembly e no
  navegador, onde allocator, custo de download e cache do Worker são diferentes.
