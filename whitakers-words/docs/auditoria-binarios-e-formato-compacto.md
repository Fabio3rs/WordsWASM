# Auditoria dos binários legados e proposta de formato compacto

Este documento avalia redundâncias, padding, larguras de campos, modelagem de
enums e portabilidade dos quatro artefatos centrais do WORDS:

- `DICTFILE.GEN`;
- `STEMFILE.GEN`;
- `INFLECTS.SEC`;
- `INDXFILE.GEN`.

As medições foram feitas sobre os arquivos presentes nesta cópia do
repositório. O layout legado completo está descrito em
[`formato-binario-dados.md`](formato-binario-dados.md), e as estruturas usadas
para inspecioná-lo estão em
[`src/legacy_data_layout.h`](../src/legacy_data_layout.h).

> **Atualização de implementação (WWDB 1.7):** as medições detalhadas de WWDB
> 1.0 abaixo são preservadas como baseline histórico. O empacotador atual já
> incorpora os 135 prefixos, 179 sufixos e 29 tackons/packons de `ADDONS.LAT`,
> as 76 análises diretas de `UNIQUES.LAT`, e o leitor/engine C++23 consome o
> perfil denso. Os tamanhos atuais são 2.977.659 bytes (`simple`), 2.731.900
> (`dense`/`columnar`) e 1.200.341
> (`search-only`). A tabela e os hashes normativos ficam no
> [README do PoC](../poc/compact-db/README.md); o estado funcional fica em
> [arquitetura-engine-cpp23.md](arquitetura-engine-cpp23.md). Salvo quando uma
> seção declarar WWDB 1.3/1.4/1.5/1.6/1.7, números e frases sobre itens ainda ausentes neste
> documento descrevem o snapshot 1.0, não o estado atual.
> O leitor atual também decodifica e valida os payloads de todas as classes
> semânticas do dicionário geral. Numerais, advérbios, verbos, particípios,
> supinos, preposições, conjunções e interjeições não exigiram mudança de
> layout: os campos já estavam presentes no WWDB 1.3. `UNIQUES` exigiu apenas
> uma nova seção de registros diretos no WWDB 1.4.
> O WWDB 1.5 acrescentou as três seções de reescrita. O WWDB 1.6 contém 170
> regras tipadas em `REWRITES.LAT`: 11 de síncope e 159 ortográficas importadas
> deterministicamente das tabelas Ada. Os registros completos passaram de 14
> para 16 bytes para representar operação, estágio e restrição semântica.
> O WWDB 1.7 acrescenta duas seções de quantidade vocálica: 3.570 bytes densos
> por `rule_id` e registros lexicais esparsos de nove bytes por lexema/slot. O
> lote atual contém três flexões e 76 alvos lexicais curados em
> `QUANTITIES.LAT`.

O objetivo aqui não é apenas compactar os arquivos atuais. É separar três
problemas que hoje estão misturados:

1. representação em memória escolhida pela ABI Ada/GNAT;
2. representação persistente e portátil;
3. modelo de tipos usado pelo motor.

## Resumo executivo

Os quatro arquivos ocupam juntos **10.704.174 bytes**. Há quatro fontes
principais de desperdício:

- strings de tamanho fixo preenchidas com espaços;
- inteiros de 32 ou 64 bits para domínios que precisam de 2 a 16 bits;
- payloads variantes e padding copiados como imagens da memória Ada;
- tabelas de capacidade fixa, inclusive 1.065 posições vazias em
  `INFLECTS.SEC`.

As repetições mais relevantes são:

- 63.520 dos 157.352 slots textuais de radical em `DICTLINE.GEN` estão vazios;
- há somente 48.712 strings de radical diferentes para 157.356 slots no
  `DICTFILE.GEN` binário;
- todos os 62.086 registros de `STEMFILE.GEN` repetem um bloco gramatical de
  20 bytes que já está disponível pela referência `MNPC`; foram conferidos os
  62.086 pares e houve **zero divergências**;
- as 1.785 regras de flexão usam apenas 460 terminações distintas;
- uma regra de flexão está duplicada literalmente;
- 461 dos 703 intervalos de `INDXFILE.GEN` estão vazios.

Quatro perfis compactos foram materializados pelo PoC. O primeiro, simples,
ocupa **2.946.117 bytes**. Uma segunda passada eliminou IDs deriváveis e
compactou os registros para **2.700.358 bytes**, redução de **74,77%** contra
o legado. O terceiro tem os mesmos bytes sem compressão, mas organiza as
colunas dos registros contiguamente; ele chegou a **1.028.906 bytes em gzip**
e **870.406 bytes em Zstd nível 19**. O quarto remove definições para o caso de
uso de autocomplete/busca e ocupa **1.183.767 bytes RAW**, **409.145 em gzip**
ou **353.268 em Zstd nível 19**. O PoC usa pools deduplicados, IDs densos de 16
bits validados e campos escritos explicitamente. No snapshot 1.0 medido nesta
seção, ele ainda não incluía `UNIQUES.LAT` nem `ADDONS.LAT`, assim como o total
legado acima também não os inclui.

> **Status dos números:** isto continua sendo uma aproximação de engenharia
> baseada em um PoC rápido, não um formato final nem um benchmark do motor
> WebAssembly completo. Os tamanhos dos artefatos gerados são medições reais;
> a adequação do schema, o pico de memória no browser e os tempos de rede são
> projeções que precisam ser confirmadas no porte funcional. Em particular,
> o perfil por colunas ainda não tem leitor nem benchmark de consultas.

Compressão geral continua útil. Aplicada diretamente aos arquivos legados,
sem nenhuma normalização, ela produz:

| Arquivo | Original | `gzip -9` | `xz -9` |
| --- | ---: | ---: | ---: |
| `DICTFILE.GEN` | 7.081.020 | 1.215.369 | 892.460 |
| `STEMFILE.GEN` | 3.476.816 | 380.460 | 219.932 |
| `INFLECTS.SEC` | 114.000 | 9.061 | 5.520 |
| `INDXFILE.GEN` | 32.338 | 3.506 | 2.168 |
| **Total** | **10.704.174** | **1.608.396** | **1.120.080** |

Isso confirma a grande redundância, mas não elimina a necessidade de um
formato explícito: compressão não fornece versionamento, tipos, validação nem
portabilidade de ABI.

## Como é hoje

### `DICTFILE.GEN`

O arquivo contém 39.339 registros de 180 bytes:

```text
4 × radical[18] + classe/payload[20] + metadados[5]
                  + significado[80] + padding[3]
```

As strings são preenchidas à direita com espaços. O payload gramatical é uma
union discriminada pela classe gramatical, mas a imagem persistida sempre
reserva os 20 bytes completos.

#### Ocupação das strings

| Região | Capacidade | Caracteres efetivos | Preenchimento à direita |
| --- | ---: | ---: | ---: |
| quatro radicais | 2.832.408 | 679.206 | 2.153.202 |
| significados | 3.147.120 | 1.748.405 | 1.398.715 |
| **Total** | **5.979.528** | **2.427.611** | **3.551.917** |

Só o preenchimento das strings representa aproximadamente metade do arquivo.
Espaços internos dos significados foram contabilizados como conteúdo; apenas
o preenchimento à direita foi removido na medição.

#### Repetição de strings

Os 157.356 slots de radical do binário contêm apenas 48.712 valores distintos.
O total de caracteres desses valores únicos é 383.263. Se cada string for
armazenada uma vez como `[comprimento:u8][bytes]`, o pool ocupa 431.975 bytes.

Os 39.339 significados contêm 32.486 strings distintas. Há 4.387 registros de
significado que repetem uma string já vista. Um pool com comprimento de um
byte ocupa 1.437.881 bytes:

```text
1.405.395 caracteres únicos + 32.486 bytes de comprimento
```

Como os dois conjuntos têm menos de 65.536 itens, cada referência cabe em um
ID de 16 bits nesta cópia.

#### Payload gramatical

Os payloads de 20 bytes ocupam 786.780 bytes. Mantendo até mesmo as larguras
legadas de 32 bits para `which`, `variant` e valor numeral, apenas cerca de
393.272 bytes correspondem a campos semanticamente ativos. O restante é
padding, cabeçalho de alinhamento ou cauda inativa da union.

Há apenas:

- 412 combinações gramaticais distintas;
- 1.370 combinações distintas dos cinco metadados de tradução;
- `which` e `variant` no intervalo observado `0..9`;
- valor numeral máximo igual a 1.000.

O padding final do registro acrescenta mais 118.017 bytes.

### `STEMFILE.GEN`

O arquivo contém 62.086 registros de 56 bytes:

```text
radical[18] + padding[2] + classe/payload[20] + key:u32
            + padding[4] + MNPC:u64
```

#### Strings

Os 1.117.548 bytes reservados para radicais contêm 466.898 caracteres. Os
650.650 bytes restantes são preenchimento à direita. Há 48.711 radicais
distintos, exatamente o conjunto do dicionário menos uma string que não chega
ao índice de radicais.

#### Bloco gramatical redundante

`MNPC` aponta para a entrada de `DICTFILE.GEN`. O bloco gramatical de 20 bytes
do radical deveria, portanto, ser derivável da entrada apontada. A conferência
foi feita normalizando somente os campos semânticos de cada variante:

```text
62.086 registros comparados
0 divergências entre STEMFILE.Part e DICTFILE[MNPC].Part
```

Logo, os **1.241.720 bytes** desses blocos podem ser eliminados sem perda de
informação nesta base.

#### Inteiros superdimensionados

- `key` usa `uint32`, mas os valores observados são `0..4`;
- `MNPC` usa `uint64`, mas o maior valor é 39.339;
- todos os 39.339 valores possíveis de `MNPC` são usados ao menos uma vez.

Distribuição de `key`:

| Valor | Registros |
| ---: | ---: |
| 0 | 28.888 |
| 1 | 10.295 |
| 2 | 7.904 |
| 3 | 7.509 |
| 4 | 7.490 |

Um registro equivalente para esta cópia pode ser:

```text
stem_id:u16 + lexeme_id:u16 + inflection_key:u8 = 5 bytes
```

Isso reduz a tabela de 3.476.816 para 310.430 bytes, além do pool compartilhado
de strings.

Uma segunda conferência mostrou que **todos os 62.086 textos de radical**
também aparecem em um dos quatro slots do lexema apontado. Isso não significa
que `key` seja o slot: 28.888 registros usam `key=0`, e 133 registros com
`key=1..4` apontam para um slot vazio enquanto o texto está em outro slot. O
primeiro registro ainda representa o radical vazio especial de `sum`.

Guardando separadamente o slot lexical real e a chave usada para casar a
flexão, o `stem_id` passa a ser derivável:

```text
lexeme_id:16 | lexical_slot:2 | inflection_key:3 | reserved:3 = 24 bits
```

Esse perfil de 3 bytes foi materializado e reconstrói os 62.086 radicais sem
divergências. Ele reduz mais 124.172 bytes crus, ao custo de uma leitura
indireta do lexema e do pool de strings durante a comparação. Por isso o
perfil de 5 bytes continua sendo uma alternativa razoável se o benchmark de
busca mostrar que a indireção é cara.

### `INFLECTS.SEC`

O arquivo reserva cinco seções de 570 registros. Cada registro mede 40 bytes;
a capacidade total de 2.850 posições é usada para apenas 1.785 regras.

| Métrica | Valor |
| --- | ---: |
| capacidade | 2.850 registros |
| regras usadas | 1.785 |
| posições nulas | 1.065 |
| bytes das posições nulas | 42.600 |
| regras semanticamente distintas | 1.784 |
| descritores morfológicos distintos | 1.538 |
| terminações distintas | 460 |
| pares distintos `(stem_key, terminação)` | 614 |

A regra duplicada corresponde a:

```text
VPAR 5 1 NOM S X PRES ACTIVE PPL 2 3 ens X A
```

Ela aparece duas vezes consecutivas em `INFLECTS.LAT`.

#### Terminações

Cada regra reserva sete bytes para a terminação, mesmo quando ela é vazia ou
curta:

| Medida | Bytes |
| --- | ---: |
| espaço reservado nas 1.785 regras | 12.495 |
| caracteres efetivamente usados | 5.560 |
| caracteres das 460 terminações únicas | 1.934 |
| pool único com comprimento `u8` | 2.394 |

Distribuição dos comprimentos:

| Comprimento | Regras |
| ---: | ---: |
| 0 | 58 |
| 1 | 271 |
| 2 | 463 |
| 3 | 215 |
| 4 | 378 |
| 5 | 249 |
| 6 | 96 |
| 7 | 55 |

#### Seções fixas

As seções atuais são um índice por letra final misturado com armazenamento.
Isso força todas as seções a ter a capacidade da maior delas. Em um formato
novo, o índice deve conter apenas intervalos `(início, quantidade)`, enquanto
os registros ficam em um vetor denso.

### `INDXFILE.GEN`

O índice contém 703 linhas ASCII de 46 bytes. Cada linha reserva dois campos
decimais de 20 caracteres para posições que nunca excedem 62.086.

- 461 intervalos, ou 65,6%, são `0 0`;
- apenas 242 intervalos contêm radicais;
- o arquivo ocupa 32.338 bytes.

Como os registros são contíguos por prefixo, basta guardar 704 fronteiras. No
perfil de 16 bits:

```text
704 × uint16 little-endian = 1.408 bytes
```

Intervalos vazios aparecem naturalmente quando duas fronteiras consecutivas
são iguais. Outra opção é eliminar o índice e fazer busca binária global, mas
1.408 bytes é um custo pequeno para preservar o algoritmo atual.

## Auditoria de bits e tipos

### Larguras atuais e larguras necessárias

Esta tabela usa o domínio declarado quando ele é relevante e o domínio
semântico efetivamente aceito quando o legado declarou um intervalo mais
amplo que o usado.

| Campo | Hoje | Domínio útil | Bits suficientes |
| --- | ---: | --- | ---: |
| classe gramatical | 8 bits | 16 valores | 4 |
| `which` | 32 bits | `0..9` | 4 |
| `variant` | 32 bits | `0..9` | 4 |
| chave de radical no dicionário | 32 bits | `0..4` | 3 |
| chave de radical na flexão | 32 bits | `1..4` | 2 |
| tamanho da terminação | 32 bits | `0..7` | 3 |
| `MNPC` | 64 bits | `1..39.339` | 16 |
| valor numeral | 32 bits | `0..1.000` | 10 |
| caso | 8 bits | 8 valores | 3 |
| número | 8 bits | 3 valores | 2 |
| gênero | 8 bits | 5 valores | 3 |
| grau | 8 bits | 4 valores | 2 |
| tempo | 8 bits | 7 valores | 3 |
| voz | 8 bits | 3 valores | 2 |
| modo | 8 bits | 6 valores | 3 |
| pessoa | 8 bits | `0..3` | 2 |
| época | 8 bits | 9 valores | 4 |
| frequência | 8 bits | 10 valores | 4 |
| área | 8 bits | 12 valores | 4 |
| geografia | 8 bits | 18 valores | 5 |
| fonte | 8 bits | 26 valores | 5 |

Não é necessário compactar todo enum até o último bit. Um byte por enum tende
a ser uma boa escolha na API em memória. A compactação vale especialmente para
campos agrupados e milhões de repetições. O problema principal do legado não é
um enum de três valores ocupar oito bits; é combinar isso com inteiros de 32
bits, unions de tamanho máximo, padding e duplicação entre tabelas.

### Enums existentes e enums ausentes

O código Ada já usa enums para classe gramatical, caso, número, gênero, grau,
tempo, voz, modo e metadados. Porém, o arquivo persistido contém somente seus
ordinais. Não há no binário:

- versão do conjunto de enums;
- tabela de nomes;
- validação de faixas;
- indicação de que uma nova constante foi inserida ou reordenada;
- distinção entre valor desconhecido, não aplicável e curinga.

Além disso, alguns conceitos continuam como intervalos numéricos genéricos:

- `Which_Type` e `Variant_Type` são apenas `Natural range 0 .. 9`;
- `Stem_Key_Type` permite `0..9`, embora o modelo tenha no máximo quatro
  radicais;
- `Person_Type` é um inteiro `0..3`;
- IDs físicos como `MNPC` são confundidos com a largura de
  `Ada.Direct_IO.Count`.

O valor `X` também acumula significados diferentes: “desconhecido”, “qualquer
um”, “nenhum” e “não aplicável”. Isso é conveniente no algoritmo legado, mas
é frágil numa API tipada. O formato pode preservar o ordinal zero, enquanto o
modelo C++ expõe conceitos separados, por exemplo `std::optional<T>` para
“não aplicável” e um operador explícito de wildcard durante a comparação.

## Como poderia ser

### Status: medição, proposta e especificação

Este relatório mistura deliberadamente três níveis de trabalho, que não devem
ser confundidos:

1. a auditoria do legado registra fatos verificados desta cópia;
2. os quatro perfis do PoC medem tamanho e compressibilidade;
3. recomendações para o WWDB de produção ainda são propostas.

O layout emitido por `wwdb_poc_pack.cpp` é portátil e determinístico para a base
inspecionada, mas **não é a especificação normativa WWDB v1**. Valores de tipos
de seção, flags, layouts completos de bits, requisitos por perfil, tratamento
de seções desconhecidas e cobertura do checksum ainda precisam ser reunidos em
um documento normativo acompanhado por um leitor independente. Até lá, o código
do empacotador descreve o artefato experimental, não um contrato congelado para
outros produtores.

### Princípios

1. O formato de disco é uma sequência especificada de bytes, não uma imagem de
   `struct`.
2. Todos os inteiros são little-endian e têm largura declarada.
3. Enums têm valores explícitos, versão e validação.
4. Registros são densos; índices guardam intervalos, não capacidade ociosa.
5. Strings são UTF-8 com comprimento explícito e deduplicação por pool.
6. Referências usam IDs, nunca ponteiros ou offsets derivados da ABI.
7. Toda seção tem tipo, offset, tamanho, quantidade e stride.
8. O carregador valida limites, overflow, cardinalidade, referências e
   checksum antes de expor qualquer view.
9. Campos reservados devem ser escritos como zero e rejeitados ou ignorados de
   acordo com a versão.

### IDs densos: o índice do vetor é o ID

Quando os IDs ocupam toda a faixa `0..N-1`, o próprio array é o mapa. O ID não
deve ser repetido dentro do registro e não é necessária uma tabela
`id -> posição`:

```cpp
enum class LexemeId : std::uint16_t {};

std::span<const LexemeRecord> lexemes;

const LexemeRecord& get(LexemeId id) {
    if (std::to_underlying(id) >= lexemes.size()) {
        throw invalid_database{"lexeme id out of range"};
    }
    return lexemes[std::to_underlying(id)];
}
```

No wire format deste PoC, isso se aplica a:

| Domínio | Faixa normalizada | Consequência |
| --- | --- | --- |
| `MNPC`/lexemas | `0..39.338` | `lexeme_id = MNPC - 1`; posição no vetor |
| referências de radical | `0..62.085` | posição na seção; nenhum ID gravado |
| regras de flexão | `0..1.784` | posição no vetor denso |
| radicais únicos | `0..48.711` | ordinal no pool |
| significados únicos | `0..32.485` | ordinal no pool |
| terminações únicas | `0..459` | ordinal no pool |
| prefixos | `0..703` | posição calculada a partir das duas letras |

Os 39.339 valores legados de `MNPC` aparecem na base; não há buracos que
exijam uma tabela de tradução. As cinco seções de flexão e os prefixos não
precisam de IDs em cada entrada: pequenos vetores de fronteiras delimitam
intervalos dentro dos arrays densos.

Há três ressalvas:

- `inflection_key` é um atributo semântico `0..4`, não uma identidade;
- pools de strings com entradas variáveis precisam de offsets em RAM ou de
  checkpoints para acesso aleatório, embora o ID continue sendo o ordinal;
- se um ID for persistido fora do WWDB, a ordem física deixa de ser um detalhe
  interno. Nesse caso é preciso ordenar deterministicamente, manter tabela de
  compatibilidade ou definir outro identificador estável.

### Organização sugerida de `words.wwdb`

```text
+-------------------------------+
| Header                        |
| magic/version/flags/checksum  |
+-------------------------------+
| Diretório de seções           |
+-------------------------------+
| StemStringPool                |
| MeaningStringPool             |
| EndingStringPool              |
| LexemeRecords                 |
| StemReferences                |
| FirstTwoLetterBoundaries      |
| InflectionRecords             |
| InflectionEndingBoundaries    |
| Uniques / Addons (opcionais)  |
+-------------------------------+
```

Um header possível tem 40 bytes:

| Campo | Tipo |
| --- | --- |
| magic | 8 bytes, por exemplo `WWDB\r\n\x1a\n` |
| versão maior/menor | `u16`, `u16` |
| tamanho do header | `u32` |
| número de seções | `u32` |
| perfil de empacotamento | `u32` (`1` simples, `2` denso, `3` colunar, `4` busca sem definições no PoC) |
| tamanho total | `u64` |
| CRC32 do payload | `u32` |
| reservado, sempre zero | `u32` |

No artefato do PoC, "payload" significa somente a concatenação dos dados das
seções; header e diretório não participam do CRC. Isso basta para reproduzir as
medições, mas deixa metadados estruturais fora da detecção de corrupção. A
especificação de produção deve calcular o checksum sobre a imagem inteira com
o campo de checksum zerado, ou oferecer proteção equivalente claramente
definida. O SHA-256 do manifesto continua sendo a identidade de distribuição.

Cada entrada do diretório pode ter 32 bytes:

```text
section_type:u32
section_flags:u32
offset:u64
byte_size:u64
record_count:u32
record_stride:u32
```

No PoC, `section_flags=1` marca registros em linhas e `section_flags=2`
registros em colunas de bytes. Uma especificação final deve transformar esses
números em flags nomeados e rejeitar combinações desconhecidas.

Os campos são serializados individualmente. Esses tamanhos não autorizam
gravar uma `struct` C++ de 40 ou 32 bytes.

### Perfil compacto desta base

#### Pools de strings

Como todas as strings atuais têm até 80 bytes, uma entrada simples basta:

```text
length:u8 + UTF-8 bytes[length]
```

O carregador percorre o pool uma vez e constrói uma tabela de offsets em RAM.
Isso evita gravar quatro bytes de offset por string. Se acesso mapeado sem
inicialização for prioritário, uma seção opcional de offsets `u32` pode ser
adicionada.

#### Perfil simples: registro lexical de 19 bytes

```text
stem_id[4]       4 × u16 = 8 bytes
meaning_id       u16     = 2 bytes
part_of_speech   u8      = 1 byte
paradigm         u8      = 1 byte   // which:4 | variant:4
attribute_0      u8      = 1 byte
attribute_1      u8      = 1 byte
numeric_value    u16     = 2 bytes
translation      22 bits = 3 bytes
                             total = 19 bytes
```

`attribute_0` e `attribute_1` são interpretados pela classe gramatical. Para
substantivos, por exemplo, guardam gênero e tipo de substantivo. Para numerais,
`numeric_value` guarda `0..1000`; nas demais classes ele deve ser zero.

Uma evolução posterior pode separar as classes em tabelas próprias e remover
campos inativos. O registro fixo de 19 bytes é proposto como primeiro passo por
ser simples, pequeno e fácil de validar.

#### Perfil simples: referência de radical de 5 bytes

```text
stem_id:u16 + lexeme_id:u16 + inflection_key:u8
```

Não há classe gramatical duplicada. Ela é obtida de `LexemeRecords[lexeme_id]`.

#### Perfil simples: regra de flexão de 8 bytes

```text
part_of_speech    u8
paradigm          u8      // which:4 | variant:4
morphology        u16     // layout depende da classe
ending_and_key    u16     // ending_id:9 | (stem_key - 1):2
age_frequency     u8      // age:4 | frequency:4
flags             u8      // zero na versão 1
```

O maior payload morfológico é o de particípio e cabe exatamente em 16 bits:

```text
case:3 + number:2 + gender:3 + tense:3 + voice:2 + mood:3 = 16
```

Verbos finitos precisam de apenas 12 bits:

```text
tense:3 + voice:2 + mood:3 + person:2 + number:2 = 12
```

Os layouts dos bits devem fazer parte da especificação versionada. Não se deve
usar bitfields C++, cujo layout é dependente da implementação.

#### Perfil denso de 16/3/6 bytes

A segunda passada usa os mesmos pools e arrays, mas remove campos deriváveis e
aproveita o bit restante nos registros:

```text
LexemeRecord       16 bytes = stem_id[4]:64 | meaning_id:16 | metadata:48
StemReference       3 bytes = lexeme_id:16 | lexical_slot:2
                              | inflection_key:3 | reserved:3
InflectionRecord    6 bytes = pofs:4 | paradigm:8 | morphology:16
                              | ending_id:9 | stem_key:2
                              | age:4 | frequency:4 | reserved:1
```

Os 48 bits de metadata lexical contêm classe, paradigma, tradução e um payload
dependente da classe. O maior é o numeral, com `sort:3 | value:10`. O
substantivo usa `gender:3 | noun_kind:4`. O empacotador rejeita valores que não
caibam antes de serializar.

#### Perfil por colunas

O terceiro perfil mantém exatamente os mesmos 2.700.358 bytes do perfil denso,
mas transforma cada seção fixa de registros de linhas para colunas de bytes:

```text
AoS: r0.b0 r0.b1 ... r0.bN  r1.b0 r1.b1 ... r1.bN
SoA: r0.b0 r1.b0 ...        r0.b1 r1.b1 ...
```

Isso implementa a ideia de vetores densos também fisicamente: o byte `k` de
um campo para todos os IDs fica contíguo. O leitor não precisa desfazer o
shuffle se fornecer accessors que reconstruam o campo diretamente das
colunas. A organização favorece compressão porque agrupa distribuições
semelhantes e pode evitar carregar colunas frias durante uma busca.

O custo potencial é espalhar a leitura de um registro completo entre várias
regiões. Logo, a escolha entre linhas e colunas deve ser feita com benchmark
do algoritmo WASM real, e não apenas pelo tamanho do arquivo.

### Estimativa do perfil simples

| Seção proposta | Cálculo | Bytes |
| --- | --- | ---: |
| pool de radicais | 383.263 caracteres + 48.712 comprimentos | 431.975 |
| pool de significados | 1.405.395 + 32.486 comprimentos | 1.437.881 |
| pool de terminações | 1.934 + 460 comprimentos | 2.394 |
| entradas lexicais | 39.339 × 19 | 747.441 |
| referências de radical | 62.086 × 5 | 310.430 |
| regras de flexão | 1.785 × 8 | 14.280 |
| fronteiras de duas letras | 704 × 2 | 1.408 |
| fronteiras das cinco seções de flexão | 6 × 2 | 12 |
| header + 8 entradas de diretório | 40 + 8 × 32 | 296 |
| **Total previsto e observado** |  | **2.946.117** |

Comparação:

| Representação | Bytes | Redução contra o legado |
| --- | ---: | ---: |
| quatro arquivos legados | 10.704.174 | — |
| PoC simples sem compressão | 2.946.117 | 72,48% |
| PoC denso/colunar sem compressão | 2.700.358 | 74,77% |
| legado com `gzip -9` | 1.608.396 | 85,0% |
| legado com `xz -9` | 1.120.080 | 89,5% |

Os números comprimidos das duas últimas linhas são medições do legado. O PoC
compacto foi medido separadamente abaixo.

## PoC rápido materializado — baseline WWDB 1.0

O empacotador está em
[`poc/compact-db/wwdb_poc_pack.cpp`](../poc/compact-db/wwdb_poc_pack.cpp). Ele
lê os três binários GNAT desta cópia e gera o container explicitamente
little-endian descrito neste documento. A opção `simple`, `dense`, `columnar`
ou `search-only` seleciona o perfil. A lista abaixo registra o estado 1.0 que
produziu as medições históricas desta seção:

- não lê ainda as fontes textuais;
- não inclui `UNIQUES.LAT` nem `ADDONS.LAT`;
- não contém, no próprio diretório do PoC, o leitor do futuro runtime;
- preserva as 1.785 regras, inclusive a duplicata conhecida;
- usa o perfil de IDs `u16`, que não é necessariamente o perfil definitivo.

O código foi compilado, sem warnings, com:

```text
g++-14 14.2.0, -std=c++23 -O2 -Wall -Wextra -Wpedantic -Wconversion
clang++-19 19.1.1, mesmas opções
```

Os dois executáveis produziram arquivos byte a byte idênticos nos quatro
perfis. Durante a investigação, um leitor auxiliar conferiu header, limites do
diretório, CRC32, equivalência `columnar -> dense` e a reconstrução dos 62.086
radicais e chaves do legado a partir das referências de 3 bytes. Esse leitor
auxiliar não está versionado neste diretório; portanto a afirmação registra uma
checagem realizada, mas ainda não constitui um teste reproduzível do repositório.
O projeto raiz passou a conter um leitor/validador C++23 para o perfil
`dense` por linhas e o usa nos testes da engine nominal. Ele torna a checagem
desse perfil reproduzível, mas ainda é uma ponte para o PoC: os outros perfis
e o formato WWDB definitivo continuam pendentes antes de congelar WWDB v1.
As cardinalidades observadas são:

```text
lexemes=39339
stem_strings=48712
meaning_strings=32486
ending_strings=460
stem_references=62086
inflections=1785
```

Tamanhos e SHA-256 dos containers:

| Perfil | Bytes | SHA-256 |
| --- | ---: | --- |
| simples | 2.946.117 | `45c58610616b5223180f56f93f04b6d64b31c1e8e232032a5b868ab497ee49f9` |
| denso por linhas | 2.700.358 | `1ba52444760d4233956a59b8275f5bd034b1e5be2ecd578b72a7ae91e2beed69` |
| denso por colunas | 2.700.358 | `40629ca3352651f14f7893875ddee85a9409a3139d20e95b6b25e7aa246d4321` |
| busca sem definições | 1.183.767 | `bde18b2eb0b2c57aab40a34681c97c39d7492b627745e7759420c702f591916f` |

Contra o perfil simples, a densificação economiza 245.759 bytes crus, ou
8,34%:

| Mudança | Economia crua |
| --- | ---: |
| lexemas de 19 para 16 bytes | 118.017 |
| referências de radical de 5 para 3 bytes | 124.172 |
| flexões de 8 para 6 bytes | 3.570 |
| **Total** | **245.759** |

### Compressão do PoC

| Perfil | Cru | `gzip -9` | `zstd -3` | `zstd -19` |
| --- | ---: | ---: | ---: | ---: |
| simples | 2.946.117 | 1.326.240 | 1.376.415 | 1.149.994 |
| denso por linhas | 2.700.358 | 1.243.838 | 1.280.537 | 1.081.409 |
| denso por colunas | 2.700.358 | 1.028.906 | 1.043.538 | 870.406 |
| busca sem definições, colunar | 1.183.767 | 409.145 | 436.141 | 353.268 |

Hashes dos artefatos exportados:

```text
45c58610616b5223180f56f93f04b6d64b31c1e8e232032a5b868ab497ee49f9  words-poc.wwdb
23ee12fd714363388f407e0b563869d6329f617edb76033126558894a5079175  words-poc.wwdb.gz
824da98502b0cf02264fa499659988827aa37e34bd9fe4a6a5f38042de751974  words-poc.wwdb.zst
1ba52444760d4233956a59b8275f5bd034b1e5be2ecd578b72a7ae91e2beed69  words-poc-dense.wwdb
1f14bfe8d4f69a9361fcc950578ef00d4baedc41e4ba13b2779c21e3032f9fa3  words-poc-dense.wwdb.gz
f260e187957c81f199fac4050d1e69b065c7339b9a83a93a21bc6923464a7393  words-poc-dense.wwdb.zst
40629ca3352651f14f7893875ddee85a9409a3139d20e95b6b25e7aa246d4321  words-poc-columnar.wwdb
5854098700ff3c0fe48280e8ad5b3667199e2a6f137a5a7d37bc234cbf25e6b3  words-poc-columnar.wwdb.gz
116bd4c2ac5d2157f2b2d8d05c883ef4d074a84c94352130b672bebb06f003fd  words-poc-columnar.wwdb.zst
bde18b2eb0b2c57aab40a34681c97c39d7492b627745e7759420c702f591916f  words-poc-search-only.wwdb
febba5c6b3b7d67a7ef203f78818fa3a2a25ddb2d1dc53e3b4e50de09599d0b8  words-poc-search-only.wwdb.gz
365c915d6bd19fdda4a92c98d4aa4482d0dde844f27261f15bdf90673aef4b7e  words-poc-search-only.wwdb.zst
```

O pacote `poc/compact-db/output/wwdb-poc-material.zip` reúne o empacotador,
este relatório, o README, os dois contratos JSON e schemas, os quatro
containers, suas representações gzip/Zstd e o manifesto `SHA256SUMS`. O hash do
próprio ZIP fica em `output/ZIP.SHA256`, fora dele, para evitar uma dependência
circular.

No perfil simples, o Zstd nível 19 reduz mais 176.246 bytes, ou 13,29%, em
relação ao gzip nível 9. O nível 3, mais próximo de compressão dinâmica rápida,
fica 50.175 bytes maior que o gzip pré-comprimido. Logo, “Zstd é menor” não
pode ser generalizado sem fixar nível, versão e estratégia de pré-compressão.

A observação nova é que **layout supera micro-bit-packing para a rede**. O
perfil denso por linhas poupa 82.402 bytes em gzip contra o simples; somente
reordenar os mesmos bytes densos por coluna poupa outros 214.932 bytes. Em
Zstd nível 19, a passagem de linhas para colunas poupa 211.003 bytes. São
medições do artefato, mas ainda falta medir o custo dos accessors no WASM.

### Comparação direta: original contra PoC

Esta é a comparação consolidada, com o mesmo escopo e os mesmos comandos. O
original é a soma dos quatro arquivos centrais `DICTFILE.GEN`, `STEMFILE.GEN`,
`INFLECTS.SEC` e `INDXFILE.GEN`. Cada arquivo original foi comprimido
separadamente, como seria servido em quatro respostas. Cada perfil WWDB é um
único arquivo/resposta.

```text
gzip -9 -n -c ARQUIVO
zstd -3  -q -c ARQUIVO
zstd -19 -q -c ARQUIVO
```

| Representação do núcleo | RAW | `gzip -9 -n` | `zstd -3` | `zstd -19` |
| --- | ---: | ---: | ---: | ---: |
| original, 4 arquivos | 10.704.174 | 1.608.344 | 1.699.489 | 1.216.351 |
| PoC simples | 2.946.117 | 1.326.240 | 1.376.415 | 1.149.994 |
| PoC denso por linhas | 2.700.358 | 1.243.838 | 1.280.537 | 1.081.409 |
| PoC denso por colunas | 2.700.358 | 1.028.906 | 1.043.538 | 870.406 |
| PoC busca sem definições | 1.183.767 | 409.145 | 436.141 | 353.268 |

Redução de cada PoC contra o original na mesma coluna:

| Perfil | RAW | `gzip -9 -n` | `zstd -3` | `zstd -19` |
| --- | ---: | ---: | ---: | ---: |
| simples | 72,48% | 17,54% | 19,01% | 5,46% |
| denso por linhas | 74,77% | 22,66% | 24,65% | 11,09% |
| denso por colunas | 74,77% | 36,03% | 38,60% | 28,44% |
| busca sem definições | 88,94% | 74,56% | 74,34% | 70,96% |

Em bytes absolutos, o perfil colunar evita 8.003.816 bytes RAW, 579.438 bytes
em gzip, 655.951 bytes em Zstd nível 3 e 345.945 bytes em Zstd nível 19. A
soma do original contém três headers de compressão a mais que o WWDB único;
esse overhead é desprezível diante das diferenças observadas, mas o escopo de
quatro respostas está declarado para a comparação ser reproduzível.

`UNIQUES.LAT` e `ADDONS.LAT` não entram nesta tabela porque ainda não haviam
sido incorporados no snapshot 1.0. A comparação histórica de rede que os
mantém iguais nos dois lados continua na seção de WebAssembly abaixo.

### Perfil de busca sem definições

Este perfil atende o caso em que o WWDB auxilia autocomplete e análise
morfológica, enquanto o índice do site estático resolve título, URL e conteúdo
da página. Ele deriva do perfil denso colunar e remove:

```text
MeaningStringPool                         1.437.881 bytes
meaning_id:u16 de 39.339 lexemas             78.678 bytes
uma entrada do diretório de seções               32 bytes
                                             ---------
economia RAW                               1.516.591 bytes
```

O registro lexical cai de 16 para 14 bytes:

```text
stem_id[4]   8 bytes
metadata     6 bytes
             --------
total       14 bytes
```

O container resultante mantém radicais, classe, paradigma, atributos,
metadados históricos, referências e flexões. Ele consegue identificar e
reconstruir formas como `anas, anatis`, mas não contém “duck/pato” nem qualquer
outra definição lexical.

Consequentemente, esse perfil não produz sozinho o documento completo
`whitakers-words.analysis`, no qual `lexeme.meaning` é obrigatório. Sua saída
natural é o contrato separado
[`formato-json-busca.md`](formato-json-busca.md), com IDs locais a um
`datasetId`. Produzir o JSON completo requer o perfil com definições ou uma
junção com o índice editorial da mesma build.

| Comparação | RAW | `gzip -9 -n` | `zstd -3` | `zstd -19` |
| --- | ---: | ---: | ---: | ---: |
| original atual, com definições | 10.704.174 | 1.608.344 | 1.699.489 | 1.216.351 |
| original ABI, campos de significado preenchidos apenas com espaços | 10.704.174 | 827.379 | 1.000.993 | 639.900 |
| PoC colunar com definições | 2.700.358 | 1.028.906 | 1.043.538 | 870.406 |
| **PoC busca sem definições** | **1.183.767** | **409.145** | **436.141** | **353.268** |

Contra o perfil colunar com definições, o `search-only` economiza 56,16% RAW,
60,23% em gzip e 59,41% em Zstd nível 19. Mesmo contra um legado
ABI-compatible com todos os significados apagados, ele economiza 50,55% em
gzip e 44,79% em Zstd nível 19. O tamanho RAW desse legado não muda porque a
ABI continua reservando 80 bytes por entrada.

#### Contrato com o índice do site

O resultado da operação de busca deve carregar identidade, não conteúdo
editorial. O tipo C++ conceitual correspondente ao JSON enxuto é:

```cpp
struct SearchHit {
    LexemeId lexeme_id;       // ID denso interno desta build
    std::optional<RuleId> inflection_id;
    std::vector<AddonId> addon_ids;
    std::uint16_t score_flags;
};
```

Há duas opções para ligar `lexeme_id` à página estática:

1. gerar WWDB e índice do site no mesmo build, usando `lexeme_id` como posição
   comum em um array externo de URLs/títulos;
2. manter no WWDB um `site_entry_id` estável, provavelmente `u32`, se páginas
   precisarem sobreviver à reordenação do léxico.

A primeira opção não custa bytes por lexema, mas exige que o manifesto do site
e o header WWDB compartilhem um `dataset_id`: o SHA-256 de um manifesto
canônico com fontes, versão do packer e atribuição de IDs. Não deve ser o hash
do arquivo que contém o próprio campo. O frontend rejeita ou recarrega
artefatos de builds diferentes. A segunda opção acrescentaria até 157.356
bytes crus com um `u32` por lexema, antes de compressão.

Para `anaticulus`, a engine pode retornar os `lexeme_id` 3339 e 3340, a análise
`N 2 1 NOM S M` e um `addon_ids` contendo `icul`. Isso dependia, no snapshot
1.0, da incorporação de `ADDONS.LAT`; sufixos e prefixos já estão incorporados
no WWDB 1.3. O componente de busca do site decide qual página, título ou
definição apresentar.

#### Glossas pequenas de addons

Eliminar definições lexicais não obriga eliminar explicações morfológicas. O
arquivo atual possui 343 regras de addon, mas apenas 225 textos explicativos
distintos. Um pool `length:u8 + bytes` para todos os textos únicos ocupa
11.440 bytes; um `meaning_id:u8` por regra acrescenta 343 bytes. Portanto, até
mesmo preservar as descrições completas de addons custaria aproximadamente
**11.783 bytes RAW**, 4.992 em gzip ou 4.858 em Zstd nível 19 quando esse
payload é medido isoladamente, antes de integrar os registros semânticos de
`ADDONS.LAT`.

As três regras `SUFFIX icul` compartilham exatamente a mesma descrição; elas
podem apontar para um único ID. Uma taxonomia curada, por exemplo
`AddonSemanticKind::diminutive`, seria ainda menor e localizável, mas é uma
mudança editorial. No snapshot 1.0 o PoC ainda não compilava `ADDONS.LAT`;
11.783 bytes é uma medição isolada do pool/gloss IDs, não o tamanho da seção
implementada posteriormente no WWDB 1.3.

#### Empacotamento por caso de uso

Não é necessário escolher um único banco para todos os clientes:

```text
words-search.wwdb             morfologia + IDs + glossas pequenas
words-meanings.wwdb           meaning_id[lexeme] + pool, opcional
índice do site estático       URL/título/definição, contrato externo
```

O autocomplete carrega somente `words-search`. Um modo de dicionário offline
pode buscar também o companion de significados, cuja identidade deve usar o
mesmo `dataset_id`. Isso evita duplicar definições em páginas que já possuem
seu próprio índice e conteúdo.

Os metadados lexicais de época, área, geografia, frequência e fonte foram
mantidos no `search-only`; frequência e época podem ser úteis para ranking e
filtros. Se o autocomplete nunca usar área/geografia/fonte, um perfil ainda
mais restrito poderia reduzir a metadata lexical de 6 para 5 bytes, economizando
mais 39.339 bytes RAW. Essa é uma projeção de layout ainda não materializada.

Os 409.145 bytes em gzip ou 353.268 em Zstd são o custo do núcleo WWDB de
busca, não o custo total da página. `words.wasm`, loader e o índice estático do
site continuam fora da conta. Quando esse índice já é necessário para a barra
de busca, o WWDB é custo marginal e não deve repetir títulos ou definições.

### CPU cache e caminho de pesquisa

Esta projeção segue o fluxo conferido em
[`algoritmo-analise-morfologica.md`](algoritmo-analise-morfologica.md) e os
procedimentos Ada `Run_Inflections`, `Dictionary_Search`, `Reduce_Stem_List` e
`Apply_Suffix`.

#### Julgamento

Há vantagem provável no cache para o WWDB como conjunto, mas não se pode
afirmar que todo perfil atual acelera uma consulta pontual:

- o núcleo denso reduz o working set decodificado de 10,70 MB para 2,70 MB;
- flexões compactas e seus finais ocupam 13,1 KB e podem permanecer em L1;
- morfologia lexical fica separada do pool de significados de 1,44 MB, que só
  precisa ser tocado na formatação dos resultados;
- a tabela legada mantém o texto de radical inline no registro de 56 bytes,
  enquanto os perfis compactos precisam seguir IDs até um pool;
- o byte-shuffle colunar melhora muito gzip/Zstd, mas espalha os bytes de um
  único registro entre colunas distantes.

Logo, o perfil colunar atual é o melhor artefato de rede medido, mas não é
automaticamente o melhor layout de execução. Um layout híbrido, colunar por
**campos semânticos** e com um índice de pesquisa próprio, é o candidato mais
forte para o runtime.

As contas abaixo usam linhas de cache de 64 bytes. A máquina desta análise tem
L1d de 32 KiB, L2 de 512 KiB e L3 de 32 MiB por domínio informado pelo sistema;
isso é apenas uma referência. O navegador poderá executar em outra
microarquitetura.

| Região potencialmente tocada | Bytes | Linhas de 64 B | Páginas de 4 KiB |
| --- | ---: | ---: | ---: |
| `STEMFILE.GEN` legado | 3.476.816 | 54.326 | 849 |
| `DICTFILE.GEN` legado | 7.081.020 | 110.641 | 1.729 |
| seção de flexões relida por palavra no Ada | 22.800 | 357 | 6 |
| referências simples de radical | 310.430 | 4.851 | 76 |
| referências densas de radical | 186.258 | 2.911 | 46 |
| lexemas densos | 629.424 | 9.835 | 154 |
| lexemas `search-only` | 550.746 | 8.606 | 135 |
| pool de radicais | 431.975 | 6.750 | 106 |
| pool de significados, frio durante a busca | 1.437.881 | 22.467 | 352 |
| finais + regras densas + fronteiras | 13.116 | 205 | 4 |

O conjunto `referências densas + lexemas densos + pool de radicais` tem cerca
de 1,25 MB. Ele não cabe no L2 de 512 KiB desta máquina, mas é muito menor que
`STEMFILE + DICTFILE`, reduz pressão sobre L3, TLB e memória em lotes de
consultas. A vantagem é especialmente plausível quando o Worker permanece
vivo e atende muitas palavras.

#### Custo de uma busca pontual nos layouts modelados

Foi feito um modelo de linhas tocadas para duas buscas binárias na faixa `an`:
primeiro `anaticul`, depois `anat`. A faixa contém 927 referências, 749 textos
distintos e requer aproximadamente dez comparações por busca. Há um hit para
`anaticul` e dois para `anat`.

| Layout modelado | Linhas distintas tocadas | Payload mínimo dessas linhas |
| --- | ---: | ---: |
| legado com radical inline, hipoteticamente em memória | 16 | 1.024 B |
| PoC simples, referência de 5 B | 19 | 1.216 B |
| PoC denso por linhas, referência de 3 B | 23 | 1.472 B |
| PoC denso com byte-shuffle colunar | 37 | 2.368 B |
| PoC `search-only` com byte-shuffle colunar | 35 | 2.240 B |

Esse modelo não é um benchmark de tempo. Ele usa uma busca `lower_bound`
idealizada, conta linhas lógicas únicas, inclui a leitura morfológica dos três
hits e não inclui flexões, addons, código, prefetch nem cache já aquecido. Para
dar ao legado o melhor caso possível, supõe `STEMFILE` mapeado em memória. O
Ada real usa `Direct_IO`, relê uma seção de 22.800 bytes de `INFLECTS.SEC` por
consulta regular e copia registros para estruturas temporárias.

O resultado explica o trade-off: o radical inline do legado é bom para uma
busca fria isolada, mas seu working set global é grande. O perfil compacto
vence em ocupação, enquanto o byte-shuffle ingênuo pode perder em número de
indireções. Isso deve orientar um quarto perfil, não uma volta aos binários de
ABI.

#### Índices sugeridos para o runtime

Um desenho cache-aware pode manter IDs densos e separar dados quentes de
dados frios:

```text
EndingTrie                       // caminha a palavra de trás para frente
EndingRuleBoundaries[u16]        // ending_id -> intervalo contíguo
InflectionRulesByEnding[]        // registros compactos agrupados pelo final

StemTrie                         // texto normalizado -> stem_group_id
StemGroupBoundaries[]            // grupo -> intervalo de referências
StemRefLexemeId[u16]             // quente depois do hit
StemRefKey[3 bits]               // quente depois do hit

LexemeMorphology[6 bytes]        // AoS pequeno ou colunas por campo
LexemeStemIds[4][u16]            // usado depois de aceitar o lexema
LexemeMeaningId[u16]             // frio até formatar a saída

AddonSuffixTrie
AddonRulesBySuffix[]
```

O pool possui 48.463 radicais distintos depois de normalizar maiúsculas,
`j -> i` e `v -> u`. Um trie simples sobre esta cópia teria 97.451 prefixos;
uma contagem de nós terminais ou de decisão de um radix trie dá 60.017. Isso
não define ainda o tamanho final, mas mostra que um índice de prefixos compacto
é plausível.

Um MPHF ou hash aberto compacto é alternativa para consultas exatas. O trie
tem uma propriedade especialmente útil para o algoritmo do WORDS: vários
radicais candidatos são prefixos da mesma forma. Uma única caminhada por
`anaticulus` pode observar os terminais `anat` e `anaticul`, evitando duas
buscas binárias que repetem os caracteres iniciais.

O layout colunar deveria ser por campo, não por byte individual. Por exemplo,
`StemRefLexemeId` deve ser um vetor de `u16`, para que uma carga leia o ID
inteiro; separar seu byte baixo e alto por 62.086 posições melhora compressão,
mas força duas linhas/streams. Se o layout de rede continuar byte-shuffled, o
loader pode transpor apenas as estruturas quentes uma vez, embora isso tenha
custo de startup e memória temporária.

#### Hash tables preparadas no runtime

Construir índices depois de validar e carregar o WWDB é uma boa estratégia de
primeira implementação. Para radicais, uma consulta média em hash troca cerca
de dez comparações binárias na faixa `an` por hash de no máximo 18 caracteres
e uma ou poucas sondagens.

O valor da tabela não deve ser um único ponteiro. Radicais, finais e addons
podem ter várias entradas com a mesma string. O valor correto é um intervalo
sobre um vetor denso:

```cpp
struct Range {
    std::uint32_t begin;
    std::uint16_t count;
};

using StemIndex = std::pmr::unordered_map<
    std::string_view, Range, TransparentStemHash, TransparentStemEqual>;

std::span<const StemRef> lookup_stem(std::string_view normalized) {
    const auto found = stem_index.find(normalized);
    if (found == stem_index.end()) {
        return {};
    }
    return stem_refs.subspan(found->second.begin, found->second.count);
}
```

`std::span` expressa melhor o tempo de vida que um par de ponteiros. Para uma
seção bit-packed ou colunar, `Range`/IDs são ainda mais adequados, pois não há
necessariamente um `StemRef` C++ real para onde apontar. Nenhum ponteiro deve
ser persistido no WWDB.

As chaves podem ser `string_view` sobre um pool canônico e imutável, evitando
copiar 48 mil strings. O packer deve gravar as chaves de busca já normalizadas
em ASCII (`lowercase`, `j -> i`, `v -> u`), mantendo grafia de exibição em
outro campo quando necessário. O storage do banco e os vetores apontados não
podem ser movidos ou realocados depois de construir o índice. No WASM, views
JavaScript sobre `WebAssembly.Memory` precisam ser refeitas após crescimento
da memória, mesmo que os offsets usados pelo código C++ continuem iguais.

Mácrons e breves futuros não exigem duplicar essas chaves. A proposta é manter
a base ASCII no índice e associar máscaras triestado de quantidade às regras e
aos slots lexicais, como detalhado em
[`plano-unicode-quantidade-vocalica.md`](plano-unicode-quantidade-vocalica.md).

Para evitar rehash e alocações gerais no startup:

```cpp
stem_index.max_load_factor(0.80F);
stem_index.reserve(stem_group_count);
```

Um `std::pmr::monotonic_buffer_resource` dimensionado no carregamento reduz o
custo do alocador e permite descartar todos os índices de uma vez. Ainda
assim, `std::unordered_map` típico é node-based: cada lookup segue bucket e
nó, e 48 mil chaves podem consumir alguns megabytes entre nós e buckets. O
tamanho é dependente da biblioteca, mas pode anular parte da economia de RAM
do formato denso.

Por isso há três níveis razoáveis de implementação:

1. **PoC funcional:** `std::pmr::unordered_map<string_view, Range>`, com
   `reserve`, lookup heterogêneo e vetores imutáveis;
2. **runtime cache-aware:** hash aberto/flat com slots contíguos contendo
   fingerprint + `stem_group_id`, verificando a string no pool em caso de hit;
3. **formato totalmente pré-computado:** MPHF ou radix trie gerado pelo packer,
   sem custo de construção no browser.

`std::map` não é uma boa substituição. A árvore mantém ordem de chave e oferece
`lower_bound`, mas usa aproximadamente uma alocação e vários ponteiros por nó.
Para dados imutáveis, uma sequência ordenada com `std::lower_bound` — ou
`std::flat_map`, quando disponível na biblioteca C++23 alvo — tende a ter
localidade muito melhor. Use árvore apenas se inserção/remoção ordenada em
runtime for realmente necessária, o que não ocorre no banco carregado.

##### Escolha de containers C++23

O container proprietário e o índice de consulta são responsabilidades
diferentes. O WWDB continua sendo uma imagem de bytes validada; containers
STL são construções de runtime e nunca definem o wire format.

| Responsabilidade | Container/view C++23 | Decisão |
| --- | --- | --- |
| imagem WWDB decodificada | `std::vector<std::byte>` proprietário + `std::span<const std::byte>` | uma alocação do tamanho final; não redimensionar depois de criar views |
| seções fixas em linhas | `std::span<const std::byte>` + accessor explícito | evita `reinterpret_cast` para structs com alinhamento/layout dependentes |
| colunas semânticas | `std::span<const T>` quando alinhado; accessor de bytes quando packed | uma view por coluna; sem cópia obrigatória |
| lexemas, regras e referências materializados | `std::pmr::vector<T>` | contíguo, iterável, IDs continuam sendo índices |
| índice exato de radicais/uniques | `std::pmr::unordered_map<std::string_view, Range>` | primeira implementação; `reserve` e lookup transparente |
| fallback ordenado/cache-friendly | `std::pmr::vector<IndexEntry>` + `std::ranges::lower_bound` | substitui `std::map` em dados imutáveis |
| trie de finais/prefixos/addons | `std::pmr::vector<TrieNode>` + `std::pmr::vector<TrieEdge>` | nós e arestas contíguos; cada terminal guarda `Range` |
| pequenos índices de cardinalidade fixa | `std::array<Range, N>` | por exemplo tamanho/final de terminação ou fronteiras conhecidas |
| comprimentos de radical candidatos | `std::bitset<19>` | substitui strings duplicadas apenas para marcar comprimentos `0..18` |
| scratch de uma consulta | `std::pmr::vector<RuleId/Candidate/Analysis>` | arena reiniciada por consulta ou lote; sem listas encadeadas |
| resultados finais | `std::pmr::vector<Analysis>` + `std::ranges::sort`/`unique` | comparador canônico; preserva todos os resultados antes de deduplicar |
| erro de carregamento | `std::expected<DatabaseHandle, LoadError>` | separa banco válido de diagnóstico sem exceção na ABI C/WASM |

`std::span`, `std::string_view`, `std::expected` e `std::bitset` não são
containers proprietários, mas completam o desenho: views não copiam, IDs
continuam pequenos e erros de validação não entram no hot path.

Uma trie compacta não exige um nó STL por prefixo:

```cpp
struct TrieNode {
    Range edges;               // intervalo em vector<TrieEdge>
    std::uint32_t group_id;    // sentinel quando não é terminal
};

struct TrieEdge {
    char character;
    std::uint32_t child;
};

std::pmr::vector<TrieNode> nodes;
std::pmr::vector<TrieEdge> edges;
```

As arestas de cada nó podem ficar ordenadas no intervalo. Como a ramificação
latina média é pequena, uma varredura linear de poucos bytes pode vencer busca
binária; isso deve ser medido. Para finais, armazenar os caracteres invertidos
usa exatamente a mesma estrutura.

O substituto disponível para `flat_map` nesta máquina é simples:

```cpp
struct IndexEntry {
    std::string_view key;
    Range values;
};

std::pmr::vector<IndexEntry> ordered_index;

auto lookup(std::string_view key) -> Range {
    const auto it = std::ranges::lower_bound(
        ordered_index, key, {}, &IndexEntry::key
    );
    return it != ordered_index.end() && it->key == key
        ? it->values
        : Range{};
}
```

Isso oferece a semântica necessária de `flat_map`, mas deixa construção e
imutabilidade explícitas. Não use `std::multimap` ou
`std::unordered_multimap`: duplicar um nó por regra perde o agrupamento que o
WWDB já pode fornecer. `map<key, Range>` é sempre melhor que
`multimap<key, value>` neste caso, e a versão contígua é melhor que ambas.

Containers a evitar no hot path:

- `std::list` e `std::forward_list`: um ponteiro/alocação por elemento;
- `std::deque`: blocos não contíguos sem necessidade de inserção nas pontas;
- `std::set`/`std::multiset`: deduplicação por árvore antes de conhecer o
  comparador semântico completo;
- `std::map`/`std::multimap`: ordem custa pointer chasing e não substitui a
  ordenação final de análises;
- `std::vector<bool>` para wire fields: representação especializada e sem
  endereço de `bool`; usar `bitset<N>` ou `vector<uint64_t>` para máscaras.

`std::array` deve ser reservado para limites realmente fixos pelo contrato,
não para repetir capacidades históricas como 570 flexões por seção. Para
listas pequenas mas variáveis, C++23 ainda não possui `std::inplace_vector`
(ele é posterior); `pmr::vector` com arena por consulta é a opção padrão.

##### Disponibilidade nesta toolchain

Um teste com `-std=c++23` na instalação atual produziu o mesmo resultado com
`g++-14` e `clang++-19` usando a libstdc++ padrão:

```text
std::expected   disponível (__cpp_lib_expected = 202211)
std::flat_map   indisponível
std::mdspan     indisponível
ranges para construção de containers indisponíveis
```

Isso é disponibilidade da biblioteca, não capacidade do compilador. O PoC
pode usar hoje `vector`, `array`, `span`, `string_view`, `pmr`,
`unordered_map`, algoritmos ranges e `expected`. `flat_map` e `mdspan` devem
ficar atrás de feature test macros ou ser representados pelas views/vetores
simples acima.

##### Propriedade e estabilidade

Uma organização segura para o Worker é:

```text
DatabaseEngine (vida inteira do Worker)
├── vector<byte> wwdb_image       // proprietário, capacidade final
├── DatabaseView                  // spans/offsets para a imagem
├── monotonic_buffer_resource     // índices reconstruíveis
├── unordered_map stem_index
├── vector trie_nodes/edges
└── QueryArena                    // temporários descartáveis
```

Construir índices somente depois de `wwdb_image` atingir tamanho/capacidade
final evita invalidar `string_view`. Tornar `DatabaseEngine` não copiável e
não movível depois da inicialização torna esse contrato auditável. Outra opção
é guardar somente offsets/IDs nos índices e resolver a string pelo pool, o que
elimina ponteiros internos ao custo de um accessor adicional.

Para os resultados, nunca expor um `span` além da vida da `QueryArena`. A ABI
C/WASM pode retornar um handle ou copiar uma representação canônica para um
buffer fornecido pelo chamador.

##### O que depende de ordem hoje

| Material/etapa | Dependência no legado | Com hash no WWDB |
| --- | --- | --- |
| `STEMFILE` + `INDXFILE` | prefixo delimita faixa, busca binária e hits iguais precisam ser adjacentes; a busca também estreita `J1` entre candidatos | ordem física deixa de ser necessária; `normalized_stem -> Range` substitui índice, busca e varredura adjacente |
| `INFLECTS.SEC` | regras contíguas por `(tamanho, última letra)` alimentam `Lelf/Lell`; finais vazios usam `Belf/Bell`; comprimentos são testados do maior para o menor | hash/trie de final aponta para um `Range`; manter ordem decrescente de comprimento e ordinal da regra para paridade |
| `ADDONS.LAT` | prefixos, sufixos, tackons e packons são percorridos na ordem carregada; há várias regras com o mesmo texto | chave aponta para todas as regras do grupo, mantidas por `source_ordinal`; nunca sobrescrever duplicatas |
| `UNIQUES.LAT` | listas encadeadas por primeira letra são pesquisadas linearmente | `normalized_word -> Range`; preservar todos os homógrafos e seus ordinais |
| `DICTFILE`/lexemas | `MNPC` é identidade baseada em 1 e acesso direto por posição | preservar `lexeme_id = MNPC - 1` em vetor; não usar map |
| resultados `SL/PDL/PA` | `Order_Stems` e `Order_Parse_Array` ordenam por `MNPC`, tamanho da terminação, qualidade, dicionário e significado; deduplicação contém condições “if ORDER is OK” | coletar em `vector`, aplicar comparador canônico e só então deduplicar; jamais usar ordem de iteração do hash |
| dicionários `General/Special/Local` | a ordem de `Dictionary_Kind` e prioridades influenciam apresentação/filtros | guardar `dictionary_kind` e incluí-lo explicitamente no comparador final |

A ordem dos dois primeiros itens é uma **estrutura de busca** e pode ser
substituída. A ordem de IDs, a precedência de regras e a ordenação final são
parte observável ou quase observável do comportamento e devem ser preservadas
por campos explícitos.

Também existe um risco histórico: arrays de resultado têm capacidade fixa. Se
uma consulta exceder o limite, mudar a ordem de enumeração pode mudar quais
resultados sobrevivem. O port deve usar vetores sem truncamento silencioso;
então a ordem do hash deixa de afetar semântica e o comparador final restaura
saída determinística.

##### Onde usar cada índice

| Domínio | Cardinalidade atual | Índice recomendado |
| --- | ---: | --- |
| grupos de radical normalizado | 48.463 | hash flat, MPHF ou radix trie; `unordered_map` serve ao primeiro PoC |
| finais de flexão | 460 | trie reverso ou hash por substring de comprimento `0..7` |
| sufixos derivacionais | 179 regras | trie reverso, valor `Range` |
| prefixos | 129 regras regulares carregadas | trie direto, valor `Range` |
| uniques | 76 entradas carregadas | hash direto para `Range` |
| lexemas, significados e regras por ID | arrays densos | nenhum map; ID já é o índice |

Para `anaticulus`, a combinação prática seria: trie reverso para `us`, hash ou
trie para `anaticul`, trie reverso para `icul` e hash/trie para `anat`. Uma
única `unordered_map` genérica para todas as etapas não representa bem as
consultas de prefixo/sufixo; tries evitam criar até sete substrings e encontram
todos os comprimentos numa caminhada.

#### Exemplo: `anaticulus` (“patinho”)

Isto é uma palavra latina submetida ao analisador; `patinho` é a tradução do
exemplo, não uma entrada portuguesa que o motor traduz antes de pesquisar.
O caminho semântico preservado do original é:

```text
anaticulus = anat + icul + us
             raiz   addon  flexão
```

1. O índice reverso de finais caminha `s`, depois `u`. Nesta base há seis
   regras com final literal `s` e 26 com `us` que coincidem com a palavra,
   produzindo somente `anaticulu` e `anaticul` como textos candidatos. O
   código legado examina 493 regras nos sete buckets de tamanho/final `s`;
   agrupar regras pelo `ending_id` permitiria iterar diretamente as 32 que
   casaram.
2. A busca direta encontra uma referência para `anaticul`: `lexeme_id=3353`
   (`MNPC=3354`), substantivo `N 1 1 F`, correspondente a `anaticula`. Ela não
   confirma a hipótese masculina de segunda declinação em `-us`.
3. Como a análise direta falhou, `AddonSuffixTrie` encontra o grupo `icul`.
   Há três regras `SUFFIX icul` em `ADDONS.LAT`; o legado percorre até 179
   regras de sufixo para encontrá-las.
4. Retirar `icul` de `anaticul` produz `anat`. Seu grupo contém duas
   referências, `lexeme_id=3339` e `3340`, ambas com chave lexical 2, radicais
   `anas`/`anat` e substantivo feminino da terceira declinação.
5. A regra derivacional aceita origem `N/key=2` e produz temporariamente
   `N 2 1 M/key=0`. Esse estado é compatível com a regra flexional em `us`,
   resultando em `N 2 1 NOM S M`.
6. Somente agora o runtime toca os IDs de significado. As duas entradas de
   `anat` geram as leituras formais “duck” e “senility in women...”, pois o
   algoritmo não faz desambiguação semântica.

Fluxo proposto:

```text
"anaticulus"
      |
      +-- EndingTrie("s","us") -> 32 rule_ids
      |                            -> "anaticul"
      |
      +-- StemTrie("anaticul") -> N 1 1 F -> incompatível
      |
      +-- AddonSuffixTrie("icul") -> "anat"
                                     |
                                     +-- stem_group -> 2 lexeme_ids
                                     +-- N/key2 -> N 2 1 M/key0
                                     +-- cruza regra "us"
                                     +-- NOM S M
```

O executável Ada desta cópia foi consultado com `anaticulus` e confirmou o
addon `icul`, `anaticul.us N 2 1 NOM S M` e `anas, anatis` com os dois
significados. O mecanismo histórico continua sendo composição limitada:

```text
[prefixo] + radical + [um sufixo derivacional] + flexão + [enclítico]
```

Para manter paridade, a primeira versão C++/WASM não deve transformar isso em
segmentação recursiva arbitrária. Uma extensão futura poderia usar busca em
grafo/DP com profundidade limitada e memoização de `(span, classe, chave,
paradigma)`, mas isso mudaria o conjunto de análises e precisaria de uma versão
de comportamento própria.

#### Como comprovar a vantagem

Já existe no projeto raiz um leitor C++23 do perfil `dense` por linhas e um
corte vertical de pesquisa nominal. Isso valida o caminho funcional, mas não
compara os quatro layouts; portanto os ganhos relativos entre perfis continuam
sendo hipóteses fundamentadas no layout. O benchmark correto deve executar o
mesmo corpus e comparar resultados idênticos entre:

1. referências simples por linhas;
2. referências densas por linhas;
3. byte-shuffle colunar atual;
4. layout híbrido por campos com trie ou hash de radicais.

No nativo, medir ciclos, instruções, misses L1/LLC, dTLB e bytes tocados; no
browser, medir startup, lote aquecido, p50/p95 por consulta e memória do Worker.
O baseline Ada já documentado em
[`benchmark-trafego-memoria.md`](benchmark-trafego-memoria.md) registra cerca
de 0,438 ms incremental por consulta no lote de 1.000, mas inclui o motor e a
formatação completos e não isola a pesquisa lexical.

### Caso específico de `INFLECTS.LAT` e `INFLECTS.SEC`

Os dois arquivos têm papéis diferentes:

- `INFLECTS.LAT` é fonte humana e deve existir no processo de build;
- `INFLECTS.SEC` é o cache binário legado, dependente da ABI GNAT;
- o browser não precisa receber nenhum dos dois separadamente;
- o WWDB deve receber somente a representação semântica compilada das regras.

O PoC rápido lê `INFLECTS.SEC`, não `INFLECTS.LAT`. Essa foi uma decisão de
escopo para conferir o layout e medir a compactação sem implementar novamente
o parser textual Ada. O empacotador definitivo deve inverter essa escolha:
ler e validar uma versão limpa de `INFLECTS.LAT`, gerar as seções compactas e
usar `INFLECTS.SEC` apenas como oráculo temporário de comparação.

As três seções equivalentes dentro do perfil simples são:

```text
EndingStringPool                 2.394 bytes
InflectionRecords              14.280 bytes
InflectionSectionBoundaries        12 bytes
                               ------
payload total                  16.686 bytes
```

Comparação isolada, sem atribuir o header/diretório compartilhado do WWDB:

| Representação das flexões | Cru | `gzip -9` | `zstd -3` | `zstd -19` |
| --- | ---: | ---: | ---: | ---: |
| `INFLECTS.SEC` legado | 114.000 | 9.048 | 9.041 | 6.150 |
| PoC simples, regras em linhas | 16.686 | 7.250 | 7.660 | 6.381 |
| PoC denso, regras em linhas | 13.116 | 7.357 | 8.526 | 6.865 |
| PoC denso, regras em colunas | 13.116 | 6.171 | 7.526 | 5.674 |

Leitura dos resultados:

- o perfil simples reduz **85,36%** sem compressão e **19,87%** com gzip;
- só apertar as regras de 8 para 6 bytes piora a compressão isolada: os bits
  de campos diferentes deixam de formar sequências de bytes homogêneas;
- organizar os mesmos 13.116 bytes por colunas reduz o payload para 6.171
  bytes em gzip e 5.674 bytes em Zstd nível 19;
- contra `INFLECTS.SEC`, o perfil colunar poupa 31,80% em gzip e 7,74% em
  Zstd nível 19, além de 88,50% sem compressão.

O resultado do perfil denso por linhas não invalida o formato compacto. Os 42.600 bytes de
registros nulos e os padrões fixos de padding de `INFLECTS.SEC` são
extremamente baratos para um compressor forte. A representação normalizada
remove essa repetição artificial, mas deixa uma sequência menor e mais densa,
na qual cada byte carrega mais informação. O perfil por colunas recupera a
regularidade sem reintroduzir slots nulos. Para memória, validação e
portabilidade, 13,1 KB continua muito melhor que 114 KB.

`INFLECTS.LAT` ocupa 127.247 bytes no estado atual da working tree e 20.625
bytes com `gzip -9`. Esses bytes não entram nas contas de runtime porque o
arquivo é fonte de build. Além disso, a cópia local teve marcadores de
comentário removidos e não deve alimentar automaticamente o empacotador final
antes de ser normalizada e comparada com o binário conhecido.

### Descoberta feita ao construir o índice

A primeira versão teórica das 704 fronteiras supunha ordenação ASCII simples.
O PoC mostrou que o índice legado:

- aceita radicais iniciados por maiúscula e minúscula no mesmo intervalo;
- normaliza `j` para `i`;
- normaliza `v` para `u`.

Por exemplo, `ajug` pertence ao intervalo `ai`, e os intervalos `aj` e `av`
ficam vazios. O PoC agora aplica exatamente essas normalizações antes de
construir as 704 fronteiras. O tamanho previsto de 1.408 bytes continua
correto, mas a semântica precisava dessa correção.

## Comparação de transferência para o projeto WebAssembly

Esta comparação parte dos seis recursos considerados anteriormente em
[`proposta-webassembly.md`](proposta-webassembly.md): os quatro arquivos
centrais mais `UNIQUES.LAT` e `ADDONS.LAT`.

Esta seção preserva o baseline já documentado, cujo gzip inclui os headers
normais de cada arquivo. Por isso o núcleo aparece como 1.608.396 bytes, 52
bytes acima dos 1.608.344 da tabela direta com `gzip -n`; não é diferença de
payload.

Como o snapshot 1.0 ainda não empacotava os dois últimos, a comparação
“completa” abaixo mantém seus tamanhos comprimidos legados e substitui apenas
os quatro arquivos centrais. Isso evita atribuir àquela versão uma economia
que ela ainda não havia demonstrado.

### Bytes transferidos

| Cenário aproximado | Núcleo | `UNIQUES` + `ADDONS` | Total de rede |
| --- | ---: | ---: | ---: |
| legado, seis respostas `gzip -9` | 1.608.396 | 10.910 | 1.619.306 |
| PoC simples + textos, `gzip -9` | 1.326.240 | 10.910 | 1.337.150 |
| PoC denso por linhas + textos, `gzip -9` | 1.243.838 | 10.910 | 1.254.748 |
| PoC denso por colunas + textos, `gzip -9` | 1.028.906 | 10.910 | 1.039.816 |
| legado, seis respostas `zstd -19` | 1.216.351 | 10.355 | 1.226.706 |
| PoC simples + textos, `zstd -19` | 1.149.994 | 10.355 | 1.160.349 |
| PoC denso por linhas + textos, `zstd -19` | 1.081.409 | 10.355 | 1.091.764 |
| PoC denso por colunas + textos, `zstd -19` | 870.406 | 10.355 | 880.761 |

Resultados:

- com gzip, o perfil simples economiza 282.156 bytes, ou **17,42%**, contra o
  conjunto anterior documentado; o colunar economiza 579.490 bytes, ou
  **35,79%**;
- comparando Zstd nível 19 nos dois lados, o perfil simples economiza 5,41% e
  o colunar economiza 345.945 bytes, ou **28,20%**;
- o perfil colunar em Zstd nível 19 contra o baseline anterior em gzip
  economiza 738.545 bytes, ou **45,61%**.

A economia comprimida é bem menor que os 72,48% descompactados. Isso é
esperado: gzip e Zstd já representam longas sequências de espaços, zeros e
payloads repetidos com poucos bytes. A normalização estrutural continua valiosa
principalmente por portabilidade, validação e memória decodificada.

### Tempo de fio idealizado

Os valores abaixo são apenas `bytes × 8 / bitrate`: não incluem RTT, slow
start, multiplexação, TLS, disputa de rede, cache, tempo de descompressão nem o
download do módulo WASM.

| Link nominal | legado gzip | simples gzip | colunar gzip | colunar Zstd `-19` |
| ---: | ---: | ---: | ---: | ---: |
| 1 Mbit/s | 12,954 s | 10,697 s | 8,319 s | 7,046 s |
| 10 Mbit/s | 1,295 s | 1,070 s | 0,832 s | 0,705 s |
| 50 Mbit/s | 0,259 s | 0,214 s | 0,166 s | 0,141 s |

Em links rápidos, a economia absoluta de transferência tende a ser pequena e
latência, cache e inicialização do WASM podem dominar. Em redes móveis lentas,
os 282 KB do perfil simples ou 579 KB do perfil colunar no caminho gzip são
perceptíveis, mas ainda precisam ser medidos por um teste de navegador com
throttling reproduzível.

### Memória depois da decodificação HTTP

O `Content-Encoding` reduz a rede, mas `response.arrayBuffer()` entrega os
bytes já descompactados. Mantendo `UNIQUES` e `ADDONS` separados:

| Estado | Legado | PoC aproximado |
| --- | ---: | ---: |
| bytes decodificados | 10.747.938 | 2.744.122 |
| `ArrayBuffer` + cópia integral em `WebAssembly.Memory` | 21.495.876 | 5.488.244 |

Essa conta de duas cópias é um limite conceitual simples, não uma medição de
RSS. Ela exclui o módulo WASM, heap do runtime, estruturas auxiliares, buffers
internos de Fetch e descompressão, alinhamento e GC. Ainda assim, mostra onde o
formato compacto provavelmente oferece o maior ganho para o browser: cerca de
aproximadamente 16,0 MB a menos durante a janela em que coexistem o buffer
JavaScript e a cópia na memória linear.

### Gzip e Zstd no browser

Para produção, o arquivo deve manter uma URL lógica, por exemplo
`words.<hash>.wwdb`, e o servidor deve negociar `Content-Encoding`. Não é
necessário que a aplicação escolha manualmente entre URLs `.gz` e `.zst`.

Fluxo recomendado:

```text
Accept-Encoding: zstd, br, gzip
                    │
                    ▼
servidor/CDN escolhe representação pré-comprimida
                    │
                    ▼
Content-Encoding: zstd | br | gzip
Vary: Accept-Encoding
                    │
                    ▼
Fetch entrega o WWDB já decodificado
                    │
                    ▼
cópia/commit em WebAssembly.Memory
```

Gzip é a opção de compatibilidade mais simples. Zstd como `Content-Encoding`
foi adicionado no Chrome 123; a documentação atual do WebKit registra suporte
no Safari 26.3 somente sobre sistemas Apple 26.3/Tahoe ou posteriores. Portanto
o servidor deve respeitar `Accept-Encoding` e manter gzip ou Brotli como
fallback, sem presumir suporte universal:

- [Chrome 123: Zstd Content-Encoding](https://developer.chrome.com/blog/chrome-123-beta#zstd-content-encoding);
- [WebKit: Zstandard no Safari 26.3](https://webkit.org/blog/17798/webkit-features-for-safari-26-3/);
- [MDN: `Content-Encoding`](https://developer.mozilla.org/en-US/docs/Web/HTTP/Reference/Headers/Content-Encoding).

Se a aplicação baixar `words-poc.wwdb.zst` como um arquivo comum, sem
`Content-Encoding: zstd`, receberá os bytes comprimidos e precisará de um
decoder Zstd próprio. A Compression Streams API disponível no browser expõe
gzip/deflate, não Zstd, segundo a documentação consultada. Isso adiciona código
ao bundle e pode criar mais um buffer. Para este caso, negociação HTTP é
preferível a descompressão manual:

- [MDN: Compression Streams API](https://developer.mozilla.org/en-US/docs/Web/API/Compression_Streams_API).

### Julgamento para a migração WASM

Com base neste PoC rápido:

1. **O formato compacto vale a pena mesmo quando a economia de rede é
   moderada.** O ganho principal é remover dependência da ABI e reduzir o
   conjunto decodificado de aproximadamente 10,75 MB para 2,74 MB.
2. **Gzip deve ser o baseline operacional inicial.** Ele é simples, já estava
   documentado e o perfil colunar reduz o conjunto transferido em
   aproximadamente 35,8%. O perfil por linhas permanece como fallback de
   implementação se os accessors colunares forem lentos.
3. **Zstd deve ser uma representação negociada adicional.** No nível 19
   pré-comprimido, o perfil colunar chega a cerca de 0,88 MB incluindo os dois textos
   legados. O servidor ainda precisa servir fallback.
4. **Não embutir Zstd no formato WWDB na primeira versão.** Compressão HTTP
   mantém cache, negociação e decodificação fora do motor. Compressão interna
   impediria acesso direto às seções e exigiria decoder dentro do WASM.
5. **Usar nome com hash e cache imutável.** O banco muda raramente; um recurso
   fingerprinted com cache longo provavelmente produz ganho maior nas visitas
   seguintes que pequenas diferenças entre níveis de compressão.
6. **Medir no browser antes de fechar o formato.** O próximo benchmark deve
   registrar `transferSize`, `encodedBodySize`, `decodedBodySize`, tempo até o
   `commit`, pico de memória, memória retida e latência de consulta, usando o
   WASM real. Isso decide linhas contra colunas.

Essas conclusões são aproximações baseadas em tamanho de arquivo e em um
empacotador rápido. Não medem ainda tempo de parsing, busca, startup do módulo,
descompressão no dispositivo nem comportamento de CDN.

### O que ainda pode melhorar

O perfil colunar não é um mínimo teórico. As próximas oportunidades, separadas
por grau de confiança, são:

| Ideia | Ganho observado/estimado | Custo ou risco |
| --- | --- | --- |
| front-coding dos pools ordenados | 435.202 bytes crus nos três pools; cerca de 91.752 bytes em Zstd `-19` quando medidos isoladamente | exige checkpoints e remapeamento dos IDs; falta medir o container inteiro |
| dicionário dos metadados de tradução | 1.370 combinações; ID de 11 bits + tabela reduziria aproximadamente 59.815 bytes crus | mais uma indireção; compressor colunar já captura boa parte |
| tabelas lexicais por classe | limite aproximado de mais 41.717 bytes crus contra os lexemas de 16 bytes | muda IDs/ordem ou exige diretório por classe |
| codificar `(age,frequency)` mais frequente | 1.472 de 1.785 flexões usam `(0,1)` | economia inferior a 1,5 KB; bitstream variável complica acesso |
| remover a flexão duplicada | 6 bytes no perfil denso | deve ser validado contra a deduplicação do runtime legado |
| índice trie/MPHF para radicais | possível ganho de latência, não de tamanho demonstrado | maior complexidade e custo de build; precisa benchmark |

A medição de front-coding foi feita ordenando cada pool e gravando
`prefix_length:u8 + suffix_length:u8 + suffix`. Isoladamente, os pools passam
de 1.872.250 para 1.437.048 bytes crus. Esse número **não** pode ser subtraído
diretamente do WWDB: ordenar o pool remapeia IDs e muda a compressibilidade das
seções que os referenciam. Um experimento completo precisa reescrever o
container, adicionar checkpoints a cada bloco e medir consulta aleatória.

Há também melhorias de engenharia mais importantes que alguns kilobytes. No
estado WWDB 1.7, o backlog correspondente é:

1. implementar o leitor C++/WASM para os perfis além do denso por linhas;
2. avaliar se a sugestão opt-in `Tricks.Two_Words`, já portada sem recursão e
   fora dos hits confirmados, merece um ranqueamento além do primeiro corte
   legado; compostos com `sum`, síncope, truques ortográficos e numerais
   romanos, inclusive enclíticos, também já estão no runtime nativo;
3. preservar o manifesto diferencial das 2.726 formas distintas da Eneida IV.
   As antigas 33 diferenças foram revisadas e não resta diferença de
   compatibilidade pendente: 14 relações nativas deliberadas estão nomeadas no
   teste e qualquer mudança volta a falhar a suíte;
4. medir linhas versus colunas no browser, inclusive cache e acessos indiretos;
5. tornar largura dos IDs e encoding das seções selecionáveis por flags;
6. usar hash criptográfico no manifesto de distribuição, mantendo CRC apenas
   para detecção rápida de corrupção interna.

Esses itens formam um backlog, não resultados já comprovados. A recomendação
atual é prototipar primeiro o leitor colunar e o teste de equivalência; só
depois investir em front-coding ou dicionários adicionais.

### Limite dos IDs de 16 bits

O perfil acima é válido para a base atual:

| Espaço | Quantidade atual | Limite `u16` |
| --- | ---: | ---: |
| radicais distintos | 48.712 | 65.536 valores |
| significados distintos | 32.486 | 65.536 valores |
| entradas lexicais | 39.339 | 65.536 valores |
| referências de radical | 62.086 | 65.536 posições |

A tabela de radicais já está relativamente próxima do limite. O formato deve
ter um flag por seção para largura de ID (`2` ou `4` bytes), ou definir desde o
início um perfil `wide` com `u32`. Nunca se deve truncar silenciosamente nem
usar `u16` apenas porque a cópia atual cabe.

## Exemplos em C++23

### Enums fortes

```cpp
#include <cstdint>
#include <optional>
#include <utility>
#include <variant>

enum class PartOfSpeech : std::uint8_t {
    unknown = 0,
    noun = 1,
    pronoun = 2,
    packon = 3,
    adjective = 4,
    numeral = 5,
    adverb = 6,
    verb = 7,
    participle = 8,
    supine = 9,
    preposition = 10,
    conjunction = 11,
    interjection = 12,
    tackon = 13,
    prefix = 14,
    suffix = 15,
};

enum class GrammaticalCase : std::uint8_t {
    unspecified = 0,
    nominative = 1,
    vocative = 2,
    genitive = 3,
    locative = 4,
    dative = 5,
    ablative = 6,
    accusative = 7,
};

enum class GrammaticalNumber : std::uint8_t {
    unspecified = 0,
    singular = 1,
    plural = 2,
};

enum class StemSlot : std::uint8_t {
    none = 0,
    first = 1,
    second = 2,
    third = 3,
    fourth = 4,
};

struct ParadigmCode {
    std::uint8_t which{};
    std::uint8_t variant{};
};

constexpr std::optional<std::uint8_t>
pack_paradigm(ParadigmCode value) noexcept {
    if (value.which > 9 || value.variant > 9) {
        return std::nullopt;
    }
    return static_cast<std::uint8_t>(
        (value.which << 4U) | value.variant
    );
}
```

No modelo de alto nível, o payload pode ser uma `std::variant` para impedir
combinações inválidas:

```cpp
struct NounTraits {
    ParadigmCode paradigm;
    std::uint8_t gender;
    std::uint8_t kind;
};

struct VerbTraits {
    ParadigmCode paradigm;
    std::uint8_t kind;
};

struct PrepositionTraits {
    GrammaticalCase governed_case;
};

using LexicalTraits = std::variant<
    NounTraits,
    VerbTraits,
    PrepositionTraits
    // Demais classes omitidas neste exemplo.
>;
```

O wire format continua sendo um registro tagged compacto. A `variant` é um
tipo de domínio em memória, não algo gravado por `memcpy`.

### Empacotamento explícito, sem bitfields

```cpp
#include <cstdint>
#include <optional>

enum class Tense : std::uint8_t {
    unspecified, present, imperfect, future,
    perfect, pluperfect, future_perfect,
};

enum class Voice : std::uint8_t {
    unspecified, active, passive,
};

enum class Mood : std::uint8_t {
    unspecified, indicative, subjunctive,
    imperative, infinitive, participle,
};

struct ParticipleMorphology {
    GrammaticalCase grammatical_case;
    GrammaticalNumber number;
    std::uint8_t gender;
    Tense tense;
    Voice voice;
    Mood mood;
};

constexpr std::optional<std::uint16_t>
pack_participle(ParticipleMorphology m) noexcept {
    const auto c = std::to_underlying(m.grammatical_case);
    const auto n = std::to_underlying(m.number);
    const auto t = std::to_underlying(m.tense);
    const auto v = std::to_underlying(m.voice);
    const auto md = std::to_underlying(m.mood);

    if (c > 7 || n > 2 || m.gender > 4 ||
        t > 6 || v > 2 || md > 5) {
        return std::nullopt;
    }

    return static_cast<std::uint16_t>(
        c |
        (n << 3U) |
        (m.gender << 5U) |
        (t << 8U) |
        (v << 11U) |
        (md << 13U)
    );
}
```

As posições dos bits acima devem ser testadas com vetores conhecidos e
descritas na especificação. Usar `struct { unsigned case_:3; ...; }` seria
novamente dependente do compilador.

### Serialização little-endian em C++23

```cpp
#include <bit>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <span>
#include <type_traits>
#include <vector>

template<std::unsigned_integral T>
void append_le(std::vector<std::byte>& output, T value) {
    if constexpr (std::endian::native == std::endian::big) {
        value = std::byteswap(value);
    }

    const auto bytes = std::as_bytes(std::span{&value, 1});
    output.insert(output.end(), bytes.begin(), bytes.end());
}

void append_enum(std::vector<std::byte>& output, PartOfSpeech value) {
    append_le(output, std::to_underlying(value));
}
```

Mesmo com essa função, o gravador deve emitir os campos na ordem da
especificação. Não se deve fazer:

```cpp
// Incorreto para persistência:
output.write(reinterpret_cast<const char*>(&record), sizeof record);
```

Um leitor deve devolver erro em vez de acessar fora do buffer:

```cpp
#include <cstring>
#include <expected>

enum class ReadError {
    truncated,
    invalid_enum,
    invalid_reference,
};

template<std::unsigned_integral T>
std::expected<T, ReadError>
read_le(std::span<const std::byte> bytes, std::size_t offset) {
    if (offset > bytes.size() || bytes.size() - offset < sizeof(T)) {
        return std::unexpected(ReadError::truncated);
    }

    T value{};
    std::memcpy(&value, bytes.data() + offset, sizeof value);
    if constexpr (std::endian::native == std::endian::big) {
        value = std::byteswap(value);
    }
    return value;
}
```

### Validação de referências

Após validar header e diretório, o carregador ainda deve conferir invariantes
entre seções:

```cpp
bool validate_stem_reference(
    std::uint32_t stem_id,
    std::uint32_t lexeme_id,
    StemSlot slot,
    std::uint32_t stem_count,
    std::uint32_t lexeme_count
) noexcept {
    const auto raw_slot = std::to_underlying(slot);
    return stem_id < stem_count &&
           lexeme_id < lexeme_count &&
           raw_slot <= 4;
}
```

Também devem ser validados:

- seções não sobrepostas e contidas em `file_size`;
- `record_count × stride` sem overflow;
- strings contidas no respectivo pool;
- IDs menores que a cardinalidade da seção referida;
- enums dentro da faixa da versão;
- bits reservados iguais a zero;
- fronteiras de índices monotônicas;
- último limite igual ao número de registros;
- checksum antes de aceitar a base.

## Repetições que devem e não devem ser removidas

Nem toda repetição é desperdício.

Pode ser removido com segurança:

- padding e caudas inativas de unions;
- capacidade nula das seções de flexão;
- bloco `Part` de `STEMFILE`, pois é idêntico ao do lexema apontado;
- strings idênticas em pools;
- a regra de flexão literalmente duplicada;
- campos decimais e espaços de `INDXFILE`.

Deve ser preservado semanticamente, mesmo que codificado de modo compacto:

- dois registros lexicais com o mesmo significado mas metadados ou radicais
  diferentes;
- dois radicais textualmente iguais ligados a lexemas diferentes;
- regras com a mesma terminação mas morfologia, época ou frequência diferente;
- ordinais históricos de enums enquanto o conversor ainda lê artefatos
  legados.

Deduplicar somente pela string visível ou pelo sufixo produziria associações
incorretas. A deduplicação deve separar conteúdo compartilhável de identidade
lexical.

## Migração sugerida

1. Tratar `DICTLINE.GEN`, `INFLECTS.LAT`, `UNIQUES.LAT` e `ADDONS.LAT` como
   fontes, com parser estrito e diagnósticos de linha.
2. Criar um modelo intermediário tipado, independente de Ada e do wire format.
3. Normalizar e validar enums, paradigmas, slots de radical e referências.
4. Detectar duplicatas exatas e emitir relatório; não removê-las
   silenciosamente na primeira versão.
5. Construir pools de strings determinísticos, preferencialmente ordenados.
6. Escrever `words.wwdb` campo a campo.
7. Ler o arquivo recém-criado com um segundo caminho de código e validar todas
   as invariantes.
8. Comparar milhares de análises do motor legado e do novo motor, incluindo
   ambiguidades e formas irregulares.
9. Só então tornar o formato compacto a fonte de runtime.

O empacotador deve ser determinístico: mesmas fontes e mesma versão devem
produzir exatamente os mesmos bytes. Isso permite hashes reproduzíveis,
cache eficiente e revisão binária simples.

## Observações sobre o estado atual das fontes

No estado inspecionado, `INFLECTS.SEC` tem 1.785 regras, enquanto a versão
registrada de `INFLECTS.LAT` tem 1.783 linhas ativas: o binário possui duas
regras de adjetivo adicionais. Além disso, a working tree removeu marcadores
`--` de centenas de linhas explicativas de `INFLECTS.LAT`. Uma regeneração
direta tentaria interpretar prosa e regras anteriormente desativadas como
registros.

Isso reforça duas necessidades do novo fluxo:

- parser que falhe com linha e coluna precisas;
- teste de reprodutibilidade que compare fontes, contagens e hash do artefato.

## Conclusão

O formato legado é eficiente para a intenção original — gravar e reler tipos
Ada no mesmo ambiente —, mas não é um formato de intercâmbio. Seu maior custo
não vem de uma única decisão ruim; vem da soma de strings fixas, inteiros
largos, unions, padding, índices textuais, tabelas de capacidade fixa e dados
duplicados entre arquivos.

A primeira modernização não precisa buscar compressão teórica máxima. O perfil
denso/colunar de aproximadamente 2,70 MB sem compressão já remove a maior parte
do desperdício estrutural, e sua organização por colunas mostrou ganho de rede
relevante no PoC. Para autocomplete integrado a um site que já possui as
definições, o perfil de 1,18 MB RAW/409 KB gzip é a escolha mais coerente e
evita duplicar conteúdo editorial. O próximo critério decisivo não é espremer
mais bits: é demonstrar equivalência funcional e medir o custo dos accessors no browser.
Compressão deve continuar no transporte, negociada pelo servidor.
