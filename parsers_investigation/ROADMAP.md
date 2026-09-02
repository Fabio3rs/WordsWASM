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

## Estado inicial

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

## Invariantes da investigação

1. **Meaning não participa das fases formais.** O perfil search-only deve ser
   suficiente até uma eventual fase semântica.
2. **Ambiguidade genuína é resultado, não erro.** O gold pode aceitar um
   conjunto de análises equivalentes.
3. **Impossibilidade elimina; preferência pontua.** Toda rejeição e todo delta
   de score devem ter uma justificativa estável.
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
  "schemaVersion": 1,
  "text": "Arma virumque cano",
  "strategy": "propagation",
  "datasetId": "sha256:...",
  "tokenCount": 3,
  "candidateCounts": [4, 8, 10],
  "rawProduct": "320",
  "domainsAfterPropagation": [3, 4, 2],
  "constraintChecks": 0,
  "statesCreated": 0,
  "statesReused": 0,
  "packedNodes": 0,
  "completeParses": 0,
  "elapsedNs": 0,
  "peakBytes": 0
}
```

`rawProduct` é string para não impor um limite inteiro. Relatórios agregados
devem apresentar no mínimo `p50`, `p95`, máximo e distribuição por tamanho da
sentença.

## Catálogo inicial de constraints

### Duras

| ID | Regra inicial |
|---|---|
| H001 | Todo token lexical precisa ser consumido exatamente uma vez. |
| H002 | Toda oração completa possui um predicado finito, salvo fixture explicitamente fragmentária. |
| H003 | Sujeito explícito concorda com pessoa e número do verbo; sujeito nulo é permitido. |
| H004 | Adjetivo atributivo concorda em caso, número e gênero com o núcleo. |
| H005 | Preposição com regência conhecida aceita o caso exigido. |
| H006 | Regência verbal conhecida deve ser respeitada. `unknown` não significa “qualquer caso”. |
| H007 | `-que` participa de uma coordenação e inicia o segundo membro segundo o escopo candidato. |
| H008 | NPs coordenados sob a mesma função externa unificam caso; número e gênero não são unificados automaticamente. |
| H009 | Features ausentes e features `unknown` não são tratadas como fatos positivos. |
| H010 | Um estado compartilhado conserva o ambiente completo de features e exigências ainda abertas. |

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
- criar baseline de pesos manuais;
- calibrar pesos em corpus sem alterar hard constraints;
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

1. Fazer o executável aceitar texto arbitrário e separar tokens preservando
   posição e forma original.
2. Adicionar `--strategy=morphology` e saída NDJSON.
3. Implementar o censo M1 sem nenhuma regra sintática.
4. Definir o schema de fixtures S0 e registrar o gold ambíguo de
   `Arma virumque cano`.
5. Adicionar `--strategy=cartesian` com orçamento configurável.
6. Implementar H001–H010 com provenance.
7. Adicionar `--strategy=propagation` e verificar equivalência com o
   cartesiano.
8. Só então iniciar relações, score e N-best.

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
