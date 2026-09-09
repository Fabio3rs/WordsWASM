# Relatório dos parsers — Gate D0 e comparativos didáticos

## Resultado

O contrato v3 foi executado em 17 frases de S0. O relatório separa morfologia, busca, attachments, árvores, recognizers e projeção; nenhuma soma combina essas unidades.

- Dataset: `sha256:bb89d1c6a7305ecfaf828747018598e7cd97c321570a80da38249c433d131803`
- Commit configurado: `fb41bd5e639163f4ecc7d8ad17e97f3f441fe481+parsers-dirty`
- Compilador: `Clang 21.0.0` (`Release`)
- Orçamento de enumeração: `1000000` atribuições
- Perfil do core: `whitakerTrim=annotate`, `orthography=classical-and-medieval`, `twoWords=disabled`; mecanismos=`{"packons":true,"prefixes":true,"productiveDerivations":true,"suffixes":true,"syncope":true,"tackons":true,"tickons":true,"verbalCompounds":true}`.
- Fixtures com proveniência didática verificada: 10/17
- Tempos: uma observação por frase, adequados apenas para diagnóstico.
- Memória: estimativa das estruturas próprias, não RSS.

## Semântica da decisão

Uma hard constraint pode eliminar uma análise como **impossível**; toda análise restante é apenas **possível**, e as features brandas ordenam esse conjunto por plausibilidade. `bestScore` e `scoreReasons` são scores manuais decomponíveis, não probabilidades calibradas. O v3 agrega rejeições por ID de constraint e expõe, sob `--include-nbest`, o N-best completo com proveniência e assessment por candidato; ele não atribui probabilidades.

## Corpus e gold

| Frase | Candidatos | Produto bruto | Scan | GAC | Arestas H005–H011 | Atribuições | Attachments | Árvores P/NP | Gold morfológico | Gold de dependências |
|---|---|---:|---|---|---:|---:|---:|---:|---:|---:|
| Petrus est bonus. | 1×2×2 | 4 | 1×2×2 | 1×2×2 | 4 | 4 | 4 | 12/12 | 1 | 1 |
| Maria est bona. | 15×2×12 | 360 | 15×2×12 | 15×2×12 | 24 | 314 | 314 | 384/172 | 1 | 1 |
| Exemplum est bonum. | 8×2×11 | 176 | 8×2×11 | 8×2×11 | 30 | 176 | 176 | 240/284 | 1 | 1 |
| Alumni sunt parvi. | 8×1×6 | 48 | 8×1×6 | 8×1×6 | 12 | 48 | 48 | 90/22 | 1 | 1 |
| Alumnae sunt altae. | 9×1×8 | 72 | 9×1×8 | 9×1×8 | 5 | 72 | 72 | 80/10 | 2 (empate no topo) | 2 (empate no topo) |
| Bella sunt aspera. | 11×1×22 | 242 | 11×1×22 | 11×1×22 | 15 | 242 | 242 | 396/98 | 1 | 1 |
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

## Integração com o core morfológico

O lattice contém 1 candidato incompatível com o trim histórico, mantidos porque o perfil usa `annotate`, e 0 candidatos com notices editoriais. Esses sinais agora são publicados por análise e não são confundidos com hard constraints do parser.

O corpus S0 acionou 0 candidatos de composto verbal. Quando presentes, H012 acopla o predicado composto ao token auxiliar e a projeção mantém ambos os nós, emitindo `aux`. Numerais romanos artificiais também entram no lattice com proveniência não-Whitaker explícita.

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
| `cartesian-leaf-check` | 17/17 | 1084 | 2691 | 2326 | 70 | 167 | 2880 |
| `incremental-dfs` | 17/17 | 1084 | 2683 | 2323 | 70 | 173 | 2232 |
| `dfs-mrv-forward-checking` | 17/17 | 1084 | 2425 | 5643 | 63 | 174 | 2060 |
| `worklist-prefilter` | 17/17 | 1084 | 2655 | 2291 | 56 | 169 | 2199 |
| `gac-propagation` | 17/17 | 1084 | 2645 | 2279 | 52 | 167 | 2177 |
| `gac-residue-cache` | 17/17 | 1084 | 2645 | 2279 | 52 | 183 | 2754 |

Equivalência extensional das seis buscas: **sim**, comparando IDs exatos, não apenas contagens.

## Propagação — scan, GAC e resíduos

| Estratégia | Remoções | Checks de suporte | Hits | Misses | Invalidações | Checks do resíduo | Queue pops | Revisões | Estados enumerados |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| `worklist-prefilter` | 6 | 264 | 0 | 0 | 0 | 0 | 0 | 0 | 2655 |
| `gac-propagation` | 8 | 1161 | 0 | 0 | 0 | 0 | 27 | 79 | 2645 |
| `gac-residue-cache` | 8 | 1089 | 30 | 238 | 0 | 56 | 27 | 79 | 2645 |

A GAC remove valores dos dois lados da constraint. Em `In urbe manet`, o scan reduz o produto 12→6; a agenda reduz 12→2.

O cache reutilizou 30 testemunhos e reduziu os checks semânticos de suporte de 1161 para 1089 (6,2%). Para isso, fez 56 checks baratos de presença no domínio. Houve 0 invalidações: S0 ainda não exercita cascatas capazes de invalidar um suporte previamente guardado. Os tempos de uma única execução e a estimativa de memória não sustentam uma conclusão de desempenho.

## Relações candidatas — H005/H006/H007/H011

Foram materializadas **216** arestas tipadas no corpus: 8 `preposition-complement`, 160 `verb-argument`, 6 `coordination`, e 42 `comparison-standard`.

| Compatibilidade | Arestas |
|---|---:|
| compatível | 17 |
| incompatível | 41 |
| indeterminada | 158 |

A projeção selecionou 8 arestas explícitas nas análises top-1. `Accredo amico` exerce H006: a aresta dativa compatível é selecionada como `iobj` e recebe S008, enquanto a alternativa ablativa permanece morfologicamente possível mas não é promovida a argumento regido. `Placet` continua válido sem complemento. Os quatro pares comparativos exercitam H011 e emitem `obl:cmp`.

`dependency-projection` escolhe deterministicamente entre candidatas compatíveis. Os 158 casos indeterminados mostram que `VerbKind` sem frame de regência não basta para decidir papéis argumentais.

## Busca exata de attachments

`dependency-attachment-search` enumerou **1113** análises relacionais sobre 1084 atribuições morfológicas, visitando 65 estados de escolha em 36 slots.

A projeção determinística pertence ao conjunto exato em 1084/1084 atribuições. Os IDs canônicos e o digest do conjunto tornam essa comparação reproduzível.

H005, H007 e H011 abrem slots obrigatórios quando a construção está selecionada; H006 permanece opcional sem um frame que prove obrigatoriedade. A busca cobre somente essas quatro famílias de relações; ainda não enumera heads para todos os tokens nem garante uma árvore de dependências completa.

O orçamento `maxProduct` também limita a materialização desse conjunto. Ao excedê-lo, a estratégia retorna `experiment-budget-exceeded` e não publica IDs parciais como se formassem um conjunto exato.

## Oráculo exato de árvores

O domínio comum materializou **5412** arcos sobre as atribuições morfológicas e o DFS exato produziu **7041** árvores: 2598 projetivas e 4443 não projetivas. Cada árvore tem exatamente uma raiz, um head por token, é conectada e acíclica.

A poda incremental rejeitou 1806 fechamentos de ciclo e 100 escolhas incompatíveis com raiz única. A projeção determinística pertence ao conjunto em 1084/1084 atribuições.

A fixture sintética `Bona rosam puella amat` torna a distinção observável: ela possui 390 árvores projetivas e 127 não projetivas; seu gold liga `Bona` a `puella` através de `rosam→amat`, formando arestas cruzadas, e fica no rank 1.

As demais árvores não projetivas pertencem a análises morfológicas alternativas e incluem arcos que atravessam a raiz artificial; não representam a mesma quantidade de frases latinas independentes.

Os scores T001 são heurísticas auditáveis de arco, não pesos treinados. O oráculo fornece agora a resposta de referência para testar decodificadores, mas sua enumeração exponencial continua restrita a S0 e ao orçamento `maxProduct`.

## Decodificadores projetivo e não projetivo

| Estratégia | Árvores | Projetivas | Não projetivas | Estados/arestas examinadas | Ciclos contraídos | Igual ao oráculo | p50 µs | p95 µs |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| `dependency-eisner` | 1084 | 1084 | 0 | 10282 | 0 | sim | 444 | 4880 |
| `dependency-mst` | 1084 | 1014 | 70 | 4157 | 2 | sim | 500 | 4836 |

Chu–Liu/Edmonds supera o ótimo projetivo em 70/1084 atribuições. Eisner emite somente árvores projetivas; o MST pode escolher cruzamentos quando aumentam o score.

Em `Bona rosam puella amat`, Eisner deliberadamente não contém o gold não projetivo (`survives=false`), enquanto Chu–Liu/Edmonds o recupera em rank 1.

Os contadores de trabalho permanecem próprios de cada algoritmo: células/splits de Eisner não são a mesma unidade que arestas examinadas e ciclos contraídos por MST.

## Baselines sintáticos — métricas próprias

| Estratégia | Atribuições aceitas | Métrica própria | Valor | p50 µs | p95 µs |
|---|---:|---|---:|---:|---:|
| `dependency-projection` | 1084 | relações emitidas | 3553 | 181 | 2265 |
| `dependency-attachment-search` | 1084 | análises de attachment | 1113 | 204 | 2488 |
| `dependency-tree-oracle` | 1084 | árvores completas | 7041 | 1133 | 42942 |
| `earley-fixed-point-recognizer` | 1084 | itens/deduções criados | 69888 | 366 | 4415 |
| `gslr-stackset-recognizer` | 1084 | configurações de pilha criadas | 18892 | 219 | 2637 |

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
- O self-test passou nas 17 fixtures com a WWDB full atual; os 238 registros deste relatório usam o schema v3.

## Decisão D0

O Gate D0 permanece satisfeito. Dez exemplos didáticos cobrem concordância e as duas construções do segundo termo da comparação. H011 torna o contraste observável sem apagar a grafia da fonte nem resolver artificialmente a categoria de `quam`. Eisner e Chu–Liu/Edmonds continuam iguais aos respectivos ótimos do oráculo. O N-best e a proveniência morfológica agora são explícitos; ainda não há probabilidades calibradas nem evidência para escolher o decodificador padrão.

## Reprodução

```sh
cmake -S . -B build/parsers -G Ninja -DCMAKE_BUILD_TYPE=Release -DENABLE_TESTS=ON \
  -DPARSERS_INVESTIGATION_CORPUS_PATH=$PWD/parsers_investigation/corpus/agreement_fixtures.json
cmake --build build/parsers --target parsers_investigation
build/parsers/parsers_investigation/parsers_investigation --self-test
build/parsers/parsers_investigation/parsers_investigation > /tmp/parsers-results-v3.ndjson
python3 parsers_investigation/generate_report.py /tmp/parsers-results-v3.ndjson --output parsers_investigation/REPORT.md
```
