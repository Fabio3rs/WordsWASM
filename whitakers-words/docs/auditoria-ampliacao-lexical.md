# Auditoria de ampliação lexical

Data do corte: 2026-08-29.

Esta auditoria mede quanto os dicionários locais podem ampliar o banco lexical
do Words sem confundir ausência estrutural com um novo lexema confirmado. O
instrumento é `poc/compact-db/audit_lexical_expansion.py`; ele é somente leitura
e não altera `DICTFILE.GEN`, o WWDB nem os dicionários externos.

## O que a contagem significa

A unidade é o grupo `(lema ASCII, classe gramatical, próprio/comum)`, não uma
entrada final do banco. Quantidades vocálicas são preservadas como evidência,
mas não dividem automaticamente homógrafos. Cada grupo é comparado somente ao
radical de citação (`slot 1`) do Words, exigindo prefixo, terminação de citação,
POS, gênero compatível e a mesma condição próprio/comum.

Um grupo `structurally_unmatched` é, portanto, candidato de importação. Ainda
pode ser variante ortográfica, forma flexionada apresentada como lema, sentido
novo de uma entrada existente ou falso negativo causado por paradigma ainda
genérico. Não se deve convertê-lo diretamente em registro canônico.

Entradas sem POS são anexadas a um grupo somente quando o mesmo lema possui
exatamente uma classe conhecida em outra fonte. Se houver zero ou várias
classes possíveis, elas permanecem fora da contagem tipada. Collatinus é
testemunho morfológico derivado e não constitui voto independente.

## Núcleo lexicográfico

O primeiro corte usa Lewis & Short, Gaffiot e o léxico Latim–Alemão como
famílias independentes, mais o Collatinus-base como fonte derivada.

| Medida | Contagem |
| --- | ---: |
| registros externos lidos | 128.148 |
| cabeçalhos simples | 115.145 |
| grupos tipados comparáveis | 49.661 |
| grupos já cobertos estruturalmente | 24.545 |
| grupos estruturalmente ausentes | 25.116 |
| ausentes corroborados por 2+ famílias independentes | 5.730 |
| — comuns | 4.237 |
| — nomes próprios | 1.493 |
| comuns com POS explícito em 2+ famílias independentes | 2.030 |
| — e com testemunho em léxico morfológico | 2.016 |
| ausentes com uma família independente | 16.318 |
| ausentes apenas no Collatinus derivado | 3.068 |

Dos 4.237 candidatos comuns corroborados, 3.199 também possuem testemunho em
um léxico morfológico — Collatinus ou Latim–Alemão. Como parte da concordância
pode vir de uma fonte cujo POS foi anexado pela regra de classe única, a fila
mais estrita contém 2.030 grupos com POS explícito em duas famílias
independentes; 2.016 deles têm também testemunho morfológico. “Testemunho
morfológico” ainda não significa que o modelo externo já tenha tradução
automática para declinação/conjugação do Words.

O `DICTFILE.GEN` deste corte possui 39.339 registros. A fila comum corroborada
equivale a cerca de 10,8% dessa contagem; a fila estrita, a 5,2%. Isso não é
uma previsão direta de crescimento: um grupo pode virar zero, uma ou várias
entradas após separar variantes e sentidos.

## Efeito do Faria v3 quality

O segundo corte acrescenta somente entradas `lexical_entry` e `publishable` do
Faria v3 quality. Classes compostas como particípio/forma verbal permanecem sem
POS importável nesta fase. O Faria é uma quarta família independente, porém é
mantido como camada revisada por OCR/LLM, separada das obras estabelecidas.

| Medida | Núcleo | Com Faria v3 | Diferença |
| --- | ---: | ---: | ---: |
| grupos tipados | 49.661 | 54.852 | +5.191 |
| já cobertos | 24.545 | 25.284 | +739 |
| estruturalmente ausentes | 25.116 | 29.568 | +4.452 |
| ausentes corroborados | 5.730 | 9.827 | +4.097 |
| corroborados comuns | 4.237 | 5.821 | +1.584 |
| corroborados próprios | 1.493 | 4.006 | +2.513 |
| comuns corroborados com testemunho morfológico | 3.199 | 4.054 | +855 |
| comuns com POS explícito em 2+ famílias | 2.030 | 3.604 | +1.574 |
| — e com testemunho morfológico | 2.016 | 3.211 | +1.195 |

O ganho de nomes próprios é grande e deve permanecer numa fila própria. Para o
léxico comum, o Faria eleva a fila corroborada de 4.237 para 5.821 grupos. A
fila inicial mais conservadora contém 3.211 grupos: comuns, POS explicitamente
confirmado por duas famílias independentes e presença em Collatinus ou no
léxico Latim–Alemão, onde há uma pista de paradigma.

## Paradigmas e radicais reconstruídos

O auditor agora aprende uma tabela de correspondência somente a partir de
verbetes externos que já encontram uma entrada Words. Cada código
`modeles.la`/`typnr` é comparado com `(POS, declinação ou conjugação, variante)`
do `DICTFILE.GEN`. Um mapa automático exige pelo menos três testemunhos e um
único paradigma observado; maioria estatística não basta.

| Correspondência de paradigma | Códigos |
| --- | ---: |
| exata, com pelo menos três testemunhos | 32 |
| um único paradigma, mas suporte insuficiente | 24 |
| um código para vários paradigmas Words | 84 |
| sem testemunho já coberto | 52 |

Os 84 casos um-para-muitos mostram que `modeles.la` e `typnr` são em geral
menos granulares que as variantes do Words. Mesmo modelos com uma maioria
muito forte não são convertidos silenciosamente em regra exata.

Para Collatinus, o auditor também resolve herança e regras `R:` de formação de
radicais. Em entradas Words já cobertas, ele aprende qual radical Collatinus
alimenta cada slot Words. Dos 64 modelos com um molde de slots completo, 31
têm um único molde sustentado por pelo menos três verbetes, 17 ainda têm pouco
suporte e 16 são um-para-muitos.

Por fim, os radicais propostos são confrontados com `INFLECTS.SEC` e com as
formas da tabela `FORM` do léxico Latim–Alemão. A validação exige que cada slot
produza pelo menos uma forma realmente registrada usando uma terminação
compatível com o paradigma Words proposto.

| Fila estrutural | Núcleo | Com Faria v3 |
| --- | ---: | ---: |
| candidatos estritos com paradigma empírico | 70 | 96 |
| rascunhos com paradigma e todos os radicais | 66 | 80 |
| — todos os slots confirmados pelo Latim–Alemão | 65 | 77 |
| — estrutura empírica sem essa terceira validação | 1 | 3 |

Os 66 rascunhos do núcleo são 64 adjetivos, um advérbio e um substantivo. O
substantivo tem gênero não ambíguo. O arquivo opcional `--draft-output` contém
esses registros sob o schema `whitakers-words.lexeme-structural-draft.v1`.
Eles ainda declaram como pendentes identidade de sentido, definição canônica,
metadados editoriais e máscara de quantidade. Portanto são rascunhos para
revisão, não entradas prontas para anexar ao WWDB.

A revisão semântica, as colisões observadas contra a engine Ada, a política de
fontes e o contrato da fila editorial estão detalhados em
[`revisao-editorial-lexemas.md`](revisao-editorial-lexemas.md). O estágio
`prepare_lexeme_review.py` atribui uma revisão SHA-256 ao conteúdo, preserva
ausência de quantidade como desconhecida e produz exatamente zero promoções
automáticas.

## Caminho até uma importação canônica

1. Normalizar o cabeçalho em Unicode e derivar a chave ASCII sem interpretar
   ausência de marca como vogal breve.
2. Separar nomes próprios, formas flexionadas, locuções, variantes e remissões.
3. Reconciliar sentidos e homógrafos; a igualdade de lema+POS não basta.
4. Revisar e persistir os mapas empíricos de `modeles.la`/`typnr`; os mapas
   exatos e os moldes de radicais já são produzidos pelo auditor.
5. Exigir duas famílias independentes para a fila padrão. Fonte única e
   Collatinus-only permanecem material de revisão ou extensão opt-in.
6. Emitir registros canônicos com proveniência por campo e então medir o ganho
   real no analisador contra os testes diferenciais e o corpus da Eneida.

Os artefatos externos continuam fora do WWDB e entram apenas no pipeline de
build/revisão. Isso evita incorporar silenciosamente licenças, OCR bruto ou
estruturas específicas de cada dicionário no runtime compacto.

## Reprodução

Na raiz de `whitakers-words`:

```bash
python3 poc/compact-db/audit_lexical_expansion.py \
  DICTFILE.GEN /caminho/superdb.sqlite \
  --source ls_dict --source gaffiot \
  --collatinus-data /caminho/collatinus/bin/data \
  --latin-german /caminho/token_latim_german.sqlite \
  --output /tmp/lexical-expansion.jsonl \
  --draft-output /tmp/lexeme-structural-drafts.jsonl \
  --report /tmp/lexical-expansion-report.json

# Camada adicional, deliberadamente separada:
#   --faria-v3 /caminho/faria-v3-quality.sqlite
```

O JSONL contém somente os grupos estruturalmente ausentes e suas testemunhas.
O relatório JSON registra entradas excluídas, cobertura, suporte por família,
POS, próprio/comum e a fila prioritária. Os resultados são determinísticos
para os mesmos snapshots.
