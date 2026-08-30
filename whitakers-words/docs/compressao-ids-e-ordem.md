# Compressão, IDs e ordem física do WWDB

Data da medição: 2026-08-29.

## Decisão para o corte de importação

O primeiro lote de lexemas externos permanece no perfil estreito atual. IDs
de lexema e de strings continuam `u16`, e `LEXEMES.LAT` deve falhar antes de
gerar o WWDB se qualquer contagem dependente ultrapassar 65.535. Isso mantém o
corte pequeno e reproduzível; não significa que todas as contagens do formato
final devam ser `u16`.

Não há um inteiro redundante em cada lexema: `LexemeId` já é o ordinal do
registro. O mesmo vale para `RuleId` e para a identidade de cada referência de
radical. O gargalo imediato é a fronteira acumulada do índice de radicais:
62.086 referências deixam 3.449 posições no contador `u16`.

## Onde estão os bytes

No WWDB 1.8 legado, as seções dominantes são:

| Seção | Full | % do full | Search | % do search |
| --- | ---: | ---: | ---: | ---: |
| meanings | 1.439.511 | 52,7% | omitida | — |
| registros de lexema | 629.424 | 23,0% | 550.746 | 45,9% |
| pool de radicais | 432.337 | 15,8% | 432.337 | 36,0% |
| referências radical → lexema | 186.258 | 6,8% | 186.258 | 15,5% |
| todas as demais seções | 44.370 | 1,6% | 31.000 | 2,6% |

Por isso, remover um ID dos 179 sufixos ou dos 170 rewrites aumenta a
complexidade sem produzir ganho observável no artefato. As otimizações úteis
precisam atingir pools, lexemas ou o índice de radicais.

## Quais IDs precisam existir

| Identidade | Representação atual | Conclusão |
| --- | --- | --- |
| lexema | ordinal no vetor | já é implícita; não gravar outro inteiro |
| regra de flexão | ordinal no vetor | já é implícita |
| referência de radical | ordinal na seção | já é implícita |
| radical e meaning | `u16` para um pool | necessário para deduplicação e resolução O(1) |
| lexema em uma referência de radical | `u16` | necessário no índice invertido; offset seria maior e dependente do layout |
| slot lexical e `stem_key` | cinco bits no registro de referência | ambos são semanticamente distintos no legado e não podem ser inferidos um do outro em geral |
| ID global de addon/rewrite | `u16` explícito | em parte derivável, mas a economia máxima é de centenas de bytes e não justifica quebrar identidade global |

Na base atual, 29.021 das 62.086 referências têm `stem_key != slot + 1`, e
28.888 usam `stem_key == 0`. Portanto, eliminar um dos dois campos por
inferência mudaria dados reais.

Os espaços densos atuais são:

| Espaço | Usado | Folga até 65.535 |
| --- | ---: | ---: |
| lexemas | 39.339 | 26.196 |
| strings de radical | 48.757 | 16.778 |
| meanings | 32.520 | 33.015 |
| referências de radical | 62.086 | 3.449 |

Referências de radical não são IDs públicos de lexema. Uma evolução pode
alargar somente as fronteiras/contagens dessa seção para `u32`, ou adotar um
stream variável com contagens `u32`, mantendo `LexemeId` e `StringId` em
`u16` enquanto esses espaços couberem. Largura deve ser indicada por versão ou
flag da seção; não por suposição do loader.

## Quanto a ordem ajuda

A ordem física tem três papéis diferentes:

1. **Identidade:** o ordinal do lexema é seu ID. Reordenar lexemas renumera os
   resultados de busca, invalida referências de quantidade e muda o
   `datasetId`. Inferir POS ou paradigma de uma faixa ordenada por classe
   economizaria poucos bits e exigiria uma tabela de permutação para conservar
   IDs estáveis.
2. **Índice:** referências são agrupadas pelos 703 buckets de prefixo. Essa
   ordem permite fronteiras pequenas e mantém lexemas próximos também
   próximos numericamente, pois o dicionário legado já possui forte localidade
   alfabética.
3. **Compressão:** organizar registros fixos por coluna coloca bytes de mesma
   distribuição juntos. Isso não altera IDs nem a ordem lógica e é o ganho de
   menor risco.

Uma medição de delta ZigZag + LEB128, reiniciado em cada bucket, reduziu a
seção de referências de 186.258 para 125.665 bytes. Em Brotli 11, a seção
colunar caiu de 45.052 para 32.948 bytes. Ordenar IDs apenas dentro de cada
grupo de radical idêntico chegou a 31.750 bytes: ganho adicional de somente
1.198 bytes. Isso confirma que a ordem existente já oferece quase toda a
localidade disponível sem renumerar lexemas.

Delta variável é apropriado como candidato de **formato de transporte**,
porque o loader atual materializa o índice sequencialmente. Ele não é tão bom
para mmap/acesso aleatório direto; nesse caso, blocos com checkpoints seriam
necessários.

## IDs dos quatro radicais do lexema

Os quatro `StemStringId` ocupam 314.712 bytes. Um protótipo sequencial usou um
byte de padrão por lexema (`empty`, `same-as-first`, `same-as-previous` ou
`explicit`) seguido apenas dos IDs explícitos:

| Representação isolada | RAW | Brotli 11 |
| --- | ---: | ---: |
| quatro `u16` fixos | 314.712 | 96.629 |
| padrão + 94.044 IDs explícitos | 227.427 | 85.964 |

A economia crua é 87.285 bytes, mas cai para 10.665 bytes depois de Brotli. O
stream variável continua interessante para memória/arquivo sem compressão,
mas deve ser comparado com o custo de branches, validação e impossibilidade de
acesso O(1). Um diretório `u32` por lexema consumiria 157.356 bytes e anularia
o ganho; checkpoints por bloco ou decodificação sequencial são as alternativas
coerentes.

## Compressão do container completo

Medição local sobre os mesmos bytes WWDB 1.8:

| Perfil | RAW | gzip 9 | Brotli 5 | Brotli 9 | Brotli 11 | Zstd 19 | XZ 9e |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| full por linhas | 2.731.900 | 1.251.212 | 1.160.518 | 1.138.079 | 1.009.235 | 1.091.956 | 909.720 |
| full por colunas | 2.731.900 | 1.042.735 | 956.165 | 934.751 | **841.556** | 880.996 | 836.828 |
| search por colunas | 1.200.341 | 416.259 | 396.389 | 391.877 | **340.452** | 359.459 | 328.664 |

O melhor ganho imediatamente aproveitável é permitir o full colunar no
loader: os bytes crus não mudam, mas Brotli 11 economiza 167.679 bytes
(16,6%) frente ao full por linhas. `RecordView` já sabe ler colunas; falta
estabilizar o perfil, validar todas as seções e executar os testes diferenciais
com ele antes de torná-lo o artefato full de release.

XZ produz o menor número isolado, mas a diferença para Brotli colunar é apenas
4.728 bytes no full e 11.788 bytes no search. Isso não paga um decoder próprio,
mais memória de pico e uma etapa manual no browser.

### Transporte web

Para deploy comum, `.br`, `.zst` ou `.gz` devem ser representações HTTP do
mesmo `.wwdb`, não novos formatos internos. O servidor negocia
`Accept-Encoding`, responde com `Content-Encoding` e `Vary: Accept-Encoding`,
e `fetch()` entrega os bytes descomprimidos ao loader. A documentação HTTP da
[MDN](https://developer.mozilla.org/en-US/docs/Web/HTTP/Reference/Headers/Accept-Encoding)
mostra `gzip`, `br` e `zstd` na negociação corrente. Zstandard tem formato e
content coding registrados no [RFC 8878](https://www.rfc-editor.org/rfc/rfc8878.html),
e o [WebKit documenta suporte no Safari 26.3](https://webkit.org/blog/17798/webkit-features-for-safari-26-3/).

Ainda assim, Brotli 11 é menor que Zstd 19 neste dataset e já existe no
exportador com fallback gzip. Zstd pode ser acrescentado como terceira
representação quando o host/CDN fizer negociação correta; não deve substituir
Brotli só por ser mais recente. `DecompressionStream` manual não é requisito
do caminho normal e tem suporte por formato diferente entre engines.

Compression Dictionary Transport (`dcb`/`dcz`) pode reduzir atualizações entre
releases muito semelhantes, mas continua com disponibilidade limitada e
exige gestão de dicionário/cache. É experimento posterior, não dependência da
primeira publicação.

## Ordem de implementação recomendada

1. manter o importador `u16` e falhar explicitamente no primeiro overflow;
2. habilitar e testar o full colunar, pois não exige renumerar nada;
3. medir startup, heap e descompressão em browser real;
4. em uma nova versão WWDB, alargar somente contagens/fronteiras de radical;
5. prototipar delta de referências com flag de seção e compatibilidade no
   loader;
6. considerar o stream variável de quatro radicais somente se memória sem
   compressão ou startup, e não apenas bytes transferidos, justificar a
   complexidade;
7. manter IDs de lexema estáveis dentro de um `datasetId`; nunca reordená-los
   apenas para inferir classe gramatical.

