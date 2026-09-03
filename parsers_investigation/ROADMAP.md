# Roadmap de investigação de parsing sintático

## Objetivo

Determinar, por experimento, qual representação e qual algoritmo são adequados
para consumir o lattice morfológico tipado do WordsWASM e produzir análises
sintáticas explicáveis para latim.

Este diretório é deliberadamente experimental. O objetivo inicial não é criar
um parser completo, mas medir onde a ambiguidade nasce, quanto pode ser
eliminado por restrições morfossintáticas e quanto trabalho pode ser
compartilhado antes de escolher dependency parsing, chart/GLR ou um formalismo
mais expressivo.

## Semântica central da decisão

O produto do parser deve separar três noções que não podem ser fundidas:

1. **impossível** — uma hard constraint encontrou conflito demonstrável e
   registra ID, escopo e evidência;
2. **possível** — a análise sobreviveu a todas as hard constraints aplicáveis,
   inclusive quando há informação insuficiente;
3. **mais ou menos plausível** — entre as análises possíveis, features brandas
   produzem um score decomposto e uma ordenação N-best.

“Possível” não significa “provável”, e ausência de informação nunca prova
impossibilidade. Os pesos manuais atuais são scores de plausibilidade, não
probabilidades. Um valor chamado de probabilidade só poderá aparecer depois de
calibração em dados separados, com universo de candidatos e método de
normalização declarados; o score bruto e suas razões devem continuar
disponíveis para auditoria.

## Estado anterior ao primeiro ciclo

O executável `parsers_investigation`:

- liga diretamente com `words_core`;
- carrega uma WWDB indicada por definição de compilação;
- obtém todas as análises morfológicas sem consultar JSON;
- demonstra a ambiguidade de `Arma virumque cano`;
- ainda não possui tokenização geral, regras sintáticas, ranking, corpus ou
  instrumentação de desempenho.

O core continua responsável somente por léxico e morfologia. A fronteira
oficial está documentada em
[`whitakers-words/docs/parser-readiness.md`](../whitakers-words/docs/parser-readiness.md).

## Primeiro ciclo implementado

O executável aceita `--text`, fixtures JSON v2 ou o TSV legado, tokeniza
preservando posição, roda sem meanings inclusive sobre a WWDB search-only e
emite NDJSON versionado. Os nomes v2 descrevem exatamente os protótipos:

| Estratégia | Papel no experimento |
|---|---|
| `morphology` | censo do lattice, contagens e produto bruto; |
| `cartesian-leaf-check` | DFS exaustiva com orçamento e rejeição nas folhas; |
| `incremental-dfs` | DFS que aplica constraints quando seus escopos ficam completos; |
| `dfs-mrv-forward-checking` | DFS em ordem MRV com checagem conservadora de suporte futuro; |
| `worklist-prefilter` | ponto fixo sobre domínios seguido da mesma enumeração exata; |
| `gac-propagation` | agenda de constraints n-árias que revisa suporte em todos os domínios do escopo; |
| `gac-residue-cache` | mesma agenda GAC, guardando e revalidando um testemunho por valor; |
| `dependency-projection` | grafo determinístico e explicável por sobrevivente; |
| `dependency-attachment-search` | enumeração exata das escolhas H005/H006/H007/H011, com IDs canônicos e comparação contra a projeção; |
| `dependency-tree-oracle` | enumeração exata de árvores completas, com raiz única, aciclicidade, score de arcos e classificação projetiva; |
| `dependency-eisner` | melhor árvore projetiva por atribuição morfológica, sobre o domínio e os scores do oráculo; |
| `dependency-mst` | melhor arborescência possivelmente não projetiva por Chu–Liu/Edmonds, sobre a mesma entrada; |
| `earley-fixed-point-recognizer` | recognizer Earley ainda sem agenda/SPPF; |
| `gslr-stackset-recognizer` | generalized SLR com pilhas explícitas, sem GSS/SPPF. |

O corpus estrutural
[`agreement_fixtures.json`](corpus/agreement_fixtures.json) contém 17 frases;
o TSV legado conserva as 13 iniciais. O conjunto cobre sujeito–verbo,
imperativo, particípio, preposição, coordenação, comparação, duas orações e
fragmento nominal. O `--self-test` exige:

- presença dos lemas gold no lattice;
- equivalência extensional exata entre as seis buscas do Track A;
- equivalência de reconhecimento entre Earley e GLR;
- preservação do gold estruturado por todos os modos aplicáveis.

Os resultados medidos e as limitações estão em [`REPORT.md`](REPORT.md). Eles
constituem um primeiro ciclo pequeno de M0–M6, não encerram D1/D2 nem alegam
cobertura geral do latim. S1 anotada, S2 e S3 continuam pendentes.

Uma segunda fonte local foi triada em
[`corpus/GRAMATICA_LATINA.md`](corpus/GRAMATICA_LATINA.md). Ela fornece 33
frases didáticas com proveniência até bloco/página e cobre concordância,
comparativos, ACI, infinitivos, passiva, relativas, duplo dativo e ablativo
absoluto. Dez exemplos de concordância e comparação foram promovidos a gold
estrutural com proveniência e entram nas métricas; os outros 23 permanecem
`candidate-unverified`.

## Gate D0 — validade da medição

O contrato em [`schema/`](schema/) congela a interpretação histórica do v1 e
define o v2. Métricas de morfologia, propagação, enumeração, parser e floresta
ficam em namespaces distintos; `preferredLemmaSequence` deixa de ser chamado
de gold; e o ranking usa somente as atribuições aceitas pela estratégia.

As 17 fixtures em
[`agreement_fixtures.json`](corpus/agreement_fixtures.json) declaram lema,
POS, caso, número, gênero, morfologia verbal e dependências. O self-test compara
IDs exatos dos sobreviventes e também exige que mutações de caso e relação
falhem. Frases completas e fragmentos selecionam gramáticas iniciais distintas.
Contagens de derivações e SPPF permanecem nulas até existir uma floresta real.

## Track A — agenda GAC e resíduos

O segundo ciclo adicionou cinco microfixtures: H002 com imperativo, H005 com
`in` + ablativo e acusativo, H007 com sujeito coordenado por `-que` e uma
regressão de H006 em `Placet.`. A nova `gac-propagation` usa constraints com
escopos explícitos, agenda por adjacência e revisão de cada valor do domínio.
Ela também alimenta os baselines sintáticos posteriores.

O terceiro ciclo acrescentou `gac-residue-cache`. Cada par
constraint–valor guarda a tupla mínima de candidatos que testemunhou suporte;
numa revisão posterior, o suporte é reutilizado enquanto todos esses candidatos
continuarem ativos. A GAC sem cache permanece como baseline.

O quarto ciclo introduziu uma `RelationLattice` entre análises morfológicas
específicas. Ela materializa `preposition-complement` (H005), `verb-argument`
(H006) e `coordination` (H007), conservando compatibilidade ternária. O oracle
cartesiano e a GAC consultam as mesmas arestas para H005/H007; H006 só valida
uma aresta quando a projeção decide tratá-la como argumento, sem inferir que o
verbo exige complemento.

O quinto ciclo tornou essa lattice enumerável em
`dependency-attachment-search`. Para cada atribuição morfológica, preposições e
`-que` abrem slots obrigatórios; argumentos de verbos com regência conhecida
abrem escolhas opcionais. Duas relações não podem ocupar o mesmo dependent. O
resultado conserva IDs canônicos de análise, digest do conjunto e a resposta
explícita à pergunta “a projeção determinística pertence ao domínio exato?”.
O orçamento de enumeração também limita esse segundo produto e impede publicar
um conjunto parcial como se fosse completo.
Ainda não é uma busca de árvores completas: tokens fora de
H005/H006/H007/H011 não
recebem heads candidatos e não há constraints de raiz única, conectividade,
aciclicidade ou projetividade.

O sexto ciclo acrescentou um domínio de arcos para todos os tokens selecionados
e `dependency-tree-oracle`. O DFS exato escolhe um head por token, poda ciclos
assim que se fecham e aceita somente uma raiz. Como cada token possui um head
ou é a raiz, raiz única mais aciclicidade também garantem conectividade. Cada
árvore recebe ID canônico, digest, score T001 inteiramente decomposto e uma
classificação projetiva/não projetiva. O mesmo orçamento impede que um prefixo
do conjunto seja publicado como resposta exata.

Para tornar a diferença estrutural observável, S0 ganhou a fixture sintética
`Bona rosam puella amat.`. Seu gold contém os arcos cruzados `Bona→puella` e
`rosam→amat`; trata-se de um teste controlado de hipérbato, não de evidência de
frequência no corpus histórico.

O sétimo ciclo implementou Eisner e Chu–Liu/Edmonds. Ambos colapsam labels
concorrentes do mesmo par head–dependent pelo maior score e testam cada raiz
permitida, preservando raiz única. Eisner usa a raiz artificial à esquerda e
reconstrói o melhor chart projetivo. O MST escolhe a melhor aresta de entrada,
contrai ciclos, reajusta os scores das arestas que entram no ciclo e expande a
solução para os arcos originais. Cada score produzido é comparado, por
atribuição morfológica, ao ótimo correspondente do oráculo; cada ID decodificado
também precisa pertencer ao conjunto exato.

Este ciclo corrigiu ainda a classificação do oráculo: projetividade precisa
considerar a aresta da raiz artificial. Uma aresta que atravessa a posição da
raiz pode ser não projetiva mesmo sem cruzar outro arco token–token.

O oitavo ciclo promoveu o primeiro lote da fonte didática. Cinco fixtures já
coincidiam literalmente com a tabela da página 54; a sexta foi restaurada de
`Alumnae sunt pulchrae` para o testemunho `Alumnae sunt altae`. Cada fixture
registra catálogo, snapshot, unidade, bloco, página, texto-fonte, afirmações da
fonte e adições editoriais. O auditor confere reciprocamente catálogo e corpus.

Esse lote também mostrou que rank ordinal e melhor score não são equivalentes:
`altae` admite o adjetivo de `altus` e o particípio de `alo`. A leitura
adjetival indicada pela lição fica em rank 2 apenas pelo desempate estável, mas
empata no melhor score. O schema passou a expor `bestScoreTie` para não contar
essa ambiguidade preservada como erro.

O nono ciclo promoveu quatro exemplos comparativos da página 114. A nova
relação `comparison-standard` (H011) distingue o segundo termo em ablativo sem
`quam` do segundo termo introduzido por `quam`, que deve repetir o caso do
primeiro termo. Na segunda construção, a aresta registra como contexto tanto o
primeiro termo quanto a análise concreta do marcador; a projeção emite
`obl:cmp` e `mark`, e S015 recompensa uma construção comparativa licenciada.

A grafia impressa `intelligentior` não existe na WWDB, que reconhece
`intellegentior`. Em vez de corrigir a fonte silenciosamente, a fixture possui
um `lookupOverride` justificado. O resultado conserva lado a lado os tokens de
superfície, os tokens consultados e os overrides. A fonte chama `quam` de
conjunção comparativa, enquanto a WWDB prefere sua análise adverbial; o gold
aceita ambas as categorias e preserva essa divergência de tagset como
ambiguidade explícita.

Findings atuais:

- as seis buscas preservam exatamente o conjunto do cartesiano nas 17
  fixtures e nas 1.062 atribuições aceitas;
- a GAC remove oito valores, contra seis do scan de ponto fixo;
- em `In urbe manet`, o produto cai de 12 para 2, contra 12 para 6 no scan;
- essa poda mais forte custa 1.160 checks semânticos de suporte, contra 264 do
  scan; com resíduos, cai para 1.088 checks semânticos (−6,2%), acrescidos de 56
  verificações baratas de presença no domínio;
- foram observados 30 hits e 237 misses de resíduo. Nenhum testemunho foi
  invalidado neste S0, portanto ainda falta uma fixture com poda em cascata e
  não há conclusão sobre comportamento amortizado;
- H006 como “todo verbo regente exige algum nominal no caso regido” era
  linguisticamente incorreta: eliminava `placeo` em `Placet.`. Agora a regência
  só rejeita uma aresta argumento–predicado incompatível, não a ausência global
  de complemento;
- a projeção agora classifica o acusativo regido por preposição como `obl`, não
  como objeto verbal apenas por causa do caso;
- nas 17 fixtures, a lattice H005/H006/H007/H011 contém 213 arestas: 157
  argumento–verbo, 42 padrões de comparação, oito complemento–preposição e
  seis de coordenação. Dezessete são formalmente compatíveis, 41 incompatíveis
  e 155 indeterminadas;
- `Accredo amico.` exerce regência dativa concreta: a projeção seleciona H006,
  emite `iobj` e registra S008. A alternativa ablativa não é eliminada como
  morfologia, e `Placet.` continua aceito sem complemento;
- a projeção top-1 seleciona oito arestas explícitas no corpus, incluindo H011
  nos quatro comparativos;
- a busca exata de attachments preserva as mesmas 1.062 atribuições
  morfológicas e enumera 1.091 análises relacionais;
- as 1.062 projeções determinísticas testadas pertencem aos respectivos
  conjuntos exatos. Isso valida a projeção como membro do domínio
  H005/H006/H007/H011, não como árvore ótima nem como melhor análise
  relacional;
- o domínio completo materializa 5.275 arcos e o oráculo enumera 6.944 árvores:
  2.525 projetivas e 4.419 não projetivas. Foram podados 1.776 fechamentos de
  ciclo e 74 escolhas incompatíveis com raiz única;
- todas as 1.062 projeções também pertencem ao conjunto exato de árvores. O gold
  completo fica no rank 1 em 16/17 fixtures e empata no melhor score em 17/17;
  os scores T001 ainda são heurísticas não treinadas;
- a fixture controlada de quatro tokens contribui 127 árvores não projetivas.
  As outras 4.292 surgem de análises morfológicas alternativas e de arcos que
  atravessam a raiz artificial; não constituem exemplos linguísticos
  independentes. S0 não estima a frequência de não projetividade no latim real;
- Eisner e Chu–Liu/Edmonds produzem uma árvore para cada uma das 1.062
  atribuições e igualam todos os ótimos do oráculo. Eisner visita 9.871
  combinações de chart/split; o MST examina 3.967 arestas e contrai um ciclo —
  unidades que não devem ser comparadas diretamente;
- o ótimo irrestrito supera o projetivo em 70/1.062 atribuições. O MST escolhe 70
  árvores não projetivas; Eisner escolhe zero;
- no hipérbato controlado, Eisner não pode preservar o gold cruzado, enquanto
  Chu–Liu/Edmonds o recupera em rank 1. Nos demais 16 casos ambos mantêm o gold
  no melhor score; `Alumnae sunt altae` fica no rank ordinal 2 por empate;
- as dez fixtures didáticas têm gold projetivo e sobrevivem em ambos os
  decodificadores. Elas validam concordância e comparação, não a frequência de
  não projetividade em latim real;
- os quatro comparativos ficam em rank 1. As duas fixtures com `intelligentior`
  preservam a forma de superfície e tornam explícito o lookup
  `intellegentior`; nas duas com `quam`, o gold aceita advérbio ou conjunção;
- no fechamento desta execução, o self-test passou nas 17 fixtures com as
  WWDBs full e search-only; corpus e 238 registros das 14 estratégias passaram
  nos schemas v2; a suíte geral passou em 94/94 testes; ASan/UBSan também
  passou, com LeakSanitizer desativado devido à limitação sob `ptrace`.

## Perguntas de pesquisa

| ID | Pergunta | Evidência necessária |
|---|---|---|
| RQ1 | Qual é a explosão morfológica real? | Distribuição de candidatos por token e do produto por sentença. |
| RQ2 | Quanto pode ser removido antes do produto cartesiano? | Domínios antes/depois de cada restrição e razão de sobrevivência. |
| RQ3 | Onde o trabalho sintático se repete? | Estados equivalentes, memo hits e razão entre árvores explícitas e nós compartilhados. |
| RQ4 | Dependências bastam para prosa e poesia? | Cobertura de relações descontínuas e cruzadas no corpus estratificado. |
| RQ5 | Uma CFG permanece legível e compacta? | Número de regras fonte e expandidas, conflitos e duplicação por permutações. |
| RQ6 | Quando GLR/chart traz ganho material? | Tempo, memória e packing ratio contra o mesmo conjunto de regras. |
| RQ7 | Quais ambiguidades sobrevivem sem meanings? | N-best e classes de empate após morfossintaxe e metadata lexical. |
| RQ8 | A falta de valência domina os erros? | Falhas classificadas por ausência de frame, não misturadas com falhas do algoritmo. |
| RQ9 | Hyperbaton exige algo além de CFG/dependências? | Suíte curada de constituintes intercalados e crescimento da gramática. |
| RQ10 | O sistema separa corretamente impossibilidade de mera baixa plausibilidade? | Recall dos sobreviventes, rejeições gold e explicações por constraint. |
| RQ11 | Scores formais podem ser convertidos em probabilidades calibradas? | N-best completo, corpus held-out, curvas de calibração, Brier score e log loss. |

## Invariantes da investigação

1. **Meaning não participa das fases formais.** O perfil search-only deve ser
   suficiente até uma eventual fase semântica.
2. **Ambiguidade genuína é resultado, não erro.** O gold pode aceitar um
   conjunto de análises equivalentes.
3. **Impossibilidade elimina; preferência pontua.** Toda rejeição e todo delta
   de score devem ter uma justificativa estável. Score não é probabilidade sem
   calibração externa.
4. **Posição é preservada; ordem não é lei geral.** Distância e ordem entram
   inicialmente como features brandas, exceto em construções com restrição
   linear explícita.
5. **Comparações usam a mesma gramática.** Não comparar um algoritmo com
   regras mais ricas contra outro com regras mais pobres.
6. **Instrumentar antes de otimizar.** Nenhuma poda aproximada antes de existir
   um baseline exaustivo em fixtures pequenas.
7. **Packing respeita features.** Estados com ambientes de unificação
   diferentes não podem ser fundidos apenas porque cobrem os mesmos tokens.
8. **Cobertura e desempenho são métricas separadas.** Um resultado rápido com
   gramática insuficiente não vence um resultado lento por definição.
9. **O experimento deve ser reproduzível.** Resultado registra commit,
   `datasetId`, compilador, configuração, corpus e seed quando aplicável.

## Decomposição do espaço de design

As alternativas levantadas pertencem a quatro eixos, e não formam vinte
parsers mutuamente exclusivos:

| Eixo | Alternativas iniciais |
|---|---|
| Entrada | cartesiano materializado; lattice morfológico |
| Representação | dependências; constituintes CFG; constituintes descontínuos |
| Busca | DFS; propagação; chart/Earley; GLR; busca com memoização |
| Decisão | hard constraints; pesos formais; ranker estatístico; modelo semântico |

O primeiro ciclo compara algoritmos de busca sobre uma representação mínima e
o mesmo conjunto de constraints. A comparação entre formalismos vem depois.

## Suítes de avaliação

### S0 — Microfixtures isoladas

Frases pequenas, criadas para exercer uma regra de cada vez:

- concordância sujeito–verbo;
- sujeito nulo;
- nome–adjetivo;
- regência preposicional;
- verbo transitivo/intransitivo conhecido;
- coordenação com `-que`;
- infinitivo e particípio;
- duas orações;
- ambiguidade que deve sobreviver.

Cada fixture declara:

- análises morfológicas permitidas por token;
- relações ou árvores permitidas;
- rejeições obrigatórias;
- ambiguidades que não podem ser resolvidas formalmente.

`Arma virumque cano` pertence a esta suíte, mas não deve ser o único caso de
coordenação.

### S1 — Prosa curta

Sentenças anotadas de aproximadamente 5–12 tokens, inicialmente sem
hyperbaton difícil. Mede cobertura da gramática básica, explosão típica e
efeito da valência.

### S2 — Poesia e descontinuidade

Casos curados de:

- hipérbato nome–adjetivo;
- genitivo descontínuo;
- dependências cruzadas;
- coordenação de constituintes descontínuos;
- orações não finitas intercaladas;
- cláusulas finitas que devem funcionar como regiões normalmente fechadas.

Esta suíte decide se a representação escolhida, e não apenas o algoritmo, é
insuficiente.

### S3 — Casos adversariais

Sentenças sintéticas formadas por tokens com muitos candidatos. Não medem
qualidade linguística; medem escalabilidade e capacidade de compartilhar
trabalho.

### S4 — Ambiguidade residual

Casos em que morfologia e sintaxe não produzem uma resposta única. Mede se o
parser preserva corretamente N-best e se metadata não semântica melhora o
ranking sem inventar certeza.

## Formato comum de medição

Todas as estratégias devem emitir NDJSON no mesmo schema experimental:

```json
{
  "schema": "words-parser-investigation",
  "schemaVersion": 2,
  "text": "Petrus est bonus.",
  "strategy": "worklist-prefilter",
  "datasetId": "sha256:...",
  "grammarMode": "complete-clause",
  "morphology": {
    "surfaceTokens": ["Petrus", "est", "bonus"],
    "lookupTokens": ["Petrus", "est", "bonus"],
    "lookupOverrides": [],
    "candidateCounts": [1, 2, 2],
    "rawProduct": "4"
  },
  "propagation": {
    "supportChecks": 6,
    "domainsAfter": [1, 2, 2],
    "prunedProduct": "4"
  },
  "enumeration": {
    "partialStates": 11,
    "completeAssignments": 4
  },
  "relationCandidates": {
    "generated": 12,
    "byKind": {"preposition-complement": 6, "verb-argument": 6},
    "byCompatibility": {"compatible": 1, "incompatible": 5, "indeterminate": 6},
    "selected": 1
  },
  "attachmentSearch": {
    "performed": true,
    "slotsCreated": 1,
    "partialStates": 2,
    "completeAnalyses": 2,
    "conflicts": 0,
    "analysisIds": ["0:0,1:0,2:0|r:-", "0:0,1:0,2:0|r:4"],
    "analysisSetDigest": {"algorithm": "fnv1a-64", "value": "..."},
    "projectionChecked": 1,
    "projectionInSearch": 1
  },
  "treeSearch": {
    "performed": true,
    "arcCandidatesGenerated": 8,
    "partialStates": 14,
    "completeTrees": 3,
    "projectiveTrees": 2,
    "nonprojectiveTrees": 1,
    "cycleRejections": 1,
    "rootRejections": 0,
    "analysisIds": ["0:0,1:0,2:0|t:0>2:nsubj,1>0:amod,2>r:root"],
    "analysisSetDigest": {"algorithm": "fnv1a-64", "value": "..."},
    "projectionChecked": 1,
    "projectionInSearch": 1,
    "bestArcScore": 25.9,
    "bestProjectiveScores": {"0:0,1:0,2:0": 25.9},
    "bestUnrestrictedScores": {"0:0,1:0,2:0": 25.9}
  },
  "decoder": {
    "performed": true,
    "algorithm": "eisner-projective",
    "arcCandidates": 8,
    "states": 17,
    "cyclesContracted": 0,
    "completeTrees": 1,
    "projectiveTrees": 1,
    "nonprojectiveTrees": 0,
    "analysisIds": ["0:0,1:0,2:0|t:0>2:nsubj,1>0:amod,2>r:root"],
    "analysisSetDigest": {"algorithm": "fnv1a-64", "value": "..."},
    "scoresByAssignment": {"0:0,1:0,2:0": 25.9}
  },
  "acceptance": {
    "morphAssignments": 4,
    "assignmentIds": ["0:0,1:0,2:0"]
  },
  "forest": {
    "available": false,
    "derivationCount": null
  }
}
```

`rawProduct` é string para não impor um limite inteiro. Relatórios agregados
devem apresentar no mínimo `p50`, `p95`, máximo e distribuição por tamanho da
sentença. Relações, estados de busca, itens Earley e pilhas explícitas nunca
são somados na mesma coluna.

## Catálogo inicial de constraints

### Duras

| ID | Regra inicial |
|---|---|
| H001 | Todo token lexical precisa ser consumido exatamente uma vez. |
| H002 | Toda oração completa possui um predicado finito, salvo fixture explicitamente fragmentária. |
| H003 | Sujeito explícito concorda com pessoa e número do verbo; sujeito nulo é permitido. |
| H004 | Adjetivo atributivo concorda em caso, número e gênero com o núcleo. |
| H005 | Preposição com regência conhecida aceita o caso exigido. |
| H006 | Se uma aresta candidata liga argumento a verbo com regência conhecida, seu caso deve ser compatível. A ausência global do complemento não é rejeição dura sem frame de obrigatoriedade. |
| H007 | `-que` participa de uma coordenação e inicia o segundo membro segundo o escopo candidato. |
| H008 | NPs coordenados sob a mesma função externa unificam caso; número e gênero não são unificados automaticamente. |
| H009 | Features ausentes e features `unknown` não são tratadas como fatos positivos. |
| H010 | Um estado compartilhado conserva o ambiente completo de features e exigências ainda abertas. |
| H011 | Se uma aresta candidata liga um comparativo ao segundo termo, este fica no ablativo sem `quam`; com um marcador `quam` selecionado, conserva o caso do primeiro termo. |

### Brandas

| ID | Feature inicial |
|---|---|
| S001 | Frequência lexical. |
| S002 | Frequência da regra flexional. |
| S003 | Compatibilidade de período com o corpus. |
| S004 | Distância entre head e dependent. |
| S005 | Número de orações. |
| S006 | Número e tipo de elipses. |
| S007 | Escopo da coordenação. |
| S008 | Regência conhecida versus inferida. |
| S009 | Complexidade estrutural. |
| S010 | Construção marcada, como vocativo ou tópico pendente. |
| S011 | Modificador possui núcleo nominal concordante ou leitura substantivada. |
| S012 | Cobertura por predicado finito e sujeito explícito compatível. |
| S013 | Cópula reconhecida pelo tipo lexical do verbo. |
| S015 | Segundo termo da comparação licenciado por H011. |

Os pesos iniciais são experimentais. Antes de introduzir beam search, o
executor deve conseguir enumerar todos os sobreviventes das microfixtures.

## Marcos

### M0 — Protocolo e fixtures

**Entregáveis**

- schema NDJSON versionado;
- representação de fixture com gold possivelmente ambíguo;
- IDs estáveis para constraints e features de score;
- relatório de ambiente reproduzível;
- primeiras microfixtures, incluindo `Arma virumque cano`.

**Critério de saída**

- a mesma execução produz resultados byte a byte estáveis, exceto campos de
  tempo e memória;
- o gold distingue “análise rejeitada”, “análise permitida” e “análise
  preferida”.

### M1 — Censo morfológico

Adicionar entrada arbitrária e modo corpus ao executável, sem sintaxe.

**Medir**

- candidatos por token;
- produto bruto por sentença;
- distribuição de POS e morfologias;
- incidência de `unknown`;
- incidência e tipo de addons;
- frequência de verbos finitos candidatos por sentença.

**Critério de saída**

- relatório para S0–S4;
- lista ordenada das sentenças que mais explodem;
- nenhuma combinação sintática materializada ainda.

### M2 — Baseline cartesiano exaustivo

Materializar todas as atribuições morfológicas apenas onde o produto estiver
dentro de um limite de segurança configurável.

**Objetivos**

- obter autoridade de contagem para comparar os algoritmos posteriores;
- medir custo por candidato completo;
- registrar precisamente qual constraint rejeitou cada atribuição.

**Critério de saída**

- baseline exato para todas as microfixtures;
- falha explícita `experiment-budget-exceeded`, nunca OOM acidental;
- contagens usadas como oracle pelos modos seguintes.

### M3 — Propagação sobre lattice

Representar o domínio de cada token como bitset e aplicar uma worklist de
constraints até ponto fixo. Restrições binárias podem começar com propagação
no estilo AC-3; coordenação, valência e oração exigirão constraints de maior
aridade.

**Medir**

- remoções por constraint;
- número de iterações da worklist;
- checks de suporte;
- produto antes/depois;
- inconsistências detectadas sem enumeração.

**Critério de saída**

- os sobreviventes são idênticos aos do baseline em S0;
- cada remoção possui constraint e evidência;
- o relatório mostra quais regras realmente podam e quais apenas consomem
  tempo.

### M4 — Throw-away de dependências e pontuação

Gerar relações candidatas a partir de anchors:

```text
verbo finito
preposição
conjunção/enclítico
subordinador
infinitivo/particípio
```

Construir grafos candidatos sem tornar distância uma restrição dura.

**Entregáveis**

- `TokenNode`, `RelationCandidate`, `ParseState` e `ScoreReason`;
- N-best com decomposição completa do score;
- rejeições separadas de preferências;
- suporte a mais de uma oração e sujeito nulo.

**Critério de saída**

- cobertura das relações de S0;
- gold presente no N-best de S1;
- ambiguidades formais declaradas no gold continuam presentes;
- nenhum acesso ao pool de meanings.

### M5 — Compartilhamento e memoização

Adicionar agenda, chave canônica de estado e compartilhamento de estruturas
parciais. A chave deve incluir domínios ativos, relações fixadas, anchors,
fronteiras de oração, exigências abertas e ambiente de features.

**Medir**

- estados criados e reutilizados;
- memo hit rate;
- árvores explícitas versus nós compartilhados;
- tempo e memória contra M2/M3/M4.

**Critério de saída**

- equivalência extensional com o throw-away não memoizado em S0;
- relatório de packing por sentença;
- identificação dos tipos de subestrutura que mais se repetem.

### Gate D1 — Vale prototipar GLR/chart?

Produzir uma decisão registrada com os seguintes dados:

- benefício real de compartilhamento;
- proporção da gramática naturalmente expressável como constituintes
  contíguos;
- custo de representar ordem livre por permutações CFG;
- número e forma dos estados que precisam carregar features;
- falhas de cobertura, separadas de falhas de busca.

**Interpretação**

- pouco reuso e boa cobertura favorecem manter dependency/constraints;
- reuso grande e gramática compacta favorecem chart/Earley ou GLR;
- explosão das regras antes mesmo do parsing aponta para problema de
  formalismo, não para falta de GSS/SPPF.

### M6 — Protótipos comparáveis de formalismo

Somente após D1, implementar o menor subconjunto possível em duas famílias:

1. dependency/weighted constraints;
2. CFG com feature unification usando chart/Earley ou GLR.

Ambos devem usar:

- a mesma suíte S0/S1;
- o mesmo lattice morfológico;
- o mesmo catálogo de constraints;
- o mesmo schema de resultado;
- a mesma política de score.

Para CFG/GLR, medir adicionalmente:

- regras fonte e regras após expansão;
- conflitos LR, quando aplicável;
- nós GSS;
- símbolos e alternativas packed no forest;
- quantas separações de estado foram causadas somente por features.

### Gate D2 — Escolha da arquitetura de produção

A decisão deve privilegiar a implementação mais simples que satisfaça os
orçamentos de cobertura, tempo e memória ainda a serem definidos com os dados
de M1. Não escolher por familiaridade ou estética.

Possíveis resultados:

- dependency/constraints como arquitetura final;
- chart/Earley como arquitetura final;
- GLR por vantagem comprovada de tabelas LR + packing;
- arquitetura híbrida, por exemplo dependências para relações e chart para
  construções locais;
- nenhuma das anteriores cobre S2 sem inflação inaceitável.

### M7 — Hyperbaton e formalismos descontínuos

Executar S2 contra a arquitetura escolhida. Classificar cada falha como:

- regra ainda ausente;
- attachment incorreto;
- busca insuficiente;
- representação incapaz;
- ranking incorreto.

Somente falhas demonstradas de representação autorizam um protótipo
IDL-PMCFG/COMPĀ ou outro formalismo mildly/context-sensitive. O custo é
relevante: parsing IDL-PMCFG é NP-hard, portanto ele não deve ser adotado
apenas porque expressa hyperbaton elegantemente.

### M8 — Valência enriquecida

Medir primeiro quantas falhas são explicadas pela limitação de `VerbKind`.
Depois avaliar uma camada lexical separada da WWDB, potencialmente baseada em
Latin Vallex ou recurso equivalente.

**Requisitos**

- frames versionados e ligados a identidade lexical verificável;
- distinção entre argumento obrigatório, opcional e adjunto;
- múltiplos frames por lexema;
- frequências por frame quando disponíveis;
- ausência de inferência a partir da glossa inglesa.

### M9 — Ranking estatístico ou semântico opcional

Somente depois de estabilizar a cobertura formal:

- medir quantos empates genuínos restam;
- expor o N-best completo, com score bruto e decomposição, para todas as
  análises possíveis dentro do orçamento exato;
- criar baseline de pesos manuais;
- calibrar pesos em corpus sem alterar hard constraints, medindo Brier score,
  log loss e calibração em held-out;
- comparar ranker estatístico pequeno;
- considerar modelo semântico apenas para N-best formalmente válido.

O modelo semântico nunca deve fabricar morfologia ou apagar provenance.

## Critérios de decisão quantitativos

Os limiares finais serão definidos após M1, mas todo gate deve considerar:

| Dimensão | Métrica |
|---|---|
| Correção | gold recall; exact set match; gold rank; violações duras |
| Cobertura | sentenças com ao menos um parse; falhas por regra/valência/representação |
| Ambiguidade | parses completos; classes equivalentes; N-best necessário |
| Poda | produto bruto; produto podado; remoções por constraint |
| Compartilhamento | memo hits; packing ratio; nós reutilizados |
| Gramática | regras fonte/expandidas; permutações; conflitos |
| Desempenho | tempo p50/p95/max; pico de memória; estados/checks |
| Explicabilidade | porcentagem de decisões com provenance completa |

Um limiar provisório pode sinalizar investigação, mas não decidir sozinho. Por
exemplo, packing acima de `10x` justifica estudar chart/GLR; não prova que GLR
é superior sem comparar complexidade de gramática, features e memória.

## Riscos conhecidos

### Confundir ausência lexical com falha do parser

`VerbKind::unknown` e frames ausentes devem produzir diagnóstico de valência
insuficiente. Não autorizar todos os casos silenciosamente e depois atribuir a
explosão ao algoritmo.

### Packing estruturalmente incorreto

Duas estruturas que cobrem o mesmo span podem carregar casos, frames ou
exigências diferentes. Compartilhar o nó sem preservar essas diferenças pode
eliminar análises válidas ou aceitar inválidas.

### Gramática e algoritmo mudarem juntos

Toda comparação deve executar um conjunto comum de regras. Extensões
específicas precisam aparecer como experimento separado.

### Beam esconder o fenômeno

Busca aproximada pode parecer rápida por perder o gold. Beam somente entra
após existir contagem exata em fixtures pequenas e métrica de gold recall.

### Tratar poesia como exceção tardia demais

S2 entra desde o início como corpus de observação, embora formalismos pesados
só sejam implementados depois. Assim a arquitetura não otimiza exclusivamente
para prosa e descobre tarde demais que escolheu uma representação inadequada.

## Próxima fila de implementação

1. Expor no schema o N-best completo dos sobreviventes, distinguindo
   `possible` + score bruto de qualquer futura probabilidade calibrada.
2. Anotar os dois exemplos de comparação de inferioridade (`minus
   intelligens`) e decidir se exigem uma relação própria ou composição de H011.
3. Acrescentar casos reais ou publicados de hipérbato e ordem livre para medir
   quando o ótimo não projetivo é necessário fora da fixture sintética.
4. Criar grafos adversariais com ciclos aninhados para testar contração e
   expansão de Chu–Liu/Edmonds além do único ciclo observado em S0.
5. Criar microfixtures de poda em cascata que invalidem resíduos e medir custo
   amortizado e memória real da agenda.
6. Anotar, em lotes revisáveis, frases do catálogo didático, mantendo texto e
   proveniência sem correções silenciosas.
7. Reescrever Earley com agenda; somente depois acrescentar SPPF.
8. Substituir o stack-set por RNGLR com GSS/SPPF.

## Leituras orientadas às decisões

1. [Van den Berg 2024 — *Analysing the structure of Latin sentences using formal grammar*](https://www.cs.ru.nl/bachelors-theses/2024/Thijs_van_den_Berg___1073084___Analysing_the_structure_of_Latin_sentences_using_formal_grammar.pdf)
   — baseline de combinações morfológicas e EAFWOBNF.
2. [Hooghof 2025 — *Revisiting the analysis of Latin sentence structure using mathematics*](https://www.cs.ru.nl/bachelors-theses/2025/Siebe_Hooghof___1105148___Revisiting_the_analysis_of_Latin_sentence_structure_using_mathematics.pdf)
   — medições de repetição, limitações do LLP e Arborator.
3. [Koch — *The Enhancement of a Dependency Parser for Latin*](https://openscholar.uga.edu/record/24881/files/ai199303.pdf)
   — dependency parsing, ordem variável e unificação.
4. [Steimann e Brzoska 1995 — *Dependency Unification Grammar for Prolog*](https://aclanthology.org/J95-1005/)
   — dependências com estruturas de features.
5. [Tomita 1990 — *The Generalized LR Parser/Compiler V8-4*](https://aclanthology.org/C90-1012/)
   — referência primária de GLR aplicado a NLP.
6. [Publicações de Alon Lavie](https://www.cs.cmu.edu/~alavie/publications.html)
   — registra GLR*, a tese e a apresentação sobre GLR para word lattices; esta
   última aparece como apresentação sem proceedings e deve ser citada com essa
   qualificação.
7. [Hublet 2022 — *IDL-PMCFG, a Grammar Formalism for Describing Free Word Order Languages*](https://link.springer.com/article/10.1007/s10849-022-09363-0)
   — interleaving, locking, hyperbaton e custo formal.
8. [Passarotti et al. 2016 — *Latin Vallex*](https://aclanthology.org/L16-1414/)
   — frames de valência ligados a treebanks e frequências observadas.

## Decisões ainda não tomadas

- dependency versus constituency;
- Earley/chart versus GLR;
- representação final de estruturas descontínuas;
- pesos e método de calibração;
- recurso externo de valência;
- uso de modelo estatístico ou semântico;
- orçamento de tempo e memória para native e WebAssembly.

Essas decisões devem permanecer abertas até que os gates correspondentes
tenham dados suficientes.
