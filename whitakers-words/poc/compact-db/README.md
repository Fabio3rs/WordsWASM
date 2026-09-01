# PoC do banco compacto

O estado consolidado da engine, dos formatos, do pipeline editorial e dos
itens pendentes está em
[`docs/estado-implementacao.md`](../../docs/estado-implementacao.md).

Este diretório materializa a aproximação descrita em
[`docs/auditoria-binarios-e-formato-compacto.md`](../../docs/auditoria-binarios-e-formato-compacto.md).

É um PoC rápido para medir tamanho e compressibilidade. Ele produz quatro perfis:

- `simple`: registros de 19/5/8 bytes em linhas;
- `dense`: registros de 16/3/6 bytes em linhas;
- `columnar`: os mesmos registros densos, organizados em colunas de bytes;
- `search-only`: perfil colunar sem definições nem `meaning_id`, com lexemas de
  14 bytes para autocomplete/busca de um site que resolve o conteúdo fora do
  WWDB. No CLI esse perfil pode ser apresentado como
  [JSON enxuto de busca](../../docs/formato-json-busca.md); no navegador ele
  alimenta diretamente as structs Embind com lema, classe e morfologia. Ele
  não produz sozinho o [JSON completo de análise](../../docs/formato-json-canonico.md)
  nem qualquer `meaning`.

No perfil colunar, IDs densos são posições nos vetores e cada coluna pode ser
acessada diretamente; não é necessário reconstruir um array de structs. Ainda
assim, este não é o formato final. A versão PoC 1.8 carrega 23 seções no
perfil full e 18 no search. Nove
delas representam todos os registros de formação de `ADDONS.LAT`: 135 prefixos
(incluindo seis tickons), 179 sufixos e 29 tackons, dos quais 11 são packons.
Cada família tem
pool de formas, pool de meanings e registros tipados. Prefixos ocupam 8 bytes,
sufixos 14 e tackons/packons 10, incluindo o `AddonId` global. O perfil
`search-only` omite os pools de meanings e usa, respectivamente, 6, 12 e 8
bytes.

O 1.8 aproveita bits antes reservados do payload de lexemas `PACK` para gravar
o seletor tipado do packon. O dado não aumenta o registro: ele substitui a
inferência histórica por prefixo de texto como `(w/-cum)`. A projeção desses
marcadores está materializada em `PACKON_REQUIREMENTS.LAT` e é conferida
integralmente contra `DICTLINE.GEN`; o packer e a engine consomem somente o
ledger tipado, nunca o meaning. Isso é necessário
para que `search-only` reproduza `cuique`, `quicumque` e as demais formas sem
carregar nenhum significado editorial.

As três seções de reescrita materializam 170 micro-regras tipadas de
`REWRITES.LAT`: 11 de síncope e 159 ortográficas importadas
deterministicamente das tabelas Ada:
pool das formas, pool dos meanings e registros de reescrita. Cada registro
completo ocupa 16 bytes e guarda IDs de `before`, `after`, nome estável e
meaning, mais payloads com tipo, estágio, operação, escopo, prioridade,
direção, contexto, época e restrições semânticas. `search-only` omite o
meaning e usa 14 bytes. A engine interpreta as operações literais, assimilação
dinâmica e duplicação de consonante, com caminhos limitados a duas reescritas.

O fonte humano permanece deliberadamente estreito, em duas linhas por regra:

```text
SYNCOPE 1 MAIN LITERAL INTERNAL REVERSE as avis V 3 0 1 ANY CLASSICAL perfect-v-contraction
Syncopated perfect often drops the 'v' and contracts vowel
```

Os campos são `kind priority stage operation scope direction before after
required-part stem-key min-before min-after constraint era stable-name`; a
linha seguinte contém o meaning. Não são opcodes arbitrários. O packer valida
os domínios e reduz o cabeçalho a payloads explícitos de 32 e 16 bits; a engine
continua responsável por um conjunto fechado de operações latinas tipadas.

As duas seções novas de quantidade são geradas de `QUANTITIES.LAT`. Esse
arquivo denso, por sua vez, é uma projeção determinística de
`QUANTITY_EVIDENCE.jsonl` feita por
[`import_quantities.py`](import_quantities.py). A coluna
`inflection_quantities` possui um `u16` por regra, com duas máscaras de sete
bits, e permite acesso O(1). `stem_quantities` é esparsa: cada registro de nove
bytes identifica lexema/slot e guarda duas máscaras de 18 bits. O corte atual
cura três regras de `-a` da primeira declinação e 76 alvos lexicais: seis da
família `malum`, 38 conferidos no primeiro lote LS/Gaffiot e 32 alvos do lote
de homógrafos diferenciados por quantidade. Entradas ASCII
continuam ignorando essas restrições. A auditoria da fila está em
[`docs/revisao-fila-quantidades.md`](../../docs/revisao-fila-quantidades.md).

O manifesto separa autoridade da fonte e confiança da observação. Lewis &
Short e Gaffiot são classificados como fontes lexicográficas estabelecidas e
de alta confiabilidade; Faria v3 como OCR revisado por LLM; migrações antigas
como auxiliares. Somente observações `confirmed` de fonte não auxiliar são
promovidas. Conflito entre confirmações é fatal; `probable` e `needs_review`
aparecem no relatório JSON do importador, mas não chegam ao WWDB.

[`suggest_quantity_evidence.py`](suggest_quantity_evidence.py) prepara a fila
editorial sem alterar esse manifesto. Ele abre o SuperDB em modo SQLite
`immutable=1`, conserva o nome e o `source_entry_id` da obra subjacente, lê os
IDs/slots diretamente de `DICTFILE.GEN` e projeta somente mácrons/breves que
caem no radical de citação (`slot 1`). POS, gênero e terminações de citação
eliminam incompatibilidades evidentes; sobreposição de glossas inglesas é
apenas sinal de revisão. Todos os registros gerados têm
`confidence: needs_review`: precisam ser conferidos e movidos manualmente para
`QUANTITY_EVIDENCE.jsonl`; o gerador jamais os promove.
O relatório marca separadamente palavras de citação idênticas em ASCII que
possuem oposição explícita longa × breve. Essa é uma prioridade editorial, não
uma autorização de promoção automática: paradigma e sentido ainda precisam
ser conferidos.

O mesmo gerador pode ler diretamente o léxico textual do Collatinus 11. Ele
resolve a herança de POS em `modeles.la`, valida as seis colunas de
`lemmes.la`, preserva o discriminador de homógrafo na proveniência e junta as
glossas de `lemmes.en`. O Collatinus é registrado como fonte derivada e
auxiliar: seu léxico foi sintetizado de Lewis & Short, Gaffiot, Georges e
outras obras, portanto seus candidatos nunca constituem um voto independente.
O arquivo medieval/estendido `lem_ext.la` é opt-in para que a fila clássica
não seja ampliada acidentalmente.

O banco Latim–Alemão pode ser acrescentado por `--latin-german`. O leitor abre
`token_latim_german.sqlite` em modo imutável, aceita somente verbetes nominais,
adjetivais e verbais, usa a segunda forma de citação dos verbos e ignora
entradas frasais. Apenas mácrons explícitos geram observações: vogal sem marca
continua `unknown` e nunca equivale a breve.

O relatório calcula decisões por alvo, posição lógica e família de fonte.
Somente um casamento estrutural sem alternativas pode votar. Duas famílias
independentes concordantes produzem `consensus_2_of_3`; uma produz
`single_source`; oposição explícita independente produz `conflict`.
Collatinus aparece separadamente em `derived_votes` e não aumenta o suporte.
Evidências `confirmed` já presentes no manifesto também participam do
relatório, embora continuem suprimidas da fila regenerada. Essas decisões
priorizam revisão; não promovem dados automaticamente.

[`audit_lexical_expansion.py`](audit_lexical_expansion.py) reutiliza os mesmos
leitores para medir cobertura lexical, inclusive lemas sem diacríticos. A
unidade é `(lema ASCII, POS, próprio/comum)`, e cobertura exige um radical de
citação compatível no slot 1. O JSONL resultante contém candidatos
estruturalmente ausentes, nunca “novos lexemas” já confirmados. O Faria v3
quality é uma camada opt-in separada; resultados e limitações estão em
[`docs/auditoria-ampliacao-lexical.md`](../../docs/auditoria-ampliacao-lexical.md).
O auditor resolve ainda as regras de radical `R:` do Collatinus, aprende mapas
de paradigma e de slots somente de exemplos Words já cobertos e valida os
radicais gerados contra `INFLECTS.SEC` e as formas do banco Latim–Alemão. A
opção `--draft-output` grava apenas estruturas corroboradas; sentidos e demais
campos editoriais continuam pendentes e impedem importação automática.

[`prepare_lexeme_review.py`](prepare_lexeme_review.py) transforma esses
rascunhos numa fila editorial determinística. O `draft_id` identifica a chave
lexical e a revisão SHA-256 fixa exatamente estrutura e testemunhas. A fila
separa voto independente de testemunho derivado, preserva toda vogal não
marcada como `unknown`, sinaliza remissões e multiplicidade de entradas e
mantém `automatic_promotion_allowed: false`. O contrato e a revisão dos 66
casos estão em
[`docs/revisao-editorial-lexemas.md`](../../docs/revisao-editorial-lexemas.md).
Um JSONL canônico da engine pode ser anexado por `--analysis-input`; ele é
resumido como baseline de cobertura e recebe hash próprio, sem participar da
decisão de identidade ou significado.

[`validate_lexeme_decisions.py`](validate_lexeme_decisions.py) é a barreira
entre a fila e o compilador. Ele valida o ledger contra a revisão exata,
confere estrutura, proveniência, sobreposição de sentidos, alvos de merge e
referências quantitativas e produz um relatório completo. Candidatos ainda sem
decisão são permitidos e reportados; uma decisão inválida faz o processo
retornar código diferente de zero. O validador não produz microdados.

[`compile_lexemes.py`](compile_lexemes.py) repete essa validação e projeta
somente `accept_new` no JSONL numérico `LEXEMES.LAT`. O packer lê o arquivo
quando presente, acrescenta os mesmos IDs ao full e ao search e rejeita
colisão ou overflow do perfil `u16`. Sem o arquivo, o snapshot legado não muda.

[`import_ada_rewrites.py`](import_ada_rewrites.py) lê as tabelas
`words_engine-trick_tables.ad[bs]`, gera o bloco ortográfico delimitado em
`REWRITES.LAT` e oferece `--check`. O teste diferencial usa essa verificação
para impedir que a cópia tipada se afaste silenciosamente da fonte Ada.

A seção `uniques` contém as 76 análises diretas de `UNIQUES.LAT`. Cada registro
completo ocupa 12 bytes: `surface_id:u16`, `meaning_id:u16` e um payload de 64
bits com classe, paradigma, morfologia e metadados editoriais. O perfil
`search-only` omite `meaning_id` e usa 10 bytes. As formas e meanings
compartilham os pools lexicais existentes.

- lê os binários legados específicos desta cópia, `REWRITES.LAT` e
  `QUANTITIES.LAT`, além do `LEXEMES.LAT` opcional;
- inclui e valida as 76 entradas de `UNIQUES.LAT`;
- não contém o leitor do runtime WebAssembly;
- usa IDs de 16 bits;
- preserva todas as 1.785 regras de `INFLECTS.SEC`, inclusive uma duplicata;
- não contém benchmark que decida o custo de linhas versus colunas no WASM.

O perfil colunar é o menor na rede, mas a auditoria mostra que seu byte-shuffle
pode tocar mais linhas de cache numa busca pontual. O runtime deve comparar
esse perfil com uma variante híbrida por campos semânticos e com trie/hash de
radicais e terminações. O leitor nativo atual materializa grupos em vetores
ordenados e usa `std::ranges::lower_bound`; trie, hash aberto ou PMR ficam
condicionados a benchmark. `std::map` não é indicado para este banco imutável.

## Reproduzir

Na raiz do repositório:

```bash
g++-14 -std=c++23 -O2 \
  -Wall -Wextra -Wpedantic -Wconversion \
  -I../include \
  poc/compact-db/wwdb_poc_pack.cpp \
  -o /tmp/wwdb_poc_pack

python3 poc/compact-db/import_ada_rewrites.py . \
  --check REWRITES.LAT

python3 poc/compact-db/import_quantities.py QUANTITY_EVIDENCE.jsonl \
  --output QUANTITIES.LAT \
  --report /tmp/quantity-import-report.json

python3 poc/compact-db/suggest_quantity_evidence.py \
  DICTFILE.GEN /caminho/somente-leitura/superdb.sqlite \
  --source ls_dict --source gaffiot \
  --collatinus-data /caminho/collatinus/bin/data \
  --latin-german /caminho/token_latim_german.sqlite \
  --existing-evidence QUANTITY_EVIDENCE.jsonl \
  --output /tmp/quantity-candidates.jsonl \
  --report /tmp/quantity-candidates-report.json

python3 poc/compact-db/audit_lexical_expansion.py \
  DICTFILE.GEN /caminho/somente-leitura/superdb.sqlite \
  --source ls_dict --source gaffiot \
  --collatinus-data /caminho/collatinus/bin/data \
  --latin-german /caminho/token_latim_german.sqlite \
  --output /tmp/lexical-expansion.jsonl \
  --draft-output /tmp/lexeme-structural-drafts.jsonl \
  --report /tmp/lexical-expansion-report.json

# O Faria v3 permanece uma camada adicional:
#   --faria-v3 /caminho/faria-v3-quality.sqlite

# Acrescentar o léxico estendido somente numa auditoria separada:
#   --collatinus-extended

python3 poc/compact-db/prepare_lexeme_review.py \
  /tmp/lexeme-structural-drafts.jsonl \
  --output /tmp/lexeme-review.jsonl \
  --report /tmp/lexeme-review-report.json

python3 poc/compact-db/validate_lexeme_decisions.py \
  /tmp/lexeme-review.jsonl \
  LEXEME_DECISIONS.jsonl \
  --quantity-evidence QUANTITY_EVIDENCE.jsonl \
  --report /tmp/lexeme-decision-validation-report.json

python3 poc/compact-db/compile_lexemes.py \
  /tmp/lexeme-review.jsonl \
  LEXEME_DECISIONS.jsonl \
  --quantity-evidence QUANTITY_EVIDENCE.jsonl \
  --output LEXEMES.LAT \
  --report /tmp/lexeme-compilation-report.json

/tmp/wwdb_poc_pack . poc/compact-db/output/words-poc.wwdb simple
/tmp/wwdb_poc_pack . poc/compact-db/output/words-poc-dense.wwdb dense
/tmp/wwdb_poc_pack . poc/compact-db/output/words-poc-columnar.wwdb columnar
/tmp/wwdb_poc_pack . poc/compact-db/output/words-poc-search-only.wwdb search-only

gzip -9 -n -c poc/compact-db/output/words-poc.wwdb \
  > poc/compact-db/output/words-poc.wwdb.gz
gzip -9 -n -c poc/compact-db/output/words-poc-dense.wwdb \
  > poc/compact-db/output/words-poc-dense.wwdb.gz
gzip -9 -n -c poc/compact-db/output/words-poc-columnar.wwdb \
  > poc/compact-db/output/words-poc-columnar.wwdb.gz
gzip -9 -n -c poc/compact-db/output/words-poc-search-only.wwdb \
  > poc/compact-db/output/words-poc-search-only.wwdb.gz

zstd -19 -f poc/compact-db/output/words-poc.wwdb \
  -o poc/compact-db/output/words-poc.wwdb.zst
zstd -19 -f poc/compact-db/output/words-poc-dense.wwdb \
  -o poc/compact-db/output/words-poc-dense.wwdb.zst
zstd -19 -f poc/compact-db/output/words-poc-columnar.wwdb \
  -o poc/compact-db/output/words-poc-columnar.wwdb.zst
zstd -19 -f poc/compact-db/output/words-poc-search-only.wwdb \
  -o poc/compact-db/output/words-poc-search-only.wwdb.zst
```

O fonte também foi compilado com `g++-14` e `clang++-21`, que produziram os
mesmos bytes. O leitor C++23 versionado no projeto raiz confere
header/diretório/CRC, carrega os perfis `dense` e `search-only` e valida seus
IDs e índices nos
testes da engine. A API nativa também reconhece a gramática fechada de
particípio/supino + `sum`/`esse`/`fuisse`/`iri`; esse caminho é algorítmico e
não acrescenta seções ao WWDB. A recuperação opt-in `Two_Words` também é
algorítmica: usa o índice lexical existente, limites fixos e onze prefixos
legados em `constexpr`, portanto não acrescenta seções próprias ao WWDB. A
rotina auxiliar que
desfazia o perfil colunar
e reconstruía os 62.086 radicais durante a investigação não está versionada;
a leitura direta dos perfis experimentais `simple` e `columnar` completo
continua pendente.

## Resultado desta execução

| Representação do núcleo | RAW | `gzip -9 -n` | `zstd -3` | `zstd -19` |
| --- | ---: | ---: | ---: | ---: |
| original, soma de 4 arquivos | 10.704.174 | 1.608.344 | 1.699.489 | 1.216.351 |
| PoC simples 1.8 | 2.977.659 | 1.339.354 | 1.415.050 | 1.160.544 |
| PoC denso por linhas 1.8 | 2.731.900 | 1.251.212 | 1.295.059 | 1.091.956 |
| PoC denso por colunas 1.8 | 2.731.900 | 1.042.735 | 1.058.053 | 880.996 |
| PoC busca sem definições 1.8 | 1.200.341 | 416.259 | 442.742 | 359.459 |

O original é comprimido como quatro arquivos independentes; cada perfil PoC é
um único WWDB. Todos os registros ativos de `ADDONS.LAT` e `UNIQUES.LAT`, além
das 170 regras de `REWRITES.LAT` e dos microdados de `QUANTITIES.LAT`, estão
incluídos.

Os arquivos de `output/` são regeneráveis e não ficam no Git. Em releases, os
hashes físicos de full, search, Brotli e gzip são publicados em
`manifest.json` e no asset `words-assets-VERSAO.sha256`.
Medições adicionais de Brotli/XZ, alternativas para IDs e o efeito da ordem
física estão em
[`docs/compressao-ids-e-ordem.md`](../../docs/compressao-ids-e-ordem.md).
