# Corpus candidato da *Gramática Latina*

## Decisão de fonte

A fonte canônica para extração é `data/ocr/pages.jsonl` do repositório
`gramatica-latina-ocr`, e não o HTML de publicação. O JSONL preserva a unidade
física, página impressa, bloco, estado de revisão, confiança e motivos de
incerteza. O HTML continua útil para leitura humana, mas perde parte dessa
proveniência operacional.

O catálogo [`gramatica_latina_candidates.json`](gramatica_latina_candidates.json)
registra 33 frases curtas provenientes de 15 blocos. Dez já foram promovidas
ao corpus estrutural; 23 permanecem candidatas:

| Suíte sugerida | Frases | Fenômenos principais |
|---|---:|---|
| S0 | 15 | concordância predicativa, comparativos, passiva e cópula |
| S1 | 15 | ACI, infinitivos, relativas, passiva impessoal e duplo dativo |
| S2 | 3 | ablativo absoluto e constituintes não locais |

As entradas sem `annotationStatus` continuam candidatas, não gold. Nenhuma
explicação extensa da obra foi copiada para a fixture; `phenomena` é uma
classificação editorial deste experimento.

## Proveniência e qualidade

- snapshot OCR: `f40f24f5d511d58c7f2fc736a519233933bdf280`;
- hash do PDF-fonte: `52ec2482e66ac1edae85ec73c4fc35048f7d9a6dcab076956a9d237a84e3735d`;
- 5 blocos vêm de registros `complete` e 10 de registros `needs_review`;
- 10 blocos têm confiança `high` e 5 têm confiança `medium`;
- o estado e a confiança pertencem ao bloco OCR, não certificam a futura
  anotação sintática.

O arquivo `RIGHTS.md` da fonte informa que não há decisão de licenciamento para
redistribuição integral. Por isso o catálogo contém somente frases breves e
proveniência, está marcado `redistributionReviewRequired` e não incorpora as
explicações nem o corpus OCR.

## Auditoria realizada

O auditor confirmou que os 33 `sourceText` ocorrem nos blocos declarados,
aceitando apenas diferenças de quantidade vocálica, ligaduras e pontuação:

```sh
python3 parsers_investigation/audit_grammar_source.py \
  parsers_investigation/corpus/gramatica_latina_candidates.json \
  /mnt/projects/Projects/textllm/gramatica-latina-ocr/data/ocr/pages.jsonl \
  --fixtures parsers_investigation/corpus/agreement_fixtures.json
```

Para repetir também o censo morfológico:

```sh
python3 parsers_investigation/audit_grammar_source.py \
  parsers_investigation/corpus/gramatica_latina_candidates.json \
  /mnt/projects/Projects/textllm/gramatica-latina-ocr/data/ocr/pages.jsonl \
  --parser build/parsers/parsers_investigation/parsers_investigation \
  --database dist/words-web/words-search.wwdb
```

No censo morfológico com a WWDB full e search-only, 29/33 frases tiveram todos
os tokens reconhecidos. As quatro frases restantes expõem somente três lacunas
distintas:

| Token da fonte | Frases afetadas | Diagnóstico |
|---|---:|---|
| `intelligentior` | 2 | a WWDB reconhece `intellegentior`, não a grafia da fonte |
| `Catilina` | 1 | nome próprio ausente |
| `Pyrrho` | 1 | nome próprio ausente |

Essas formas permanecem intactas no catálogo. Uma variante de busca pode ser
registrada futuramente, mas não deve substituir silenciosamente o testemunho
da fonte nem transformar ausência lexical em erro do parser sintático.

## Lotes promovidos

Os seis exemplos de concordância predicativa da página 54 foram conferidos no
bloco `s0029-l-b007`, junto à regra explícita do bloco `s0029-l-b006`. As
fixtures registram separadamente:

- o que a fonte afirma: caso, número e gênero do sujeito e do predicativo;
- o que a anotação editorial acrescenta: análise verbal completa e
  heads/labels de dependência;
- snapshot, página, unidade, bloco, texto-fonte e data da revisão.

O auditor verifica reciprocamente `catalogId`/`fixtureId`, proveniência, texto e
presença do gold completo. A frase originalmente sintética `Alumnae sunt
pulchrae` foi substituída pelo testemunho real `Alumnae sunt altae`.

Esse caso expõe uma ambiguidade legítima: `altae` pode ser o adjetivo de
`altus` ou o particípio perfeito passivo de `alo`. A leitura adjetival indicada
pela lição sobrevive e empata no melhor score, mas fica no rank ordinal 2 pelo
desempate estável. O particípio não deve ser eliminado apenas porque o contexto
didático privilegia outra leitura.

O segundo lote promoveu os quatro pares comparativos da página 114, nos
blocos `s0059-l-b009` e `s0059-l-b010`, apoiados pela regra explícita de
`s0059-l-b008`. Eles contrastam:

- segundo termo em ablativo sem `quam`;
- segundo termo no mesmo caso do primeiro, com `quam`.

As formas impressas `intelligentior` permanecem inalteradas. Como a WWDB só
resolve `intellegentior`, as duas fixtures registram um `lookupOverride` com
índice e justificativa. Resultado e schema expõem separadamente tokens de
superfície e de consulta.

A fonte chama `quam` de conjunção comparativa, mas a WWDB também oferece e
ranqueia melhor sua análise adverbial. O gold preserva ambas as categorias e
trata sua função comum de marcador da comparação como `mark`; isso evita
converter uma diferença de tagset em falsa certeza sintática.

## Próxima anotação

Antes de incorporar as frases ao benchmark, cada uma deve receber:

1. alternativas morfológicas aceitas e rejeitadas;
2. relações ou constituintes aceitos;
3. ambiguidades que devem sobreviver;
4. ligação entre a afirmação didática e o bloco de origem;
5. revisão humana para todos os blocos `medium` ou `needs_review` relevantes.

O próximo lote deve acrescentar os dois exemplos de comparação de
inferioridade (`minus intelligens`) e, em paralelo, um caso real/publicado de
ordem livre ou hipérbato para testar não projetividade fora da fixture
sintética.
