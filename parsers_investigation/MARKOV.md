# Investigação Markoviana sobre as saídas do parser

A matriz que distingue dados executados, evidência textual, inspiração e
material ainda não usado está em [`PROVENANCE.md`](PROVENANCE.md). Ela também
registra licenças, hashes locais e transformações.

## Pergunta experimental

Uma cadeia de Markov treinada com frases latinas conhecidas consegue ordenar
as análises morfológicas que sobreviveram às hard constraints?

Rodada regenerada em 2026-09-09 com dataset
`sha256:bb89d1c6a7305ecfaf828747018598e7cd97c321570a80da38249c433d131803`,
fonte `fb41bd5e639163f4ecc7d8ad17e97f3f441fe481+parsers-dirty` e perfil do core
`whitakerTrim=annotate`, `orthography=classical-and-medieval`, `twoWords=disabled`,
com todos os mecanismos morfológicos habilitados. Os resultados sequencial e
estrutural usam, respectivamente, schemas v3 e v2.

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

Uma transição não observada recebe massa por suavização aditiva ou por um
backoff hierárquico selecionável. Portanto, ausência no corpus de treino nunca
elimina uma análise aceita pelo parser.

## Implementação C++23

- [`markov_model.hpp`](markov_model.hpp) contém o modelo genérico de contagens,
  pesos fracionários, duas suavizações e telemetria de desconhecidos;
- [`markov_ranker.cpp`](markov_ranker.cpp) executa o parser
  `dependency-projection`, recolhe todas as atribuições possíveis e faz a
  avaliação;
- [`markov_model_test.cpp`](markov_model_test.cpp) testa frequência,
  suavização, pesos e pré-condições;
- `parsers_investigation --include-nbest` publica opcionalmente o mesmo domínio,
  incluindo a árvore determinística de cada candidato, em `morphologyNBest` no
  NDJSON v3.

`AnalysisChoice` preserva ainda uma projeção tipada de caso, número, gênero,
grau, tempo, voz, modo e pessoa. Uma máscara compacta de aplicabilidade permite
serializar `null` como “não aplicável”; o valor enum `unknown` continua
significando “aplicável, mas desconhecido”. Um `static_assert` limita essa
projeção a 16 bytes. O v3 acrescenta método de derivação, assessment do trim,
notices e papel em span composto; esses campos documentam a origem do candidato
e ainda não entram no estado Markoviano. Nenhum estimador fatorado é escolhido
por essa mudança.

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
Irmãos são ordenados por uma assinatura recursiva que contém relação, estado,
lema e as assinaturas dos descendentes. A posição do token desempata somente
subárvores indistinguíveis sob essa representação. O estado estrutural usa
eventos de entrada e saída:

```text
enter|root|verb
enter|nsubj|noun
exit|nsubj
enter|predicative|adjective
exit|predicative
exit|root
```

As fronteiras distinguem, por exemplo, dois dependentes irmãos de uma cadeia
núcleo–filho–neto. Os testes cobrem versões SVO/VOS, irmãos com o mesmo lema,
relação e estado mas subárvores diferentes, permutação dos índices e colisão
entre topologias. A representação anterior, sem fronteiras e com desempate
posicional, falhava nesses dois últimos casos.

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

O baseline padrão continua usando suavização aditiva com $\alpha=0{,}1$:

$$
P(x\mid c)=
\frac{\operatorname{count}(c,x)+\alpha}
     {\operatorname{count}(c)+\alpha|V|}.
$$

O modo opcional `--smoothing hierarchical-backoff` mantém tabelas para os
sufixos do contexto. A distribuição de ordem zero usa suavização aditiva; cada
nível observado interpola sua contagem com a distribuição anterior:

$$
P_k(x\mid c)=
\frac{N_k(c,x)+\tau P_{k-1}(x\mid b(c))}
     {N_k(c)+\tau}.
$$

`--backoff-strength` define $\tau$ (padrão 1). O relatório registra estados
desconhecidos, acertos e faltas do contexto completo, fallbacks uniformes,
transições com backoff e um histograma da maior profundidade observada. Assim,
“faltou evidência” deixa de ser indistinguível de uma preferência aprendida.

O documento também publica `evaluationCoverage`: fixtures gold solicitadas,
cobertura lexical, orçamento, status do parser, N-best não vazio, gold presente
no lattice, gold sobrevivente às constraints e casos efetivamente ranqueáveis.

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
| POS + morfologia | 1 | híbrida | 5/10 | 7/10 | 5/10 | 0,592 |
| POS + morfologia | 1 | canônica | 5/10 | 7/10 | 5/10 | 0,598 |
| POS + morfologia | 2 | superficial | 8/10 | 9/10 | 0/10 | 0,870 |
| POS + morfologia | 2 | híbrida | 8/10 | 10/10 | 5/10 | 0,883 |
| POS + morfologia | 2 | canônica | 8/10 | 10/10 | 5/10 | 0,883 |

O baseline manual tem gold top-1 em 9/10; `Alumnae sunt altae` mantém o gold
adjetival no rank 2, empatado no melhor score com a leitura participial.

POS puro continua empatando demais: a ordem estrutural resolve exclusivamente
apenas 1/10. A representação morfológica completa discrimina mais. Depois da
correção das fronteiras estruturais, memória 2 híbrida ou canônica obtém 8/10
top-1, 10/10 top-3 e MRR 0,883; a superfície obtém 8/10, 9/10 e 0,870. O ganho
é pequeno e não sustenta escolher a vizinhança estrutural como arquitetura.
Cinco escolhas são exclusivamente determinadas pelo melhor score Markoviano.

## Efeito da ampliação

A ablação abaixo fixa a configuração comparada anterior — POS + morfologia,
memória 2 e ordem canônica — e varia apenas as camadas de treino:

| Treino | Frases | Gold top-1 | Gold top-3 | Top-1 exclusivamente Markov | MRR |
|---|---:|---:|---:|---:|---:|
| somente gold verificado | 9 | **8/10** | **10/10** | 5/10 | **0,883** |
| verificado + sintético | 16 | 7/10 | 9/10 | 5/10 | 0,789 |
| verificado + silver | 22 | 7/10 | 9/10 | 5/10 | 0,803 |
| três camadas | 29 | 6/10 | 9/10 | 5/10 | 0,736 |

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
| superficial | 7/10 | 8/10 | **0,754** |
| híbrida | 5/10 | 7/10 | 0,595 |
| canônica | 5/10 | 6/10 | 0,576 |

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
| TLL + editorial | alvo fora | 3/5 | 3/5 | 4/5 | 3/5 | 0,676 |
| TLL + editorial | alvo dentro | 3/5 | **5/5** | **5/5** | **5/5** | **1,000** |
| LDT 2.1 | alvo fora | 4/5 | 3/5 | 4/5 | 3/5 | 0,700 |
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

O relatório v3 calcula três rankings sobre os mesmos candidatos:

1. `manualOnly`: apenas o score determinístico anterior;
2. `markovOnly`: log-score Markoviano, com `assignment-id` somente para tornar
   empates reproduzíveis;
3. `markovThenManual`: Markov e, depois, score manual e `assignment-id`.

Na configuração POS+morfologia, memória 2 e ordem canônica, sem exposição do
alvo, a ablação é:

| Recorte | Ranking | Top-1 | Top-3 | MRR |
|---|---|---:|---:|---:|
| TLL | manual apenas | 3/5 | 5/5 | 0,767 |
| TLL | Markov puro | 3/5 | 3/5 | 0,647 |
| TLL | Markov + manual | 3/5 | 4/5 | **0,676** |
| LDT | manual apenas | **4/5** | **5/5** | **0,867** |
| LDT | Markov puro | 3/5 | 4/5 | 0,700 |
| LDT | Markov + manual | 3/5 | 4/5 | 0,700 |

No TLL, o desempate manual melhora top-3 e MRR, mas não top-1; no LDT canônico,
o baseline manual continua superior. `markovStrictTop1` continua sendo a medida
que não dá crédito ao desempate.

O modo `exposure-curve` remove todos os duplicados textuais do alvo e reinsere
somente a fixture-alvo com multiplicadores controlados. A resposta do Markov
puro foi:

| Exposição do alvo | TLL top-1 / MRR | LDT top-1 / MRR |
|---:|---:|---:|
| 0 | 3/5 / 0,647 | 3/5 / 0,700 |
| 0,01 | 3/5 / 0,676 | 3/5 / 0,700 |
| 0,10 | 4/5 / 0,822 | 4/5 / 0,867 |
| 0,25 | **5/5 / 1,000** | 5/5 / 1,000 |
| 1,00--8,00 | 5/5 / 1,000 | 5/5 / 1,000 |

Esta curva é um controle positivo: doses muito pequenas da análise correta já
alteram o ranking Markoviano sem auxílio do score manual.

Há dois controles negativos. `shuffle-within-sequence` preserva o multiconjunto
de estados de cada exemplo, mas destrói suas transições com semente registrada.
Nas sementes 1--20, o TLL caiu, em média, de 3 para 1,05 top-1 no Markov puro e
de MRR 0,647 para 0,299. No LDT, porém, o controle embaralhado ficou
artificialmente melhor em top-1 médio (3,4/5) e MRR (0,745). O recorte é
pequeno demais para usar esse controle aleatório isoladamente como prova.

O controle `counterfactual-analysis` é mais direto: para cada fixture de
treino, seleciona o melhor candidato não-gold cuja sequência projetada difere
das sequências gold. Com exposição 0,25, ensinar a análise correta leva ambos
os recortes a 5/5 e MRR 1,0; ensinar a alternativa errada deixa o TLL em 2/5 e
MRR 0,517 e o LDT em 4/5 e MRR 0,840. Com peso 1,0, o contrafactual cai para
1/5 e MRR 0,357 no TLL e 2/5 e MRR 0,580 no LDT. O sentido oposto das curvas
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

### Backoff como ablação, não como arquitetura escolhida

No recorte TLL held-out, POS+morfologia, memória 2 e ordem canônica, a
suavização aditiva e o backoff hierárquico obtiveram o mesmo top-1 3/5, top-3
3/5 e MRR 0,647. A telemetria, porém, confirma caminhos diferentes em 3.856
transições pontuadas: o baseline fez 1.838 fallbacks uniformes; o modo
hierárquico fez 1.838 backoffs e nenhum fallback uniforme. Foram observados
798 estados completos desconhecidos em ambos.

Isso valida a execução da alternativa, não uma melhoria. Suavização aditiva e
backoff permanecem configurações concorrentes. Da mesma forma, os atributos
tipados são infraestrutura para experimentar projeções; não estabelecem uma
fatoração probabilística preferida. O ranker continua sem consumir árvores
alternativas do MST/Eisner.

Uma única execução nativa desse recorte, medida com `/usr/bin/time`, reportou
16.864 KiB de RSS máximo e 0,19 s para o baseline, contra 17.016 KiB e 0,21 s
para backoff. A diferença de 152 KiB é compatível com o objetivo leve neste
corpus, mas esses valores são apenas um smoke de recursos, não um benchmark
estável nem projeção para um treebank completo.

## Baseline estrutural por ancestrais

O experimento adicional
[`dependency_markov_ranker.cpp`](dependency_markov_ranker.cpp) preserva o
ranker sequencial acima e muda somente a unidade estatística. Ele executa
`dependency-tree-oracle`, recebe `treeNBest` completo e reordena essas árvores
sem criar, remover ou recombinar arcos.

[`dependency_markov.hpp`](dependency_markov.hpp) extrai exatamente um fator de
raiz e um fator por token não raiz. A memória 1 consulta o núcleo local; a
memória 2 acrescenta o avô e a relação de entrada do núcleo:

```text
raiz:       ROOT                       -> estado(verbo-raiz)
ordem 1:    estado(núcleo)             -> relação + estado(dependente)
ordem 2:    avô + relação-do-núcleo + núcleo
                                        -> relação + estado(dependente)
```

Os heads são indexados diretamente. A ordem do vetor de relações, DFS/BFS e a
posição superficial não determinam o contexto. Cada arco contribui uma vez,
mesmo quando aparece no prefixo de vários caminhos raiz--folha. O marcador
artificial `ROOT` é separado do verbo que ocupa a raiz segundo a convenção
interna atual.

Uma ablação opcional acrescenta um evento de valência por verbo. O evento é o
multiconjunto ordenado de todos os seus dependentes diretos, incluindo relação
e estado; multiplicidades são preservadas. Assim o modelo consegue observar
conjuntamente, por exemplo, `nsubj+obj+obl`, em vez de reduzir toda decisão a
pares independentes.

O treino estrutural usa somente candidatos que coincidem simultaneamente com
o gold morfológico e de dependências. Alternativas que produzem a mesma
fatoração são deduplicadas e conservam, juntas, o peso total da frase. Os dez
alvos verificados são avaliados por leave-one-fixture-out; as sete fixtures
sintéticas entram no treino com peso 0,5. O corpus atestado apenas com
morfologia e o mapeamento LDT ainda não entram, pois projetar dependências do
parser nesses dados repetiria o erro que o experimento procura medir.

Resultados da execução de 2026-09-09, com $\alpha=0{,}1$ e força de backoff 1:

| Estado | Ancestrais | Perfil conjunto | Árvore top-1 | Árvore top-3 | Raiz top-1 | UAS | LAS | Estrutural + manual top-1 |
|---|---:|---|---:|---:|---:|---:|---:|---:|
| POS | 1 | não | 6/10 | 8/10 | 10/10 | 1,000 | 0,867 | 6/10 |
| POS | 1 | sim | **7/10** | **9/10** | **10/10** | **1,000** | **0,933** | **10/10** |
| POS | 2 | não | 6/10 | 8/10 | 10/10 | 1,000 | 0,867 | 6/10 |
| POS | 2 | sim | **7/10** | **9/10** | **10/10** | **1,000** | **0,933** | **10/10** |
| POS+morfologia | 1 | não | 2/10 | 4/10 | 10/10 | 0,827 | 0,693 | 2/10 |
| POS+morfologia | 1 | sim | 4/10 | 4/10 | 10/10 | 0,867 | 0,767 | 4/10 |
| POS+morfologia | 2 | não | 4/10 | 4/10 | 8/10 | 0,733 | 0,667 | 4/10 |
| POS+morfologia | 2 | sim | 4/10 | 4/10 | 10/10 | 0,867 | 0,767 | 4/10 |

O gold estrutural esteve presente no conjunto candidato em 10/10. O baseline
manual sozinho obteve 9/10. O ganho do perfil conjunto mostra sinal adicional,
mas o conjunto é pequeno e contém construções paralelas; 10/10 após desempate
manual não é estimativa de confiabilidade externa. A projeção morfológica
atômica ficou pior por esparsidade, e a memória 2 não superou a memória 1. O
próximo teste útil é decompor atributos e avaliar em treebank estrutural com
partição por obra/autor.

## Reprodução

```sh
cmake -S . -B build/parsers -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DENABLE_TESTS=ON \
  -DPARSERS_INVESTIGATION_CORPUS_PATH=$PWD/parsers_investigation/corpus/agreement_fixtures.json
cmake --build build/parsers --target \
  parsers_investigation markov_parser_ranker dependency_markov_ranker \
  markov_model_test

build/parsers/parsers_investigation/markov_model_test
build/parsers/parsers_investigation/parsers_investigation --self-test
build/parsers/parsers_investigation/markov_parser_ranker
build/parsers/parsers_investigation/dependency_markov_ranker

build/parsers/parsers_investigation/markov_parser_ranker \
  --evaluation-tier attested \
  --smoothing hierarchical-backoff --backoff-strength 1
```

Esses programas de investigação só são executados pelos comandos explícitos
acima. O CMake não os registra no CTest/CI por padrão. Para uma rodada local
deliberada via CTest, é necessário configurar também
`-DPARSERS_INVESTIGATION_REGISTER_TESTS=ON`.

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
  --strategy dependency-tree-oracle --include-nbest
```

## Próximos testes

1. Criar um importador reprodutível de LDT/PROIEL e separar treino/teste por
   obra, autor e gênero.
2. Comparar suavização aditiva e backoff em recortes maiores, incluindo
   cobertura e profundidade efetiva de contexto.
3. Experimentar tabelas condicionais por atributo sobre a projeção tipada,
   sempre com ablação por fator.
4. Ampliar o gold estrutural real e separar treino/teste por obra, autor e tipo
   de construção; o N-best estrutural e os fatores por aresta já estão
   implementados.
5. Mapear explicitamente os tagsets de dependência LDT/PROIEL antes de usar
   esses treebanks no treino estrutural; nunca treinar a projeção determinística
   como se fosse gold.
6. Comparar pesos aprendidos entre arcos, perfil de valência e score manual;
   nenhuma dessas alternativas é agora a arquitetura oficial.
7. Ampliar os pares de reordenação validados: a estrutura correta deve
   sobreviver, enquanto a preferência de linearização pode mudar.
8. Repetir os controles aleatórios em corpus maior: o comportamento invertido
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
