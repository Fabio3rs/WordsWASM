# Formato dos arquivos de dados legados

Este documento descreve os arquivos presentes nesta cópia do repositório,
gerados por GNAT 13.3.0 para x86-64 Linux. O layout foi verificado por três
fontes independentes:

- as declarações Ada e as instanciações de `Ada.Direct_IO`;
- a informação de representação emitida por `gcc -gnatR3`;
- inspeção dos artefatos com `file`, `xxd`, `od`, tamanhos e contagem dos
  registros.

As estruturas C compatíveis com C++ estão em
[`src/legacy_data_layout.h`](../src/legacy_data_layout.h).

## Visão geral

Não existe cabeçalho, magic number, versão, contador ou checksum em nenhum dos
três binários. Eles são imagens consecutivas da representação em memória de
tipos Ada:

| Arquivo | Unidade física | Quantidade nesta cópia | Tamanho total |
| --- | ---: | ---: | ---: |
| `DICTFILE.GEN` | `Dictionary_Entry`, 180 bytes | 39.339 | 7.081.020 bytes |
| `STEMFILE.GEN` | `Dictionary_Stem`, 56 bytes | 62.086 | 3.476.816 bytes |
| `INFLECTS.SEC` | `Lel_Section`, 22.800 bytes | 5 | 114.000 bytes |

Os artefatos concretos inspecionados têm estes SHA-256:

| Arquivo | SHA-256 |
| --- | --- |
| `DICTFILE.GEN` | `49b49bb648255a68383b4c93a6954fccd6857d024f01a2a73ae3f7b9b40bbe47` |
| `STEMFILE.GEN` | `34d3e48a346c3c4b60f0b6505644823457a75d6d8b02d9678cacfae248e14102` |
| `INDXFILE.GEN` | `baf36db98d76120a0e0571d4aed7ed86fea1b3ad0489b82680d22a01c67abe58` |
| `INFLECTS.SEC` | `3baf613d0363badf05f109193e34bd00b49f1366c2d89b279da9fd8e4bdd97fd` |

Cada `Lel_Section` contém exatamente 570 `Inflection_Record` de 40 bytes.
Todos os inteiros multibyte observados estão em little-endian. `Integer`,
`Natural`, `Stem_Key_Type`, `Which_Type`, `Variant_Type` e
`Ending_Size_Type` ocupam 32 bits; `Ada.Direct_IO.Count` ocupa 64 bits. Os
enums ocupam um byte e armazenam a posição ordinal declarada em Ada, começando
em zero.

Esse é um layout de ABI, não um formato estável. Em especial, há bytes de
padding e caudas de unions discriminadas que não são inicializados pelo
gerador. Eles aparecem, por exemplo, como `6b 52 54` e outros valores variáveis
no primeiro registro de `DICTFILE.GEN`. Um leitor deve ignorá-los.

`INDXFILE.GEN`, apesar do nome e de ser tratado junto aos índices, **não é
binário**. `UNIQUES.LAT`, `ADDONS.LAT`, `DICTLINE.GEN` e `INFLECTS.LAT` também
são texto.

## `DICTFILE.GEN`

O arquivo é:

```text
Dictionary_Entry[39339]
```

Cada registro mede 180 bytes:

| Offset | Bytes | Campo | Representação |
| ---: | ---: | --- | --- |
| 0 | 72 | `Stems` | quatro strings de 18 bytes, preenchidas com espaço |
| 72 | 20 | `Part` | classe gramatical + payload variante |
| 92 | 5 | `Tran` | época, área, geografia, frequência e fonte; 1 byte cada |
| 97 | 80 | `Mean` | significado de tamanho fixo, preenchido com espaço |
| 177 | 3 | padding final | sem significado |

`Part` tem este envelope:

| Offset relativo | Bytes | Campo |
| ---: | ---: | --- |
| 0 | 1 | `Pofs`, ordinal de `Part_Of_Speech_Type` |
| 1 | 3 | padding |
| 4 | 16 | payload variante |

O payload é escolhido por `Pofs`:

| `Pofs` | Valor | Payload dentro dos 16 bytes |
| --- | ---: | --- |
| `N` | 1 | `Decn_Record` em +0, gênero em +8, tipo do substantivo em +9 |
| `PRON` / `PACK` | 2 / 3 | `Decn_Record` em +0, tipo do pronome em +8 |
| `ADJ` | 4 | `Decn_Record` em +0, comparação em +8 |
| `NUM` | 5 | `Decn_Record` em +0, tipo em +8, valor `uint32` em +12 |
| `ADV` | 6 | comparação em +0 |
| `V` | 7 | `Decn_Record` em +0, tipo do verbo em +8 |
| `PREP` | 10 | caso regido em +0 |
| demais | outros | nenhum payload semântico |

Um `Decn_Record` mede 8 bytes: `Which` é um `uint32` em +0 e `Var` é um
`uint32` em +4.

O primeiro registro começa assim:

```text
00000000: 41 20 20 20 20 20 20 20 20 20 20 20 20 20 20 20
...
00000048: 01 6b 52 54 09 00 00 00 08 00 00 00 01 05 00 00
00000058: 90 02 00 00 00 00 00 03 07 41 75 6c 75 73 20 28
```

Em `0x48`, `01` é `Pofs=N`; `09 00 00 00` e `08 00 00 00` são declinação 9,
variante 8; `01` é gênero masculino; `05` é `Noun_Kind=N`. Em `0x5c` começam
os cinco bytes de tradução (`X X X C G`) e em `0x61` começa `Aulus ...`. Os
bytes intermediários não pertencentes a campos são padding.

O número físico do registro é o `MNPC` usado nos outros arquivos e é
**baseado em 1**: o registro `MNPC=n` começa em `(n - 1) * 180`.

## `STEMFILE.GEN`

O arquivo é:

```text
Dictionary_Stem[62086]
```

Cada registro mede 56 bytes:

| Offset | Bytes | Campo | Representação |
| ---: | ---: | --- | --- |
| 0 | 18 | `Stem` | string fixa preenchida com espaço |
| 18 | 2 | padding | sem significado |
| 20 | 20 | `Part` | exatamente o envelope descrito acima |
| 40 | 4 | `Key` | `uint32` little-endian |
| 44 | 4 | padding | sem significado |
| 48 | 8 | `MNPC` | contador de 64 bits little-endian, baseado em 1 |

Os primeiros 56 bytes vistos por `xxd -g4` são:

```text
00000000: 20202020 20202020 20202020 20202020
00000010: 20200000 07000000 05000000 01000000
00000020: 01000000 a8770000 02000000 00000000
00000030: ab990000 00000000
```

Eles representam o radical vazio especial de `esse`: `Pofs=V` (7),
conjugação `5 1`, `Verb_Kind=TO_BE` (1), chave 2 e `MNPC=39339` (`0x99ab`).
`a8 77` e bytes adjacentes pertencem à cauda não inicializada do payload ou ao
padding e devem ser descartados.

## `INDXFILE.GEN`

É um arquivo ASCII com 703 linhas de exatamente 46 bytes, incluindo o `LF`:

```text
offset  tamanho  conteúdo
0       2        chave de duas letras, espaços permitidos
2       1        espaço
3       20       primeiro índice, decimal alinhado à direita
23      1        espaço
24      20       último índice, decimal alinhado à direita
44      1        espaço
45      1        LF (0a)
```

As 703 linhas são uma chave em branco, 26 chaves de uma letra e 676 pares de
letras (`1 + 26 + 26*26`). Intervalos inexistentes contêm `0 0`. Os índices
são posições baseadas em 1 em `STEMFILE.GEN`, com os dois extremos inclusivos.

O começo do arquivo em bytes deixa o formato visível:

```text
00000000: 20 20 20 20 20 20 20 20 20 20 20 20 20 20 20 20
00000010: 20 20 20 20 20 20 31 20 20 20 20 20 20 20 20 20
00000020: 20 20 20 20 20 20 20 20 20 20 20 31 20 0a 61 20
```

## `INFLECTS.SEC`

O tipo passado a `Ada.Direct_IO` não é um registro individual, mas um array
`Lel_Section` inteiro. O arquivo contém cinco seções, sem diretório:

| Seção Direct_IO | Offset | Conteúdo | Regras usadas nesta cópia |
| ---: | ---: | --- | ---: |
| 1 | 0 | terminações finais `a` a `i` | 562 |
| 2 | 22.800 | finais `m` a `r` | 508 |
| 3 | 45.600 | final `s` | 493 |
| 4 | 68.400 | finais `t` a `u`, mais regras pronominais especiais | 164 |
| 5 | 91.200 | terminações vazias | 58 |

Cada seção reserva 570 posições. As regras usadas vêm primeiro; o restante é
preenchido com `Null_Inflection_Record`. O teste robusto de posição nula é o
registro inteiro semântico nulo, não apenas bytes de padding.

Cada `Inflection_Record` mede 40 bytes:

| Offset | Bytes | Campo |
| ---: | ---: | --- |
| 0 | 20 | `Qual`: classe gramatical + payload variante |
| 20 | 4 | chave do radical, `uint32` |
| 24 | 4 | comprimento da terminação, `uint32` |
| 28 | 7 | terminação fixa, preenchida com espaço |
| 35 | 1 | padding do `Ending_Record` |
| 36 | 1 | época |
| 37 | 1 | frequência |
| 38 | 2 | padding final |

`Qual` começa com `Pofs` em +0, três bytes de padding e um payload de 16 bytes
em +4. Os layouts do payload são:

| `Pofs` | Payload relativo a +4 |
| --- | --- |
| `N`, `PRON`, `PACK`, `SUPINE` | `Decn_Record` +0; caso +8; número +9; gênero +10 |
| `ADJ` | campos anteriores e comparação +11 |
| `NUM` | campos anteriores e tipo de numeral +11 |
| `ADV` | comparação +0 |
| `V` | `Decn_Record` +0; tempo/voz/modo +8/+9/+10; pessoa +11; número +12 |
| `VPAR` | `Decn_Record` +0; caso/número/gênero +8/+9/+10; tempo/voz/modo +11/+12/+13 |
| `PREP` | caso regido +0 |

Por exemplo, o primeiro registro começa com:

```text
00000000: 08 00 00 00 06 00 00 00 02 00 00 00 02 02 03 01
00000010: 01 05 00 00 01 00 00 00 05 00 00 00 65 6e 74 69
00000020: 61 20 20 20 00 01 20 20
```

Isso decodifica para `VPAR 6 2 VOC P N PRES ACTIVE PPL`, chave 1, terminação
`entia` de tamanho 5, época `X` e frequência `A`. O valor 8 de `Pofs` é a
posição ordinal de `VPAR`; caso 2 é `VOC`; número 2 é plural; gênero 3 é
neutro; tempo/voz/modo 1/1/5 são `PRES/ACTIVE/PPL`.

## Ordinais

Os valores completos usados pelos structs estão nomeados no header. As séries
mais importantes são:

```text
Pofs:       X=0 N=1 PRON=2 PACK=3 ADJ=4 NUM=5 ADV=6 V=7 VPAR=8
            SUPINE=9 PREP=10 CONJ=11 INTERJ=12 TACKON=13 PREFIX=14 SUFFIX=15
Case:       X=0 NOM=1 VOC=2 GEN=3 LOC=4 DAT=5 ABL=6 ACC=7
Number:     X=0 S=1 P=2
Gender:     X=0 M=1 F=2 N=3 C=4
Comparison: X=0 POS=1 COMP=2 SUPER=3
Tense:      X=0 PRES=1 IMPF=2 FUT=3 PERF=4 PLUP=5 FUTP=6
Voice:      X=0 ACTIVE=1 PASSIVE=2
Mood:       X=0 IND=1 SUB=2 IMP=3 INF=4 PPL=5
Age:        X=0 A=1 B=2 C=3 D=4 E=5 F=6 G=7 H=8
Frequency:  X=0 A=1 B=2 C=3 D=4 E=5 F=6 I=7 M=8 N=9
```

## Implicações para o porte

As estruturas permitem inspecionar e converter **estes** arquivos, desde que o
host tenha a ABI esperada. Elas não tornam o formato adequado ao WebAssembly:

- a endianness não está marcada;
- não há versão nem validação de integridade;
- padding contém dados indefinidos;
- offsets e alinhamento vêm do compilador Ada;
- uma mudança de alvo, runtime ou opções pode mudar a representação.

O empacotador de `words.wwdb` deve ler os campos semânticos e escrevê-los um a
um no formato portátil proposto, nunca copiar esses structs diretamente para o
novo banco.
