# Formato JSON canônico de análise morfológica

Este documento define o contrato lógico da análise latim → inglês do novo
motor do Whitaker's WORDS. Ele serve a três consumidores:

- o *harness* diferencial entre o executável Ada e o motor C++;
- a ABI JSON entre WebAssembly e JavaScript;
- interfaces que precisam dos resultados sem interpretar a apresentação de
  terminal do programa legado.

O schema correspondente está em
[`../../schemas/analysis-v1.schema.json`](../../schemas/analysis-v1.schema.json).
A versão 1 cobre consultas de uma palavra latina, sugestões opcionais para uma
grafia que possa conter duas palavras concatenadas e a gramática fechada de
dois tokens para compostos com `sum`. Pesquisa inglês → latim e análise
contextual de frases terão contratos próprios ou uma versão posterior.

Este é o contrato **completo** de análise: ele inclui forma de citação,
significado, metadados lexicais e morfologia. O perfil de banco sem definições
`search-only` não consegue produzi-lo isoladamente. Respostas enxutas ligadas a
um índice externo usam o contrato separado de
[`formato-json-busca.md`](formato-json-busca.md).

## Princípios

O JSON representa significado, não a disposição dos tipos Ada em memória.
Em particular, não são expostos os arrays de capacidade fixa, posições vazias,
padding, ordinais de enums ou flags usadas somente pela interface textual.

Cada elemento de `analyses` é uma análise morfológica independente. O formato
é deliberadamente plano: se cinco flexões apontam para o mesmo lexema, há cinco
elementos. A pequena repetição simplifica consumo, ordenação e comparação
diferencial.

O campo `dictionaryForm` substitui o nome impreciso `lemma` usado em exemplos
anteriores. O WORDS não armazena um lema único: ele reconstrói uma forma de
citação como `rosa, rosae` ou `rodo, rodere, rosi, rosus`.

## Envelope

```json
{
  "schema": "whitakers-words.analysis",
  "schemaVersion": 1,
  "query": {
    "text": "zzzzzz",
    "normalized": "zzzzzz",
    "mode": "latin"
  },
  "status": "unknown",
  "analyses": [],
  "diagnostics": []
}
```

`query.text` preserva a entrada recebida. `query.normalized` registra a forma
efetivamente entregue ao analisador depois de conversão de caixa e das
normalizações habilitadas. A versão 1 aceita somente `mode = "latin"`.

`status` tem três valores:

| Valor | Significado |
| --- | --- |
| `analyzed` | uma ou mais análises foram encontradas |
| `unknown` | a consulta foi válida, mas nenhuma análise sobreviveu |
| `error` | a consulta não pôde ser analisada |

`unknown` e `error` exigem `analyses` vazio. Um diagnóstico possui um código
estável, severidade e parâmetros escalares opcionais. Texto traduzido para o
usuário não faz parte do resultado canônico. O estado `error` exige ao menos
um diagnóstico de severidade `error`.

O estado `error` descreve uma consulta recebida pela API, mas rejeitada antes
ou durante a análise, por exemplo UTF-8 inválido ou uma entrada que não seja
uma palavra. Falhas que impedem a construção de qualquer documento — falta de
memória, handle inválido ou banco não inicializado — pertencem ao status da ABI,
não a este envelope.

Uma consulta desconhecida pode conter `suggestions` sem mudar para
`analyzed`. O método `two-words` é uma recuperação opt-in de baixa confiança:

```json
"suggestions": [
  {
    "method": "two-words",
    "splitAt": 3,
    "classification": "unconstrained",
    "segments": [
      { "text": "res", "analyses": ["..."] },
      { "text": "publica", "analyses": ["..."] }
    ]
  }
]
```

No documento real, cada array de segmento contém ao menos uma análise completa;
as strings `"..."` acima são apenas abreviações editoriais. `splitAt` conta
letras lógicas NFC, não bytes UTF-8. `classification` é `number-pair` quando
ambos os lados possuem uma leitura numeral e `unconstrained` nos demais casos.
Somente a primeira divisão aceita pelo algoritmo legado é publicada. Ausência
de sugestão omite o campo inteiro.

## Estrutura de uma análise

Uma análise contém quatro blocos e a classe gramatical da forma encontrada:

```json
{
  "partOfSpeech": "noun",
  "lexeme": {
    "dictionary": "general",
    "entryId": 33806,
    "dictionaryForm": "rosa, rosae",
    "partOfSpeech": "noun",
    "meaning": "rose; (also as term of endearment); rose bush; rose oil;",
    "properties": {
      "declension": 1,
      "variant": 1,
      "gender": "feminine",
      "nounKind": "thing"
    },
    "metadata": {
      "age": null,
      "subject": null,
      "geography": null,
      "frequency": "frequent",
      "source": "oxford-latin-dictionary"
    }
  },
  "form": {
    "stem": "ros",
    "stemKey": 2,
    "ending": "ae",
    "rule": {
      "age": null,
      "frequency": "most-frequent"
    }
  },
  "morphology": {
    "declension": 1,
    "variant": 1,
    "case": "genitive",
    "number": "singular",
    "gender": "feminine"
  },
  "derivation": {
    "method": "regular",
    "steps": []
  }
}
```

O `entryId` é o `MNPC` dentro do dicionário indicado. O par
`dictionary + entryId`, e não a forma de dicionário, é a identidade lexical.
Um resultado artificial que não possua registro persistido usa `entryId` igual
a `null`.

`partOfSpeech` no nível da análise descreve a forma encontrada;
`lexeme.partOfSpeech` descreve a entrada do dicionário. Eles podem ser
diferentes: `rosae` pode ser um `participle` ligado ao lexema verbal
`rodo, rodere, rosi, rosus`.

`properties` contém características do lexema. `morphology` contém as
características resolvidas da forma consultada. A repetição aparente de
declinação, variante ou gênero é intencional: uma vem do registro lexical e a
outra é o resultado do cruzamento com a regra de flexão.

## Morfologia por classe gramatical

`partOfSpeech` discrimina o formato fechado de `morphology`:

| Classe da análise | Campos |
| --- | --- |
| `noun`, `pronoun` | `declension`, `variant`, `case`, `number`, `gender` |
| `adjective` | campos nominais + `degree` |
| `numeral` | campos nominais + `numeralType` |
| `verb` | `conjugation`, `variant`, `tense`, `voice`, `mood`, `person`, `number` |
| `participle` | `conjugation`, `variant`, `case`, `number`, `gender`, `tense`, `voice` |
| `supine` | `conjugation`, `variant`, `case`, `number`, `gender` |
| `adverb` | `degree` |
| `preposition` | `governs` |
| `conjunction`, `interjection` | objeto vazio |

Campos aplicáveis mas desconhecidos usam `null`. Campos que não se aplicam à
classe são omitidos. Assim, `{"gender": null}` significa “gênero aplicável,
mas não determinado”; a ausência de `gender` significa “gênero não se aplica”.

Os valores zero usados pelo Ada como curinga ou desconhecido também se tornam
`null`. Os números 1 a 9 permanecem números JSON. `person` aceita 1, 2, 3 ou
`null`.

## Vocabulário dos enums

O contrato usa nomes completos, em inglês, e nunca ordinais da representação
Ada. Os principais vocabulários são:

- caso: `nominative`, `vocative`, `genitive`, `locative`, `dative`,
  `ablative`, `accusative`;
- número: `singular`, `plural`;
- gênero: `masculine`, `feminine`, `neuter`, `common`;
- grau: `positive`, `comparative`, `superlative`;
- tempo: `present`, `imperfect`, `future`, `perfect`, `pluperfect`,
  `future-perfect`;
- voz: `active`, `passive`;
- modo: `indicative`, `subjunctive`, `imperative`, `infinitive`,
  `participle`;
- numeral: `cardinal`, `ordinal`, `distributive`, `adverbial`.

O mesmo princípio vale para `nounKind`, `pronounKind`, `verbKind`, idade,
área, geografia, frequência e fonte. A conversão deve ficar em tabelas
explícitas e testadas; não deve derivar nomes com `Enum'Image` nem gravar o
ordinal do enum.

As classificações lexicais usam estas conversões:

| Enum Ada | Token JSON |
| --- | --- |
| `Noun_Kind`: `S M A G N P T L W` | `singular-only`, `plural-only`, `abstract`, `group`, `proper-name`, `person`, `thing`, `locale`, `place` |
| `Pronoun_Kind`: `PERS REL REFLEX DEMONS INTERR INDEF ADJECT` | `personal`, `relative`, `reflexive`, `demonstrative`, `interrogative`, `indefinite`, `adjectival` |
| `Verb_Kind`: `TO_BE TO_BEING GEN DAT ABL TRANS INTRANS IMPERS DEP SEMIDEP PERFDEF` | `to-be`, `compound-of-to-be`, `governs-genitive`, `governs-dative`, `governs-ablative`, `transitive`, `intransitive`, `impersonal`, `deponent`, `semideponent`, `perfect-definite` |

Área e geografia também têm vocabulários fechados no schema. Para `source`,
os tokens são nomes estáveis das referências, como
`oxford-latin-dictionary`, `lewis-short`, `collatinus` e `whitaker`. Os códigos
Ada `A` e `U` não têm descrição no fonte e são preservados como `source-a` e
`source-u`; inventar um significado seria pior que registrar explicitamente a
lacuna histórica.

`properties` tem um conjunto fechado de campos. Campos relevantes podem ser
omitidos quando não existe registro lexical persistido, como em um numeral
romano artificial; quando emitidos, devem pertencer à classe lexical indicada:

| Classe lexical | Propriedades relevantes |
| --- | --- |
| substantivo | `declension`, `variant`, `gender`, `nounKind` |
| pronome/packon | `declension`, `variant`, `pronounKind` |
| adjetivo | `declension`, `variant`, `degree` |
| numeral | `declension`, `variant`, `numeralType`, `numeralValue` |
| advérbio | `degree` |
| verbo | `conjugation`, `variant`, `verbKind` |
| preposição | `governs` |

O `Pofs = Pack` artificial do Ada é normalizado como lexema `pronoun`; a
operação que o produziu aparece como passo `packon` em `derivation`.

## Segmentação e regra

`form.stem` é o radical do `Parse_Record`; `stemKey` indica qual slot lexical
foi usado; `ending` contém somente os caracteres significativos da desinência.
Uma desinência de tamanho zero é `""`, nunca `null`.

`form.rule` registra a idade e a frequência da regra de flexão. A frequência
de regra usa o vocabulário `most-frequent`, `sometimes`, `uncommon`,
`infrequent`, `rare`, `very-rare` e `inscription`, diferente da frequência de
uma entrada lexical.

Não há `ruleId` no contrato semântico. Duas regras literalmente iguais não
devem produzir identidades públicas diferentes.

## Derivações e comportamento histórico

Uma análise regular usa:

```json
{
  "method": "regular",
  "steps": []
}
```

Uma forma derivada registra as operações na ordem em que foram aplicadas:

```json
{
  "method": "derived",
  "steps": [
    {
      "type": "suffix",
      "text": "ic",
      "meaning": "of, pertaining/belonging to; connected with; derived from"
    }
  ]
}
```

No dataset legado, o caractere de conexão é uma restrição interna usada para
aceitar ou rejeitar o addon; ele não é projetado no JSON canônico. Campos
opcionais sem valor são omitidos, não emitidos como `null`. `connector`
permanece reservado para fontes futuras nas quais essa informação deva fazer
parte do resultado público.

Os tipos de passo são `prefix`, `suffix`, `tackon`, `packon`, `syncope`,
`orthographic`, `compound` e `roman-numeral`. Um passo pode ainda registrar
`before`, `after` e um nome estável em `rule`.

Esse bloco faz parte da identidade da análise. Em `ludica`, os caminhos pelos
sufixos `ic` e `c` não podem ser colapsados, embora parte da saída morfológica
seja igual.

`derivation.method` aceita `regular`, `derived`, `unique`, `syncope`,
`orthographic`, `compound`, `roman-numeral` e `unknown-name`.

Uma consulta de dois tokens pode representar a gramática fechada de compostos
com `sum`. Por exemplo, `amata est` preserva a análise do particípio e produz
uma análise verbal adicional cuja forma é `PPL+est`:

```json
{
  "method": "compound",
  "steps": [
    {
      "type": "compound",
      "text": "est",
      "meaning": "PERF PASSIVE PPL + verb TO_BE => PASSIVE perfect system",
      "rule": "finite-sum"
    }
  ]
}
```

As construções estáveis são `finite-sum`, `esse`, `fuisse` e `iri`. O contrato
não aceita frases gerais: neste corte, mais de dois tokens ou um par que não
forme uma dessas construções retorna `status: error`.

Uma forma sincopada publica a segmentação lexical recuperada e preserva a
reescrita que permitiu encontrá-la. Por exemplo, `amasti` é analisado como
`amav + isti` e usa:

```json
{
  "method": "syncope",
  "steps": [
    {
      "type": "syncope",
      "meaning": "Syncopated perfect often drops the 'v' and contracts vowel",
      "before": "as",
      "after": "avis",
      "rule": "perfect-v-contraction"
    }
  ]
}
```

O executável Ada encontra a mesma análise, mas seu exportador canônico atual
colapsa a proveniência em `method: regular`. O backend nativo não reproduz essa
perda de informação: `derivation` é parte da identidade pública e o JSON de
busca leva os `rewriteIds` correspondentes, na ordem em que as regras foram
aplicadas. Hoje o array tem no máximo dois elementos: ortografia e, quando a
forma recuperada ainda exige isso, síncope.

Numerais romanos não recebem um verbete sintético. Eles usam dicionário
`roman-numeral`, `entryId: null`, propriedades e metadados vazios e um passo
final `roman-numeral`. O reconhecedor conserva o dialeto histórico do Ada:
formas aditivas como `IIII` são aceitas; sequências compostas apenas por
dígitos romanos mas rejeitadas pela gramática estrita, como `IIV`, só aparecem
como fallback de menor frequência quando nenhum caminho lexical venceu. Um
homógrafo válido, como `mi`, conserva simultaneamente as análises lexicais e o
valor romano.

## Exemplo real: `rosae`

Com os dados desta cópia, o executável Ada produz nove análises:

- quatro particípios perfeitos passivos femininos do verbo `rodo`, com
  genitivo singular, dativo singular, nominativo plural e vocativo plural;
- cinco formas do substantivo `rosa`: genitivo singular, locativo singular,
  dativo singular, nominativo plural e vocativo plural.

O exemplo antigo com somente duas formas não é um resultado canônico completo.
O array deve conservar as nove análises; não há seleção contextual da mais
provável.

Um dos elementos participiais tem esta forma:

```json
{
  "partOfSpeech": "participle",
  "lexeme": {
    "dictionary": "general",
    "entryId": 33779,
    "dictionaryForm": "rodo, rodere, rosi, rosus",
    "partOfSpeech": "verb",
    "meaning": "gnaw, peck;",
    "properties": {
      "conjugation": 3,
      "variant": 1,
      "verbKind": null
    },
    "metadata": {
      "age": null,
      "subject": null,
      "geography": null,
      "frequency": "lesser",
      "source": null
    }
  },
  "form": {
    "stem": "ros",
    "stemKey": 4,
    "ending": "ae",
    "rule": {
      "age": null,
      "frequency": "most-frequent"
    }
  },
  "morphology": {
    "conjugation": 3,
    "variant": 1,
    "case": "genitive",
    "number": "singular",
    "gender": "feminine",
    "tense": "perfect",
    "voice": "passive"
  },
  "derivation": {
    "method": "regular",
    "steps": []
  }
}
```

## Forma desconhecida

```json
{
  "schema": "whitakers-words.analysis",
  "schemaVersion": 1,
  "query": {
    "text": "zzzzzz",
    "normalized": "zzzzzz",
    "mode": "latin"
  },
  "status": "unknown",
  "analyses": [],
  "diagnostics": [
    {
      "code": "unknown-word",
      "severity": "info",
      "parameters": {}
    }
  ]
}
```

## Ordenação e equivalência

Antes da serialização, `analyses` deve ser ordenado pela tupla:

```text
dictionary
entryId (null antes dos números)
partOfSpeech
form.stemKey
form.stem
form.ending
morphology em ordem de campos do schema
derivation.method
derivation.steps em sua ordem original
```

A comparação diferencial deve:

1. validar os dois documentos contra o schema;
2. normalizar strings segundo as regras abaixo;
3. aplicar a ordenação canônica;
4. comparar cada valor, inclusive a multiplicidade dos elementos.

Não se deve converter `analyses` em conjunto simples. Um multiconjunto conserva
duplicatas observáveis e, principalmente, caminhos de derivação diferentes.

O contrato exige UTF-8. Espaços de preenchimento são removidos à direita,
finais de linha internos são `\n` e não há espaços no início ou fim de nomes
de enum. `query.text` preserva exatamente a entrada da API. A engine C++ já
valida seu alfabeto de entrada e publica `query.normalized` em NFC; o exportador
Ada atual não implementa NFC e os dados históricos continuam essencialmente
ASCII. O WWDB 1.7 já possui máscaras capazes de confirmar, contradizer ou
deixar desconhecida a quantidade de uma análise. O resultado `QuantityMatch`
permanece deliberadamente interno no JSON v1; publicá-lo exige uma revisão do
contrato, não apenas uma mudança de implementação. A semântica de mácrons,
breves, forma de lookup ASCII e evolução deste contrato está registrada em
[`plano-unicode-quantidade-vocalica.md`](plano-unicode-quantidade-vocalica.md).

Para testes, é preferível comparar os valores JSON já interpretados. O termo
"canônico" neste documento descreve primeiro o modelo semântico e a ordem dos
arrays; a ordem textual das propriedades emitida pelo exportador Ada não é
JCS. Quando forem necessários bytes ou hashes reproduzíveis, uma etapa posterior
deve aplicar o JSON Canonicalization Scheme (JCS, RFC 8785). JCS ordena
propriedades, mas não arrays; por isso `analyses` precisa ser ordenado antes.

## Mapeamento do Ada

O núcleo da conversão é:

| Ada | JSON |
| --- | --- |
| `Parse_Record.D_K + MNPC` | `lexeme.dictionary + entryId` |
| `Dictionary_Entry` | `lexeme` |
| `Parse_Record.Stem` | `form.stem` |
| `Inflection_Record.Key` | `form.stemKey` |
| `Inflection_Record.Ending` | `form.ending` |
| `Inflection_Record.Age/Freq` | `form.rule` |
| `Quality_Record.Pofs` | `partOfSpeech` e variante de `morphology` |
| `Part_Entry` | `lexeme.partOfSpeech` e `properties` |
| `Translation_Record` | `lexeme.metadata` |
| `Explanations` e addons | `derivation` |

`Word_Analysis` é privado e ainda agrupa flexões e entradas em arrays fixos.
O exportador Ada fica em um pacote filho de `Words_Engine.List_Package`, onde
esses campos são visíveis. Analisar a saída de terminal deve ser apenas um
*bootstrap*: ela perde identidades e mistura formatação com conteúdo.

## Exportador Ada

O exportador está implementado no pacote filho
[`Words_Engine.List_Package.Canonical_JSON`](../src/words_engine/words_engine-list_package-canonical_json.ads).
Por ser filho de `List_Package`, ele enxerga a parte privada de
`Word_Analysis` sem torná-la pública. `Serialize` devolve uma string JSON e
`Put` grava um documento seguido de uma quebra de linha. As duas operações têm
uma sobrecarga que recebe o texto original da consulta, preservando
`query.text` mesmo quando o analisador aplica normalizações como `QV` → `QU`.

O executável [`words_json`](../src/commands/words_json.adb) inicializa o mesmo
motor e banco de dados usados pelo programa legado e aceita uma consulta em um
único argumento, inclusive um composto quoted de dois tokens:

```sh
make words_json
bin/words_json rosae
bin/words_json "amata est"
```

Esse executável é um *harness* de referência, não a futura ABI C. Erros de
invocação ou falhas anteriores à criação de `Word_Analysis` são informados em
`stderr` e por status de processo; por isso ele ainda não exercita o envelope
`status = "error"`. A ABI C++ deve validar que recebeu uma palavra ou um par
aceito pela gramática de compostos e produzir o envelope de erro quando ainda
for possível serializá-lo.

Para que a mesma consulta e a mesma base sempre gerem o mesmo documento,
`words_json` usa `Initialize_Canonical_Engine` e ignora os arquivos de
preferências interativas `WORD.MOD` e `WORD.MDV`. O perfil canônico:

- desabilita corte e omissão de análises arcaicas, medievais ou incomuns;
- habilita prefixos, sufixos, *tackons*, síncope e truques ortográficos;
- limita o resultado ao primeiro item, mas habilita o lookahead fechado de
  compostos com `sum` que pode consumir o segundo token;
- preserva a política histórica de tentar derivações somente quando a busca
  regular falha e não habilita conversões opcionais de `i/j` ou `u/v`;
- desabilita arquivos de saída, estatísticas e atualizações do dicionário.

Esse perfil faz parte do comportamento da versão 1. Uma futura API que permita
escolher opções deve registrá-las no envelope e versionar qualquer alteração
semântica correspondente.

O exportador percorre `Dict` e `Stem_IAA`, materializa um item para cada
flexão, associa registros de addon precedentes como passos de derivação e
ordena os itens antes de serializar. Ele não captura nem interpreta a saída de
terminal.

Os testes de regressão específicos podem ser executados com:

```sh
make test-json
```

Eles cobrem `rosae`, forma desconhecida, as duas derivações de `ludica`, o
sufixo diminutivo de `anaticulus`, numeral romano, todas as classes gramaticais
e a independência em relação a `WORD.MOD` e `WORD.MDV`. Quando o módulo Python
`jsonschema` está disponível, cada documento também é validado contra o schema
Draft 2020-12. Testes negativos confirmam que chaves de radical fora de `1..4`
e propriedades incompatíveis com a classe lexical são rejeitadas.

## Versionamento

Como o schema usa propriedades e enums fechados, acrescentar um campo ou valor
que possa aparecer na saída exige nova versão. Correções editoriais que não
alterem os documentos aceitos podem manter `schemaVersion = 1`. Renomear
campos, mudar significado, alterar `null` versus ausência ou trocar a identidade
lexical também exige nova versão.

O hash ou a versão do banco de dados deve acompanhar o relatório do *harness*,
mas não entra neste documento canônico: o Ada e o C++ usam representações de
banco diferentes e ainda assim precisam produzir o mesmo resultado semântico.
