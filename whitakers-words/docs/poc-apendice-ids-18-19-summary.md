# Snapshot textual do summary WWAX `u16`/`u18`/`u19`

Data do snapshot: 2026-09-09.

Este documento materializa em Markdown o conteúdo de
`poc/compact-db/output/variable-width-appendix/summary.json` e os campos
complementares dos `report.json` de cada perfil. O diretório `output/` é
regenerável e ignorado pelo Git; este arquivo é o registro textual versionável
do resultado medido.

## Identificação e estado

| Campo | Valor |
| --- | --- |
| schema | `whitakers-words.variable-width-appendix-report.v1` |
| publicação permitida | não (`publication_allowed: false`) |
| candidatos de entrada | 29.568 |
| candidatos elegíveis com fonte admitida | 25.244 |
| arquivo-fonte | `poc/compact-db/output/variable-width-appendix/summary.json` |

## Baseline Whitaker

| Medida | Quantidade |
| --- | ---: |
| lexemas | 39.339 |
| uniques | 76 |
| referências de radical | 62.086 |
| strings distintas | 48.757 |

## Resultado da extração

### Por método morfológico

| Método | Candidatos |
| --- | ---: |
| `auxiliary-majority-map` | 4.995 |
| `empirical` | 237 |
| `empirical-attested` | 96 |
| `mapped-class-headword-stem` | 28 |
| `unmapped-headword-stem` | 19.888 |
| **total elegível** | **25.244** |

### Por nível de fonte

| Nível | Candidatos |
| --- | ---: |
| `lewis-short+gaffiot` | 3.005 |
| `primary+faria-scan` | 4.732 |
| `single-primary` | 11.924 |
| `faria-scan-only` | 5.583 |
| **total elegível** | **25.244** |

### Exclusões

| Motivo | Candidatos |
| --- | ---: |
| sem Lewis & Short, Gaffiot ou Faria | 4.320 |
| não representável pelo POC | 4 |
| **total excluído** | **4.324** |

Os 25.244 elegíveis mais os 4.324 excluídos reproduzem exatamente os 29.568
candidatos de entrada.

## Comparação geral dos perfis

| Campo | `u16` | `u18` | `u19` |
| --- | ---: | ---: | ---: |
| lexemas selecionados | 2.610 | 25.244 | 25.244 |
| novas referências | 3.449 | 30.888 | 30.888 |
| novas strings | 3.120 | 27.321 | 27.321 |
| lexemas + uniques combinados | 42.025 | 64.659 | 64.659 |
| referências combinadas | 65.535 | 92.974 | 92.974 |
| strings combinadas | 51.877 | 76.078 | 76.078 |
| tamanho WWAX | 72.719 B | 704.645 B | 704.645 B |
| manifesto | 1.245 B | 1.255 B | 1.255 B |
| payload | 71.454 B | 703.370 B | 703.370 B |
| verificação integral | passou | passou | passou |

Compressão medida fora do `summary.json`:

| Artefato | `u16` | `u18` | `u19` |
| --- | ---: | ---: | ---: |
| WWAX gzip-9 | 35.633 B | 311.053 B | 310.982 B |
| WWAX zstd-19 | 31.565 B | 283.388 B | 283.079 B |
| sidecar JSONL raw | 2.107.041 B | 16.380.632 B | 16.380.632 B |
| sidecar JSONL gzip-9 | 113.393 B | 775.985 B | 775.985 B |
| sidecar JSONL zstd-19 | 98.293 B | 690.673 B | 690.673 B |

## Perfil `u16`

### Seleção

| Método | Lexemas |
| --- | ---: |
| `auxiliary-majority-map` | 761 |
| `empirical` | 23 |
| `empirical-attested` | 13 |
| `mapped-class-headword-stem` | 1 |
| `unmapped-headword-stem` | 1.812 |
| **total** | **2.610** |

| POS | Lexemas |
| --- | ---: |
| `NOUN` | 1.845 |
| `ADJ` | 510 |
| `ADV` | 145 |
| `VERB` | 104 |
| `CONJ` | 2 |
| `INTERJ` | 2 |
| `PRON` | 2 |

Todos os 2.610 lexemas pertencem ao nível `lewis-short+gaffiot`. Foram
descartados 22.634 candidatos por falta de capacidade no contador de
referências. Há testemunho morfológico auxiliar do Collatinus em 919 itens e
do Latim–Alemão em 416; essas contagens podem se sobrepor.

### Binário e seções

Arquivo:
`poc/compact-db/output/variable-width-appendix/u16/lexical-appendix-u16.wwax`

SHA-256 do WWAX:
`288f7f38c72168693977369db10b87a9b01eb8cdf6d53cb5a211a72f03396127`

SHA-256 do `selection.jsonl`:
`b8d71c111f8c3477f7e16477e182352fa67907ce6e4131211272736eb666e0b8`

| Seção | Offset | Bytes | SHA-256 |
| --- | ---: | ---: | --- |
| `lexeme_records` | 0 | 31.320 | `2fa28c0ef86c709f0e9fbc89174a6d139e5a8598fab379b792b315aaa780aec5` |
| `stem_references` | 31.320 | 10.347 | `bd4e7253f46cbf530ad2ccb0efb419671367637fd6494faa620dc9aec8e1e031` |
| `combined_prefix_boundaries` | 41.667 | 1.408 | `23872aed1637c5db1ab2d526f7cf045223b5f7ae29f8d5f6bdaa608f9ed9f512` |
| `new_string_pool` | 43.075 | 28.379 | `6546ec9639f9dd12f6b267c311618ab8d39329cf381fb8b4f25a3805e94ba27e` |

## Perfil `u18`

### Seleção

| Método | Lexemas |
| --- | ---: |
| `auxiliary-majority-map` | 4.995 |
| `empirical` | 237 |
| `empirical-attested` | 96 |
| `mapped-class-headword-stem` | 28 |
| `unmapped-headword-stem` | 19.888 |
| **total** | **25.244** |

| POS | Lexemas |
| --- | ---: |
| `NOUN` | 15.582 |
| `ADJ` | 6.317 |
| `VERB` | 1.772 |
| `ADV` | 1.415 |
| `NUM` | 66 |
| `PRON` | 61 |
| `PREP` | 12 |
| `CONJ` | 10 |
| `INTERJ` | 9 |

Por nível de fonte: 3.005 `lewis-short+gaffiot`, 4.732
`primary+faria-scan`, 11.924 `single-primary` e 5.583 `faria-scan-only`. Não
houve descarte por capacidade. Há testemunho morfológico auxiliar do
Collatinus em 6.343 itens e do Latim–Alemão em 3.696.

### Binário e seções

Arquivo:
`poc/compact-db/output/variable-width-appendix/u18/lexical-appendix-u18.wwax`

SHA-256 do WWAX:
`600445aa0ff6b5cb726272c196ad785f30093537696f2db61722faaae53f4174`

SHA-256 do `selection.jsonl`:
`37b64de1606b69787db90caa14a08dfa973bf4997bd5c67afec429b3967d18ab`

| Seção | Offset | Bytes | SHA-256 |
| --- | ---: | ---: | --- |
| `lexeme_records` | 0 | 353.416 | `7d892e11b06f3af2839e2cef0255285db1cdc80d4f701039f2e0d953c4166f58` |
| `stem_references` | 353.416 | 92.664 | `b23283d42b2fdcc44a33330a0b8f6810aa3c605fb60b6021c94fcd50888ae9a8` |
| `combined_prefix_boundaries` | 446.080 | 2.112 | `f28e7938217d5f9516d27a7a29db4f2680120f4489fcbc35674a71cfe032da4a` |
| `new_string_pool` | 448.192 | 255.178 | `3f1a4df7380ceadbbcae9aa83be16f04e2be63f0851bda2949d009269492a63d` |

## Perfil `u19`

As distribuições por método, POS, nível de fonte e testemunhos auxiliares são
idênticas às do `u18`. Também não houve descarte por capacidade.

### Binário e seções

Arquivo:
`poc/compact-db/output/variable-width-appendix/u19/lexical-appendix-u19.wwax`

SHA-256 do WWAX:
`682bb1c861580d501f28d02178308c6acecb88bc4b738fc112fe15ece6a70eb4`

SHA-256 do `selection.jsonl`:
`37b64de1606b69787db90caa14a08dfa973bf4997bd5c67afec429b3967d18ab`

| Seção | Offset | Bytes | SHA-256 |
| --- | ---: | ---: | --- |
| `lexeme_records` | 0 | 353.416 | `b527c20b90a4e811ecc0490a990a249c26ac1578880afc4ec673284a0bbd6086` |
| `stem_references` | 353.416 | 92.664 | `23a082d23c8700cd71a2bae31cce73d0eba6e1b2ae0623e5579824c3c72bcf85` |
| `combined_prefix_boundaries` | 446.080 | 2.112 | `f28e7938217d5f9516d27a7a29db4f2680120f4489fcbc35674a71cfe032da4a` |
| `new_string_pool` | 448.192 | 255.178 | `3f1a4df7380ceadbbcae9aa83be16f04e2be63f0851bda2949d009269492a63d` |

Os hashes das seções de lexemas e referências diferem do `u18` porque a
posição dos campos muda com a largura do ID. Boundaries e strings são
idênticos. O tamanho final permanece igual porque ambos os perfis usam os
mesmos strides byte-alinhados.

## Diferenças entre o WWDB atual e o WWAX estendido

Nesta comparação histórica, “produção” significava o WWDB 1.9 então gerado
pelo packer e aceito por `Database::load_poc`. O WWAX é somente o contêiner de
apêndice deste experimento; ele não é uma nova versão pronta do WWDB.

Fontes da comparação: esquema wire
[`wwdb_schema.hpp`](../../include/words/detail/wwdb_schema.hpp), loader
[`database.cpp`](../../src/database.cpp), packer
[`wwdb_poc_pack.cpp`](../poc/compact-db/wwdb_poc_pack.cpp) e gerador WWAX
[`build_variable_width_appendix_poc.py`](../poc/compact-db/build_variable_width_appendix_poc.py).

| Aspecto | WWDB atual | WWAX estendido |
| --- | --- | --- |
| finalidade | banco completo usado pelo runtime | apêndice isolado para medir capacidade e tamanho |
| magic | `WWDB\r\n\x1a\n` | `WWAPX\r\n\x1a` |
| versão | major/minor binários; loader aceita 1.6–1.9 | versão própria `1`, incompatível com WWDB |
| cabeçalho | 40 bytes fixos | 20 bytes fixos mais manifesto JSON |
| diretório | entrada binária de 32 bytes por seção | descritores de seção dentro do manifesto JSON |
| perfil físico | dense ou search-only; registros row/column conforme flags | registros bit-packed, byte-alinhados por registro |
| largura dos IDs | `u16` fixo no wire format | uma largura por arquivo: 16, 18 ou 19 bits |
| configuração mista | não existe | ainda não existe; o cabeçalho escolhe uma largura uniforme |
| conteúdo | banco completo com até 24 tipos de seção | somente quatro seções de apêndice |
| strings | pools completos de radicais, meanings, endings e addons | somente strings ASCII novas; IDs virtuais continuam após o pool base |
| lexema | quatro IDs de radical, meaning opcional e metadado denso de 48 bits | ID de lema, quatro IDs de radical e metadado reduzido de 16 bits |
| referência de radical | registro packed de 24 bits: `lexeme:u16 + slot:2 + key:3 + reservado:3` | 21, 23 ou 24 bits úteis em stride de 3 bytes |
| boundaries | contadores `u16` | contadores com a largura 16/18/19 do arquivo |
| meanings | presentes no full e omitidos no search-only | sempre omitidos |
| flexões e addons | inflections, suffixes, prefixes, tackons, uniques e rewrites | não incluídos |
| quantidade e avisos | seções de quantidade e avisos morfológicos da versão 1.9 | não incluídos |
| proveniência editorial | códigos compactos no metadado; sem IDs externos por lexema | IDs externos ficam no `selection.jsonl`, fora do WWAX |
| integridade | tamanho declarado e CRC32 global do payload | CRC32 do payload e SHA-256 individual por seção |
| suporte do loader | implementado e validado | inexistente; o loader rejeita o magic |

### Registro de lexema

No perfil atual de produção, o registro dense/search-only mede 14 bytes:

```text
stem_id[4]: 4 × u16 = 8 bytes
metadata:              48 bits = 6 bytes
```

O full acrescenta `meaning_id:u16`, totalizando 16 bytes. O metadado de 48
bits contém POS (4), paradigma (8), idade/assunto/geografia/frequência/fonte
(22), payload de classe (13) e um bit não usado.

O WWAX usa:

```text
lemma_id + stem_id[4]: 5 × largura configurada
POS:4 + classe:4 + variante:4 + gênero:3 + próprio:1
```

Isso resulta em 12 bytes no `u16` e 14 bytes no `u18`/`u19`. O `lemma_id`
separado existe para preservar o cabeçalho extraído, algo que o WWDB atual não
armazena separadamente dos radicais. Em contrapartida, o WWAX não preserva os
22 bits editoriais, o payload de classe completo, valor numérico, meaning ou
atributos específicos de todas as classes gramaticais.

### Referência de radical

O registro `u16` do WWAX tem os mesmos 21 bits semânticos da referência dense
atual. Nos perfis maiores, os bits reservados do registro de 24 bits são
reaproveitados:

| Perfil | LexemeId | Slot | Key | Padding | Stride |
| --- | ---: | ---: | ---: | ---: | ---: |
| WWDB atual / WWAX `u16` | 16 | 2 | 3 | 3 | 3 B |
| WWAX `u18` | 18 | 2 | 3 | 1 | 3 B |
| WWAX `u19` | 19 | 2 | 3 | 0 | 3 B |

Essa reutilização explica por que aumentar apenas a referência até 19 bits
não aumenta seu stride. Ela não resolve automaticamente os demais locais que
continuam `u16`, como IDs de strings, boundaries, quantidades, avisos
morfológicos e referências mantidas por outras seções.

### Dependência da base

WWDB é autocontido. WWAX não é: guarda somente os registros e strings novos,
além das contagens da base. Seus IDs de strings e boundaries são definidos no
espaço combinado “base + apêndice”. Um futuro loader teria de validar a
identidade da base e mesclar as referências por bucket; o formato atual ainda
não grava um hash obrigatório do WWDB base.

### Um arquivo físico contendo WWDB e WWAX

É possível empacotar os dois blobs sem convertê-los em um novo WWDB. Para o
caso atual, em que há exatamente uma base e um apêndice, o envelope mínimo é
um trailer de 8 bytes:

```text
0                         base_size
+-------------------------+------------------------+--------------+
| WWDB 1.9, sem alteração | WWAX, sem alteração    | u32 | magic4 |
+-------------------------+------------------------+--------------+
                                                    ^ trailer EOF-8
```

O `u32` é o tamanho do WWAX em little-endian e `magic4` identifica o
supercontêiner. Lendo os oito últimos bytes, o loader calcula
`base_size = file_size - 8 - wwax_size`. Não é preciso guardar dois offsets:
os membros são contíguos, o WWDB começa em zero e o início do WWAX é
`base_size`. Também não é preciso repetir CRC ou largura: cada membro já tem
magic, versão e integridade próprios, e o WWAX já declara 16/18/19 bits.

Para os artefatos medidos, esse envelope produziria:

| Base | Base | WWAX `u19` | Trailer | Arquivo combinado |
| --- | ---: | ---: | ---: | ---: |
| search-only | 1.200.388 B | 704.645 B | 8 B | 1.905.041 B |
| dense | 2.731.947 B | 704.645 B | 8 B | 3.436.600 B |
| columnar | 2.731.900 B | 704.645 B | 8 B | 3.436.553 B |
| full | 2.977.659 B | 704.645 B | 8 B | 3.682.312 B |

O overhead estrutural é, portanto, oito bytes, além daquilo que já existe nos
dois arquivos. Um tamanho é preferível a um ponteiro absoluto neste layout:
ele permite localizar o apêndice a partir do EOF, funciona igualmente depois
de mover o arquivo e deixa o tamanho da base verificável contra o campo
`file_size` do próprio WWDB. `u32` limita cada apêndice a menos de 4 GiB, margem
muito acima deste POC; uma versão posterior poderia selecionar `u64`.

Isso preserva os blobs, mas não torna o arquivo inteiro compatível com o
loader 1.9. O loader atual exige que o `file_size` declarado pelo WWDB seja
igual ao tamanho do vetor recebido e que suas seções cubram até o EOF. Logo,
ele rejeita os bytes anexados. Um shim novo deve localizar o trailer e entregar
ao parser antigo somente o subspan `[0, base_size)`, depois carregar o WWAX.
Como `Database::load_poc` hoje recebe um `vector` proprietário, uma
implementação ingênua copiaria a base; para evitar esse custo, o loader deve
aceitar span com dono compartilhado ou o frontend deve separar os membros
antes de construir o vetor.

#### Variante parecida com uma tabela COFF, mas menor

Se houver mais de um apêndice, pode-se trocar o trailer fixo por uma tabela no
fim do arquivo. Como os blobs permanecem contíguos e ordenados, cada entrada
precisa somente de `kind:u8 + size:u32`, cinco bytes; os offsets são somas
prefixadas dos tamanhos:

```text
[membro 0][membro 1]...[membro N-1]
[kind:u8 size:u32] × N
[magic:u32 version:u8 count:u8]
```

Dois membros custariam 16 bytes no total: 10 bytes de entradas e 6 de footer.
Uma entrada de seis bytes (`kind:u8 + flags:u8 + size:u32`) elevaria isso a 18
bytes e deixaria flags para compressão ou delta. Não há nome de seção,
alinhamento, relocation, symbol table, offset ou checksum duplicado; por isso é
substancialmente menor que as entradas de 32 bytes do diretório WWDB e que um
cabeçalho COFF. As validações essenciais são: `count` limitado, soma dos
tamanhos igual ao início da tabela, tipos únicos quando exigido e validação
interna de cada membro.

Para este POC, o trailer fixo de oito bytes é a melhor primeira implementação.
A tabela compacta só se paga se pretendermos encadear vários overlays. Já uma
integração real no WWDB v2 eliminaria a duplicação do cabeçalho/manifesto WWAX,
mas exigiria novos tipos de seção, versão e loader; ela resolve o formato de
produção, enquanto o supercontêiner resolve apenas empacotamento e entrega.

### Compatibilidade e trabalho necessário para produção

Não existe compatibilidade binária ou fallback automático entre os formatos.
Renomear WWAX para WWDB não funciona, e nem o WWAX `u16` é aceito pelo loader.
Para transformar o experimento em formato de produção ainda seria necessário:

1. criar uma nova versão WWDB e declarar largura por seção ou perfil;
2. alargar todos os leitores, máscaras e validações transitivamente afetados;
3. definir se o resultado será um banco completo ou um apêndice ligado por
   hash/dataset ID a uma base específica;
4. restaurar o metadado completo de 48 bits e as seções omitidas;
5. decidir como quantities e morphological notices referenciarão IDs maiores;
6. testar full/search-only, row/column, lookup, startup e WebAssembly;
7. definir política de rejeição clara para leitores 1.9.

Portanto, a configuração “16 ou 24 bits no mesmo arquivo” continua sendo uma
proposta de evolução por flags de seção. Este POC implementa apenas “um
arquivo com uma largura global escolhida entre 16, 18 e 19 bits”.

## Política de fontes preservada nos relatórios

- fontes primárias: `lewis`, `gaffiot`;
- secundária digitalizada: `faria`;
- fontes auxiliares não podem criar entrada;
- Latim–Alemão: `hackerpschorr/Latin-GermanDictionary`;
- licença declarada do Latim–Alemão: `GPL-3.0-only`;
- fonte lexicográfica subjacente: não declarada;
- papel no POC: somente morfologia auxiliar.

## Limitações registradas

- ausência é estrutural e não prova distinção semântica;
- definições e significados editoriais foram omitidos;
- cabeçalhos sem mapa preservam POS, mas usam classe `0` e variante `0` como
  desconhecidas;
- morfologia auxiliar pode ordenar ou reconstruir um lema previamente
  selecionado, mas não pode criar entrada;
- WWAX é um contêiner experimental ainda não aceito pelo runtime WWDB.

## Verificação

Os três valores `binary.verified` são `true`. O verificador releu cada arquivo
e conferiu prefixo, versão, largura, CRC32 do payload, SHA-256 e extensão de
cada seção, strides dos registros, cobertura integral do payload e número de
strings. A suíte relevante terminou com três testes aprovados e nenhuma
falha: auditoria de expansão lexical, POC `u16` e POC de largura variável.
