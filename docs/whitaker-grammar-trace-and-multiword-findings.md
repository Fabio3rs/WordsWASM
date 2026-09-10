# Auditoria gramatical do Whitaker e análise multiword

Data da auditoria: 2026-09-10

Estado: auditoria consolidada e primeira correção multiword implementada. A suíte completa passa com 121/121 testes.

## Objetivo

Este documento consolida os achados e as sugestões da auditoria de cobertura gramatical do WordsWASM em relação ao Whitaker's Words, incluindo:

- o caminho DB original → packer WWDB → representação compacta → engine;
- especializações codificadas diretamente no Whitaker original e no core nativo;
- comparação prática entre `words`, `words_json` e o CLI nativo;
- confronto das diferenças com as gramáticas locais;
- proposta de um manifesto rico e rastreável;
- caracterização TDD do comportamento multiword antes de qualquer correção.

## Eixos que não devem ser misturados

Cada comportamento deve ser classificado em pelo menos um destes eixos:

1. **Compatibilidade Whitaker**: o WordsWASM reproduz o programa Ada original?
2. **Validade gramatical**: o comportamento é sustentado pela gramática latina?
3. **Contrato nativo do WordsWASM**: há uma representação melhor, mais completa ou Unicode-aware que deliberadamente diverge do legado?

Uma igualdade diferencial com o Whitaker não prova correção linguística. Do mesmo modo, uma melhoria linguística ou estrutural não deve ser registrada como regressão sem distinguir o contrato afetado.

## Observação sobre ASCII e Unicode

O executável original `whitakers-words/bin/words` deve ser usado somente com entrada ASCII. Ele não oferece suporte à quantidade vocálica em Unicode. A aceitação de caracteres Unicode e de marcas de quantidade é uma extensão do WordsWASM e deve ser testada como funcionalidade nativa, não como compatibilidade literal do oráculo original.

## Fontes examinadas

### Implementação e dados

- `whitakers-words/bin/words`: oráculo textual original.
- `whitakers-words/bin/words_json`: adaptador JSON do original.
- fontes Ada sob `whitakers-words/src/`.
- packer e formato WWDB do WordsWASM.
- `src/engine.cpp` e os tipos de dados internos.
- testes nativos, diferenciais, configurados e de corpus.

### Gramáticas locais

- `.study/gramatica-latina-luna-completa`.
- `.study/pg18251-images.html`, edição digital de Bennett.

## Conclusão executiva

Não foi encontrada, até aqui, uma família estrutural de morfologia presente no banco do Whitaker e completamente ausente do caminho WWDB → engine. O transporte dos dados é amplo e os testes diferenciais cobrem bem o comportamento comum.

Entretanto, isso ainda não permite afirmar que “todas as regras gramaticais” estejam comprovadamente implementadas:

- o corpus da Eneida exercita somente 740 dos 1.785 `RuleId` de flexão, aproximadamente 41,5%;
- especializações em código não possuem hoje uma identidade e uma cobertura equivalentes às regras do banco;
- regras ortográficas, sincopadas, compostos, abreviações e filtros de configuração precisam de rastreamento próprio;
- o processamento multiword é destrutivo: ele acrescenta a hipótese composta, mas descarta análises independentes válidas do primeiro token;
- há comportamentos herdados do Whitaker que são compatíveis, mas gramaticalmente discutíveis ou incorretos.

### Resumo dos achados prioritários

| Achado | Compatibilidade | Avaliação gramatical | Prioridade |
|---|---|---|---|
| `C.` mantém só a abreviação no original, mas também recebe `100` no nativo | divergência real | questão de tokenização/especialização | alta |
| `licent` é aceito como impessoal no plural pelo original e pelo nativo | compatível | contraria a restrição normativa a 3ª pessoa singular | registrar como bug legado |
| `amaturus est` recebe rótulo passivo | compatível | perífrase ativa, logo rótulo legado incorreto | oferecer projeção normalizada versionada |
| compostos descartam análises independentes do primeiro token | em grande parte compatível | perda informacional desnecessária | alta |
| infinitivos compostos homógrafos geram quatro hipóteses nativas e uma no original | divergência de cardinalidade | quatro regras-fonte são reais | preservar proveniência, não deduplicar arbitrariamente |
| regras ortográficas têm gate de importação, regras de síncope não têm gate equivalente | risco de drift | não é lacuna morfológica comprovada | média |
| cobertura por corpus atinge cerca de 41,5% dos `RuleId` | testes atuais passam | cobertura insuficiente para alegar completude | alta |

## Inventário do WWDB auditado

No dataset WWDB 1.9 foram observados:

| Entidade | Quantidade |
|---|---:|
| Lexemas | 39.339 |
| Referências de radical | 62.086 |
| Flexões | 1.785 |
| Sufixos | 179 |
| Prefixos | 135 |
| Tickons, incluídos nos prefixos | 6 |
| Tackons | 29 |
| Packons, incluídos nos tackons | 11 |
| Entradas únicas | 76 |
| Rewrites | 170 |
| Regras de síncope | 11 |
| Regras ortográficas | 159 |
| Notices | 5 |

Os registros dos bancos full e search foram comparados. A evidência atual aponta para transporte completo das famílias inventariadas, sem uma classe inteira silenciosamente ausente.

## Rastreamento dos bits até a engine

As disposições compactas observadas foram:

| Registro | Largura física | Bits usados |
|---|---:|---:|
| Lexema | `u48` | 47 |
| Flexão | `u48` | 47 |
| Referência de radical | `u24` | 21 |
| Sufixo | `u64` | 46 |
| Prefixo | `u16` | 16 |
| Tackon | `u32` | 22 |
| Unique | `u64` | 50 |
| Notice | `u16 + u8` | 6 bits de metadados no campo relevante |

Os bits são uma representação de transporte. Depois do decode, a engine trabalha majoritariamente com enums e estruturas tipadas. Portanto, um manifesto útil não deve parar em “bit 12 presente”; deve conectar:

`fonte Ada/DB → registro lógico → campo empacotado → valor decodificado → ramo de código → testemunha de teste`.

## Famílias de regras confirmadas

A inspeção não revelou omissão integral das seguintes famílias:

- lexemas e radicais;
- flexões nominais e verbais;
- prefixos e sufixos;
- tackons, packons e tickons;
- uniques e notices;
- regras ortográficas;
- regras de síncope;
- análises compostas com formas de `sum` e com `iri`;
- filtros e qualificadores de frequência, época, área, geografia e fonte.

Isso significa “família presente no pipeline”, não “todas as combinações executadas e verificadas”.

## Cobertura observada

O corpus da Eneida possui 2.726 formas no conjunto examinado. A classificação diferencial observada foi:

| Categoria | Quantidade |
|---|---:|
| Exata | 2.630 |
| Diferença somente de proveniência | 41 |
| Semanticamente equivalente | 41 |
| Diferença nativa aceita | 14 |

Apesar do bom resultado por forma, apenas 740 dos 1.785 `RuleId` de flexão foram exercitados, cerca de 41,5%. A métrica por forma não substitui cobertura por regra: várias formas comuns repetem o mesmo subconjunto de regras.

## Finding 1: abreviação `C.`

### Observação

No Whitaker original, `C.` retém somente a leitura de abreviação. No WordsWASM, o mesmo token também pode produzir a leitura de numeral romano `100`.

### Causa provável

O Ada contém uma especialização durante o sweep da lista em `words_engine-list_sweep.adb`, próxima da linha 442. O lexer nativo reconhece a fronteira pelo ponto, mas `Engine::analyze(TextToken)` não aplica hoje uma especialização equivalente nesse caminho.

### Classificação

- compatibilidade: **lacuna confirmada**;
- morfologia de banco: não é uma família ausente;
- natureza: tokenização/especialização codificada em código.

### Sugestão

Representar a regra no manifesto como `codeSpecialization`, criar uma testemunha mínima para `C.` e decidir explicitamente se o modo compatível deve remover a leitura numeral. O modo nativo pode preservar ambas, desde que a divergência seja documentada e versionada.

## Finding 2: `licet` e `licent`

### Observação

O original e o WordsWASM aceitam `licent` como forma do uso impessoal. A lógica Ada restringe a terceira pessoa, mas não restringe o número ao singular.

### Gramática

A Gramática Latina de Luna, §342, descreve o verbo impessoal finito como restrito à terceira pessoa singular. Portanto, aceitar `licent` nesse sentido é compatibilidade com o legado, mas não a leitura normativa esperada.

### Classificação

- compatibilidade: **igual ao original**;
- validade gramatical: **bug legado/disputado**;
- implementação: não deve ser corrigido silenciosamente dentro do modo compatível.

### Sugestão

Separar os estados `differentially-equal` e `linguistically-supported`. Se houver normalização normativa, expô-la como política ou projeção versionada, mantendo o resultado legado disponível.

## Finding 3: voz de `amaturus est`

### Observação

O original e o WordsWASM descrevem o composto sintético como presente passivo. O core preserva esse comportamento histórico explicitamente na região de `src/engine.cpp` próxima da linha 2543.

### Gramática

Luna §285 classifica a construção de particípio futuro ativo com `sum` como conjugação perifrástica ativa. O rótulo passivo é, portanto, um erro semântico herdado, não uma lacuna de paridade.

### Sugestão

Não alterar o payload compatível sem versionamento. Acrescentar uma projeção semântica normalizada que corrija a voz, com proveniência indicando que o rótulo legado foi preservado no resultado bruto.

## Finding 4: política de maiúsculas e nomes desconhecidos

### Observação

Configurações do Whitaker como `IGNORE_UNKNOWN_NAMES` e `IGNORE_UNKNOWN_CAPS` podem impedir certas transformações para uma forma capitalizada, como `Teologia`. O WordsWASM faz case-folding e pode chegar a uma leitura medieval.

### Classificação

Trata-se de política de configuração e ordem de processamento, não de ausência de regra gramatical.

### Sugestão

Registrar essas opções no manifesto como filtros/políticas e cobri-las com testes configurados. Não incorporá-las aos bits morfológicos do WWDB.

## Finding 5: risco de drift em rewrites

As 159 regras ortográficas possuem um gate exato no importador Ada, em `import_ada_rewrites.py` próximo da linha 159. As 11 regras de síncope são mantidas manualmente em `REWRITES.LAT` e não possuem uma verificação de drift equivalente.

### Sugestão

Criar um gate determinístico para as regras de síncope: identidade semântica, contagem, origem e testemunhas. O gate deve falhar se o conjunto importado divergir do conjunto esperado, sem depender apenas da contagem total.

## Finding 6: análise multiword é destrutiva

### Requisito desejado

A análise multiword deve preservar todas as análises independentes dos tokens e acrescentar a hipótese composta, sem usar o composto para substituir o conjunto original.

### Comportamento atual

Em `src/engine.cpp`, na região aproximada de 2523–2607, a engine:

1. encontra análises do primeiro token que podem sustentar um composto;
2. gera as hipóteses compostas;
3. substitui `result.analyses` apenas pelas análises participantes.

Essa última etapa perde leituras válidas que não participam do composto.

### Exemplos quantificados

| Consulta | Análises isoladas do primeiro token | Fontes preservadas na frase | Perda atual |
|---|---:|---:|---:|
| `amata est` | 15 | 1 | 14 |
| `amatam esse` | 3 | 1 | 2 |
| `amatum iri` | 10 | 1 | 9 |

O composto pode estar correto e, simultaneamente, o resultado ser incompleto.

## Finding 7: desacordo finito é tratado corretamente no caminho por linha

### Exemplo

`amatam est` não deve formar o composto finito, pois o particípio acusativo não satisfaz o requisito nominativo da construção com `sum` finito.

O caminho nativo `analyze_line` preserva as análises independentes de `amatam` e `est` quando o composto é rejeitado. Esse comportamento foi caracterizado como teste verde.

Há uma diferença de API:

- o `words` textual mostra os dois tokens independentemente;
- `words_json` retorna somente as análises do primeiro token nesse caso;
- `analyze_line` nativo preserva os dois;
- um caminho batch de `analyze_text` pode tratar a consulta não reconhecida como erro.

Essa diferença deve ser atribuída ao contrato de cada API, não confundida com flexão latina.

## Finding 8: concordância de compostos finitos

Foram confirmadas as restrições de nominativo e concordância de número na combinação de particípio com forma finita de `sum`:

- `amati sunt`: composto válido;
- `amata sunt`: composto válido pela leitura nominativa plural neutra;
- `amatam est`: composto inválido.

A filtragem equivalente aparece no parser Ada perto de `words_engine-parse.adb:372`.

## Finding 9: infinitivos compostos e caso do particípio

Uma hipótese inicial sugeria restringir `esse` e `fuisse` a particípios acusativos por causa do ACI. A revisão gramatical mostrou que essa regra seria excessiva e foi descartada.

- Luna §282 enfatiza o acusativo com infinitivo no ACI.
- Bennett §§205–206 fornece paradigmas como `amatus (-a, -um) esse` e `amaturus (-a, -um) esse`.
- Bennett também descreve a concordância do predicativo do infinitivo complementar com o sujeito principal.

Logo, o core não deve impor uma restrição global ao acusativo para todos os infinitivos compostos. O contexto sintático completo é que determina o caso; a análise morfológica isolada deve preservar as possibilidades.

Já a construção supino em `-um` + `iri`, como `amatum iri`, exige o supino acusativo e está alinhada com Luna.

## Finding 10: homografia em infinitivos compostos

### Observação

Consultas como estas produzem uma hipótese sintética no original, mas quatro no WordsWASM:

- `amatum esse`;
- `captum esse`;
- `amaturum esse`;
- `amaturum fuisse`.

No nativo, as quatro hipóteses têm regras-fonte distintas. Para `captum esse`, por exemplo, foram observados `RuleId` 733, 731, 735 e 732 no banco search-v2.

### Interpretação

Isto não é simplesmente duplicação inútil. A superfície é homógrafa e há quatro análises morfológicas de suporte reais. O original colapsa a apresentação; o nativo expõe a proveniência.

### Sugestão

Escolher explicitamente um dos contratos:

- quatro hipóteses compostas, cada uma com `source_rule` próprio; ou
- uma hipótese composta agregada com `supports[]` contendo todas as análises-fonte.

Não escolher arbitrariamente uma das quatro regras para imitar a cardinalidade do original, pois isso apagaria proveniência.

## Matriz prática comparada

| Entrada | Whitaker original | WordsWASM antes da correção | Avaliação |
|---|---|---|---|
| `amata est` | 1 fonte participante + 1 composto | 1 fonte participante + 1 composto | composto correto, mas 14 leituras isoladas de `amata` são ocultadas |
| `amati sunt` | composto aceito | composto aceito | correto |
| `amata sunt` | composto aceito | composto aceito | correto pela leitura nominativa plural neutra |
| `amatam est` | tokens apresentados separadamente no modo textual | sem composto; `analyze_line` preserva ambos | correto |
| `captum esse` | 4 leituras participiais + 1 composto | 4 fontes + 4 compostos | divergência de apresentação/proveniência |
| `amatum esse` | 4 leituras participiais + 1 composto | 4 fontes + 4 compostos | divergência de apresentação/proveniência |
| `amaturum esse` | 4 leituras participiais + 1 composto | 4 fontes + 4 compostos | divergência de apresentação/proveniência |
| `amaturum fuisse` | 4 leituras participiais + 1 composto | 4 fontes + 4 compostos | divergência de apresentação/proveniência |
| `amatam esse` | 1 fonte + 1 composto | 1 fonte + 1 composto | cardinalidade equivalente |
| `amaturam esse` | 1 fonte + 1 composto | 1 fonte + 1 composto | cardinalidade equivalente |
| `amatum iri` | 1 supino participante + 1 composto | 1 supino participante + 1 composto | composto correto, mas outras leituras isoladas são ocultadas |

## TDD adicionado antes da implementação

Foram adicionados ou ajustados em `tests/engine_test.cpp`:

### Testes verdes de caracterização

- `AnalyzesBoundedCompoundsWithSum`: deixou de fixar artificialmente o total combinado em dois, preservando as asserções morfológicas do composto.
- `FiniteCompoundsSelectNominativeParticiplesMatchingNumber`: confirma nominativo e concordância de número.
- `AnalyzeLinePreservesTokensWhenFiniteAgreementRejectsCompound`: confirma que `amatam est` mantém os tokens independentes sem inventar composto.
- `PreservesCompoundSourceRulesForHomographicParticiples`: confirma quatro proveniências distintas nas superfícies homógrafas.

### Teste vermelho intencional

- `CompoundAnalysisPreservesIndependentFirstTokenAnalyses`: compara a análise isolada do primeiro token com as fontes preservadas no resultado multiword para `amata est`, `amatam esse` e `amatum iri`.

Antes da implementação, a suíte ficou em 120/121 testes: somente esse teste vermelho falhou. As suítes diferenciais, configuradas, de corpus, full e search continuaram verdes.

## Correção mínima sugerida no modelo atual

No modelo corrente, `QueryResult::analyses` está associado à superfície do primeiro token. Portanto, a correção mínima segura é:

- usar os índices/fontes participantes somente para construir `compound_analyses`;
- não substituir o vetor completo `result.analyses` pelo subconjunto participante;
- preservar a ordem e a identidade das análises isoladas;
- manter todas as hipóteses compostas com suas regras-fonte.

Não se deve colocar as análises independentes do segundo token dentro de `result.analyses`, porque seus intervalos, superfície e identidade pertencem a outro token. A preservação completa de todos os tokens exige um IR multi-token versionado.

## Contrato futuro: `MultiTokenAnalysisIR`

Uma representação lossless futura pode separar claramente:

- `tokens[]`, cada qual com superfície, intervalo e `analyses[]` independentes;
- `compoundHypotheses[]`, contendo os tokens envolvidos;
- `supports[]`, apontando para análises por identidade, e não copiando registros sem contexto;
- projeções `legacy` e `normalized` para compatibilidade e correções semânticas.

Isso evita sobrecarregar `QueryResult::analyses` com análises de superfícies diferentes e permite representar compostos sem perda.

## Proposta de manifesto rico

### Artefatos

1. `whitakers-words/GRAMMAR_SPECIALIZATIONS.jsonl`: fonte canônica, revisável por humanos, para mapeamentos e especializações em código.
2. `*.grammar-trace.jsonl`: sidecar determinístico emitido pelo packer para cada dataset.
3. `build/reports/grammar-coverage.json`: relatório de CI e execução, fora da identidade do dataset.
4. `grammar-trace.sqlite`: índice derivado opcional para consultas; nunca a fonte de verdade.

O sidecar deve ficar fora do payload WWDB. O runtime precisa apenas dos IDs, flags e `datasetId` necessários para execução; explicações, citações, witnesses e estado de cobertura não precisam ocupar o formato binário.

### Entidades recomendadas

- `dataset`: versão, hash, perfil full/search e origem.
- `source`: arquivo original, revisão, intervalo e digest.
- `wireLayout`: largura, máscara, deslocamento e enum decodificado.
- `logicalRule`: identidade semântica estável da regra.
- `ruleOrigin`: ligação a `INFLECTS`, addons, uniques, rewrites ou código Ada.
- `packedRecord`: tabela, índice local e bytes relevantes.
- `decodedField`: valor após o decode e tipo forte correspondente.
- `codeSpecialization`: ramo não dirigido apenas pelo banco, como `C.` ou composição verbal.
- `implementationMapping`: símbolos e locais no packer/core.
- `grammarClaim`: descrição linguística independente da compatibilidade.
- `witness`: entrada mínima positiva ou negativa.
- `verification`: teste, oráculo, resultado e data.
- `coverage`: inventariado, carregado, executado e comparado.

### Estados sugeridos

- `inventoried`;
- `transported`;
- `decoded`;
- `scheduled`;
- `executed`;
- `differentially-equal`;
- `intentional-difference`;
- `linguistically-supported`;
- `disputed`;
- `legacy-bug`;
- `missing`;
- `not-applicable`.

Uma regra pode ter vários estados em eixos distintos. Exemplo: `licent` pode ser `differentially-equal` no eixo de compatibilidade e `legacy-bug` no eixo normativo.

### Identidade estável

`RuleId` sozinho não é identidade estável entre datasets. Recomenda-se:

- um digest semântico baseado nos campos normalizados da regra;
- um ID local do dataset para acesso rápido;
- registro explícito de colisões e aliases;
- identidade separada para especializações que não vêm do banco.

O packer consome `INFLECTS.SEC`; para provar a origem exata em `INFLECTS.LAT`, o gerador deve emitir um sidecar de correspondência LAT → SEC em vez de tentar reconstruí-la posteriormente.

### Exemplo conceitual

```json
{
  "kind": "codeSpecialization",
  "id": "compound.participle-plus-sum",
  "source": {
    "implementation": "whitakers-words/src/words_engine/words_engine-parse.adb",
    "symbol": "Parse"
  },
  "native": {
    "implementation": "src/engine.cpp",
    "symbol": "Engine::analyze"
  },
  "claims": [
    "finite auxiliary requires a nominative participle",
    "participle and auxiliary agree in number"
  ],
  "witnesses": [
    {"input": "amati sunt", "expect": "compound"},
    {"input": "amatam est", "expect": "no-compound"}
  ]
}
```

## Gates de CI sugeridos

1. **Inventário**: toda entrada fonte deve aparecer no trace do packer.
2. **Transporte**: os campos semânticos devem sobreviver ao encode/decode.
3. **Especializações**: todo ramo gramatical em código deve possuir ID e witness.
4. **Cobertura**: relatar `RuleId` e identidades semânticas nunca executados.
5. **Diferencial**: classificar cada diferença como regressão, proveniência, equivalência ou divergência intencional.
6. **Normativo**: manter separada a avaliação por gramática, incluindo bugs legados.
7. **Drift**: comparar rewrites ortográficos e de síncope com suas fontes.
8. **Determinismo**: o mesmo input deve gerar o mesmo WWDB e sidecar.

O CI deve inicialmente reportar lacunas de cobertura sem bloquear. Após criação de witnesses mínimos para uma família, a regressão daquela cobertura pode tornar-se bloqueante.

## O que não parece ser uma lacuna gramatical

As opções e rotinas abaixo são relevantes para compatibilidade, mas não indicam por si mesmas morfologia ausente:

- `OMIT_ARCHAIC`;
- `OMIT_MEDIEVAL`;
- `OMIT_UNCOMMON`;
- `FOR_WORD_LIST_CHECK`;
- formatação textual e paginação;
- ferramentas de manutenção e geração de listas;
- política de maiúsculas e nomes desconhecidos.

Elas devem ser registradas como filtros, apresentação ou tooling.

## Ordem recomendada de implementação

1. ~~Tornar verde o teste lossless do primeiro token sem alterar a construção dos compostos.~~ Concluído.
2. ~~Rodar testes direcionados, diferenciais e a suíte completa.~~ Concluído: 121/121.
3. ~~Atualizar este documento com a diferença de compatibilidade causada pela preservação adicional.~~ Concluído.
4. Criar a identidade e o schema inicial do manifesto.
5. Instrumentar primeiro as regras de composição, `C.`, `licet` e rewrites de síncope.
6. Gerar witnesses para os 1.045 `RuleId` ainda não exercitados pelo corpus atual.
7. Projetar `MultiTokenAnalysisIR` antes de prometer preservação independente do segundo token em uma única resposta estruturada.

## Critérios para considerar a correção multiword concluída

- todas as análises isoladas do primeiro token permanecem em `result.analyses`;
- as hipóteses compostas válidas continuam presentes;
- compostos inválidos por caso ou número continuam ausentes;
- as quatro regras-fonte homógrafas continuam rastreáveis;
- os resultados dos tokens independentes em `analyze_line` não regridem;
- testes diferenciais que dependem da cardinalidade legada são classificados conscientemente;
- nenhuma correção normativa de `licet` ou da voz perifrástica é misturada a essa mudança estrutural.

## Resultado da primeira implementação

A correção foi aplicada somente em `analyze_compound`, em `src/engine.cpp`. Foram removidos o vetor temporário `source_indices` e a reconstrução destrutiva de `result.analyses`. A função agora percorre as análises independentes, acrescenta os compostos aceitos e retorna se alguma hipótese composta foi criada.

Consequências verificadas:

- `CompoundAnalysisPreservesIndependentFirstTokenAnalyses` passou de vermelho para verde;
- as análises independentes do primeiro token permanecem semanticamente iguais às da consulta isolada;
- seleção de nominativo e concordância de número continuam verdes;
- rejeição de `amatam est` continua verde;
- as quatro proveniências homógrafas continuam distintas;
- a projeção JSON precisou apenas deixar de supor que o composto seria o último item ordenado;
- nenhuma representação do segundo token foi misturada ao vetor associado ao primeiro token;
- `words_differential`, `words_configured_oracle` e `words_aeneid_corpus` passaram;
- a suíte completa passou com 121 testes e zero falhas.

Essa mudança cria uma divergência nativa intencional nas consultas em que o Whitaker oculta análises não participantes: o WordsWASM agora é lossless para o primeiro token e mantém a hipótese composta como informação adicional.

## Comandos de reprodução

Os caminhos abaixo assumem a raiz do repositório como diretório atual:

```bash
printf 'amata est\n' | whitakers-words/bin/words
printf 'amata est\n' | whitakers-words/bin/words_json --configured
./build/words_cli \
  --database whitakers-words/poc/compact-db/output/words-poc-dense.wwdb \
  --dataset-id sha256:0000000000000000000000000000000000000000000000000000000000000000 \
  --format analysis-v2 'amata est'
ctest --test-dir build --output-on-failure
```

Para comparação de compatibilidade, mantenha a entrada enviada ao Whitaker original estritamente em ASCII.
