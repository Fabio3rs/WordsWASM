# Fronteira morfológica para parsers futuros

## Escopo

O WordsWASM continua sendo somente um engine lexical e morfológico. Não há
GLR, LR, CFG, gerador de parser, GSS, SPPF, regras sintáticas, concordância
contextual, ranking nem AST sintática implementados neste trabalho.

A fronteira preparada é:

```text
texto latino
  -> QueryResult
  -> zero ou mais AnalysisIR independentes
  -> Morphology tipada + LexemeRecord + InflectionRule + DerivationIR
  -> consumidor futuro, sem dependência do layout WWDB
```

Ambiguidade é parte do resultado. `QueryResult::analyses` não escolhe um lema,
caso ou POS preferido. Por exemplo, as leituras genitivo singular, dativo
singular e nominativo plural de `puellae` permanecem candidatos separados.

## Resultado da auditoria end-to-end

A auditoria comparou os tipos Ada e os arquivos legados empacotados neste
snapshot (`DICTFILE.GEN`/`DICTLINE.GEN`, `INFLECTS.SEC`/`INFLECTS.LAT`,
`ADDONS.LAT`, `UNIQUES.LAT`, `REWRITES.LAT` e `QUANTITIES.LAT`) com o packer,
WWDB 1.8, loader, core, JSON e Embind. O repositório contém apenas o dicionário
`GEN`; não há `DICTFILE.SPE` ou `DICTFILE.LOC` a preservar neste dataset.

| Informação no WORDS | Representação legada | WWDB 1.8 | C++ público | JSON/WASM | Estado após a auditoria |
| --- | --- | --- | --- | --- | --- |
| identidade lexical | dicionário + posição/MNPC | `LexemeId`, perfil e ordinal | `LexemeId`, `dictionary`, `dictionary_entry` | browser: `lexemeId`, `dictionary`, `entryId`; JSON search: `lexemeId` | preservada |
| quatro radicais e forma de citação | `Dictionary_Entry.Stems` | quatro `StringId` | `LexemeRecord::stems`, `citation_lemma` | `lemma`/`dictionaryForm` | preservada; lema é projeção, IDs/radicais são autoridade |
| meaning editorial | `Meaning_Type` | pool somente full | `meaning()` quando full | somente contratos full | intencionalmente ausente no search |
| POS lexical | `Part_Of_Speech_Type` | campo de 4 bits | `PartOfSpeech` | token canônico | preservada e tipada |
| declinação/conjugação e variante | `Which_Type`, `Variant_Type` 0..9 | byte de paradigma | inteiros validados e nomeados por contexto | número ou `null` | preservada; são índices numéricos, não enums artificiais |
| gênero lexical do nome | `Gender_Type` | payload de classe | `Gender` | token canônico | preservada e tipada |
| tipo de nome | `Noun_Kind_Type` | payload de classe | `NounKind` | `nounKind` | antes byte cru; agora tipada |
| tipo de pronome | `Pronoun_Kind_Type` | payload de classe | `PronounKind` | `pronounKind` | preservada e tipada |
| packon exigido pelo lexema `PACK` | convenção histórica `(w/-...)` no registro lexical | `AddonId` opcional tipado | `required_packon` | browser: `requiredPackonId` | antes extraído pelo packer do meaning; agora materializado em `PACKON_REQUIREMENTS.LAT` e auditado contra o legado |
| grau lexical de adjetivo/advérbio | `Comparison_Type` | payload de classe | `Degree` | `degree` | preservada e tipada |
| tipo e valor numeral | `Numeral_Sort_Type`, `Numeral_Value_Type` | payload de classe | `NumeralType`, inteiro 0..1000 | `numeralType`, `numeralValue` | preservada |
| classificação verbal | `Verb_Kind_Type` | payload de classe | `VerbKind` | `verbKind` em JSON/browser | preservada end-to-end; não derivada da tradução |
| regência preposicional | `Preposition_Entry.Of_Case` | payload de classe | `GrammaticalCase governs` | `governs` | preservada e tipada |
| idade lexical/de regra | `Age_Type` | payload lexical e da regra separados | `Age` em ambos | `age` em blocos separados | antes byte cru; agora tipada |
| área temática | `Area_Type` | payload lexical | `SubjectArea` | `subject` | antes byte cru; agora tipada |
| geografia | `Geo_Type` | payload lexical | `Geography` | `geography` | antes byte cru; agora tipada |
| frequência lexical | `Frequency_Type` com semântica de dicionário | payload lexical | `LexicalFrequency` | `lexical.frequency` | antes byte cru; agora tipo próprio |
| frequência flexional | os mesmos códigos com vocabulário de regra | payload da regra | `RuleFrequency` | `rule.frequency` | antes byte cru; agora tipo distinto da frequência lexical |
| fonte | `Source_Type` | payload lexical | `Source` | `source` | antes byte cru; agora tipada; códigos A/U continuam sem significado inventado |
| regra flexional e metadata | `Inflection_Record` | `RuleId`, morfologia, ending, stem key, age/frequency | `InflectionRule`, `rule()`/`rules()` | browser: regra resolvida e ID; JSON search: `ruleId` | preservada |
| caso, número e gênero da forma | records nominais de `Inflection_Record` | payload morfológico | `GrammaticalCase`, `GrammaticalNumber`, `Gender` | tokens canônicos | preservada e tipada |
| tempo, voz, modo e pessoa | `Verb_Record`/`Vpar_Record` | payload morfológico | `Tense`, `Voice`, `Mood`, `Person` | tokens e pessoa 1..3 | pessoa era byte cru; agora tipada |
| particípio | `Vpar_Record` | paradigma + atributos nominais e verbais | `ParticipleMorphology` | variant discriminada | preserva simultaneamente caso/número/gênero e tempo/voz |
| supino | `Supine_Record` | paradigma + caso/número/gênero | `SupineMorphology` | variant discriminada | preserva exatamente os campos legados, sem propriedades inventadas |
| formas únicas | `UNIQUES.LAT` | lexema + morfologia completa, sem regra fictícia | `rule == nullopt`, `UniqueReference` | regra `null` | preservada |
| prefixos e tickons | `Prefix_Item` e posição na família | `AddonId`, família, forma, root/target, connector | `PrefixRule`, `AddonKind`, accessors públicos | browser distingue `prefix`/`tickon`, ID e texto | preservada |
| sufixos | `Suffix_Item`/`Target_Entry` | `AddonId` + target tipado | `SuffixRule` | ID, tipo e texto na derivação browser | preservada; atributo cru redundante foi removido do core |
| tackons e packons | `Tackon_Item`; separação histórica de arrays | `AddonId`, `packon`, `enclitic` | `TackonRule`, `AddonKind` | ID, tipo, texto e `enclitic` | preservada; packon é classificado pelos campos `PACK`, não pelo meaning |
| ordem/posição de addons | marcadores ordenados no `Parse_Array` | IDs globais estáveis | `DerivationIR::steps()` | array ordenado e `target` | preservada |
| síncope e ortografia | tabelas Ada projetadas em `REWRITES.LAT` | `RewriteId` e micro-regra tipada | `RewriteRule`, `RewrittenFormIR` | ID/nome/before/after no browser; IDs no JSON search | preservada sem descrição opaca |
| quantidade vocálica | `QUANTITIES.LAT` | máscaras por regra e lexema/slot | `QuantityMask`, `QuantityMatch` | `quantityMatch` | preservada |

O JSON canônico v1 continua sendo um formato de apresentação compatível com o
oracle Ada e deliberadamente não publica todas as identidades densas. O JSON
compacto de search publica `lexemeId`, `ruleId`, `addonIds` e `rewriteIds`. O
contrato browser v3 reúne identidade, morfologia resolvida, propriedades
lexicais e derivação. Um parser C++ deve consumir a API nativa, não combinar
documentos JSON de apresentação.

## Tipos e distinção lexical versus flexional

`LexemeRecord` descreve a entrada de dicionário: POS lexical, paradigma,
gênero lexical, `NounKind`, `PronounKind`, grau lexical, numeral,
`VerbKind`, regência preposicional, packon exigido e metadata.

`AnalysisIR::morphology` descreve a forma reconhecida. É uma `std::variant`
entre `NounMorphology`, `PronounMorphology`, `AdjectiveMorphology`,
`NumeralMorphology`, `AdverbMorphology`, `VerbMorphology`,
`ParticipleMorphology`, `SupineMorphology`, `PrepositionMorphology` e
`InvariableMorphology`. Campos não aplicáveis não existem naquela variant;
valores aplicáveis mas desconhecidos usam o membro `unknown` do domínio.
Ausência real, como a falta de regra flexional em `UNIQUES`, usa `optional`.

Um particípio ou supino mantém o `LexemeId` do verbo de origem. Assim o
consumidor observa a categoria da forma pela variant e ainda consulta
`lexeme.verb_kind` na entrada verbal.

`Person` virou enum fechado porque o legado aceita somente 0..3. Declinação,
conjugação, variante e stem key permaneceram inteiros compactos: no WORDS são
índices numéricos de paradigma, não enumerações com significados independentes.
Os nomes dos campos e das structs impedem confundir seu contexto sem fabricar
um catálogo semântico inexistente.

## Consumo C++ parser-agnostic

```cpp
for (const words::AnalysisIR &candidate : result.analyses) {
    std::visit(overloaded{
        [](const words::NounMorphology &noun) {
            use(noun.grammatical_case, noun.number, noun.gender);
        },
        [](const words::VerbMorphology &verb) {
            use(verb.tense, verb.voice, verb.mood,
                verb.person, verb.number);
        },
        [](const auto &) {}
    }, candidate.morphology);

    const words::LexemeRecord &lexeme = db.lexeme(candidate.lexeme);
    if (lexeme.verb_kind == words::VerbKind::governs_dative) {
        use(words::governed_case(lexeme.verb_kind));
    }
    if (candidate.rule) {
        use(db.rule(*candidate.rule));
    }
    for (const words::AddonId id : candidate.derivation.steps()) {
        use(id, db.addon_kind(id));
    }
}
```

Não é necessário conhecer offsets, bitfields ou seções WWDB. `lexemes()`,
`rules()`, `suffixes()`, `prefixes()`, `tackons()` e `rewrites()` permitem
auditoria pública do snapshot inteiro; os accessors por ID são o caminho
normal por análise e não copiam registros.

`governed_case(VerbKind)` é uma projeção opcional e lossless apenas para
`governs_genitive`, `governs_dative` e `governs_ablative`. O `VerbKind`
original continua disponível. Em particular, o core não equipara
`transitive` a acusativo.

## Addons e provenance

`DerivationIR` conserva até um prefixo, um sufixo e um tackon/packon na ordem
real do caminho legado. Cada `AddonId` resolve publicamente para `AddonKind` e
para `PrefixRule`, `SuffixRule` ou `TackonRule`. `TackonRule::enclitic` permite
reconhecer `-que` sem testar `ends_with("que")`. O browser expõe `id`, `type`,
`text`, `enclitic` e o alvo do passo. Isso é evidência morfológica; nenhuma
semântica de coordenação foi adicionada.

`RewrittenFormIR` intercala os `RewriteId` com addons usando
`leading_addon_count` e guarda radical/desinência reconstruídos. Cada ID
resolve para tipo, operação, escopo, estágio, restrições, before/after e nome
estável. Síncope e reescrita ortográfica permanecem provenance, não strings
que o consumidor precise interpretar.

## Perfil search-only

Full e search compartilham todos os 39.339 lexemas, 1.785 regras, 343 addons,
76 uniques, 170 rewrites, IDs, metadata e quantidades. Um teste percorre todos
esses registros e compara cada propriedade gramatical, além das máscaras de
quantidade. Search omite somente `meaning_id` e os pools de meanings.

O packer recompilado produziu exatamente os mesmos arquivos do snapshot:

| Perfil | Bytes | SHA-256 |
| --- | ---: | --- |
| dense/full | 2.731.900 | `6e3b1b4beb8c4fcbe746fd3a4f969170447cb9c5c7afb4a2e6626620798e0529` |
| search-only | 1.200.341 | `0f69f7d8e7e8510152fe5b46ad5eff080dda7b37a49d3043e2dbf076695034d5` |

Não houve mudança de versão, layout ou tamanho do WWDB. O novo ledger entra no
`datasetId`, pois altera a autoridade estrutural de geração mesmo quando os
bytes resultantes continuam iguais.

## Limitações do dataset

- `VerbKind` é uma classificação única e limitada por lexema. Não representa
  múltiplas regências por sentido, objeto direto + indireto, infinitivos,
  orações `ut/ne`, ACI, papéis semânticos nem frames completos de valência.
- O snapshot atual possui entradas reais para todos os `VerbKind` exceto
  `governs_genitive`; o enum e `governed_case()` representam `GEN` porque o
  tipo Ada o define, mas nenhum fixture fictício foi criado.
- Os nomes `source-a` e `source-u` preservam códigos cuja bibliografia não é
  explicada no fonte legado.
- A distinção `enclitic` não era um campo Ada: o loader histórico distinguia o
  primeiro bloco ordenado de tackons. O WWDB materializa essa fronteira; o
  comentário estrutural e a ordem de `ADDONS.LAT` continuam sendo a autoridade.
- O requerido de lexemas `PACK` existia apenas na convenção `(w/-...)`. O
  ledger tipado a preserva e um teste de auditoria impede drift, mas isso não
  transforma o WORDS original em um modelo geral de formação pronominal.
- Não há informação sintática contextual, valência enriquecida, probabilidade
  ou preferência de análise no WWDB atual.

Essas limitações pertencem aos dados, não devem ser preenchidas analisando
glossas inglesas e não autorizam disambiguation prematura.

## Invariante final

O core expõe as distinções morfológicas e lexicais disponíveis neste dataset
por APIs C++ tipadas e neutras, preserva candidatos ambíguos e permite que um
parser futuro opere com o perfil search sem meanings. Nenhum parser sintático
foi implementado.
