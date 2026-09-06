# Investigação Markoviana sobre as saídas do parser

A matriz que distingue dados executados, evidência textual, inspiração e
material ainda não usado está em [`PROVENANCE.md`](PROVENANCE.md). Ela também
registra licenças, hashes locais e transformações.

## Pergunta experimental

Uma cadeia de Markov treinada com frases latinas conhecidas consegue ordenar
as análises morfológicas que sobreviveram às hard constraints?

O experimento preserva a semântica central do diretório:

```text
lattice morfológico
        ↓
hard constraints (decidem impossível versus possível)
        ↓
N-best completo das atribuições possíveis
        ↓
cadeia de Markov (somente ordena)
```

Uma transição não observada recebe massa por suavização aditiva. Portanto,
ausência no corpus de treino nunca elimina uma análise aceita pelo parser.

## Implementação C++23

- [`markov_model.hpp`](markov_model.hpp) contém o modelo genérico de contagens,
  pesos fracionários e suavização;
- [`markov_ranker.cpp`](markov_ranker.cpp) executa o parser
  `dependency-projection`, recolhe todas as atribuições possíveis e faz a
  avaliação;
- [`markov_model_test.cpp`](markov_model_test.cpp) testa frequência,
  suavização, pesos e pré-condições;
- `parsers_investigation --include-nbest` publica opcionalmente o mesmo domínio,
  incluindo a árvore determinística de cada candidato, em `morphologyNBest` no
  NDJSON v2.

O código usa a configuração C++23 do projeto e seus tipos existentes. A demo
isolada em [`../markov_demo/`](../markov_demo/) continua útil para visualizar
geração de texto sem suavização; o ranker integrado é outro experimento: não
gera frases e consome somente candidatos produzidos pelo parser.

## Estados comparados

Foram testadas duas projeções, com memória de uma e duas posições:

| Projeção | Exemplo | Hipótese |
|---|---|---|
| `part` | `noun verb adjective` | generaliza mais, mas colapsa muitas análises; |
| `part+morphology` | `noun:nominative-singular-masculine …` | distingue flexões, mas exige mais dados. |

O lema não entra no estado deste primeiro baseline. Isso evita que o pequeno
corpus apenas memorize vocabulário e permite compartilhar contagens entre
lexemas.

Cada projeção é avaliada com três linearizações:

| Linearização | Peso superficial | Peso canônico | Definição |
|---|---:|---:|---|
| `surface` | 1 | 0 | ordem original dos tokens; |
| `hybrid` | 0,5 | 0,5 | média dos dois log-scores; |
| `parser-canonical` | 0 | 1 | percurso estrutural determinístico. |

O percurso canônico começa na raiz e visita os dependentes em profundidade.
Irmãos são ordenados por relação, estado projetado, lema e, apenas como último
desempate, posição do token. O estado estrutural inclui a relação de entrada:

```text
root|verb
nsubj|noun
predicative|adjective
```

Quando duas ordens superficiais preservam a mesma árvore e os mesmos estados,
essa representação produz a mesma sequência canônica. O teste contém versões
SVO e VOS da mesma árvore para congelar essa propriedade.

Para peso superficial $\beta$, a combinação é:

$$
S_\beta
=
\beta S_{\mathrm{superfície}}
+(1-\beta)S_{\mathrm{canônico}}.
$$

## Treino e avaliação

Por padrão, a avaliação usa as dez fixtures `verified-didactic`: seis frases
de concordância e quatro de comparação. Os recortes atestado, treebank e
completo também podem ser selecionados pela linha de comando. Em
*leave-one-fixture-out*, cada alvo é retirado do próprio treino.

O treino ampliado possui cinco camadas:

| Camada | Frases por rodada | Peso por frase | Seleção |
|---|---:|---:|---|
| gold didático verificado | 9 | 1,00 | todas as realizações do gold; |
| gold morfológico atestado | 5 | 1,00 | texto atestado na cópia local da The Latin Library e morfologia revisada; |
| Latin Dependency Treebank 2.1 | 5 | 1,00 | morfologia anotada no LDT e mapeada ao vocabulário do WordsWASM; |
| gold sintético controlado | 7 | 0,50 | todas as realizações do gold; |
| locuções conhecidas silver | 13 | 0,25 | melhor empate manual compatível com os lemas preferidos. |

No alvo didático em *leave-one-out*, cada rodada ampliada usa 39 frases. As
cinco frases atestadas vêm de
[`corpus/attested_gold_fixtures.json`](corpus/attested_gold_fixtures.json):
Virgílio, Horácio, Juvenal e o título de César transmitido por Suetônio. A
The Latin Library estabelece a ocorrência textual; a morfologia é gold
editorial revisado. Dependências não são declaradas como gold nesse conjunto:
a ordem estrutural é deliberadamente a saída determinística do parser.

Cinco frases curtas de Cícero vêm diretamente do Latin Dependency Treebank 2.1:
`Nox nulla intercessit`, `Patent portae`, `Purga urbem`, `Abiit excessit
evasit erupit` e `Demonstrabo iter`. O corpus preserva o ID de sentença e o
`subdoc`; a pontuação é removida, os sufixos de sentido dos lemas LDT são
mapeados aos lemas WWDB e as etiquetas finas são convertidas para o esquema do
parser. A fonte declara também dependências, mas elas ainda não entram como
gold do WordsWASM porque os inventários de relações e a tokenização diferem.

As locuções silver vêm de
[`corpus/common_phrases.tsv`](corpus/common_phrases.tsv); elas passam por lexer,
análise morfológica e parser, mas não são apresentadas como gold independente.
Em *leave-one-out*, uma locução silver com o mesmo texto do alvo também é
retirada, mesmo que possua outro ID.

Quando uma frase admite mais de uma sequência selecionada distinta, conserva
seu peso total, dividido igualmente entre as sequências. O rank primário é o
log-score Markoviano. Empates são resolvidos pelo score manual já existente e,
por fim, pelo ID canônico da atribuição. O relatório distingue esse rank
ordinal de uma escolha exclusivamente determinada por Markov.

O modelo usa suavização aditiva com $\alpha=0{,}1$:

$$
P(x\mid c)=
\frac{\operatorname{count}(c,x)+\alpha}
     {\operatorname{count}(c)+\alpha|V|}.
$$

### Aumento por relinearização controlada

Para cada análise gold estruturada, o treino também pode gerar duas analogias
de ordem: percurso do núcleo antes dos dependentes (*head-first*) e depois dos
dependentes (*head-last*). Duplicatas e a ordem superficial original são
removidas. O peso padrão total dessas variantes é 0,10 do peso da frase e é
dividido igualmente entre elas.

Essas variantes treinam somente o modelo superficial. O modelo canônico já
representa a mesma árvore independentemente da ordem dos tokens; treiná-lo de
novo apenas duplicaria evidência. A opção `--reordering-weight 0` desliga esse
aumento.

## Baseline somente com gold verificado

| Estados | Memória | Linearização | Gold top-1 | Gold top-3 | Top-1 exclusivamente Markov | MRR |
|---|---:|---|---:|---:|---:|---:|
| POS | 1 | superficial | 10/10 | 10/10 | 0/10 | 1,000 |
| POS | 1 | híbrida | 10/10 | 10/10 | 1/10 | 1,000 |
| POS | 1 | canônica | 10/10 | 10/10 | 1/10 | 1,000 |
| POS | 2 | superficial | 10/10 | 10/10 | 0/10 | 1,000 |
| POS | 2 | híbrida | 10/10 | 10/10 | 1/10 | 1,000 |
| POS | 2 | canônica | 10/10 | 10/10 | 1/10 | 1,000 |
| POS + morfologia | 1 | superficial | 6/10 | 8/10 | 0/10 | 0,690 |
| POS + morfologia | 1 | híbrida | 6/10 | 8/10 | 4/10 | 0,705 |
| POS + morfologia | 1 | canônica | 7/10 | 8/10 | 4/10 | 0,757 |
| POS + morfologia | 2 | superficial | 8/10 | 9/10 | 0/10 | 0,870 |
| POS + morfologia | 2 | híbrida | 8/10 | 9/10 | 4/10 | 0,870 |
| POS + morfologia | 2 | canônica | 9/10 | 10/10 | 4/10 | 0,950 |

O baseline manual tem gold top-1 em 9/10; `Alumnae sunt altae` mantém o gold
adjetival no rank 2, empatado no melhor score com a leitura participial.

POS puro continua empatando demais: a ordem estrutural resolve exclusivamente
apenas 1/10. A representação morfológica completa discrimina mais. Sua melhor
configuração é memória 2 com ordem canônica: gold top-1 em 9/10 e top-3 em
10/10, contra 8/10 e 9/10 na superfície; MRR sobe de 0,870 para 0,950. Mesmo
assim, somente 4/10 escolhas são exclusivamente determinadas pelo melhor score
Markoviano. O resultado é um sinal favorável à vizinhança estrutural, ainda não
uma validação de generalização.

## Efeito da ampliação

A ablação abaixo fixa a melhor arquitetura anterior — POS + morfologia,
memória 2 e ordem canônica — e varia apenas as camadas de treino:

| Treino | Frases | Gold top-1 | Gold top-3 | Top-1 exclusivamente Markov | MRR |
|---|---:|---:|---:|---:|---:|
| somente gold verificado | 9 | **9/10** | 10/10 | 4/10 | **0,950** |
| verificado + sintético | 16 | 8/10 | 10/10 | 4/10 | 0,883 |
| verificado + silver | 22 | 8/10 | 10/10 | 4/10 | 0,900 |
| três camadas | 29 | 7/10 | 10/10 | 4/10 | 0,833 |

A infraestrutura de treino foi ampliada, mas os dados auxiliares não melhoram
esta avaliação. O resultado sugere *negative transfer*: as fixtures sintéticas
têm outra distribuição e os lemas silver não determinam toda a morfologia nem
garantem dependências corretas. A ampliação aumenta cobertura de transições,
mas não deve ser habilitada como modelo preferido sem um conjunto de
desenvolvimento e mais gold real.

No perfil ampliado atual, com as cinco camadas e 39 frases por rodada, a
comparação entre as três linearizações da configuração POS +
morfologia/memória 2 é:

| Linearização | Gold top-1 | Gold top-3 | MRR |
|---|---:|---:|---:|
| superficial | 7/10 | 8/10 | 0,757 |
| híbrida | 7/10 | 8/10 | 0,763 |
| canônica | 7/10 | **10/10** | **0,833** |

O corpus também contém pares quase paralelos. O leave-one-out evita memorizar a
mesma fixture, mas não equivale a uma partição independente por autor, bloco ou
tipo de construção. Estes resultados validam o encadeamento do experimento,
não a generalização linguística do ranker.

## Gold real: fora e dentro do treino

Há dois recortes separados. `attested` contém cinco frases verificadas na cópia
local da The Latin Library com morfologia editorial. `treebank` contém cinco
sentenças de Cícero cuja morfologia vem do Latin Dependency Treebank 2.1. Todos
os dez golds sobrevivem às hard constraints do `dependency-projection`.

Fixando POS + morfologia, memória 2 e percurso canônico:

| Recorte | Política | Manual top-1 | Markov top-1 | Markov top-3 | Top-1 exclusivamente Markov | MRR |
|---|---|---:|---:|---:|---:|---:|
| TLL + editorial | alvo fora | 3/5 | 4/5 | 4/5 | 3/5 | 0,812 |
| TLL + editorial | alvo dentro | 3/5 | **5/5** | **5/5** | **5/5** | **1,000** |
| LDT 2.1 | alvo fora | 4/5 | 3/5 | 4/5 | 3/5 | 0,683 |
| LDT 2.1 | alvo dentro | 4/5 | **5/5** | **5/5** | **5/5** | **1,000** |

No recorte LDT em *leave-one-out*, a ordem superficial é melhor que a canônica:
top-1 4/5, top-3 5/5 e MRR 0,867. Portanto, estas frases não sustentam a tese de
que a ordenação do parser sempre melhora a inferência; ela é uma configuração a
ser comparada, não uma premissa.

Dois casos tornam o teste *in-sample* informativo. Em `Veni vidi vici`, o manual
coloca a leitura perfeita gold no rank 2, empatada com o imperativo de `veni`;
Markov a põe no rank 17 sem o exemplo e no rank 1 com ele. Em `Demonstrabo
iter`, o acusativo gold começa no rank 3 e sobe ao rank 1 quando entra no treino.
Isso demonstra capacidade de assimilação/memorização, não generalização.

### Controles contra efeito placebo

O relatório v2 calcula três rankings sobre os mesmos candidatos:

1. `manualOnly`: apenas o score determinístico anterior;
2. `markovOnly`: log-score Markoviano, com `assignment-id` somente para tornar
   empates reproduzíveis;
3. `markovThenManual`: Markov e, depois, score manual e `assignment-id`.

Na configuração POS+morfologia, memória 2 e ordem canônica, sem exposição do
alvo, a ablação é:

| Recorte | Ranking | Top-1 | Top-3 | MRR |
|---|---|---:|---:|---:|
| TLL | manual apenas | 3/5 | 5/5 | 0,767 |
| TLL | Markov puro | 3/5 | 3/5 | 0,633 |
| TLL | Markov + manual | **4/5** | 4/5 | **0,812** |
| LDT | manual apenas | **4/5** | **5/5** | **0,867** |
| LDT | Markov puro | 3/5 | 4/5 | 0,683 |
| LDT | Markov + manual | 3/5 | 4/5 | 0,683 |

Portanto, o ganho held-out do TLL não é inteiramente atribuível ao Markov
puro; existe interação útil com o desempate manual. No LDT canônico, o baseline
manual ainda é superior. `markovStrictTop1` continua sendo a medida que não dá
crédito ao desempate.

O modo `exposure-curve` remove todos os duplicados textuais do alvo e reinsere
somente a fixture-alvo com multiplicadores controlados. A resposta do Markov
puro foi:

| Exposição do alvo | TLL top-1 / MRR | LDT top-1 / MRR |
|---:|---:|---:|
| 0 | 3/5 / 0,633 | 3/5 / 0,683 |
| 0,01 | 4/5 / 0,812 | **5/5 / 1,000** |
| 0,10 | 4/5 / 0,812 | 5/5 / 1,000 |
| 0,25 | **5/5 / 1,000** | 5/5 / 1,000 |
| 1,00--8,00 | 5/5 / 1,000 | 5/5 / 1,000 |

Esta curva é um controle positivo: doses muito pequenas da análise correta já
alteram o ranking Markoviano sem auxílio do score manual.

Há dois controles negativos. `shuffle-within-sequence` preserva o multiconjunto
de estados de cada exemplo, mas destrói suas transições com semente registrada.
Em vinte sementes, o TLL caiu, em média, de 3 para 1,25 top-1 no Markov puro e
de MRR 0,633 para 0,318. No LDT, porém, o controle embaralhado ficou
artificialmente melhor em top-1 médio (3,9/5) e MRR (0,848), embora o top-1
estrito caísse de 3 para 2,35. O recorte é pequeno demais para usar esse
controle aleatório isoladamente como prova.

O controle `counterfactual-analysis` é mais direto: para cada fixture de
treino, seleciona o melhor candidato não-gold cuja sequência projetada difere
das sequências gold. Com exposição 0,25, ensinar a análise correta leva ambos
os recortes a 5/5 e MRR 1,0; ensinar a alternativa errada derruba o TLL para
1/5 e MRR 0,346 e mantém o LDT em 3/5 e MRR 0,657. Com peso 1,0, o
contrafactual chega a 1/5 no TLL e 2/5 no LDT. O sentido oposto das curvas
positiva e contrafactual confirma que o conteúdo do treino causa a mudança.

A relinearização sintética de peso 0,10 não altera os ranks top-1/top-3/MRR nas
configurações POS+morfologia/memória 2 desses dois recortes. No LDT, ela apenas
transforma um empate superficial em top-1 exclusivamente Markov (3/5 para 4/5).
O mecanismo fica disponível, mas não há ganho robusto para promovê-lo a padrão.

O `Schmid-Laws.pdf` e o RFTagger local sugerem um próximo baseline: decompor a
etiqueta fina em atributos e estimar quais atributos contextuais são úteis, em
vez de tratar toda a string morfológica como um estado atômico. O Latin
Macronizer é GPLv3 e é usado aqui apenas como referência e potencial anotador
silver externo; nenhum código dele foi incorporado.

## Reprodução

```sh
cmake -S . -B build/parsers -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DENABLE_TESTS=ON \
  -DPARSERS_INVESTIGATION_CORPUS_PATH=$PWD/parsers_investigation/corpus/agreement_fixtures.json
cmake --build build/parsers --target \
  parsers_investigation markov_parser_ranker markov_model_test

build/parsers/parsers_investigation/markov_model_test
build/parsers/parsers_investigation/parsers_investigation --self-test
build/parsers/parsers_investigation/markov_parser_ranker
```

O comando acima usa o perfil ampliado. Para reproduzir o baseline apenas com
gold verificado:

```sh
build/parsers/parsers_investigation/markov_parser_ranker \
  --attested-weight 0 --treebank-weight 0 \
  --synthetic-weight 0 --silver-weight 0 --reordering-weight 0
```

Para comparar gold fora e dentro do treino:

```sh
build/parsers/parsers_investigation/markov_parser_ranker \
  --evaluation-tier attested \
  --evaluation-policy leave-one-fixture-out
build/parsers/parsers_investigation/markov_parser_ranker \
  --evaluation-tier attested \
  --evaluation-policy in-sample

build/parsers/parsers_investigation/markov_parser_ranker \
  --evaluation-tier treebank \
  --evaluation-policy leave-one-fixture-out
build/parsers/parsers_investigation/markov_parser_ranker \
  --evaluation-tier treebank \
  --evaluation-policy in-sample
```

Para reproduzir a curva e os controles negativos:

```sh
build/parsers/parsers_investigation/markov_parser_ranker \
  --evaluation-tier attested --evaluation-policy exposure-curve

build/parsers/parsers_investigation/markov_parser_ranker \
  --evaluation-tier attested \
  --training-control shuffle-within-sequence --shuffle-seed 20260906

build/parsers/parsers_investigation/markov_parser_ranker \
  --evaluation-tier attested --evaluation-policy exposure-curve \
  --training-control counterfactual-analysis \
  --exposure-multipliers 0,0.25,1,8
```

Para inspecionar o domínio que alimenta o ranker:

```sh
build/parsers/parsers_investigation/parsers_investigation \
  --strategy dependency-projection --include-nbest
```

## Próximos testes

1. Criar um importador reprodutível de LDT/PROIEL e separar treino/teste por
   obra, autor e gênero.
2. Acrescentar uma projeção inspirada em Schmid--Laws que preserve caso e papel
   verbal sem
   codificar toda a flexão.
3. Combinar o log-score como feature de peso $\lambda$ do score decomposto, com
   $\lambda$ escolhido em desenvolvimento e não no teste.
4. Comparar o percurso canônico com transições diretamente fatoradas por aresta,
   como `head-part + dependency-label + dependent-part`.
5. Ampliar os pares de reordenação validados: a estrutura correta deve sobreviver,
   enquanto a preferência de linearização pode mudar.
6. Repetir os controles aleatórios em corpus maior: o comportamento invertido
   do pequeno recorte LDT impede interpretar uma única semente como baseline.

Esse último passo segue a evidência mais diretamente relevante do estado da
questão: Lee, Naradowsky e Smith (2011) integram morfologia e dependências em
latim; Gubbins e Vlachos (2013) aplicam memória curta a ancestrais sintáticos;
Krishna et al. (2016, 2018) pontuam grafos de candidatos em sânscrito. A
hipótese para o próximo ciclo é que “vizinhança” definida por relações do
parser seja mais útil que mera adjacência superficial.

## Referências diretamente acionáveis

- [Lee, Naradowsky e Smith 2011 — joint morphological disambiguation and dependency parsing](https://aclanthology.org/P11-1089.pdf)
- [Gubbins e Vlachos 2013 — dependency language models](https://aclanthology.org/D13-1143.pdf)
- [Krishna et al. 2016 — path-constrained random walks em sânscrito](https://aclanthology.org/C16-1048.pdf)
- [Krishna et al. 2018 — energy-based model para ordem livre](https://aclanthology.org/D18-1276.pdf)
- [Brants 2000 — TnT](https://aclanthology.org/A00-1031.pdf)
- [Eger, vor der Brück e Mehler 2015 — tagging e lematização em latim](https://aclanthology.org/W15-3716/)
