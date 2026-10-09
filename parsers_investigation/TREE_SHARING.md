# Censo de compartilhamento entre árvores

Este documento preserva a auditoria **anterior** à mudança. O compartilhamento
foi depois implementado e medido em [TREE_SHARING_RESULTS.md](TREE_SHARING_RESULTS.md).
O auditor atual produz schema v2; os números abaixo são os resultados v1
preservados em `reports/tree-sharing-audit/`.

Auditoria local de 28/09/2026, após o [baseline DHAT/Callgrind](MEMORY.md).
Nenhuma representação do parser, algoritmo, produção ou CI foi alterada.

## O que está duplicado

As **7.041 árvores do oráculo são distintas**, tanto por `tree_id` quanto por
`(assignment_id, arcos ordenados)`. Não há árvores repetidas para eliminar.
O problema é que árvores distintas possuem cópias dos mesmos dados morfológicos.

`populate_best` cria uma entrada de `morphology_nbest` por atribuição aceita.
Depois, `populate_tree_best` reconstrói um vetor completo de `AnalysisChoice`
para cada árvore, mesmo quando a atribuição já está nessa primeira coleção.
As escolhas incluem strings, features tipadas, derivação, notices, trim e span.

| Cenário | Atribuições | Árvores | Cópias de escolhas nas árvores | Payload de análises nas árvores | Payload já existente em morphology N-best |
|---|---:|---:|---:|---:|---:|
| Oráculo, 17 fixtures | 1.084 | 7.041 | 30.830 | **8,81 MiB** | 1,01 MiB |
| MST, 17 fixtures | 1.084 | 1.084 | 3.553 | **1,01 MiB** | 1,01 MiB |
| Eisner, 17 fixtures | 1.084 | 1.084 | 3.553 | **1,01 MiB** | 1,01 MiB |
| MST + beam, frase longa | 512 | 512 | 9.728 | **2,75 MiB** | 2,75 MiB |

Payload aqui soma `capacity() * sizeof(AnalysisChoice)` dos vetores com seus
buffers dinâmicos de strings e vetores internos. Não inclui o header da árvore,
arcos, IDs, overhead do alocador ou memória temporária da busca. É armazenamento
retido no `Result`, **não uma nova medição de RSS nem economia já realizada**.
No corpus, os valores somam resultados por fixture; esse é o padrão de retenção
do ranker estrutural. O CLI que processa uma fixture por vez não os mantém todos
simultaneamente.

No ABI medido (Clang 21 + libstdc++, x86-64):

| Tipo | `sizeof` |
|---|---:|
| `AnalysisChoice` | 264 bytes |
| `Relation` | 56 bytes |
| `RankedMorphologyAnalysis` | 96 bytes |
| `RankedDependencyTreeAnalysis` | 152 bytes |

Os vetores de escolhas das árvores do oráculo reservam exatamente **8.139.120
bytes**, coincidindo com o ponto de alocação identificado pelo DHAT em
`parser.cpp:4396`. Seus buffers internos acrescentam **1.094.060 bytes**,
totalizando **9.233.180 bytes**. Essa é a base concreta da proposta de compartilhar.

Há até **105 árvores por atribuição** em duas fixtures de comparação com `quam`.
`comparison-quam-intelligentior` tem 56 atribuições e 2.600 árvores;
`comparison-quam-prudentior`, 56 e 1.908. Juntas respondem por **73,2%** do payload
morfológico repetido nas árvores do corpus. O custo está bastante concentrado.

## O que foi verificado

O executável local [audit_tree_sharing.cpp](audit_tree_sharing.cpp), ligado ao
mesmo parser do baseline, percorreu oráculo, MST e Eisner nas 17 fixtures e o
beam/MST na frase longa: **52 resultados**, **9.721 árvores** e **47.664 escolhas
nas árvores**.

Para cada árvore, verificou que:

1. `assignment_id` referencia exatamente uma entrada de `morphology_nbest`.
2. Todas as escolhas coincidem campo a campo, inclusive `MorphologyFeatures`
   completo e sua máscara, notices, derivação, avaliação Whitaker e span.
3. `morphology_score`, `matches_morphology_gold` e `matches_preferred_lemmas`
   coincidem com os metadados da entrada morfológica.
4. IDs e pares `(atribuição, arcos)` não têm duplicatas nos cenários medidos.

O censo também verificou identidade das escolhas por `(token, candidate)` dentro
de cada resultado. Os IDs não são globais entre fixtures. Todos esses checks
passaram; diferenças de morfologia não foram mascaradas por conversão para JSON.

## As relações pertencem à árvore

Não se pode trocar `tree.relations` pelas relações de `morphology_nbest`.
A segunda coleção contém a projeção determinística usada pelo baseline
sequencial, que pode ser diferente da árvore escolhida pelo decoder:

| Cenário | Árvores cujos arcos coincidem com a projeção morfológica |
|---|---:|
| Oráculo | 1.084 / 7.041 |
| MST | 955 / 1.084 |
| Eisner | 895 / 1.084 |
| Frase longa / MST | **0 / 512** |

Portanto, a referência compartilhada deve resolver **as escolhas morfológicas**;
os arcos continuam os da árvore. Reutilizar a projeção silenciosamente mudaria
o modelo estrutural e o resultado sintático.

Existem topologias de arcos repetidas entre atribuições diferentes: soma de
1.005 topologias distintas por fixture no oráculo e 49 na frase longa.
Isso permite estudar interning de arcos em outra etapa, mas não permite fundir
essas análises: morfologia, score e correspondência ao gold podem diferir.
Os labels das relações medidos cabem em SSO: zero bytes externos para suas
strings neste censo, embora o objeto `Relation` ainda ocupe 56 bytes.

## Primeira mudança recomendada

Reutilizar `Result::morphology_nbest` como dona das atribuições. Ela já existe;
não é necessário introduzir primeiro uma segunda arena com os mesmos dados.
Em `RankedDependencyTreeAnalysis`, substituir o vetor `analysis` por um índice
da entrada morfológica. Manter os arcos, IDs, scores e flags atuais nessa
primeira ablação reduz o número de mudanças simultâneas.

```cpp
using MorphologyIndex = std::uint32_t;

// Esboço; não implementado no parser.
struct Tree {
    MorphologyIndex morphology_index;
    std::vector<Relation> relations;
    // IDs, scores e flags atuais.
};

// Resolução durante a leitura:
const auto& choices = result.morphology_nbest[tree.morphology_index].analysis;
```

O índice é local ao `Result`, com conversão verificada para `uint32_t`.
`morphology_nbest` deve estar finalizado antes de construir as árvores e não
ser reordenado depois. Índices conservam a ligação ao copiar ou mover o
`Result`; views/spans podem ser construídas temporariamente no ponto de uso,
sem armazenar ponteiros persistentes para outro objeto.

O custo nominal de um índice de quatro bytes por árvore seria **28.164 bytes**
no oráculo e **2.048 bytes** no beam longo. Os payloads de 9.233.180 e 2.878.507
bytes nas árvores deixariam de ser necessários. Padding dos headers, alocador,
temporários e mudança do instante de pico precisam ser medidos após a mudança;
não subtrair esses valores diretamente do RSS atual.

Os consumidores a adaptar são pequenos e conhecidos:

- `populate_tree_best`: resolver o índice, sem reconstruir `AnalysisChoice`.
- `dependency_markov_ranker.cpp`: extrair fatores de uma view composta pelas
  escolhas da atribuição e pelas relações da árvore.
- `dependency_markov.hpp`: o extrator já é templated e acessa `.analysis` e
  `.relations`; uma view com spans conserva esse acesso sem copiar os vetores.
- `to_json`: resolver as escolhas compartilhadas e continuar emitindo o mesmo
  JSON completo, com a mesma ordem e os mesmos IDs.
- Self-tests/censo: conferir validade dos índices e igualdade dos resultados.

O ranker sequencial pode continuar consumindo `morphology_nbest` como hoje.
A ordem de acumulação dos scores, o desempate e a busca podem ser preservados.
Assim, esta etapa permite uma comparação direta de todos os resultados contra
o baseline, sem alterar smoothing, corpus, beam ou serialização externa.

## Compartilhamento adicional e temporários

Depois dessa primeira mudança, ainda haveria cópias entre atribuições:
as 3.553 escolhas morfológicas do corpus representam apenas **212 candidatos
selecionados distintos**, contados por fixture/token/candidato. Na frase longa,
9.728 escolhas representam **36 candidatos selecionados**.

Uma segunda etapa poderia guardar um pool de candidatos e sequências de IDs:
pool denso mais índices de quatro bytes ocuparia, para esses componentes,
**77.447 bytes** no corpus e **49.570 bytes** na frase longa. São estimativas
de payload que excluem headers das atribuições, capacidades excedentes, arcos,
scores, IDs e candidatos do lattice não selecionados. Essa etapa alcança mais
consumidores e exige preservar toda a informação de derivação e provenance.

Também existe duplicação temporária anterior ao `Result`: cada
`DependencyTreeAnalysis` guarda `Assignment` e `vector<DependencyArc>`, e ambos
permanecem vivos enquanto `populate_tree_best` constrói a representação final.
No oráculo, a inserção da árvore também copia o vetor local de arcos
(`parser.cpp:3239`). Esse custo pode ser tratado em uma ablação separada;
o censo atual mede a representação final e não atribui um pico próprio a esses
temporários.

## Reprodução

Usar o build e a WWDB 1.11 locais descritos em [MEMORY.md](MEMORY.md).
O auditor é compilado manualmente, sem adicionar alvos CMake ou testes de CI:

```sh
/home/fabio/.llvm/toolchains/source-llvmorg-21-init/bin/clang++ \
  -std=c++23 -O3 -g -gdwarf-4 -Wall -Wextra -Werror \
  -Iinclude -Iparsers_investigation -isystem vendor/json/include \
  parsers_investigation/audit_tree_sharing.cpp \
  build/parsers-memory/parsers_investigation/libparsers_investigation_lib.a \
  build/parsers-memory/libwords_core.a \
  build/parsers-memory/vendor/utf8proc/libutf8proc.a \
  -o build/parsers-memory/audit_tree_sharing
build/parsers-memory/audit_tree_sharing \
  build/parsers-memory/words-profile.wwdb \
  parsers_investigation/corpus/agreement_fixtures.json dependency-tree-oracle
```

Trocar a estratégia por `dependency-mst` ou `dependency-eisner` para as outras
linhas do corpus. A [fixture longa](reports/tree-sharing-audit/long-fixture.json)
usa o ID `ad-hoc` para acionar o mesmo fallback de beam do CLI.

Resultados: [resumo](reports/tree-sharing-audit/summary.json),
[oráculo](reports/tree-sharing-audit/oracle.json),
[MST](reports/tree-sharing-audit/mst.json),
[Eisner](reports/tree-sharing-audit/eisner.json),
[beam longo](reports/tree-sharing-audit/long-beam.json) e
[manifest com comandos e hashes](reports/tree-sharing-audit/manifest.json).

A detecção de SSO inspeciona se `string.data()` aponta para dentro do objeto;
é uma medição do ABI local, não uma garantia portátil de layout ou uma contagem
do overhead de malloc. As estruturas auxiliares usadas pelo auditor não entram
nos totais de payload. O auditor não foi usado para medir desempenho ou RSS.
