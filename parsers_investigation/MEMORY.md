# Baseline local de memória: DHAT e Callgrind

Rodada inicial de 27/09/2026, sem alterações no parser, core, produção ou CI.
A medição muda a prioridade: no corpus atual, a retenção de análises por árvore
e a montagem dos diagnósticos têm impacto maior no pico que as tabelas Markov.
Há também muito trabalho transitório em scoring, extração de features e treino
repetido. Compactar as tabelas continua útil, mas não resolve sozinho esses custos.

## Ambiente e validade

- Código C++: commit `9334a4e7c6ed3685bdc9ceac0d9c43dde0fb084e`, sem mudanças
  nos arquivos medidos. Hashes de fontes, entradas e executáveis nos manifests.
- Build separado: `build/parsers-memory`, Clang 21.0.0, Release,
  `-O3 -g -gdwarf-4 -DNDEBUG`, sem sanitizers; backend Unicode FULL.
- Valgrind 3.22.0; pilhas DHAT de até 32 frames, símbolos e linhas disponíveis.
- GNU time: três processos nativos novos por cenário, executados sequencialmente.
  RSS e duração da tabela são medianas; os intervalos de RSS estão no JSON.
  Cache do SO não foi limpo. São medições iniciais, sem estimativa confiável de p95.
- Uma execução DHAT por cenário; quatro execuções Callgrind, evento `Ir`.
  Tempo e RSS sob Valgrind não são usados como desempenho nativo.
- Saída gravada em arquivos. O RSS de Python e dos analisadores não entra no
  RSS do executável medido. Não foi necessário `ptrace` ou NativeLab.
- O self-test passou nas 17 fixtures. As 48 saídas nativas/instrumentadas
  coincidiram por cenário, ignorando **somente `elapsedNs`**.

A WWDB em `web/engine/words-full.wwdb` foi rejeitada pelo engine atual:
`unsupported-version: analysis engine requires WWDB addon attributes`.
A tentativa está preservada em `reports/memory-baseline-native/` e não entra
nas estatísticas. Foi gerada uma imagem **somente em build**, perfil dense,
WWDB 1.11, 2.732.004 bytes:

`sha256:c33f62c19cf8f266986ba8c88e826c627509b2ad56649869111b3e3d799bcafd`

Portanto, não interpretar diferenças em relação ao smoke histórico de ~17 MiB
em `MARKOV.md` como regressão ou melhoria controlada.

## Cenários e medidas

Os cenários `*-corpus` percorrem as 17 fixtures estruturais. O Markov sequencial
executa a matriz de 12 configurações, leave-one-out sobre cinco itens atestados,
com os pesos padrão, incluindo reordenação sintética 0,1. O estrutural executa
oito configurações, dez alvos didáticos, e mantém os resultados das 17 fixtures
do oráculo para reutilização. Estes rankers medem o experimento completo,
incluindo treino repetido, não a inferência de um único modelo congelado.

| Cenário | RSS nativo MiB | Heap vivo no pico, DHAT MiB | Alocado acumulado, DHAT MiB | Alocações DHAT | Tempo nativo s |
|---|---:|---:|---:|---:|---:|
| Morfologia / corpus | 12,27 | 7,99 | 9,74 | 25.647 | 0,02 |
| MST / corpus | 12,89 | 8,84 | 35,72 | 510.808 | 0,04 |
| Oráculo / corpus | 20,09 | 15,15 | 164,24 | 2.944.321 | 0,14 |
| Markov sequencial aditivo | 17,88 | 12,92 | 305,49 | 4.819.323 | 0,19 |
| Markov sequencial backoff | 18,22 | 12,95 | 325,74 | 5.326.147 | 0,21 |
| Markov estrutural | 28,96 | 23,56 | 295,29 | 4.988.670 | 0,23 |
| Frase longa / morfologia | 11,99 | 7,85 | 8,21 | 1.773 | 0,02 |
| Frase longa / beam normal | 21,34 | 16,36 | 131,12 | 1.557.578 | 0,11 |
| Frase longa / só N-best | 88,82 | 87,05 | 345,47 | 5.134.912 | 0,31 |
| Frase longa / só rejeições | 203,14 | 172,64 | 567,55 | 8.685.690 | 0,55 |
| Frase longa / ambos | 240,67 | 196,24 | 782,55 | 12.263.019 | 0,71 |

A frase longa é a mesma da planilha local:

> Quae res in Civitate duae plurimum possunt, eae contra nos ambae faciunt in hoc tempore, summa gratia et eloquentia;

São 19 tokens, produto morfológico bruto e pós-propagação de 118.513.704.960,
39.296 estados parciais visitados, 29.254 cortes por score e 1.542 rejeições
H005. O beam conserva 512 atribuições; o MST emite 512 árvores. O resultado
permanece `approximate`; a frase ad hoc não declara gold.

RSS inclui componentes que DHAT não mede como heap solicitado, incluindo
código, stack e overhead do alocador. DHAT acompanha bytes pedidos às
alocações, não memória linear WASM. Alocado acumulado não é memória residente.
O pico DHAT é a soma de `gb` no instante global de máximo, nunca a soma dos
máximos individuais `mb`. Todos os cenários terminaram com zero bytes vivos
rastreados por DHAT.

## Atribuição por pilha

Os números abaixo são alocações cuja pilha contém a função indicada. Categorias
inclusivas podem se sobrepor; não devem ser somadas como fases independentes.
Esta rodada mede o pico global e seus donos por pilha, **não máximos individuais
de cada fase**. Os cenários isolados ajudam na comparação, mas subtrair seus
picos também não produz um high-water mark de fase.

### Árvores retidas dominam o pico do ranker estrutural

No pico de 24.703.812 bytes:

- `populate_tree_best`: 12.647.700 bytes vivos, **51,2% do pico**.
- Só `ranked.analysis.reserve(...)`, em `parser.cpp:4396`:
  8.139.120 bytes vivos, **32,9% do pico**, em 7.041 vetores.
- `ranked.relations.reserve(...)`: 1.726.480 bytes vivos.
- A leitura da WWDB mais alocações de `Database::load_poc` respondem por
  8.102.934 bytes vivos. São infraestrutura compartilhada, não tabelas Markov.
- Pilhas de `dependency_markov.hpp`, incluindo seu extrator: 273.142 bytes
  vivos nesse instante. Isso não mede o máximo isolado do modelo.

O código mantém `ParsedFixture::parser_output` para todas as fixtures durante
as avaliações; cada árvore repete as escolhas morfológicas. A arena de
atribuições tem, portanto, evidência concreta de prioridade para reduzir pico.
Ainda é necessário medir a economia líquida após implementá-la.

### Recomputação e texto dominam boa parte das alocações transitórias

- Markov aditivo: `synthetic_relinearizations` acumula **227.532.756 bytes** e
  **3.373.944 alocações**, 71,0% dos bytes alocados no processo. Isso inclui
  geração das sequências e features chamadas por essa função, repetidas entre
  folds/configurações. O modelo em `markov_model.hpp` aparece em pilhas que
  somam 12.575.823 bytes acumulados, 3,9% do total.
- Markov estrutural: `extract_factors` acumula **109.828.112 bytes** e
  **1.699.564 alocações**, 35,5% dos bytes. IDs apenas dentro da tabela não
  eliminam a construção anterior de fatores/contextos textuais.
- `candidate_score` no estrutural acumula **86.670.677 bytes** e
  **2.152.180 alocações**, 28,0% dos bytes. Em `parser.cpp:4275`, o comparador
  de ordenação chama `assignment_score` repetidamente; em `parser.cpp:2003`,
  o score constrói o texto de `add_score_reason` antes de a função verificar
  se `reasons` é nulo (`parser.cpp:1969`).

As duas últimas porcentagens descrevem caminhos diferentes, mas outros grupos
do JSON se sobrepõem, por exemplo `candidate_score` e `populate_tree_best`.
Pré-calcular scores por atribuição e só construir mensagens quando solicitadas
são candidatos baratos a uma próxima ablação. Nenhuma correção foi aplicada.

### Diagnósticos materializados dominam a frase longa detalhada

Com N-best e rejeições, pilhas de `parsers::to_json` têm **172.766.775 bytes
vivos no pico (84,0%)**, de um total de 205.769.737. Elas acumulam
654.114.542 bytes e 10.630.496 alocações durante a execução.

Há coexistência das estruturas do parser, DOM JSON e buffer textual do
`dump()`. No pico, dois buffers associados à serialização somam 58.195.970
bytes; o crescimento do buffer pode manter temporariamente o anterior vivo.
O JSON final tem 27.145.552 bytes (~25,9 MiB), enquanto a saída normal tem
262.609 bytes. As ablações mostram que tanto N-best quanto rejeições têm
custo expressivo, com rejeições mais caras nesta frase.

Para reduzir esse pico, streaming da saída e evitar cópias no DOM merecem
prioridade. Limitar relatórios mudaria o contrato observável; preservar a saída
completa é uma comparação mais limpa. Backpointers no beam continuam úteis,
mas não atacam o maior componente observado neste cenário detalhado.

## Callgrind

| Cenário | Instruções `Ir` | Fração exclusiva em `malloc.c` |
|---|---:|---:|
| Markov aditivo | 2.550.932.077 | 25,22% |
| Markov estrutural | 2.970.388.801 | 24,52% |
| Frase longa / beam normal | 1.448.420.055 | 15,45% |
| Frase longa / ambos os diagnósticos | 8.081.279.586 | 28,41% |

A fração soma custos **exclusivos** de funções atribuídas ao arquivo glibc
`malloc.c` pelo `callgrind_annotate --inclusive=no --threshold=100`;
não inclui todo o custo de strings, cópias ou wrappers C++.
O relatório inclusivo também destaca `candidate_score` e o comparador de
`populate_tree_best`, coerentes com o DHAT e a inspeção do código.
Custos inclusivos e linhas de funções inline não devem ser somados ingenuamente.
`Ir` não é tempo, ciclos de CPU ou cache misses; cache/branch simulation não foi
habilitada. Alguns perfis DHAT e Callgrind se sobrepuseram em tempo de execução;
nenhuma medição nativa da tabela foi feita durante esses perfis.

## Próximas ablações indicadas pelos dados

1. Evitar construção de mensagens quando `reasons == nullptr` e pré-calcular
   scores usados na ordenação, preservando ordem das contas e desempates.
2. Compartilhar atribuições morfológicas entre árvores.
3. Reutilizar projeções/reordenações/fatores imutáveis entre avaliações,
   respeitando folds e sem armazenar estatísticas treinadas entre splits.
4. Serializar diagnósticos sem um segundo grafo inteiro de objetos JSON.
5. Compactar Markov com IDs e arrays, incluindo o caminho de extração,
   inicialmente mantendo `double` e a semântica atual.

Essa ordem vale para os cenários medidos. Corpus maiores podem tornar o modelo
o principal consumidor. Ainda não há medição de treino em treebank grande,
modelo congelado, runtime WASM ou bytes por fase com marcação explícita.

## Reprodução e artefatos

Executar da raiz do repositório; escolher diretórios de saída novos, pois o
runner recusa sobrescrever uma rodada existente.

```sh
cmake -S . -B build/parsers-memory -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  '-DCMAKE_CXX_FLAGS_RELEASE=-O3 -g -gdwarf-4 -DNDEBUG' \
  '-DCMAKE_C_FLAGS_RELEASE=-O3 -g -gdwarf-4 -DNDEBUG' \
  -DENABLE_TESTS=OFF -DENABLE_SANITIZERS=OFF \
  -DPARSERS_INVESTIGATION_REGISTER_TESTS=OFF
cmake --build build/parsers-memory --target \
  parsers_investigation markov_parser_ranker dependency_markov_ranker wwdb_poc_pack -j 3
build/parsers-memory/wwdb_poc_pack whitakers-words \
  build/parsers-memory/words-profile.wwdb dense
python3 parsers_investigation/profile_memory.py \
  --database build/parsers-memory/words-profile.wwdb \
  --output parsers_investigation/reports/memory-rerun --tools native dhat
python3 parsers_investigation/profile_memory.py \
  --database build/parsers-memory/words-profile.wwdb \
  --output parsers_investigation/reports/memory-rerun-callgrind \
  --tools callgrind --cases markov-additive dependency-markov long-beam long-beam-diagnostics
python3 parsers_investigation/summarize_memory.py \
  parsers_investigation/reports/memory-rerun \
  parsers_investigation/reports/memory-rerun-callgrind \
  --output parsers_investigation/reports/memory-rerun-analysis.json
```

- [Dados consolidados e verificação das 48 saídas](reports/memory-baseline-analysis.json).
- [Nativo: manifest](reports/memory-baseline-native-v11/manifest.json) e
  [amostras](reports/memory-baseline-native-v11/summary.json).
- [DHAT: manifest](reports/memory-baseline-dhat/manifest.json) e
  [resumo](reports/memory-baseline-dhat/summary.json).
- [Ablações dos diagnósticos](reports/memory-baseline-diagnostics-split/summary.json).
- [Callgrind](reports/memory-baseline-callgrind/summary.json) e
  [custos exclusivos do alocador](reports/memory-baseline-callgrind/allocator-summary.json).

Os diretórios contêm stdout, stderr, logs, perfis brutos e manifests com comandos
completos. Os perfis `*.dhat.1.profile` podem ser abertos no visualizador DHAT
instalado com Valgrind. Os `*.callgrind.1.profile` podem ser abertos no
KCachegrind ou lidos com `callgrind_annotate --auto=no --threshold=100`, escolhendo
`--inclusive=yes` ou `--inclusive=no`. As anotações desta rodada também foram
salvas no diretório Callgrind.
