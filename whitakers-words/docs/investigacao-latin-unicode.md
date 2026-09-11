# Investigação do normalizador latino finito

## Resultado executivo

Existe agora uma biblioteca C++23 experimental, separada de `words_core`, que
reproduz o comportamento observável do `LatinLexer` para o domínio latino com
quantidade vocálica. Ela usa um decoder UTF-8 estrito e uma tabela `constexpr`
de 22 entradas; não depende de utf8proc em runtime. Há tanto uma API
proprietária conveniente quanto uma API não proprietária que escreve em
buffers do chamador e não aloca.

A equivalência foi demonstrada contra o lexer atual/utf8proc 2.11.3 e inclui
todos os campos da superfície, diagnósticos alcançáveis, 1.112.064 escalares
Unicode válidos, as 444.828 sequências de até três escalares do alfabeto
aceito, 16.843.008 strings de um a três bytes, 8.388.608 candidatos estruturais
de quatro bytes, um corpus congelado de 212 formas reais e 200.000 execuções
de libFuzzer. Nenhuma divergência foi encontrada.

Esta conclusão demonstra viabilidade técnica, não autoriza migração. O
`LatinLexer`, o `TextTokenCursor`, o link público de `words_core` e o WASM de
produção não foram alterados.

## 1. Contrato observado

As responsabilidades atuais em `src/lexer.cpp` são distintas:

1. **Decoding e validade UTF-8.** `utf8proc_iterate` percorre os bytes e rejeita
   sequências malformadas. O lexer não emite U+FFFD para substituir erro.
2. **Validação do alfabeto original.** Antes de decompor ou fazer casefold, cada
   escalar precisa ser ASCII `A-Z/a-z`, U+0304, U+0306 ou um dos 22
   precompostos da tabela abaixo. Assim `ß`, `K` e `ſ` não podem virar ASCII.
3. **Decomposição canônica.** `utf8proc_map` com `UTF8PROC_STABLE |
   UTF8PROC_DECOMPOSE | UTF8PROC_CASEFOLD` produz uma sequência decomposta.
   NFD opera sobre codepoints, embora a API receba e devolva sua codificação
   UTF-8.
4. **Case folding.** O mesmo `utf8proc_map` executa casefold por causa de uma
   opção independente. Casefold não pertence a NFD nem a NFC.
5. **Quantidade.** U+0304 torna a vogal anterior longa; U+0306 a torna breve.
   Vogal sem marca permanece `unknown`. Marca inicial, sobre consoante,
   repetida ou conflitante produz `invalid_vowel_quantity`.
6. **Composição.** O lexer reconstrói base ASCII + marca e chama outro
   `utf8proc_map`, agora com `UTF8PROC_STABLE | UTF8PROC_COMPOSE`. O resultado é
   `normalized_nfc`. NFC não implica um codepoint por letra: `y` + U+0306
   continua com dois codepoints.
7. **Representações.** `orthography_ascii` preserva `j/v`; somente
   `lookup_ascii` aplica `j -> i` e `v -> u`. `quantities` tem uma entrada por
   letra lógica. `nfc_byte_offsets` delimita essas letras na saída NFC e
   alimenta `SurfaceForm::slice`.
8. **Tokenização.** `TextTokenCursor` também decodifica com utf8proc e usa
   `utf8proc_category` para `Pi`, `Pf`, `Pd`, `Ps`, `Pe` e `Pc`–`Po`, além de
   listas próprias de whitespace e pontuação. Essa classificação Unicode geral
   não faz parte desta PoC.

O `original_utf8` preserva exatamente os bytes válidos recebidos. Entrada vazia
é `empty_input`; NUL e controles são UTF-8 válido, mas
`unsupported_character`.

## 2. Tabela auditável

Os nomes foram conferidos programaticamente com `unicodedata` 15.0.0. Bytes,
decomposição e composição foram conferidos nos testes contra o utf8proc
vendorizado, que declara Unicode 17.0.0. Os caracteres são antigos e seus nomes
e decomposições canônicas coincidem nas duas versões.

Na coluna “NFC Words”, maiúsculas aparecem em minúsculas porque o casefold é
aplicado antes da composição; isso não faz parte da decomposição canônica.

| Glyph | Codepoint | Nome Unicode | UTF-8 | Base | Quantidade | NFD canônico | NFD UTF-8 | NFC Words |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| Ā | U+0100 | LATIN CAPITAL LETTER A WITH MACRON | C4 80 | a | long | U+0041 U+0304 | 41 CC 84 | ā |
| ā | U+0101 | LATIN SMALL LETTER A WITH MACRON | C4 81 | a | long | U+0061 U+0304 | 61 CC 84 | ā |
| Ă | U+0102 | LATIN CAPITAL LETTER A WITH BREVE | C4 82 | a | short | U+0041 U+0306 | 41 CC 86 | ă |
| ă | U+0103 | LATIN SMALL LETTER A WITH BREVE | C4 83 | a | short | U+0061 U+0306 | 61 CC 86 | ă |
| Ē | U+0112 | LATIN CAPITAL LETTER E WITH MACRON | C4 92 | e | long | U+0045 U+0304 | 45 CC 84 | ē |
| ē | U+0113 | LATIN SMALL LETTER E WITH MACRON | C4 93 | e | long | U+0065 U+0304 | 65 CC 84 | ē |
| Ĕ | U+0114 | LATIN CAPITAL LETTER E WITH BREVE | C4 94 | e | short | U+0045 U+0306 | 45 CC 86 | ĕ |
| ĕ | U+0115 | LATIN SMALL LETTER E WITH BREVE | C4 95 | e | short | U+0065 U+0306 | 65 CC 86 | ĕ |
| Ī | U+012A | LATIN CAPITAL LETTER I WITH MACRON | C4 AA | i | long | U+0049 U+0304 | 49 CC 84 | ī |
| ī | U+012B | LATIN SMALL LETTER I WITH MACRON | C4 AB | i | long | U+0069 U+0304 | 69 CC 84 | ī |
| Ĭ | U+012C | LATIN CAPITAL LETTER I WITH BREVE | C4 AC | i | short | U+0049 U+0306 | 49 CC 86 | ĭ |
| ĭ | U+012D | LATIN SMALL LETTER I WITH BREVE | C4 AD | i | short | U+0069 U+0306 | 69 CC 86 | ĭ |
| Ō | U+014C | LATIN CAPITAL LETTER O WITH MACRON | C5 8C | o | long | U+004F U+0304 | 4F CC 84 | ō |
| ō | U+014D | LATIN SMALL LETTER O WITH MACRON | C5 8D | o | long | U+006F U+0304 | 6F CC 84 | ō |
| Ŏ | U+014E | LATIN CAPITAL LETTER O WITH BREVE | C5 8E | o | short | U+004F U+0306 | 4F CC 86 | ŏ |
| ŏ | U+014F | LATIN SMALL LETTER O WITH BREVE | C5 8F | o | short | U+006F U+0306 | 6F CC 86 | ŏ |
| Ū | U+016A | LATIN CAPITAL LETTER U WITH MACRON | C5 AA | u | long | U+0055 U+0304 | 55 CC 84 | ū |
| ū | U+016B | LATIN SMALL LETTER U WITH MACRON | C5 AB | u | long | U+0075 U+0304 | 75 CC 84 | ū |
| Ŭ | U+016C | LATIN CAPITAL LETTER U WITH BREVE | C5 AC | u | short | U+0055 U+0306 | 55 CC 86 | ŭ |
| ŭ | U+016D | LATIN SMALL LETTER U WITH BREVE | C5 AD | u | short | U+0075 U+0306 | 75 CC 86 | ŭ |
| Ȳ | U+0232 | LATIN CAPITAL LETTER Y WITH MACRON | C8 B2 | y | long | U+0059 U+0304 | 59 CC 84 | ȳ |
| ȳ | U+0233 | LATIN SMALL LETTER Y WITH MACRON | C8 B3 | y | long | U+0079 U+0304 | 79 CC 84 | ȳ |

As marcas aceitas são:

| Glyph | Codepoint | Nome Unicode | UTF-8 | Semântica |
| --- | --- | --- | --- | --- |
| ◌̄ | U+0304 | COMBINING MACRON | CC 84 | `long_vowel` sobre vogal anterior |
| ◌̆ | U+0306 | COMBINING BREVE | CC 86 | `short_vowel` sobre vogal anterior |

Há composição canônica para os dez pares `a/e/i/o/u` × `macron/breve` e para
`y + macron`. Não há composição correspondente no domínio para `y + breve`:
`79 CC 86` é simultaneamente a forma decomposta e a NFC esperada.

## 3. Desenho da biblioteca experimental

`latin_unicode_poc` depende somente da STL e vive no namespace
`words::poc::latin_unicode`. Seus tipos não são expostos pelos headers públicos
da engine:

- o decoder estrito é exposto pela função `decode_utf8_scalar`, que devolve
  escalar e largura ou um erro estrutural com offset;
- `map_latin_codepoint` aceita somente o domínio finito;
- `LatinGlyph` contém base ASCII minúscula e `LatinQuantity`;
- `encode_latin_glyph_nfc` emite a composição tabular ou `y` + breve;
- `LatinSurfaceNormalizer` produz uma representação própria equivalente a
  `SurfaceForm`.

O caminho totalmente ASCII valida `[A-Za-z]+` e constrói a superfície em uma
passagem. O caminho não ASCII primeiro valida toda a entrada e depois a
interpreta. Essa segunda passagem preserva a precedência de diagnósticos do
lexer atual: um escalar não permitido encontrado durante a validação não pode
ser mascarado por uma marca mal posicionada anterior.

Durante a construção existe apenas um `LatinGlyph` pendente. Uma nova base
finaliza a anterior; uma marca modifica a pendente. Isso elimina o vetor
transitório de glifos e permite produzir offsets junto com a NFC.

Há dois contratos de construção:

- `normalize` devolve strings e vetores próprios e continua sendo a interface
  simples para testes e comparação;
- `requirements` calcula, sem alocar, a quantidade exata de letras lógicas e
  bytes NFC; `normalize_into` recebe cinco `std::span`s do chamador e devolve
  um `LatinSurfaceView` não proprietário.

`normalize_into` valida integralmente entrada e capacidades antes da primeira
escrita. Buffer insuficiente em qualquer uma das cinco saídas produz
`insufficient_output_space` e deixa todos intactos. Entrada e buffers devem ter
lifetimes válidos e não se sobrepor; essa precondição está documentada no tipo.
No caminho ASCII, o cálculo e a escrita usam diretamente `[A-Za-z]+`; no
caminho com quantidade, a passagem adicional conserva a precedência de erros
do oracle. Nenhuma dessas funções chama uma operação alocadora.

O decoder distingue:

- continuation byte sem starter;
- starter inválido;
- continuation byte inválido;
- sequência truncada;
- overlong;
- surrogate U+D800–U+DFFF;
- valor acima de U+10FFFF.

U+FFFD corretamente codificado é decodificado e depois rejeitado pela allowlist.
Nunca há replacement silencioso.

O `simple_utf8view.hpp` fornecido como inspiração foi auditado, mas não
reutilizado. As verificações de overlong, surrogate e limite Unicode são
conceitualmente adequadas para uma view tolerante, porém seu contrato converte
qualquer erro em U+FFFD e avança um byte. Isso torna indistinguíveis uma
sequência malformada e U+FFFD real para quem apenas itera, contrariando o
fail-fast exigido aqui. Além disso, expressões como `p + 2`/`p + 3` podem formar
um ponteiro além de one-past-end antes da comparação em buffers truncados. O
decoder da PoC usa índices verificados e erros explícitos, preservando a boa
separação entre decoding e allowlist sem copiar essa política de substituição.

## 4. Por que não há algoritmo geral de CCC/NFC

O domínio aceita somente duas combining marks, imediatamente depois de uma
base ASCII permitida. A semântica aceita no máximo uma delas e rejeita
duplicação ou conflito. Portanto não há duas marcas válidas que precisem ser
reordenadas por Canonical Combining Class.

A tabela só codifica equivalências já pertencentes ao contrato. Isso não se
generaliza para novos diacríticos, compatibilidade Unicode ou grego politônico;
qualquer ampliação exige revisar o algoritmo e repetir a prova diferencial.

## 5. Evidência de corretude

Os testes comparam aceitação e, em sucesso, `original_utf8`,
`orthography_ascii`, `lookup_ascii`, `quantities`, `normalized_nfc` byte a byte,
`nfc_byte_offsets` e cada slice lógico. Em erro comparam a classe diagnóstica;
mensagens textuais não fazem parte da equivalência da PoC.

Cobertura dirigida:

- 52 letras ASCII e todos os 22 precompostos isolados;
- todas as formas canonicamente decompostas, maiúsculas e minúsculas;
- 96 contextos gerados para seis vogais, duas caixas, duas marcas e quatro
  posições de palavra;
- palavras solicitadas (`puella`, `mālum`, `mălum`, `juvenis`, `iuuenis`) e
  todas as formas pedidas de `y`;
- marcas iniciais, sobre todas as consoantes, repetidas, conflitantes e após
  precomposto;
- 564 truncamentos, bit flips e inserções determinísticas sobre entradas
  válidas;
- acutos, graves, circunflexos, diérese, ligaturas, `ß`, `K`, `ſ`, fullwidth,
  grego, cirílico, emoji, U+FFFD, NUL e controles;
- vetores explícitos `80`, `C0 80`, `C1 BF`, `C2`, `E0`, `E0 80 80`,
  `ED A0 80`, `F0 80 80 80`, `F4 90 80 80`, `F5 80 80 80`, starters
  inválidos, continuations inválidas e truncamentos de dois a quatro bytes.

Propriedades verificadas:

- precomposto e decomposto têm a mesma semântica;
- normalização é idempotente, desconsiderando `original_utf8`;
- encode/decode faz round-trip para todo escalar aceito;
- toda `normalized_nfc` é UTF-8 válida segundo ambos os decoders;
- caixa e forma física não alteram a contagem lógica;
- nenhum escalar isolado é aceito quando o lexer atual o rejeita;
- a API proprietária e a API com armazenamento do chamador produzem todos os
  mesmos campos e slices;
- requisitos exatos bastam, cada buffer insuficiente falha antes de escrever,
  e slices permanecem totais mesmo diante de offsets públicos corrompidos.

### Argumento finito/composicional

O alfabeto de entrada aceito possui exatamente 76 escalares: 52 letras ASCII,
duas marcas e 22 precompostos. A interpretação é um transdutor determinístico
cujo único estado persistente é “sem base pendente”, “consoante pendente”,
“vogal sem marca”, “vogal longa” ou “vogal breve”, junto da base concreta a
emitir. Toda decisão de aceitar, rejeitar ou emitir depende apenas desse estado
e do próximo escalar.

Enumerar todas as sequências não vazias de tamanho até três cobre
`76 + 76² + 76³ = 444.828` casos: todos os estados alcançáveis, toda transição,
o flush no fim e uma transição posterior a cada emissão. Como a saída é uma
concatenação por glifo e não há estado adicional, equivalência de sequências
maiores segue por indução sobre o prefixo já emitido. Isso não é uma alegação
sobre Unicode geral; é uma prova específica desse transdutor finito.

Para decoding, um escalar UTF-8 ocupa no máximo quatro bytes. Todas as strings
de um a três bytes foram enumeradas. Para quatro bytes, foram enumerados os
starters `F0`–`F7`, todos os dois primeiros continuations e qualquer valor no
quarto byte. Starter ou continuation inválido antes disso já decide o erro e
está coberto pelos prefixos exaustivos de uma a três posições. Assim ficam
cobertos todos os resultados possíveis da primeira decodificação; a extensão
a uma string arbitrária segue por indução a cada escalar consumido.

Enumerações no build Clang 21 Debug:

| Universo | Casos | Resultado | Tempo |
| --- | ---: | --- | ---: |
| alfabeto aceito, sequências de 1–3 escalares | 444.828 | superfície/erro idênticos | 3,81 s |
| escalares válidos U+0000–U+10FFFF | 1.112.064 | sem divergência | 0,98 s |
| strings de 1–3 bytes | 16.843.008 | decoder idêntico ao oracle | 1,28 s |
| starters F0–F7 + dois continuations + byte final livre | 8.388.608 | decoder idêntico ao oracle | 0,67 s |

O libFuzzer com dicionário de 19 tokens, `-runs=200000 -max_len=64`, terminou em
cinco segundos, 493 counters/1.011 features cobertos e nenhuma divergência. O
harness compara o lexer, a superfície proprietária, `requirements` e
`normalize_into`.

O corpus congelado veio de uma abertura read-only/immutable de
`thelatinlibrary.sqlite3` (schema `user_version=9`). Entre 142.761 tokens CLTK,
foram encontrados 22.843 surfaces distintos no domínio e 108 com quantidade.
O arquivo versionado guarda as 108 e até seis formas ASCII frequentes por
comprimento de 1 a 20, totalizando 212 formas/19.981 ocorrências. Cada forma e
sua decomposição canônica pelo oracle são comparadas. O corpus é evidência
empírica, não uma definição linguística do domínio nem uma dependência runtime.

## 6. Tamanho

Medições feitas com Clang/LLVM 21.0.0, `MinSizeRel`, ELF x86-64 dinâmico e
Emscripten 5.0.6/LLVM 23 com `-Oz -flto`. Há drivers mínimos separados para a
superfície proprietária, a API com buffers do chamador e utf8proc. O comparador
utf8proc faz apenas decomposição/casefold/composição, portanto a comparação é
conservadora em relação ao código da PoC.

### Objetos e executáveis nativos

| Artefato | `.text*` | `.rodata*` | arquivo bruto |
| --- | ---: | ---: | ---: |
| `latin_utf8.cpp.o`, ambas as APIs | 8.411 B | 679 B | 24.184 B |
| microexecutável PoC proprietário | 8.923 B | 696 B | 28.032 B |
| microexecutável PoC com buffers | 8.867 B | 696 B | 28.032 B |
| microexecutável utf8proc | 6.108 B | 336.936 B | 358.056 B |
| `utf8proc.c.o` | 5.732 B | 336.919 B | 350.504 B |

A tabela `special_latin_mappings` ocupa 264 bytes lógicos, incluindo padding.
O objeto PoC possui 43 símbolos definidos contando templates fracos da STL. O
objeto utf8proc possui 32 símbolos globais de texto/dados. Os quatro grandes
símbolos de dados do utf8proc somam 328.538 bytes: 201.240 de properties,
92.672 de stage 2, 25.922 de sequences e 8.704 de stage 1. No ELF nativo, as
duas APIs residem no mesmo objeto e são puxadas juntas pelo linker; a diferença
entre os dois drivers não representa o custo incremental isolado de cada API.

Compressibilidade dos microexecutáveis nativos:

| Variante | RAW | gzip-9 | Brotli-11 |
| --- | ---: | ---: | ---: |
| PoC proprietária | 28.032 B | 10.638 B | 9.200 B |
| PoC com buffers | 28.032 B | 10.644 B | 9.275 B |
| utf8proc | 358.056 B | 89.288 B | 55.008 B |

### Micro-WASM isolado

| Variante | CODE | DATA | RAW | gzip-9 | Brotli-11 |
| --- | ---: | ---: | ---: | ---: | ---: |
| PoC proprietária | 13.187 B | 1.134 B | 14.595 B | 7.007 B | 6.025 B |
| PoC com buffers | 4.506 B | 684 B | 5.349 B | 2.924 B | 2.636 B |
| utf8proc | 13.615 B | 318.716 B | 332.520 B | 88.076 B | 54.494 B |

O LTO do micro-WASM consegue eliminar o caminho proprietário quando somente
`normalize_into` é referenciado; por isso esse artefato mostra melhor o custo
potencial do núcleo sem ownership. Ainda são microprogramas isolados, não o
delta do WordsWASM de produção. Dead stripping, sobreposição com a STL e o uso
restante do tokenizador mudariam um A/B integrado.

## 7. Performance exploratória

O benchmark usa sete amostras após uma amostra de warmup e reporta a mediana.
O corpus ASCII contém 4.573 tokens/26.113 bytes extraídos da Eneida IV. O
corpus quantitativo contém 2.304 palavras/14.336 bytes com formas maiúsculas,
minúsculas, precompostas e decompostas. Foram usadas 20 iterações por amostra.

Cada instrumento responde uma pergunta diferente:

| Instrumento | Unidade principal | Responde | Não demonstra sozinho |
| --- | --- | --- | --- |
| benchmark | ns/byte | latência local sem instrumentação | causa do custo ou estabilidade entre máquinas |
| Callgrind | instruções simuladas | trabalho determinístico e atribuição por função | tempo real, JIT ou custo do browser |
| DHAT | blocos, bytes e lifetime | churn e origem das alocações | RSS ou heap vivo do WASM |
| LLVM fprofile | contadores de execução | frequência de funções/regiões | custo por execução |
| `llvm-size`/WASM sections | bytes por seção | composição do artefato isolado | delta do produto integrado |
| gzip/Brotli | bytes transferidos | compressibilidade | heap ou tempo de execução |

### 7.1 Tempo de parede sem instrumentação

Os intervalos consolidam as três execuções originais e as três repetições da
revisão independente. Os ratios são os da execução representativa original,
não uma divisão entre extremos de amostras diferentes.

| Implementação | ASCII, ns/B | Ratio contra lexer | Quantidade, ns/B | Ratio contra lexer |
| --- | ---: | ---: | ---: | ---: |
| PoC proprietária | 15,34–16,81 | 4,72x | 25,36–27,47 | 2,72x |
| `normalize_into` | 3,98–4,16 | 18,38x | 22,81–24,87 | 3,12x |
| `LatinLexer`/utf8proc | 71,72–74,69 | baseline | 72,24–73,50 | baseline |

O ganho ASCII de `normalize_into` vem do fast path validado `[A-Za-z]+` e do
reuso de buffers de pilha. No caminho quantitativo, a garantia de validar
entrada e capacidades antes de escrever exige passagens adicionais e reduz a
distância entre as duas APIs da PoC.

As etapas isoladas servem para localizar o custo interno, não devem ser
somadas para reconstruir a superfície completa:

| Etapa isolada | ASCII, ns/B | Quantidade, ns/B |
| --- | ---: | ---: |
| decode | 4,07 | 1,97 |
| mapping | 2,75 | 1,82 |
| composição | 7,35 | 5,75 |

### 7.2 Callgrind — instruções determinísticas

Callgrind 3.22 foi usado com coleta alternada na entrada de cada implementação.
As contas abaixo isolam as oito varreduras medidas, excluem a preparação do
corpus e normalizam o resultado para uma varredura conjunta dos dois corpora.

| Implementação | Instruções/varredura | Redução contra lexer | Speedup por instruções |
| --- | ---: | ---: | ---: |
| PoC proprietária | 11,29 milhões | 76,6% | 4,27x |
| `normalize_into` | 5,91 milhões | 87,7% | 8,15x |
| `LatinLexer`/utf8proc | 48,18 milhões | baseline | baseline |

Essas são instruções simuladas, não tempo. A repetição independente encontrou
47.292.976 instruções nas oito varreduras de `normalize_into` e 385.422.675
nas oito do lexer, reproduzindo 5,91 e 48,18 milhões por varredura.

### 7.3 DHAT — churn de heap

O DHAT 3.22 mediu 55.016 normalizações por implementação. Preparação do corpus
foi excluída pela atribuição dos stacks. Os valores incluem a construção das
superfícies devolvidas, não apenas as chamadas ao utf8proc.

| Implementação | Blocos | Blocos/operação | Bytes | Bytes/operação |
| --- | ---: | ---: | ---: | ---: |
| PoC proprietária | 123.786 | 2,25 | 2.067.777 | 37,58 |
| `normalize_into` | 0 | 0 | 0 | 0 |
| `LatinLexer`/utf8proc | 385.112 | 7,00 | 5.730.248 | 104,16 |

Em relação ao lexer, a API proprietária reduz 67,9% dos blocos e 63,9% dos
bytes. A ausência de stack contendo `normalize_into` confirma zero alocações
somente dentro desse caminho; não implica uma engine ou fronteira JS sem heap.

### 7.4 LLVM fprofile — frequência, não custo

Foi usado `RelWithDebInfo` com `-fprofile-instr-generate` e
`-fcoverage-mapping`, consolidado por `llvm-profdata` 21. Esse instrumento
responde quais regiões são executadas, mas não mede instruções, tempo ou heap.

| Backend | Regiões com maiores contadores internos | Leitura |
| --- | --- | --- |
| `LatinLexer`/utf8proc | `unsafe_get_property`, `utf8proc_iterate`, `utf8proc_decompose_custom`, `utf8proc_decompose_char` | confirma repetição de lookup/decomposição geral |
| PoC finita | lowercase ASCII, decoding, mapping e composição | confirma que o trabalho restante é o transdutor pequeno |
| `normalize_into` | aparece entre as 30 funções com maior contador | confirma que o caminho foi exercitado, não que seja caro |

Não há gate de performance: são microresultados locais e a PoC ainda não foi
inserida na engine.

## 8. Validação de toolchains

- Clang 21.0.0, warnings do projeto (`-Weverything -Werror`): passou;
- Clang 19.1.1, os mesmos warnings: passou;
- GCC 14.2.0, `-Wall -Wextra -Wconversion -Wsign-conversion -Werror`: passou;
- Clang 21 com ASan, UBSan, implicit-conversion, unsigned-overflow e
  float-divide-by-zero: todos os testes passaram, inclusive LeakSanitizer em
  execução no NativeLab com suporte a `ptrace`;
- Emscripten 5.0.6/LLVM 23: os três microtargets compilaram.

Além da matriz isolada de 19 testes, o `ctest` completo do repositório terminou
com 148/148 testes aprovados. LeakSanitizer foi repetido no NativeLab sobre os
15 testes dirigidos/corpus que exercitam ambas as APIs, com saída limpa; as
enumerações longas passaram separadamente sob ASan/UBSan.

## 9. Avaliação separada da categorização do tokenizador

Todos os usos de produção permanecem deliberadamente presentes:

- validação, decomposição/casefold, encoding e composição em `LatinLexer`;
- decoding e `utf8proc_category` em `TextTokenCursor`;
- dependência pública `utf8proc::utf8proc` de `words_core`.

`TextTokenCursor` não precisa de todas as propriedades Unicode. Depois do
decoder, `boundary_flag` possui uma tabela própria de `White_Space`, switches
editoriais para pontuação com semântica fina e consulta somente estas General
Categories:

| Resultado | Categorias | Codepoints Unicode 17 | Ranges mesclados |
| --- | --- | ---: | ---: |
| `quote` | Pi, Pf | 22 | 11 |
| `dash` | Pd | 27 | 20 |
| `bracket` | Ps, Pe | 156 | 38 |
| `other_punctuation` | Pc, Po | 651 | 194 |
| **Total** | — | **856** | **263** |

Os números foram enumerados sobre todos os escalares com utf8proc 2.11.3,
Unicode 17.0.0. Quatro arrays de intervalos `{first,last}`, com a flag implícita
pelo array, custariam no máximo 2.104 bytes de endpoints `uint32_t`, antes de
alinhamento/código. Isso é uma estimativa estrutural, ainda não um tamanho de
artefato. Uma representação paginada ou com deltas pode ser menor, mas só deve
ser escolhida após medir simplicidade, lookup e compressão WASM.

### Contrato que uma substituição futura teria de preservar

1. Extrair ou revisar o decoder estrito em um componente de nome neutro; UTF-8
   malformado atualmente não vira delimitador, integra o token byte a byte e
   acaba diagnosticado pelo lexer. Esse comportamento surpreendente precisa de
   teste antes de qualquer decisão de alterá-lo.
2. Manter a lista explícita de `White_Space`; ela vem de uma propriedade
   binária e não equivale simplesmente às categorias `Z*`.
3. Executar primeiro os switches específicos. Por exemplo, U+2019 é
   `apostrophe` pelo contrato local, não `quote` embora sua categoria seja Pf.
4. Consultar depois somente os quatro grupos de intervalos acima. Não é
   necessário recriar `utf8proc_category` nem as demais propriedades Unicode.
5. Fixar Unicode 17.0.0 junto dos dados. A atualização da versão deve ser uma
   mudança explícita e revisável, não dependente de locale ou do sistema.

A opção recomendada para equivalência é gerar offline os intervalos a partir
de `UnicodeData.txt` da versão fixada, conferir a geração contra o utf8proc
vendorizado e versionar somente arrays `constexpr`. O build/runtime normal não
deve baixar dados. Invariantes estáticas devem provar ranges ordenados,
disjuntos, dentro de U+10FFFF e associados a exatamente uma flag.

Os testes de uma tarefa futura devem comparar `boundary_flag` para todos os
1.112.064 escalares, todos os vetores de bytes do decoder já enumerados e o
`TextTokenCursor` completo com cada pontuação entre, antes e depois de palavras,
inclusive sequências de flags misturadas e entradas malformadas. Os switches
específicos precisam de asserts de precedência. utf8proc deve continuar como
oracle de teste mesmo que deixe o runtime.

### Evidência do corpus e alternativa editorial

Uma varredura read-only/immutable das 393.961 unidades canônicas marcadas como
`latin` em `thelatinlibrary.sqlite3` percorreu 145.331.549 escalares, sem byte
UTF-8 inválido. Apareceram 47 codepoints de fronteira; 35 dependem apenas da
categorização geral. Entre eles estão colchetes/parênteses, `*`, hífen,
travessão, `&`, ponto médio, chaves, `_`, `/`, guillemets, aspas curvas e sinais
editoriais.

Congelar apenas esses 35 seria menor, mas mudaria o comportamento para
pontuação válida ainda ausente do corpus. Isso só é aceitável como uma nova
política editorial do produto, não como substituição equivalente. Portanto a
recomendação técnica é a tabela exata de Unicode 17; o corpus deve servir como
teste de uso real, não como allowlist.

Mesmo uma integração futura do normalizador não permitiria remover utf8proc do
runtime sem concluir também esse trabalho. Depois de ambos os componentes
passarem seus A/Bs, a dependência poderia ser movida para targets de teste e
oracle; alterar `target_link_libraries(words_core ...)` continua fora desta
investigação.

O estudo complementar
[estudo-backends-texto-nativo-wasm.md](estudo-backends-texto-nativo-wasm.md)
avalia contratos de entrada no browser, a fronteira Embind, alternativas para
um build nativo completo e um WASM finito e os testes necessários antes de
qualquer integração.

## 10. Riscos e avaliação

Riscos conhecidos:

- a prova vale somente para a allowlist atual; ampliar o alfabeto exige novas
  tabelas e nova enumeração;
- a precedência de diagnósticos obriga duas passagens no caminho não ASCII;
- a API proprietária possui seu próprio tipo de superfície; a alternativa
  não proprietária evita ownership, mas transfere ao chamador cinco buffers,
  lifetimes e uma precondição de não sobreposição;
- entradas próximas do limite de 2 GiB não foram materializadas em teste;
- os números de performance/tamanho são de microtargets e não substituem um
  A/B do WordsWASM completo;
- o oráculo e a PoC podem compartilhar uma interpretação errada do contrato,
  mitigada pela tabela explícita, vetores negativos e enumeração de bytes;
- uma tabela futura de pontuação ficará vinculada a Unicode 17; atualizar a
  versão pode mudar tokenização e precisa de diff gerado e revisão.

Avaliação: o domínio atual é pequeno o bastante para uma implementação
especializada demonstrável. A API com buffers mostra que zero alocações no
normalizador é viável e reduz bastante o micro-WASM; a economia potencial
concentra-se nas tabelas de dados e há sinal favorável de CPU. A categorização
restante também é finita — 263 ranges bastam para o contrato Unicode 17 — mas
é uma tarefa distinta que requer geração, enumeração escalar e testes do cursor.
A decisão de integração deve continuar separada e incluir um A/B do produto e
revisão da API interna. Manter utf8proc em produção continua sendo uma opção
tecnicamente válida.

## 11. Revisão independente dos resultados

Uma revisão posterior no HEAD `4bbbe3f`, ainda sem integrar a PoC à produção,
reconstruiu os targets que estavam desatualizados em relação às fontes e
repetiu as medições. O resultado consolidado está organizado por instrumento
na seção 7; esta tabela registra apenas a reprodutibilidade:

| Evidência | Estudo original | Revisão | Estado |
| --- | --- | --- | --- |
| testes específicos | 19 aprovados | 19/19 em 7,03 s | reproduzido |
| matriz completa | 148 aprovados | 148/148 em 11,56 s | reproduzido |
| fuzz diferencial | 200.000 execuções | 200.000, seed `20260910` | sem divergência |
| benchmark nativo | três execuções/corpus | mais três dentro das faixas; seis consolidadas | reproduzido |
| Callgrind | 5,91 M versus 48,18 M | 5,91 M versus 48,18 M | reproduzido |
| DHAT | 123.786/0/385.112 blocos | mesmos valores por stack | reprodução exata |
| três micro-WASM | tamanhos e hashes registrados | mesmos tamanhos e hashes após rebuild | reprodução exata |
| corpus congelado | 212 formas/19.981 ocorrências | mesmos totais; 108 formas não ASCII | reproduzido |

### Quanto o tokenizador ainda retém

Foi acrescentado à revisão um micro-WASM temporário que chama apenas
`utf8proc_category`, compilado com a mesma biblioteca estática, `-Oz`, LTO e
settings dos três targets de tamanho. O resultado foi:

| Uso isolado | RAW | DATA | gzip-9 | Brotli-11 |
| --- | ---: | ---: | ---: | ---: |
| somente `utf8proc_category` | 285.496 B | 285.070 B | 58.252 B | 36.551 B |
| transformação utf8proc do estudo | 332.520 B | 318.716 B | 88.076 B | 54.494 B |

Portanto, trocar somente a normalização e conservar
`TextTokenCursor::boundary_flag` mantém 85,9% dos bytes brutos e 67,1% do
Brotli do micro-WASM utf8proc. Isso fortalece a separação das metas: a PoC do
normalizador já sustenta um experimento de CPU/alocação, mas a redução grande
do binário depende também dos ranges de fronteira. Os totais desses ranges
foram novamente enumerados diretamente contra o vendorizado e coincidiram com
o relatório: 22/11, 27/20, 156/38 e 651/194 codepoints/ranges, totalizando 856
codepoints em 263 ranges.

O programa que gerou originalmente os ranges ainda não está versionado. Antes
de substituir `utf8proc_category`, a geração precisa tornar-se um artefato
reproduzível ou os arrays finais precisam entrar no repositório junto de um
teste exaustivo contra o oracle.

### Leitura para a engine

No perfil global anterior, `LatinLexer::lex` representou 11,75% das instruções
da engine. Aplicar mecanicamente as razões de Callgrind da PoC dá um teto
exploratório de aproximadamente 9,0% de redução com a API proprietária e 10,3%
com buffers, ou speedups globais de cerca de 1,10x e 1,12x. Isso é somente uma
aplicação da lei de Amdahl: mudanças de layout, ownership e inlining exigem o
A/B integrado.

O resultado de alocações também não deve ser lido como “engine sem heap”. A
PoC proprietária caiu de sete para uma média de 2,25 blocos por superfície; o
caminho com buffers zera apenas a normalização. `QueryResult` ainda precisa
possuir a superfície pública. Uma integração simples da API proprietária é o
baseline de menor complexidade. Scratch reutilizável é mais promissor para as
superfícies transitórias de síncope e ortografia, cujos dados são consumidos e
copiados para a derivação antes do retorno, mas precisa acomodar chamadas
aninhadas e palavras sem um limite público de 64 bytes.

Outras limitações observadas na revisão:

- `normalize_into` percorre o caminho não ASCII três vezes: validação total,
  interpretação/tamanhos e escrita. Essa é a contrapartida da garantia de não
  tocar nos buffers em caso de erro;
- o benchmark usa `MinSizeRel`/`-Os`, enquanto produção usa outro objetivo de
  otimização; seus ratios são sinal para o experimento, não um gate;
- a comparação exaustiva do decoder prova aceitação, escalar e largura contra
  `utf8proc_iterate`, mas, quando ambos rejeitam, não iguala a classe interna
  detalhada do erro. O contrato público atual compara corretamente o
  `DiagnosticCode`; se `Utf8DecodeError` virar API, essa matriz deve ser
  ampliada;
- os cinco spans de `normalize_into` têm precondição documentada de não
  sobreposição, não uma verificação em runtime;
- o `dist/words-web/words_wasm.wasm` de 767.066 bytes citado no estudo data de
  30/08. O artefato local de produção do HEAD da revisão mede 859.224 bytes;
  futuros A/Bs precisam congelar ambos os lados no mesmo commit e toolchain.
