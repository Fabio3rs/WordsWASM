# Revisão da fila de quantidades vocálicas

Data do corte: 2026-08-29.

Este relatório registra a revisão do primeiro snapshot produzido por
`poc/compact-db/suggest_quantity_evidence.py`. A fila é uma ferramenta
editorial regenerável; não integra o WWDB e nenhum score promove dados por si
só.

## Resultado

O casamento inicial por prefixo produzia 221.692 candidatos e foi descartado.
Depois dos filtros por radical de citação, POS, gênero e terminação, a primeira
fila tinha 18.466 registros. A revisão encontrou e corrigiu quatro fontes de
ruído:

- distinção entre nome próprio e nome comum;
- rejeição de palavras truncadas por `^`, `_`, hífen ou segundo termo;
- remoção do próprio lema/radical da sobreposição semântica;
- uso de gênero nominal explícito para substituir `OTHER` migrado por `NOUN`;
- recálculo de alternativas depois da supressão de evidência já registrada.

Após essas correções e a promoção dos lotes explicitamente conferidos, restam:

| Medida | Contagem |
| --- | ---: |
| candidatos | 16.780 |
| testemunhos externos | 15.027 |
| alvos Whitaker | 15.924 |
| candidatos com alvo único | 13.646 |
| testemunhos com mais de um alvo | 1.381 |
| alvos com conflito real longa × breve | 127 |
| candidatos envolvidos nesses conflitos | 312 |
| Lewis & Short | 16.460 |
| Gaffiot | 320 |

“Conflito” exige marcas opostas na mesma posição do mesmo alvo. Uma fonte com
máscara parcial e outra com máscara mais completa não é conflito.

## Lotes promovidos

Foram promovidos 38 alvos lexicais após conferência dos verbetes nativos,
morfologia e sentido:

- 20 de Lewis & Short: `adminiculum`, `alacer`, `amarus`, `anima`, `animus`,
  `calor`, `clarus`, `cupiditas`, `cutis`, `decus`, `harundo`, `hiems`,
  `imago`, `integer`, `litus`, `lumen`, `nomen`, `puella`, `ruber` e `vetus`;
- 18 alvos Gaffiot: onze invariáveis exatos (`absque`, `abusque`, `adhuc`,
  `aliter`, `antequam`, `apud`, `dudum`, `dummodo`, `erga`, `etenim`, `etsi`)
  e sete registros Whitaker pertencentes a `congruo`, `defendo` e `dissicio`.

O segundo lote segue a prioridade semântica nova. Ele promove 32 alvos de 13
famílias em que a mesma forma de citação ASCII representa lexemas distintos:
`anas`, `furor`, `incido`, `latesco`, `latus`, `lego`, `levis`, `liber`,
`occido`, `palus`, `plecto`, `populus` e `tuber`. Exemplos centrais são
`lĕgo` “ler” × `lēgo` “legar”, `lĕvis` “leve” × `lēvis` “liso” e `occĭdo`
“cair” × `occīdo` “matar”.

Somados às seis entradas `mal-` anteriores, o WWDB contém agora 76 máscaras
lexicais e três máscaras flexionais. Os locators Gaffiot promovidos usam
`entry_id` do `gaffiot.db`, não a chave textual transitória do SuperDB.

## Rejeições confirmadas

| Fonte | Candidato incorreto | Razão |
| --- | --- | --- |
| LS `n10377`, `Cŏnōn` | `con-`, “cone” | antropônimo × substantivo comum |
| LS `n11126`, `Cŏra` | `cor-`, “pupila” | nome próprio × substantivo comum |
| LS `n11234`, `cŏrō^na` | `cor-` | corte indevido no marcador editorial |
| LS `n11380`, `Cŏsa` | `cos-`, “pedra de amolar” | topônimo × substantivo comum |
| LS `n11489`, `Crătĕrus` | `crater-`, “taça” | antropônimo × substantivo comum |
| Gaffiot `7552`, `Bacchĭus` | entradas de taça/pé métrico | adjetivo “de Baco” |
| Gaffiot `19450`, `Delphĭs` | `delphis`, “golfinho” | adjetivo “de Delfos” |
| Gaffiot `7652`, `bālans` | `balan-`, “bolota” | particípio “balindo” |

## Priorização restante

Preencher cada vogal conhecida não é o objetivo imediato. Uma máscara tem
maior valor quando permite ao usuário selecionar outro lexema ou sentido. O
gerador agora exige, para a faixa de homógrafos:

1. a mesma palavra de citação depois da remoção de mácrons e breves;
2. dois testemunhos distintos;
3. alvos Whitaker distintos;
4. oposição explícita longa × breve na mesma posição e dentro do radical que
   será armazenado.

| Faixa editorial | Contagem | Uso |
| --- | ---: | --- |
| homógrafo direto | 8 grupos, 17 mapeamentos, 16 alvos únicos | revisar primeiro; cada testemunho tem um só alvo estrutural |
| homógrafo com revisão semântica | 31 grupos adicionais, 140 mapeamentos | escolher o alvo correto pela definição e pelo paradigma |
| quantidade não distintiva | 16.623 candidatos | baixa prioridade enquanto não houver outro consumidor |
| conflito | 312 candidatos em 127 alvos | bloquear até resolver longa × breve |

“Direto” ainda não significa confirmado. Por exemplo, o filtro genérico de
terminações associa incorretamente `sŏlum`/`sŏlus` ao radical de `sol`, “sol”;
o paradigma real não produz essas formas. Isso deve ser rejeitado na revisão
e, futuramente, por terminações de citação específicas do paradigma.

As faixas são prioridade de trabalho, não níveis de verdade. Em especial,
`OTHER`, POS ausente, gênero ausente, sobreposição textual ou unicidade
estrutural nunca bastam para `confirmed`.

O Gaffiot migrado tem POS em apenas 651 de 28.258 entradas e gênero em uma.
Além disso, alguns `subst` foram normalizados como `NOUN` apesar de o cabeçalho
principal ser adjetival. Por isso os substantivos Gaffiot não devem ser
promovidos em massa; formas invariáveis exatas e verbetes individualmente
conferidos são o caminho seguro.

## Collatinus como testemunho derivado

O corte seguinte integrou o clone local do Collatinus 11 no mesmo gerador de
fila. `lemmes.la` contém 24.072 registros de seis campos; `modeles.la` fornece
POS por modelos com herança, e `lemmes.en` fornece glossas indexadas pelo nome
ASCII mais o discriminador de homógrafo. O importador conserva como locator o
arquivo e essa chave (`lemmes.la:levis2`, por exemplo), não o número de linha.

Com somente o léxico-base do Collatinus e a mesma supressão da evidência já
curada, a fila combinada passou de 16.780 para 32.211 candidatos. Os 15.431
novos candidatos Collatinus alcançam 40 grupos adicionais com oposição de
quantidade, cinco deles estruturalmente diretos. No total combinado aparecem
79 grupos e 13 diretos. Esses totais medem cobertura editorial, não fontes
independentes.

O Collatinus contém os dois membros explicitamente marcados de dez famílias
críticas já revisadas: `furor`, `incido`, `latus`, `lego`, `levis`, `liber`,
`occido`, `palus`, `populus` e `malum`. Para `anas`, `plecto` e `tuber`, o
snapshot oferece apenas parte da oposição útil; `latesco` não produziu
candidato. Isso o torna uma boa verificação de cobertura e um localizador de
casos novos, mas não um terceiro voto automático.

O relatório agora publica, para cada fonte, `family` e
`independent_quantity_authority`. Collatinus usa a família
`collatinus-derived` e valor `false`, porque seu léxico agrega Lewis & Short,
Gaffiot, Georges e outras obras. Contá-lo ao lado de suas fontes componentes
criaria consenso circular. Toda evidência gerada continua
`confidence: needs_review`, e a fonte auxiliar não pode ser promovida pelo
compilador de quantidades.

O léxico `lem_ext.la`, com 57.909 registros adicionais, permanece opt-in por
`--collatinus-extended`, pois mistura ampliação medieval e variantes que não
devem aumentar silenciosamente a fila clássica. A auditoria separada dos dois
arquivos produziu 46.371 candidatos totais, 29.591 deles Collatinus, além de
134 grupos de oposição e 27 diretos. O aumento confirma que o padrão seguro é
manter apenas o léxico-base na revisão clássica corrente.

## Terceira família: léxico Latim–Alemão

`token_latim_german.sqlite` contém 36.140 registros em `VOC`; 21.524 citações
possuem pelo menos um mácron e nenhuma usa breve. O leitor considera apenas as
classes `s`, `a` e `v`, mapeadas para substantivo, adjetivo e verbo. Para
verbos, a primeira pessoa depois da vírgula é a forma de citação comparada ao
slot 1 de Whitaker. Entradas com `grammar = '-'`, incluindo locuções, ficam
fora deste corte.

Com Lewis & Short, Gaffiot, Collatinus-base e o Latim–Alemão, a fila contém
44.080 candidatos: 11.869 vêm do Latim–Alemão e 15.431 do Collatinus derivado.
As três famílias independentes somam 28.649 candidatos; isso não implica que
todos sejam casamentos lexicais corretos.

O consenso é calculado por posição de letra e somente para testemunhos cujo
casamento tem um único alvo estrutural. O snapshot produziu:

| Decisão por posição | Contagem |
| --- | ---: |
| `consensus_2_of_3` | 6.089 |
| `single_source` | 29.687 |
| `conflict` | 147 |
| `derived_only` | 13.649 |
| posições excluídas por casamento ambíguo | 17.070 |

As 76 máscaras lexicais já confirmadas cobrem 127 posições no relatório:
18 agora têm concordância de duas famílias independentes, 92 permanecem com
uma fonte independente e 17 têm somente observações derivadas adicionais.
Nenhuma das posições já curadas entrou em conflito. Entre as confirmações
novas aparecem `ămārus`, `clārus`, `ĭmāgo`, `lātus`, `lītus`, `lūmen`,
`mālus` “mastro”, `nōmen`, `pālus`, `pōpŭlus` e `tūber`.

O Latim–Alemão é particularmente útil como testemunho positivo de vogal longa.
Uma grafia não marcada não vota em “breve” nem contradiz outra fonte. Os 147
conflitos globais continuam bloqueados para revisão: podem indicar divergência
lexicográfica, mas também um casamento estrutural único com sentido incorreto.
Por isso nem mesmo `consensus_2_of_3` autoriza promoção automática neste corte.

## Limite atual

O testemunho de uma forma de citação alimenta somente o `slot 1`. Mesmo quando
outro slot contém bytes iguais, propagá-lo exige uma regra editorial explícita
ou conferência do principal part correspondente. Os testes de runtime cobrem
o primeiro lote em `pŭella`, `ăpŭd` e `dēfendo` e o lote crítico em
`ănăs/ănās`, `incĭdo/incīdo`, `lĕgo/lēgo`, `lĕvis/lēvis`, `lĭber/līber`,
`occĭdo/occīdo`, `pŏpulus/pōpulus` e `tŭber/tūber`, inclusive a rejeição da
quantidade oposta para o mesmo lexema.
