# Compartilhamento morfológico implementado e medido

28/09/2026. Primeira ablação da [auditoria de árvores](TREE_SHARING.md),
restrita a `parsers_investigation`; sem mudanças em produção ou CI.

Cada `RankedDependencyTreeAnalysis` agora guarda um `MorphologyIndex` de
32 bits para `Result::morphology_nbest`, em lugar de outro vetor de
`AnalysisChoice`. As relações continuam pertencendo a cada árvore.
`dependency_tree_view` resolve as escolhas e os arcos como spans somente-leitura
no ponto de consumo. Não há ponteiros persistentes entre membros do `Result`.

## Resultado de memória

Mesma WWDB 1.11, corpus, parâmetros, compilador e flags do
[baseline](MEMORY.md). Build novo em `build/parsers-sharing`, preservando os
executáveis anteriores em `build/parsers-memory`. RSS é mediana de três processos
nativos; heap é o máximo global de uma execução DHAT por cenário.

| Cenário | RSS antes → depois, MiB | Heap no pico antes → depois, MiB | Redução do heap |
|---|---:|---:|---:|
| Markov estrutural | 28,96 → **19,98** | 23,56 → **14,59** | **38,1%** |
| Oráculo / corpus | 20,09 → **16,17** | 15,15 → **11,36** | **25,0%** |
| Frase longa / beam normal | 21,34 → **18,64** | 16,36 → **13,63** | **16,7%** |
| Frase longa / N-best e rejeições | 240,67 → **237,51** | 196,24 → **193,48** | **1,4%** |

No estrutural, o pico caiu **9.401.926 bytes**. O censo da representação final
confirma a remoção de **9.233.180 bytes** de payload morfológico duplicado e
**168.984 bytes** dos vetores de headers das árvores. O header passou de
152 para 128 bytes neste ABI. Esses valores descrevem armazenamento retido;
o instante do pico e outros temporários explicam a pequena diferença em
relação ao delta DHAT.

O CLI do oráculo libera resultados por fixture, enquanto o ranker estrutural
mantém o corpus para as avaliações. Por isso a queda de pico difere mesmo
usando as mesmas árvores. O mapa temporário que resolve IDs em índices também
entra nas medições.

| Cenário | Bytes alocados acumulados antes → depois, MiB | Alocações antes → depois |
|---|---:|---:|
| Markov estrutural | 295,29 → 271,52 | 4.988.670 → 4.919.046 |
| Oráculo / corpus | 164,24 → 140,46 | 2.944.321 → 2.874.697 |
| Frase longa / beam normal | 131,12 → 124,13 | 1.557.578 → 1.539.146 |
| Frase longa / diagnósticos | 782,55 → 775,56 | 12.263.019 → 12.244.587 |

A redução proporcional do churn é menor que a do pico: scoring, extração de
features e JSON continuam alocando. Com diagnósticos completos, a saída ainda
repete os tokens de cada árvore para preservar o contrato JSON; o DOM e o
buffer de serialização continuam dominantes. Não houve mudança de algoritmo,
scoring, desempate, cache, smoothing ou política de saída.

Tempos nativos medianos foram 0,23 → 0,20 s no estrutural, 0,14 → 0,12 s no
oráculo, 0,11 → 0,10 s no beam e 0,71 → 0,70 s com diagnósticos. Três amostras
e a resolução dessas medições não sustentam uma conclusão forte de aceleração.
Nenhuma medição nativa nova foi executada junto dos profilers ou da compilação
do auditor. DHAT terminou com zero bytes vivos em todos os quatro cenários.

## Equivalência e validade das referências

[verify_tree_sharing.py](verify_tree_sharing.py) executou os binários anterior e
novo em **12 cenários**, comparando **60 documentos de resultado por versão**:

- oráculo, MST e Eisner nas 17 fixtures, com N-best completo;
- `amatus est` nos três algoritmos, exercendo o composto verbal;
- fragmento `Bona puella`;
- frase longa em MST com N-best/rejeições e em Eisner com N-best;
- matrizes Markov sequencial aditiva, sequencial com backoff e estrutural.

Todos os campos de domínio coincidiram, incluindo arrays na mesma ordem,
scores sem tolerância, ranks, gold, IDs, contadores e diagnósticos. A comparação
remove apenas os campos **de topo** `elapsedNs` e `sourceCommit`: o tempo varia
e o novo executável identifica corretamente o código como `+parsers-dirty`.
Não houve mudança de schema JSON. Os 16 outputs das novas execuções nativas e
DHAT também coincidiram entre si, excluindo somente `elapsedNs`.

O self-test passou nas 17 fixtures. O teste Markov passou, incluindo novos
casos de duas árvores compartilhando um índice não zero, arcos próprios,
cópia/movimentação de `Result`, destruição do dono original e índice inválido.
Esse teste também passou sob Memcheck: **zero erros e zero bytes vivos ao sair**.

O censo pós-mudança reproduziu os mesmos 52 resultados, 9.721 árvores e 47.664
referências a escolhas da auditoria anterior. Conferiu independentemente o
índice contra o `assignment_id`, a identidade do buffer compartilhado e os
metadados. Não há mais payload morfológico possuído individualmente pelas árvores.

## Invariantes da implementação

- `populate_best` finaliza `morphology_nbest` antes de `populate_tree_best`.
- A construção valida o limite de 32 bits e rejeita IDs morfológicos duplicados;
  uma atribuição ausente falha no lookup, em vez de produzir índice arbitrário.
- Depois de criar as árvores, não se deve reordenar a coleção morfológica.
- A view faz acesso com verificação de limites e vale enquanto seu `Result`
  permanecer vivo e sem mutações que invalidem os buffers.
- Copiar ou mover o `Result` conserva os índices locais; as views são obtidas
  novamente sobre o dono correto.
- A view combina morfologia compartilhada com **relações da árvore**, nunca
  com a projeção armazenada no N-best morfológico.

## Reprodução e artefatos

Usar os mesmos argumentos CMake de [MEMORY.md](MEMORY.md), trocando o diretório
de build por `build/parsers-sharing`, e compilar também `markov_model_test`.
Manter os executáveis anteriores para a comparação:

```sh
python3 parsers_investigation/verify_tree_sharing.py \
  --before-build build/parsers-memory --after-build build/parsers-sharing \
  --database build/parsers-memory/words-profile.wwdb \
  --output parsers_investigation/reports/sharing-equivalence-rerun
python3 parsers_investigation/profile_memory.py \
  --build build/parsers-sharing \
  --database build/parsers-memory/words-profile.wwdb \
  --output parsers_investigation/reports/sharing-memory-rerun \
  --tools native dhat \
  --cases dependency-markov long-beam long-beam-diagnostics oracle-corpus
python3 parsers_investigation/summarize_memory.py \
  parsers_investigation/reports/sharing-memory-rerun \
  --output parsers_investigation/reports/sharing-memory-rerun-analysis.json
```

O auditor atual emite schema v2: `treeChoiceSlots` conta referências lógicas,
`treeAnalysisOwnedBytes` e `treeAnalysisBufferBytes` são zero, e
`treeMorphologyIndexBytes` registra os índices. A fonte anterior foi preservada
em `reports/tree-sharing-audit/audit_tree_sharing-before.cpp`; os resultados
schema v1 da auditoria original não foram sobrescritos.

- [Comparação completa de resultados](reports/tree-sharing-equivalence/verification.json).
- [Memcheck](reports/tree-sharing-equivalence/model-test-memcheck.log).
- [Perfis novos: comandos e hashes](reports/tree-sharing-memory/manifest.json).
- [Amostras e medidas brutas](reports/tree-sharing-memory/summary.json).
- [DHAT consolidado e verificação](reports/tree-sharing-memory-analysis.json).
- [Deltas antes/depois](reports/tree-sharing-memory-comparison.json).
- [Comparação dos censos](reports/tree-sharing-audit-after/comparison.json).

A próxima ablação indicada continua sendo evitar mensagens não solicitadas e
recomputações de score na ordenação. O compartilhamento de candidatos entre
atribuições e a serialização sem DOM completo permanecem etapas separadas.
