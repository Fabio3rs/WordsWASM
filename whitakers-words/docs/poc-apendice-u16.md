# POC do apêndice lexical no limite `u16`

Data do corte: 2026-09-09.

## Escopo

Este experimento continua o pipeline de `poc/compact-db`. Ele mede capacidade,
custo de build, tamanho e ganho mecânico de cobertura. Não é uma proposta de
publicação e não passa pelo ledger editorial. Os significados são testemunhos
curtos não revisados e o perfil relaxado aceita o mapeamento morfológico mais
frequente mesmo quando o mapa observado é um-para-muitos.

Todas as fontes externas foram abertas pelo auditor como SQLite
`mode=ro&immutable=1`. O gerador novo consome apenas o JSONL já materializado no
workspace e não acessa os bancos externos.

## Corte reproduzido

O comando documentado em `poc/compact-db/README.md`, acrescido do Faria v3
quality, leu Lewis & Short, Gaffiot, Collatinus, Latim–Alemão e Faria:

| Medida | Resultado |
| --- | ---: |
| entradas externas lidas | 159.877 |
| grupos tipados | 54.852 |
| grupos estruturalmente ausentes | 29.568 |
| ausentes corroborados por 2+ famílias | 9.827 |
| comuns corroborados | 5.821 |
| tempo do auditor | 5,46 s |
| RSS máximo do auditor | 488.320 KiB |

Os JSONL e relatórios regeneráveis estão em
`poc/compact-db/output/max-u16/`, diretório ignorado pelo Git.

## O teto real

O `DICTFILE.GEN` contém 39.339 lexemas e `UNIQUES.LAT`, 76 análises diretas. A
seção `STEMFILE.GEN` já contém 62.086 referências. O packer impõe:

```text
lexemas + uniques <= 65.536
referências de radical <= 65.535
```

Logo, a folga nominal é de 26.121 lexemas, mas de somente 3.449 referências.
Como todo lexema compilado precisa de pelo menos um radical, é impossível
esgotar `LexemeId` com o layout atual: a fronteira do índice de radicais acaba
primeiro.

## Dois perfis gerados

`build_u16_appendix_poc.py` produz dois cortes determinísticos, sempre usando
apenas grupos comuns corroborados por ao menos duas famílias:

| Perfil | Lexemas novos | Referências novas | Total de referências | Natureza |
| --- | ---: | ---: | ---: | --- |
| `max-cardinality` | 3.449 | 3.449 | 65.535 | um radical/cabeçalho por lexema |
| `morphology-first` | 1.845 | 3.449 | 65.535 | paradigmas e slots primeiro; cabeçalhos completam a folga |

O perfil máximo contém 1.912 substantivos, 1.040 adjetivos, 489 advérbios,
quatro conjunções e quatro interjeições. Substantivos e adjetivos são
deliberadamente estreitos, tratados como cabeçalhos indeclináveis para que o
experimento meça o maior número de itens reconhecíveis.

O perfil morfológico contém:

| Método | Lexemas |
| --- | ---: |
| estrutura empírica com formas atestadas | 92 |
| estrutura empírica sem essa validação | 10 |
| mapa majoritário relaxado | 1.205 |
| cabeçalho estreito para preencher capacidade | 538 |

São 738 substantivos, 455 adjetivos, 155 verbos, 489 advérbios, quatro
conjunções e quatro interjeições.

## Validação da engine

Os 3.449 cabeçalhos do perfil máximo retornaram seu novo `LexemeId` tanto no
WWDB full quanto no `search-only`: 100% de recuperabilidade mecânica.

No perfil morfológico, 1.818 dos 1.845 cabeçalhos retornaram o novo ID. Todos os
27 desvios pertencem ao mapa majoritário, ou 2,24% dos 1.205 registros desse
método. Quatro ficaram `unknown`; os demais 23 encontraram outra análise já
existente. Essa medição é de autoconsistência do cabeçalho, não de correção
filológica das formas geradas.

Antes do apêndice, 769 das 3.449 consultas do perfil máximo eram `unknown` e
2.680 já alcançavam algum fallback. Depois, todas têm entrada direta. No corte
morfológico, os `unknown` caíram de 290 para quatro. No perfil máximo, o total
de análises marcadas como `derived` nessa bateria caiu de 5.262 para 38, pois a
entrada lexical exata passou a preceder o fallback artificial.

O teste de integração existente também confirmou que uma referência adicional
é rejeitada com `u16 lexeme/reference capacity`, antes de gravar um WWDB
parcial.

## Custo de armazenamento

| Corte | Full RAW | Full gzip-9 | Full zstd-19 | Search RAW | Search gzip-9 | Search zstd-19 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| base | 2.731.947 | 1.257.012 | 1.091.933 | 1.200.388 | 417.452 | 359.454 |
| máximo | 3.200.167 | 1.436.999 | 1.246.424 | 1.292.481 | 440.230 | 382.440 |
| morfologia | 2.986.888 | 1.360.211 | 1.185.062 | 1.254.804 | 437.398 | 376.577 |

O máximo acrescenta 468.220 bytes ao full e 92.093 bytes ao search. Com
zstd-19, os acréscimos caem para 154.491 e 22.986 bytes. Isso equivale, no
`search-only` comprimido, a aproximadamente 6,7 bytes por novo cabeçalho.

O perfil morfológico acrescenta 254.941 bytes ao full e 54.416 ao search;
com zstd-19, 93.129 e 17.123 bytes. O custo por lexema é maior porque há menos
lexemas compartilhando a mesma capacidade fixa de 3.449 referências.

## Custo de execução

Medição local de uma execução do packer:

| Perfil | Build full | RSS full | Build search | RSS search |
| --- | ---: | ---: | ---: | ---: |
| máximo | 0,66 s | 45.988 KiB | 0,56 s | 32.152 KiB |
| morfologia | 0,57 s | 44.084 KiB | 0,46 s | 31.488 KiB |

Dez inicializações nativas seguidas, cada qual carregando o full e consultando
`amo`, levaram 1,89 s na base, 2,34 s no perfil máximo e 2,29 s no perfil
morfológico. O RSS máximo subiu de aproximadamente 13,0 MiB para 13,7 MiB.
Esses números incluem criação do processo, validação integral do WWDB e a
consulta; não são benchmark de navegador.

## Índice semântico encontrado

O artefato lexical pronto para KNN é, neste corte, FAISS e não SQLite:

```text
/mnt/projects/Projects/Dicionarios/dicionarios/index_mix/index.faiss
/mnt/projects/Projects/Dicionarios/dicionarios/index_mix/meta.jsonl
```

O metadado contém 116.034 vetores: 51.636 de Lewis & Short, 36.140 do
Latim–Alemão e 28.258 do Gaffiot. O script de origem usa `IndexFlatIP`, vetores
normalizados e o modelo `nomic-embed-text`; pelo tamanho físico, o índice tem
768 dimensões. Ele permite KNN cosseno exato, mas não traz Faria nem entradas
do Words e não possui manifesto/hash de modelo suficiente para uma release.

A varredura dos SQLite nas quatro raízes encontrou apenas:

- Chroma `patristic`, com 16 documentos de 4.096 dimensões, sem relação com o
  apêndice lexical;
- colunas `embedding_json` no The Latin Library e na cópia de trabalho do
  Faria, com zero embeddings preenchidos;
- hashes/texto usados pelo OCR do Faria, não vetores KNN.

Portanto o FAISS pode ser reaproveitado no próximo POC de ranking. Se havia um
SQLite lexical vetorial mais novo, ele não está materializado neste corte.

## Reprodução resumida

Depois de gerar `candidates.jsonl` e `audit-report.json` pelo auditor:

```bash
python3 poc/compact-db/build_u16_appendix_poc.py \
  poc/compact-db/output/max-u16/candidates.jsonl \
  poc/compact-db/output/max-u16/audit-report.json \
  --dictionary DICTFILE.GEN \
  --stems STEMFILE.GEN \
  --uniques UNIQUES.LAT \
  --profile max-cardinality \
  --output poc/compact-db/output/max-u16/max-cardinality/LEXEMES.LAT \
  --selection-output poc/compact-db/output/max-u16/max-cardinality/selection.jsonl \
  --report poc/compact-db/output/max-u16/max-cardinality/report.json
```

Trocar o perfil e o diretório para `morphology-first` reproduz o segundo lote.
O `selection.jsonl` preserva as referências externas de cada item; ele existe
para auditoria do experimento e não substitui o ledger editorial.

## Próximo POC

Usar o FAISS apenas para ordenar candidatos do mesmo lema/POS e comparar três
políticas sob o mesmo teto de 3.449 referências:

1. cardinalidade pura;
2. morfologia reconstruída;
3. morfologia mais coerência semântica pelo índice antigo.

O resultado deve medir cobertura em corpus, tamanho, startup e taxa de
autoconsistência. Nenhum desses perfis deve entrar em release sem voltar ao
pipeline de decisões editoriais.
