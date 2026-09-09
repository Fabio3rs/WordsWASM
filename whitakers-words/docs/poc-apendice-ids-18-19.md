# POC de apêndice lexical com IDs de 18 e 19 bits

Data do corte: 2026-09-09.

O snapshot textual exaustivo do `summary.json`, incluindo distribuições,
seções e hashes, está em
[`poc-apendice-ids-18-19-summary.md`](poc-apendice-ids-18-19-summary.md).

## Decisão do experimento

Este POC mede somente viabilidade técnica. Ele não publica dados e não altera
o leitor WWDB. O gerador produz um contêiner separado, `WWAX`, com largura
declarada no cabeçalho. Os bancos externos continuam somente leitura; esta
etapa consome o `candidates.jsonl` já produzido pela auditoria.

A seleção aceita um grupo estruturalmente ausente apenas quando existe ao
menos um testemunho de Lewis & Short, Gaffiot ou Faria. A prioridade é:

1. Lewis & Short mais Gaffiot;
2. uma dessas fontes mais o scan do Faria;
3. Lewis & Short ou Gaffiot isoladamente;
4. Faria isoladamente, sempre marcado como scan.

Collatinus e o Latim–Alemão podem ajudar a reconstruir classe e radicais de um
lema já admitido por essa política, mas não criam uma entrada. Definições não
entram no binário.

## Material extraído

Dos 29.568 grupos estruturalmente ausentes, 25.244 são representáveis e têm
uma das três fontes admitidas. Quatro outros grupos admitidos não cabem no
alfabeto/limite de 18 caracteres do formato experimental e 4.320 aparecem
somente em fontes auxiliares.

O corte completo de 18/19 bits contém:

| Origem da admissão | Lexemas |
| --- | ---: |
| Lewis & Short + Gaffiot | 3.005 |
| fonte primária + Faria | 4.732 |
| uma fonte primária | 11.924 |
| somente Faria scan | 5.583 |
| **total** | **25.244** |

Há 3.696 lexemas selecionados com testemunho morfológico do Latim–Alemão e
6.343 com testemunho derivado do Collatinus. Essas contagens se sobrepõem e
não são votos de autoridade lexical.

Classe, variante e radicais foram reconstruídos para 5.356 entradas: 333 por
estrutura empírica, 4.995 pelo mapa auxiliar majoritário e 28 por classe
mapeada sem paradigma de radicais. As 19.888 restantes preservam lema e POS,
mas ficam explicitamente em classe `0`, variante `0`, com apenas o radical do
cabeçalho. Isso é suficiente para medir armazenamento e busca direta, não para
afirmar uma flexão correta.

## Layout WWAX

Cada arquivo possui prefixo com magic/version/largura, manifesto JSON, CRC32 e
SHA-256 por seção. Os registros são bit-packed internamente e alinhados ao
próximo byte por registro:

```text
lexema: lemma_id + 4 stem_id + POS:4 + classe:4 + variante:4
        + gênero:3 + próprio:1
referência: lexeme_id + slot:2 + stem_key:3
boundary: contador com a largura do perfil
```

| Perfil | Bits úteis/lexema | Stride/lexema | Bits úteis/ref. | Stride/ref. |
| --- | ---: | ---: | ---: | ---: |
| `u16` | 96 | 12 B | 21 | 3 B |
| `u18` | 106 | 14 B | 23 | 3 B |
| `u19` | 111 | 14 B | 24 | 3 B |

Assim, `u18` e `u19` têm o mesmo tamanho bruto neste desenho. Com packing
contínuo entre registros, `u18` economizaria cerca de 19,7 KiB neste lote, mas
perderia acesso por stride fixo e ainda exigiria leitura atravessando bytes.

## Resultado de capacidade e tamanho

Base: 39.339 lexemas, 76 uniques, 62.086 referências e 48.757 strings.

| Perfil | Novos lexemas | Novas refs. | Total refs. | Novas strings | WWAX raw | gzip-9 | zstd-19 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| `u16` | 2.610 | 3.449 | 65.535 | 3.120 | 72.719 B | 35.633 B | 31.565 B |
| `u18` | 25.244 | 30.888 | 92.974 | 27.321 | 704.645 B | 311.053 B | 283.388 B |
| `u19` | 25.244 | 30.888 | 92.974 | 27.321 | 704.645 B | 310.982 B | 283.079 B |

O `u16` termina no contador de referências e, pela prioridade, contém apenas
2.610 entradas corroboradas simultaneamente por Lewis & Short e Gaffiot. Os
dois perfis maiores carregam todo o lote.

O sidecar de auditoria não faz parte do runtime. Ele mede 2.107.041 bytes no
`u16` e 16.380.632 bytes no `u18/u19`; com zstd-19, 98.293 e 690.673 bytes.
Esse arquivo preserva IDs das fontes e a distinção entre testemunhos admitidos
e auxiliares.

O build dos três perfis levou 2,61 s e atingiu 349.708 KiB de RSS nesta
máquina. Cada arquivo é relido após a gravação e validado integralmente contra
CRC, hashes, extensões das seções, strides e número de strings.

## Margem

O lote completo termina em 64.659 posições de lexema mais uniques, 92.974
referências e 76.078 strings. Portanto, 17 bits já representariam este corte
específico. `u18` oferece capacidade de 262.144 posições; `u19`, 524.288.

`u18` é a escolha mais estreita plausível para o POC atual, mas não satisfaz a
meta anterior de 250 mil lexemas com a razão observada de referências e muito
menos um milhão de referências. `u19` dá margem bem melhor pelo mesmo custo de
stride, mas também não representa um milhão de referências. Essa meta exige
ao menos 20 bits para o contador e recomenda `u24` se todas as classes de ID
forem uniformes.

O resultado favorece flags de largura por seção: IDs de lexema/string podem
ser `u18` ou `u19`, enquanto contadores de referências podem evoluir para 20
ou 24 bits. Um formato misto evita aumentar todas as colunas apenas por causa
da seção de maior cardinalidade.

## Relação com o formato atual

WWAX não é uma revisão do WWDB de produção. É um apêndice experimental com
magic, versão e manifesto próprios; contém somente lexemas novos, referências,
boundaries combinadas e strings novas. O WWDB 1.9 é autocontido, mantém até 24
tipos de seção, metadado lexical de 48 bits e IDs `u16`; o runtime atual rejeita
WWAX antes de ler as seções.

Também não há ainda largura mista dentro de um WWAX: cada arquivo escolhe
globalmente 16, 18 ou 19 bits. A comparação campo a campo, inclusive os itens
necessários para uma migração real, está em
[`poc-apendice-ids-18-19-summary.md`](poc-apendice-ids-18-19-summary.md#diferenças-entre-o-wwdb-atual-e-o-wwax-estendido).

Também foi especificada a alternativa de entrega em um único arquivo físico:
`[WWDB][WWAX][wwax_size:u32][magic:u32]`. Ela acrescenta somente oito bytes e
permite recuperar os dois blobs sem duplicar offsets, largura ou checksums. O
loader 1.9 não pode receber o arquivo combinado diretamente porque valida que
o tamanho declarado pelo WWDB chega exatamente ao EOF; um shim precisa passar
somente o subspan da base ao parser existente. Para vários overlays, a versão
generalizada usa uma tabela final de entradas de cinco bytes
`kind:u8 + size:u32`, com offsets derivados por soma prefixada. A análise e os
tamanhos exatos estão na seção
[`Um arquivo físico contendo WWDB e WWAX`](poc-apendice-ids-18-19-summary.md#um-arquivo-físico-contendo-wwdb-e-wwax).

## Latim–Alemão: proveniência e uso

O artefato local coincide por hash e estrutura com os compactados do
repositório `hackerpschorr/Latin-GermanDictionary`: `README.md`, GPL-3.0,
`dict.zip` e `dict.tar.bz2`, ambos contendo o mesmo `token.sqlite`. O banco tem
36.140 registros `VOC`, além de tabelas grandes de formas e gramática.

O repositório, porém, descreve-se apenas como “Latin-German Dictionary
(SQLite)”. Não identifica autor do léxico, obra/edição de origem, processo de
digitalização ou controles de qualidade. Os IDs e o conteúdo interno não
permitem atribuí-lo com segurança a Georges ou a outro dicionário conhecido.

Conclusão do POC: incluí-lo como evidência auxiliar de paradigma e formas é
tecnicamente útil; promovê-lo a autoridade capaz de criar lemas não é
justificável com a proveniência disponível. Uma distribuição derivada também
deve permanecer separada e cumprir GPL-3.0.

## Reprodução

```bash
python3 poc/compact-db/build_variable_width_appendix_poc.py \
  poc/compact-db/output/max-u16/candidates.jsonl \
  poc/compact-db/output/max-u16/audit-report.json \
  --dictionary DICTFILE.GEN \
  --stems STEMFILE.GEN \
  --uniques UNIQUES.LAT \
  --output-directory poc/compact-db/output/variable-width-appendix
```

Os arquivos ficam em `poc/compact-db/output/variable-width-appendix/`, que é
ignorado pelo Git. `selection.jsonl` é o ledger técnico reproduzível do corte;
não substitui revisão editorial.
