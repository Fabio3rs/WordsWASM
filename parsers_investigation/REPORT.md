# Relatório dos parsers — Gate D0 e comparativos didáticos

## Resultado

O contrato v2 foi executado em 17 frases de S0. O relatório separa morfologia, busca, attachments, árvores, recognizers e projeção; nenhuma soma combina essas unidades.

- Dataset: `sha256:99f23fa3d2a962d2e325b192f064454046c78954af0fb20b4856b0624a288d05`
- Commit configurado: `9fa0257d61eae79e0ff31ce6c83f87dc9f76415d`
- Compilador: `Clang 21.0.0` (`Release`)
- Orçamento de enumeração: `1000000` atribuições
- Fixtures com proveniência didática verificada: 10/17
- Tempos: uma observação por frase, adequados apenas para diagnóstico.
- Memória: estimativa das estruturas próprias, não RSS.

## Semântica da decisão

Uma hard constraint pode eliminar uma análise como **impossível**; toda análise restante é apenas **possível**, e as features brandas ordenam esse conjunto por plausibilidade. `bestScore` e `scoreReasons` são scores manuais decomponíveis, não probabilidades calibradas. O v2 ainda agrega rejeições por ID de constraint, sem evidência individual por análise, e não expõe o N-best completo nem um campo de probabilidade; esses são requisitos do próximo ciclo, não propriedades retroativas destes números.

## Corpus e gold

| Frase | Candidatos | Produto bruto | Scan | GAC | Arestas H005–H011 | Atribuições | Attachments | Árvores P/NP | Gold morfológico | Gold de dependências |
|---|---|---:|---|---|---:|---:|---:|---:|---:|---:|
| Petrus est bonus. | 1×2×2 | 4 | 1×2×2 | 1×2×2 | 4 | 4 | 4 | 12/12 | 1 | 1 |
| Maria est bona. | 15×2×12 | 360 | 15×2×12 | 15×2×12 | 24 | 314 | 314 | 384/172 | 1 | 1 |
| Exemplum est bonum. | 8×2×11 | 176 | 8×2×11 | 8×2×11 | 30 | 176 | 176 | 240/284 | 1 | 1 |
| Alumni sunt parvi. | 8×1×6 | 48 | 8×1×6 | 8×1×6 | 12 | 48 | 48 | 90/22 | 1 | 1 |
| Alumnae sunt altae. | 9×1×8 | 72 | 9×1×8 | 9×1×8 | 5 | 72 | 72 | 80/10 | 2 (empate no topo) | 2 (empate no topo) |
| Bella sunt aspera. | 10×1×22 | 220 | 10×1×22 | 10×1×22 | 12 | 220 | 220 | 323/74 | 1 | 1 |
| Filius est intelligentior patre. | 1×2×2×3 | 12 | 1×2×2×3 | 1×2×2×3 | 14 | 12 | 16 | 32/74 | 1 | 1 |
| Filius est intelligentior quam pater. | 1×2×2×7×2 | 56 | 1×2×2×7×2 | 1×2×2×7×2 | 34 | 56 | 64 | 468/2132 | 1 | 1 |
| Asinus est prudentior equo. | 2×2×2×2 | 16 | 2×2×2×2 | 2×2×2×2 | 10 | 16 | 24 | 36/52 | 1 | 1 |
| Asinus est prudentior quam equus. | 2×2×2×7×1 | 56 | 2×2×2×7×1 | 2×2×2×7×1 | 28 | 56 | 64 | 448/1460 | 1 | 1 |
| Veni. | 6 | 6 | 3 | 3 | 0 | 3 | 3 | 3/0 | 1 | 1 |
| In urbe manet. | 2×3×2 | 12 | 1×3×2 | 1×1×2 | 12 | 2 | 2 | 2/0 | 1 | 1 |
| In urbem venit. | 2×1×3 | 6 | 1×1×3 | 1×1×3 | 5 | 3 | 3 | 3/0 | 1 | 1 |
| Puer puellaque veniunt. | 2×3×1 | 6 | 2×2×1 | 2×2×1 | 11 | 2 | 2 | 3/0 | 1 | 1 |
| Accredo amico. | 1×7 | 7 | 1×7 | 1×7 | 2 | 7 | 8 | 8/0 | 1 | 1 |
| Bona rosam puella amat. | 12×2×3×1 | 72 | 12×2×3×1 | 12×2×3×1 | 10 | 68 | 68 | 390/127 | 1 | 1 |
| Placet. | 3 | 3 | 3 | 3 | 0 | 3 | 3 | 3/0 | 1 | 1 |

## Corpus didático verificado

Foram promovidas 10 frases de *Gramática Latina*: 6 exemplos de concordância predicativa da página 54 e 4 exemplos de comparação da página 114. Cada fixture registra snapshot, página, unidade, bloco, texto-fonte e o bloco que sustenta a anotação.

Nas frases de concordância, a fonte afirma caso, número e gênero. Nos pares comparativos, ela contrasta o segundo termo em ablativo com a construção `quam` + caso paralelo ao primeiro termo. A análise verbal completa e os heads/labels continuam adições editoriais explícitas, não alegações atribuídas ao livro.

Os 10/10 golds didáticos são projetivos e sobrevivem em Eisner e no MST. Este lote valida concordância, mas ainda não decide a necessidade de não projetividade em latim real.

`Alumnae sunt altae` expôs ambiguidade lexical genuína: o gold adjetival `altus` aparece no rank 2, mas empata no melhor score (`bestScoreTie=true`). O desempate estável mostra primeiro o particípio de `alo`. A sintaxe isolada admite tanto ‘são altas’ quanto ‘foram criadas’; a preferência didática não autoriza eliminar a segunda leitura.

Os pares comparativos exercitam H011 nas duas realizações. 2 fixtures preservam a grafia impressa `intelligentior`, mas consultam explicitamente `intellegentior`; o override e sua justificativa aparecem no resultado, sem alterar o testemunho da fonte.

A fonte chama `quam` de conjunção comparativa, enquanto a WWDB ranqueia primeiro a análise `adverb`. O gold aceita ambas as categorias como uma ambiguidade POS ainda não resolvida; as duas projetam `mark` e dependem da mesma relação `comparison-standard`/`obl:cmp`.

## Track A — busca morfológica/CSP

As seis linhas abaixo têm a mesma unidade: estados parciais da enumeração e atribuições completas.

| Estratégia | Cobertura | Atribuições aceitas | Estados parciais | Checks de constraints | Backtracks | p50 µs | p95 µs |
|---|---:|---:|---:|---:|---:|---:|---:|
| `cartesian-leaf-check` | 17/17 | 1062 | 2645 | 2282 | 70 | 104 | 1783 |
| `incremental-dfs` | 17/17 | 1062 | 2637 | 2279 | 70 | 103 | 1803 |
| `dfs-mrv-forward-checking` | 17/17 | 1062 | 2380 | 5576 | 63 | 101 | 1598 |
| `worklist-prefilter` | 17/17 | 1062 | 2609 | 2247 | 56 | 99 | 1803 |
| `gac-propagation` | 17/17 | 1062 | 2599 | 2235 | 52 | 98 | 1855 |
| `gac-residue-cache` | 17/17 | 1062 | 2599 | 2235 | 52 | 98 | 1789 |

Equivalência extensional das seis buscas: **sim**, comparando IDs exatos, não apenas contagens.

## Propagação — scan, GAC e resíduos

| Estratégia | Remoções | Checks de suporte | Hits | Misses | Invalidações | Checks do resíduo | Queue pops | Revisões | Estados enumerados |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| `worklist-prefilter` | 6 | 264 | 0 | 0 | 0 | 0 | 0 | 0 | 2609 |
| `gac-propagation` | 8 | 1160 | 0 | 0 | 0 | 0 | 27 | 79 | 2599 |
| `gac-residue-cache` | 8 | 1088 | 30 | 237 | 0 | 56 | 27 | 79 | 2599 |

A GAC remove valores dos dois lados da constraint. Em `In urbe manet`, o scan reduz o produto 12→6; a agenda reduz 12→2.

O cache reutilizou 30 testemunhos e reduziu os checks semânticos de suporte de 1160 para 1088 (6,2%). Para isso, fez 56 checks baratos de presença no domínio. Houve 0 invalidações: S0 ainda não exercita cascatas capazes de invalidar um suporte previamente guardado. Os tempos de uma única execução e a estimativa de memória não sustentam uma conclusão de desempenho.

## Relações candidatas — H005/H006/H007/H011

Foram materializadas **213** arestas tipadas no corpus: 8 `preposition-complement`, 157 `verb-argument`, 6 `coordination`, e 42 `comparison-standard`.

| Compatibilidade | Arestas |
|---|---:|
| compatível | 17 |
| incompatível | 41 |
| indeterminada | 155 |

A projeção selecionou 8 arestas explícitas nas análises top-1. `Accredo amico` exerce H006: a aresta dativa compatível é selecionada como `iobj` e recebe S008, enquanto a alternativa ablativa permanece morfologicamente possível mas não é promovida a argumento regido. `Placet` continua válido sem complemento. Os quatro pares comparativos exercitam H011 e emitem `obl:cmp`.

`dependency-projection` escolhe deterministicamente entre candidatas compatíveis. Os 155 casos indeterminados mostram que `VerbKind` sem frame de regência não basta para decidir papéis argumentais.

## Busca exata de attachments

`dependency-attachment-search` enumerou **1091** análises relacionais sobre 1062 atribuições morfológicas, visitando 65 estados de escolha em 36 slots.

A projeção determinística pertence ao conjunto exato em 1062/1062 atribuições. Os IDs canônicos e o digest do conjunto tornam essa comparação reproduzível.

H005, H007 e H011 abrem slots obrigatórios quando a construção está selecionada; H006 permanece opcional sem um frame que prove obrigatoriedade. A busca cobre somente essas quatro famílias de relações; ainda não enumera heads para todos os tokens nem garante uma árvore de dependências completa.

O orçamento `maxProduct` também limita a materialização desse conjunto. Ao excedê-lo, a estratégia retorna `experiment-budget-exceeded` e não publica IDs parciais como se formassem um conjunto exato.

## Oráculo exato de árvores

O domínio comum materializou **5275** arcos sobre as atribuições morfológicas e o DFS exato produziu **6944** árvores: 2525 projetivas e 4419 não projetivas. Cada árvore tem exatamente uma raiz, um head por token, é conectada e acíclica.

A poda incremental rejeitou 1776 fechamentos de ciclo e 74 escolhas incompatíveis com raiz única. A projeção determinística pertence ao conjunto em 1062/1062 atribuições.

A fixture sintética `Bona rosam puella amat` torna a distinção observável: ela possui 390 árvores projetivas e 127 não projetivas; seu gold liga `Bona` a `puella` através de `rosam→amat`, formando arestas cruzadas, e fica no rank 1.

As demais árvores não projetivas pertencem a análises morfológicas alternativas e incluem arcos que atravessam a raiz artificial; não representam a mesma quantidade de frases latinas independentes.

Os scores T001 são heurísticas auditáveis de arco, não pesos treinados. O oráculo fornece agora a resposta de referência para testar decodificadores, mas sua enumeração exponencial continua restrita a S0 e ao orçamento `maxProduct`.

## Decodificadores projetivo e não projetivo

| Estratégia | Árvores | Projetivas | Não projetivas | Estados/arestas examinadas | Ciclos contraídos | Igual ao oráculo | p50 µs | p95 µs |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| `dependency-eisner` | 1062 | 1062 | 0 | 9871 | 0 | sim | 218 | 4159 |
| `dependency-mst` | 1062 | 992 | 70 | 3967 | 1 | sim | 209 | 4058 |

Chu–Liu/Edmonds supera o ótimo projetivo em 70/1062 atribuições. Eisner emite somente árvores projetivas; o MST pode escolher cruzamentos quando aumentam o score.

Em `Bona rosam puella amat`, Eisner deliberadamente não contém o gold não projetivo (`survives=false`), enquanto Chu–Liu/Edmonds o recupera em rank 1.

Os contadores de trabalho permanecem próprios de cada algoritmo: células/splits de Eisner não são a mesma unidade que arestas examinadas e ciclos contraídos por MST.

## Baselines sintáticos — métricas próprias

| Estratégia | Atribuições aceitas | Métrica própria | Valor | p50 µs | p95 µs |
|---|---:|---|---:|---:|---:|
| `dependency-projection` | 1062 | relações emitidas | 3487 | 110 | 1926 |
| `dependency-attachment-search` | 1062 | análises de attachment | 1091 | 128 | 2112 |
| `dependency-tree-oracle` | 1062 | árvores completas | 6944 | 790 | 34721 |
| `earley-fixed-point-recognizer` | 1062 | itens/deduções criados | 68344 | 283 | 4228 |
| `gslr-stackset-recognizer` | 1062 | configurações de pilha criadas | 18474 | 145 | 2300 |

Equivalência extensional dos recognizers: **sim**. Isso demonstra equivalência nesta gramática mínima, não equivalência entre Earley e GLR como famílias.

Os valores da coluna ‘métrica própria’ não são comparáveis entre linhas: relações, itens Earley e pilhas explícitas são unidades diferentes.

## Validade do gold e limites

- Gold morfológico completo em rank 1: 16/17.
- Gold morfológico empatado no melhor score: 17/17.
- Gold de dependências da projeção em rank 1: 16/17.
- Gold de dependências da projeção empatado no melhor score: 17/17.
- Gold de dependências em rank 1: Eisner 15/17; Chu–Liu/Edmonds 16/17.
- Gold de dependências empatado no melhor score: Eisner 16/17; Chu–Liu/Edmonds 17/17.
- O self-test também muta caso e relação mantendo o restante da análise; ambas as mutações precisam falhar.
- `preferredLemmaSequence` permanece apenas como sinal de compatibilidade e nunca é chamado de gold estrutural.
- `forest.available` é falso e contagens de derivações/SPPF são nulas: estes protótipos não constroem floresta.
- `dependency-projection` é uma projeção determinística, não um decodificador ótimo.
- `dependency-attachment-search` é um oráculo exato apenas para H005/H006/H007/H011, não uma busca de árvores completas.
- `dependency-tree-oracle` enumera árvores completas exatamente em S0, mas não é um algoritmo adequado para corpus longo.
- Eisner e Chu–Liu/Edmonds igualam, respectivamente, os ótimos projetivo e irrestrito do oráculo em cada atribuição de S0.
- Os scores T001 formalizam a política atual; ainda não foram calibrados em train/dev nem validados externamente.
- `gslr-stackset-recognizer` usa um conjunto de pilhas explícitas, não GSS.
- H006 não exige a presença global de um complemento: `Placet.` preserva `placeo`. Quando uma aresta argumento–predicado é escolhida, o caso incompatível é rejeitado na relação sem apagar a análise morfológica como possível adjunto.
- O catálogo didático tem 10/33 frases promovidas a gold estrutural; as outras 23 continuam `candidate-unverified`.
- A auditoria reencontrou 33/33 frases nos 15 blocos declarados e validou reciprocamente as 10 promoções. O censo lexical bruto continua em 29/33: `intelligentior` requer os overrides explícitos nas duas fixtures; `Catilina` e `Pyrrho` ainda não existem na WWDB.
- O self-test passou nas 17 fixtures com WWDB full e search-only; o corpus e os 238 registros passaram nos schemas v2; a suíte geral passou em 94/94 testes; ASan/UBSan também passou (LeakSanitizer desativado sob `ptrace`).

## Decisão D0

O Gate D0 permanece satisfeito. Dez exemplos didáticos agora cobrem concordância e as duas construções do segundo termo da comparação. H011 torna o contraste observável sem apagar a grafia da fonte nem resolver artificialmente a categoria de `quam`. Eisner e Chu–Liu/Edmonds continuam iguais aos respectivos ótimos do oráculo. O próximo ciclo deve tornar explícito o N-best dos casos possíveis e só então ampliar comparação de inferioridade e ordem livre; ainda não há probabilidades calibradas nem evidência para escolher o decodificador padrão.

## Reprodução

```sh
cmake -S . -B build/parsers -G Ninja -DCMAKE_BUILD_TYPE=Release -DENABLE_TESTS=ON \
  -DPARSERS_INVESTIGATION_CORPUS_PATH=$PWD/parsers_investigation/corpus/agreement_fixtures.json
cmake --build build/parsers --target parsers_investigation
build/parsers/parsers_investigation/parsers_investigation --self-test
build/parsers/parsers_investigation/parsers_investigation > /tmp/parsers-results-v2.ndjson
python3 parsers_investigation/generate_report.py /tmp/parsers-results-v2.ndjson --output parsers_investigation/REPORT.md
```
