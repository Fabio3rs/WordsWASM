# Formato JSON enxuto de busca

Este documento define a resposta compacta usada com o perfil `search-only` do
banco WWDB. Ela identifica resultados dentro de uma build do conjunto de dados,
mas não transporta forma de citação, significado, metadados editoriais nem a
morfologia completa.

Este é o backend textual do CLI e dos testes de aceitação. A API WebAssembly
não serializa esse JSON: `search()` retorna structs Embind versão 2 que resolvem
lema, classe, morfologia e flags diretamente, ainda sem definições. Assim o
contrato enxuto persistível por IDs permanece disponível sem impor JSON à
engine ou ao navegador.

O schema correspondente está em
[`../../schemas/search-v1.schema.json`](../../schemas/search-v1.schema.json).
Uma interface que precise apresentar ou comparar a análise completa deve usar
[`formato-json-canonico.md`](formato-json-canonico.md) e carregar o banco com
definições ou realizar a junção com o índice editorial.

## Envelope

```json
{
  "schema": "whitakers-words.search",
  "schemaVersion": 1,
  "datasetId": "sha256:0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef",
  "query": {
    "text": "rosae",
    "normalized": "rosae",
    "mode": "latin"
  },
  "status": "analyzed",
  "hits": [
    {
      "lexemeId": 33805,
      "ruleId": 1727,
      "addonIds": [],
      "scoreFlags": 0
    }
  ],
  "diagnostics": []
}
```

O hash do exemplo é ilustrativo. `datasetId` identifica exatamente a build que
atribuiu os IDs densos. Na versão 1 ele é `sha256:` seguido pelo SHA-256 de um
manifesto canônico que registra fontes, versão do packer e atribuição de IDs. O
valor não é o hash do WWDB que o contém, o que criaria uma autorreferência. O
índice externo, os perfis WWDB relacionados e qualquer resposta persistida
devem usar o mesmo valor. Resultados de datasets diferentes não podem ser
misturados.

`status` segue as mesmas regras do contrato completo:

- `analyzed` exige ao menos um elemento em `hits`;
- `unknown` e `error` exigem `hits` vazio;
- `error` exige ao menos um diagnóstico de severidade `error`.

Quando a recuperação opt-in `Two_Words` encontra uma divisão, `status`
continua `unknown` e `hits` continua vazio. O campo opcional `suggestions`
mantém os dois lados agrupados; cada segmento contém `text` e seu próprio array
de hits enxutos. `splitAt` é um índice lógico NFC, `classification` vale
`number-pair` ou `unconstrained`, e a sugestão vem acompanhada do diagnóstico
`two-words-suggestion`. Assim uma segmentação especulativa não é promovida a
hit lexical confirmado.

## Identidade de um hit

Os IDs são ordinais densos, baseados em zero e locais ao `datasetId`:

- `lexemeId` aponta para o vetor lexical; é `null` somente para uma análise
  artificial que não possua registro persistido;
- `ruleId` aponta para a regra de flexão ou é `null` quando não houver regra;
- `addonIds` registra prefixos, sufixos, *tackons* e outras operações na ordem
  em que foram aplicadas;
- `rewriteIds`, quando presente, contém de um a dois IDs das regras tipadas que
  recuperaram a forma, na ordem de aplicação. O limite representa os caminhos
  legados suportados: uma correção ortográfica pode ser seguida por uma
  síncope, sem recursividade geral;
- `compound`, quando presente, registra a construção fechada e a forma
  auxiliar. `ruleId` continua apontando para a flexão do particípio ou supino
  que licenciou o composto;
- `scoreFlags` está reservado e deve ser zero na versão 1; bits futuros exigem
  documentação e nova versão do contrato.

Resultados artificiais incluem ainda o discriminador `artificial`. Numerais
romanos carregam o valor já calculado e se a grafia passou pelo reconhecedor
estrito do WORDS:

```json
{
  "lexemeId": null,
  "ruleId": null,
  "addonIds": [],
  "scoreFlags": 0,
  "artificial": {
    "method": "roman-numeral",
    "value": 4,
    "wellFormed": true
  }
}
```

Esse ramo explícito evita IDs sentinela e permite que o perfil `search-only`
represente a análise sem depender do vetor lexical. Se um enclítico tiver sido
removido antes do reconhecimento, seu ID continua em `addonIds`.

Um composto continua sendo lexical e não usa o ramo `artificial`:

```json
{
  "lexemeId": 2870,
  "ruleId": 108,
  "addonIds": [],
  "scoreFlags": 0,
  "compound": {
    "construction": "finite-sum",
    "auxiliary": "est"
  }
}
```

A tupla completa faz parte da identidade observável do hit. Dois caminhos de
derivação para o mesmo lexema não devem ser colapsados.

## Ordenação

Antes da serialização, `hits` é ordenado por:

```text
lexemeId (null depois dos IDs lexicais)
ruleId (null antes dos números)
addonIds em ordem lexicográfica, preservando a ordem interna do caminho
rewriteIds (ausente antes dos arrays; arrays em ordem lexicográfica)
compound.construction e compound.auxiliary (ausente antes dos compostos)
scoreFlags (sempre zero na versão 1)
resultados artificiais depois dos lexicais
```

Diagnósticos são ordenados por `severity`, `code` e pela representação
canônica de `parameters`. Para comparação, os valores JSON interpretados são a
referência. JCS pode ser aplicado posteriormente quando forem necessários bytes
reproduzíveis.

## Relação com o JSON completo

Este contrato não é uma projeção autossuficiente do JSON completo. A conversão
exige resolver os IDs no WWDB e, para significados e conteúdo editorial, no
índice externo com o mesmo `datasetId`.

A ABI pode, portanto, expor duas operações independentes:

```cpp
std::uint32_t ww_analyze_json(const char* text, std::uint32_t size);
std::uint32_t ww_search_json(const char* text, std::uint32_t size);
```

`ww_analyze_json` produz `whitakers-words.analysis` e requer os dados completos.
`ww_search_json` produz `whitakers-words.search` e funciona com `search-only`.
Os handles de resultado podem compartilhar as mesmas funções de leitura e
liberação da ABI.

O runtime nativo projeta todas as classes semânticas, addons, formas únicas,
síncope, compostos e resultados artificiais a partir da mesma IR usada pelo
JSON completo. Os testes validam ambos os envelopes contra seus schemas e
contra fixtures do executável Ada.

## Fora de escopo

A versão 1 não inclui snippets, títulos, URLs, traduções nem pontuação editorial.
Esses valores pertencem ao índice do site. Também não promete estabilidade de
IDs entre builds: essa relação é garantida pelo `datasetId`, não pelo número
isolado.
