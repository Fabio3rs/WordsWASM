# Auditoria de performance da engine e do CLI C++23

Executada em 10 de setembro de 2026. A primeira coleta registrou somente a
investigação e o harness de profiling. Depois dela, a hipótese F-04 foi
implementada isoladamente e medida novamente contra o mesmo corpus, executável
e configuração. As demais propostas continuam sendo possibilidades de estudo.

## Resumo executivo

Os perfis separam três domínios de custo:

1. no formato `analysis`, a construção e destruição do DOM JSON é o custo
   dominante do CLI;
2. dentro da engine, síncope/ortografia, consultas repetidas aos índices e a
   materialização do vetor de candidatos são os hotpaths mais promissores;
3. na inicialização, o loader reconstrói e ordena índices que poderiam vir
   prontos do WWDB.

As oportunidades com melhor relação entre impacto e risco são:

- escrever JSON diretamente em um buffer ou `ostream`, sem construir um DOM;
- manter um cache limitado por consulta no modo batch;
- indexar regras de reescrita por tipo, estágio e prioridade;
- percorrer os spans de terminações sem materializar `CandidateIR`
  (implementado e medido em F-04);
- experimentar tries compactos para stems e afixos;
- persistir os índices já ordenados no WWDB;
- substituir a normalização Unicode geral por um lexer de domínio latino com
  fast path ASCII e tabelas finitas para quantidade vocálica.

A primeira otimização implementada substituiu o vetor transitório de
`CandidateIR` por grupos inline e materialização por valor somente no consumo.
No perfil isolado da engine, ela removeu 18.790.000 bytes de alocações, reduziu
as instruções em 12,15%, os branches em 22,09% e os misses D1 em 16,75%, sem
alterar checksums, resultados diferenciais ou aumentar `.text`.

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

Na execução de validação, que inclui um warmup e três iterações no perfil LLVM,
os maiores block counts foram:

- `normalized_char`: 70.183.235;
- `normalized_compare`: 35.091.563;
- `rewrite_attempts`: 17.816.000;
- `normalized_less`: 16.722.004;
- `append_suffix_analyses`: 7.363.072;
- `analyze_syncope`: 5.133.848.

Isso reforça simultaneamente F-03, sobre o scheduler de reescritas, e F-05,
sobre normalização repetida dentro dos lookups.

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

## Ordem sugerida dos experimentos

1. writer JSON streaming, mantendo golden outputs byte a byte;
2. cache LRU por bytes no CLI batch;
3. buckets contíguos de reescritas por tipo/estágio/prioridade;
4. chaves canônicas e protótipo de trie reverso para as terminações; a
   enumeração lazy de candidatos já foi concluída;
5. índices pré-ordenados no WWDB e loader linear;
6. fast path ASCII e normalizador latino especializado, validado
   diferencialmente contra o lexer atual;
7. ordenação indireta dos resultados;
8. redução da tabela de categorias de pontuação do tokenizador, conforme o
   contrato editorial escolhido.

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
