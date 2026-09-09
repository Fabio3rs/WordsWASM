# Auditoria morfológica diferencial do Whitaker's Words

Data da sessão: 2026-09-08

## Método e autoridades

1. O código Ada em `whitakers-words/src` define o comportamento legado.
2. A execução do CLI só vale como evidência quando o diretório de dados e os
   arquivos `WORD.MOD`/`WORD.MDV` são controlados pelo teste.
3. A gramática em `.study/gramatica-latina-luna-completa` serve para avaliar a
   correção linguística; uma divergência não é silenciosamente convertida em
   regra normativa.
4. A documentação gh-pages descreve intenção e operação, mas não prevalece
   sobre código executado e defaults compilados.

Revisão local inicialmente observada: `45c5a0df3a12807397442aca3d99e3732f822471`.

## Risco de configuração

- A árvore de dados contém `whitakers-words/WORD.MDV` e não contém
  `WORD.MOD`.
- `bin/words` chama `Initialize_Engine` e lê os dois arquivos pelo diretório
  indicado em `WHITAKERS_WORDS_DATADIR`.
- `bin/words_json` chama `Initialize_Canonical_Engine`, fixa os modos em
  memória e não lê esses arquivos.
- O help de `DO_MEDIEVAL_TRICKS` declara default negativo, enquanto
  `Default_Mdev_Array` o inicializa como verdadeiro. Portanto nem o texto de
  ajuda nem a ausência parcial de uma configuração são suficientes para
  caracterizar um perfil.
- Decisão: o oráculo configurado usará um diretório temporário próprio e
  fixtures completas. O `WORD.MDV` da árvore não será movido nem alterado.

Hashes iniciais:

- `WORD.MDV`: `sha256:256c9775d007bcaa7ed98b67bb0c2a55d795cfbf5b3470b82113978de3abbe50`
- `REWRITES.LAT`: `sha256:697f95ff41612319a2852d323e167a3315a1052f7b90453ee7bfa7362652a892`

## Findings morfológicos iniciais

### O que o WW torna configurável e o que vale transportar

Os 16 `Mode_Type` e 28 `Mdev_Type` misturam domínios diferentes. A divisão
arquitetural útil é:

| família do original | exemplos | decisão para a engine |
|---|---|---|
| geração/análise morfológica | `DO_COMPOUNDS`, `DO_FIXES`, `USE_PREFIXES`, `USE_SUFFIXES`, `USE_TACKONS`, `DO_SYNCOPE`, `DO_TRICKS`, `DO_MEDIEVAL_TRICKS`, `DO_TWO_WORDS` | transportar como opções tipadas; implementado nesta sessão sem depender de arquivo |
| política de retenção | `TRIM_OUTPUT`, `OMIT_ARCHAIC`, `OMIT_MEDIEVAL`, `OMIT_UNCOMMON`, `FOR_WORD_LIST_CHECK` | `TRIM_OUTPUT` virou avaliação explicável; os demais devem ser filtros/ranking posteriores, não apagamento durante geração |
| modos de pesquisa | `DO_ONLY_FIXES`, `DO_FIXES_ANYWAY` | potencialmente úteis para auditoria de cobertura; não pertencem ao perfil normal e `DO_FIXES_ANYWAY` é documentado pelo próprio WW como não implementado |
| projeção textual | `DO_DICTIONARY_FORMS`, `SHOW_AGE`, `SHOW_FREQUENCY`, `DO_EXAMPLES`, `DO_ONLY_MEANINGS`, `NO_MEANINGS`, `DO_I_FOR_J`, `DO_U_FOR_V`, `MINIMIZE_OUTPUT` | não são mecanismos morfológicos; idade/frequência já são dados tipados e o restante pertence a apresentadores |
| operação/arquivos | output, unknowns, estatísticas, pausa, contexto, Pearse codes | não copiar para o core |
| manutenção mutável | update de dicionário/significados | manter fora da engine imutável |

O WW agrupa tickons, tackons e packons sob `USE_TACKONS`; a engine já os
distingue no IR e agora permite desligá-los separadamente, sem perder a opção
coletiva `productive_derivations`. `DO_TRICKS` foi modelado como política
ortográfica e `DO_MEDIEVAL_TRICKS` como o limite clássico/medieval. Isso
preserva a intenção configurável sem copiar o acoplamento histórico da UI.

`SHOW_AGE` e `SHOW_FREQUENCY` são um bom exemplo de opção que não precisa
existir no analisador: a informação deve sempre sobreviver no resultado e o
cliente decide se a mostra. Analogamente, `OMIT_*` não deve destruir candidatos
antes de um ranker acadêmico poder examiná-los.

### `Trim_Output` contém validação gramatical

`Words_Engine.List_Sweep.Allowed_Stem` só é aplicado dentro de
`Trim_Output`. O nome de apresentação esconde cinco famílias de restrições:

- imperativo presente curto, com ending vazio, permitido somente para stems
  terminados em `dic`, `duc`, `fac` ou `fer`;
- imperativo presente somente na segunda pessoa; imperativo futuro somente na
  segunda ou terceira pessoa;
- verbos marcados `Impers` somente na terceira pessoa;
- verbos `Dep` rejeitam formas ativas finitas/infinitivas, exceto infinitivo
  futuro ativo;
- verbos `Semidep` rejeitam a passiva do sistema do presente e a ativa do
  sistema do perfeito.

O C++ já antecipa a restrição de depoentes em `deponent_verb_matches`, mas não
implementa as outras famílias. Essa antecipação perde os candidatos antes de
ser possível explicar por que foram rejeitados.

### Confirmação hermética dos defaults compilados

Uma execução em `/tmp`, com links para os dados e sem `WORD.MOD`/`WORD.MDV`,
produziu:

| consulta | resultado relevante |
|---|---|
| `reg` | `UNKNOWN` |
| `liceo` | somente o lexema intransitivo `liceo` |
| `audetur` | `UNKNOWN` |
| `audemur` | `UNKNOWN` |

Isso confirma o caminho do código, mas não transforma todos os descartes em
juízo gramatical.

### Semidepoentes: divergência linguística relevante

A gramática local, página/unidade `s0150-r`, §§ 311–313, define semidepoente
como verbo de forma ativa no sistema do presente e forma passiva com sentido
ativo no perfeito e derivados: `audeo`, mas `ausus sum`, não `audevi`.

- Rejeitar a ativa do perfeito concorda com o paradigma ordinário do § 311,
  mas não é uma proibição histórica universal: `ausim` e a leitura verbal de
  `ausi` são exceções documentadas.
- Rejeitar a passiva do presente é discutível: § 313 diz que a passiva dos
  tempos não depoentes se processa regularmente.

Decisão: a API chamará esse resultado de compatibilidade com o trim de
Whitaker, não de validade universal. O padrão conservará e anotará candidatos;
um modo explícito reproduzirá o filtro legado.

Uma segunda conferência fornecida durante a sessão reforça essa decisão:

- Bennett e Allen & Greenough definem o sistema do presente como ativo e o
  sistema do perfeito como passivo na forma, sem sentido passivo, citando
  `audeo`, `gaudeo`, `soleo` e `fido`.
- O material do UK National Archives enumera também os compostos `confido` e
  `diffido`; “quatro” versus “seis” é uma contagem lexical, não outra classe
  morfológica.
- Lewis & Short registra passivas atestadas de `audeo`, incluindo
  `audebantur` e `auderi`. Logo, a rejeição de `audetur`/`audemur` pelo Ada é
  perda de análise no perfil trim, e não evidência de forma impossível.
- Possíveis erros OCR em exemplos da gramática (`eum` lido `cum`, casos de
  `aliquis` e o perfeito `fidi`) não serão usados como fixtures da engine.

Fontes externas indicadas pelo usuário para a auditoria: Bennett/Project
Gutenberg, Allen & Greenough/Dickinson College Commentaries, UK National
Archives e Lewis & Short/Scaife ATLAS. O HTML inglês local
`.study/pg18251-images.html` confirma que depoentes possuem o infinitivo
futuro ativo (§ 338a) e registra `soleo` como semi-depoente.

### Imperativos e impessoais

- `s0127-l`, § 273, confirma imperativo presente curto para `dico`, `duco` e
  `facio`; `fer`/`ferte` também aparecem no próprio Napoleão, § 316. Portanto,
  os quatro casos do Ada não constituem divergência entre as fontes.
- O mesmo parágrafo confirma presente na segunda pessoa e futuro na segunda ou
  terceira pessoa.
- `s0164-l`, § 346, descreve construções impessoais sem sujeito e formas na
  terceira pessoa, apoiando a restrição estrutural do Ada.

## Reescritas ortográficas

- O WWDB preserva `RewriteRule.medieval`, mas o scheduler C++ não consulta o
  campo.
- O IR guarda somente IDs e a forma lexical final; perde posição, fragmento
  observado e substituição concreta.
- Testemunhos atuais: `pretor` usa regra clássica; `teologia` usa substituição
  medieval `t -> th`; `literatura` usa duplicação consonantal medieval.
- Decisão: manter os passos ordenados como fonte única da proveniência e
  acrescentar época, operação, escopo e aplicação concreta. Não criar uma
  segunda lista paralela de “grafias alternativas”.

## Log TDD

Esta seção será ampliada em cada ciclo com o formato:

`feature | perfil do oráculo | consulta | red | implementação | green | clang-tidy | refactor | green final`

### Ciclos executados

| feature | perfil do oráculo | consultas | red observado | implementação | green |
|---|---|---|---|---|---|
| depoentes sem perda | Ada sem trim / C++ IR | `res` | o C++ descartava `reor` antes do IR | candidato retido com `deponent-active-form`; JSON v1 conserva a projeção antiga | unitário e diferenciais existentes |
| trim morfológico | `WORD.MOD` completo, `TRIM_OUTPUT=Y/N` | `reg`, `dic`, `liceo`, `audetur`, `audemur`, `ausi` | C++ não representava quatro famílias de `Allowed_Stem` | avaliação central e modos `annotate`/`filter` | oráculo configurado coincide por assinatura morfológica |
| semidepoente com uso passivo relacionado | trim ligado e desligado | `audetur`, `audemur` | Ada devolvia `UNKNOWN` com trim | candidato preservado; três notices curados para `audeo` | unitário + diferencial hermético |
| época ortográfica | `DO_MEDIEVAL_TRICKS=Y/N` | `teologia`, `pretor` | scheduler ignorava `RewriteRule.medieval` | modos disabled/classical/classical+medieval | unitário + diferencial hermético |
| aplicação da reescrita | C++ v2 | `teologia` | IR guardava somente IDs e forma final | posição, trecho observado, substituição e metadados da regra | unitário JSON v2 |
| mecanismos produtivos | flags completas Ada | `archipuella`, `anaticulus`, `puellaque`, `quispiam`, `ecquidam`, `amasti` | opções do WW não tinham equivalentes independentes no core | controles tipados para fixes, prefix/suffix/tickon/tackon/packon/syncope | unitário + diferencial hermético |
| notices lexicais data-driven | Ada trim + evidência curada | `audetur` | teste não compilava sem lookup e depois retornava zero notices com WWDB 1.8 | WWDB 1.9, seção binária esparsa e remoção do `if` por stems no engine | unitário full/search + integração que repacka ambos os perfis |
| exceções semidepoentes adicionais | Ada/C++ por assinatura + evidência curada | `ausim`, `ausi`, `diffideretur`, `reverti` | o índice retornava zero notices para perfeito ativo de `audeo` e passiva de `diffido` | cinco pares lexema/trigger; lookup comum para ambos os motivos semidepoentes | unitário + integração full/search |

O teste `configured_oracle_test.py` cria um diretório temporário, liga somente
os dados necessários e escreve fixtures **completas** de `WORD.MOD` e
`WORD.MDV`. Se um `WORD.MDV` local opcional existe, ele verifica que seu hash
permanece inalterado; em checkout limpo, verifica que o teste não cria esse
arquivo ignorado. Assim, a configuração usada para medir o original nunca
vaza da pasta temporária nem modifica a árvore que está sendo investigada.

### Caso defensivo sem testemunho no dataset

Há 93 regras imperativas em `INFLECTS.LAT`; todas já respeitam presente/segunda
pessoa ou futuro/segunda-terceira pessoa. Portanto a ramificação
`invalid-imperative-person` de `Allowed_Stem` não possui hoje um candidato real
que sirva como diferencial positivo. Um teste de invariância do WWDB registra
essa ausência, enquanto o enum permanece para reproduzir fielmente a política
caso dados futuros introduzam tal candidato.

### Separação de evidências

A sessão consolidou quatro conceitos que não devem ser colapsados pelo ranking:

- candidato gerado pelas regras/dados do Whitaker;
- compatibilidade com o trim histórico;
- evidência linguística curada (por exemplo, usos passivos relacionados de
  `audeo`);
- plausibilidade contextual, ainda pertencente a uma camada posterior.

O core agora materializa os três primeiros sem alegar que “gerado” equivale a
“atestado”. A plausibilidade sintática não foi inventada nesta etapa.

### Auditoria da suposta atestação do Whitaker

A inspeção do fonte Ada descartou três falsos atalhos para uma concordância:

- `Source_Type` identifica a principal obra de referência usada para derivar
  o **verbete**; o comentário do próprio tipo diz que WORDS não é uma cópia da
  fonte e que o código é um ponto principal de referência/checagem;
- `Frequency_Type` é granularidade lexical ou flexional. Nos verbetes, `E/F`
  mencionam duas ou três citações ou uma única citação em OLD/Lewis & Short,
  mas não guardam forma, passagem nem análise morfológica exata;
- `DO_EXAMPLES` chama `Put_Example_Line`, que monta paráfrases inglesas a
  partir de pessoa, número, tempo, modo e voz. Não recupera exemplos de corpus.

Não existe, nos dados consultados, um índice `forma exata → passagem → análise`.
Logo, ausência no output de WORDS e ausência de uma linha gerada não podem
virar `unattested`. O estado futuro correto é `not-checked` até que uma fonte
de concordância com cobertura explícita seja compilada.

### Local arquitetural dos notices e da atestação futura

O branch lexical `is_audeo_lexeme` era útil como POC, mas estava na fronteira
errada: quais lexemas recebem uma exceção curada é dado editorial. O WWDB 1.9
agora compila `MORPHOLOGICAL_NOTICES.LAT` em uma seção esparsa ordenada. Cada
registro ocupa três bytes:

```text
lexeme_id:u16 | trigger:3 | notice_flags:3 | reserved:2
```

Há cinco registros: as entradas gerais 5550 e 5551 de `audeo`, cada uma para
os dois triggers semidepoentes, e a entrada 17683 de `diffido` para a passiva
do sistema do presente. São 15 bytes de payload. O diretório da nova seção
acrescenta 32 bytes; o delta total é 47 bytes em full e search. O full usa
perfil global `dense = 2`; o search
usa `search-only = 4` e mantém lexemas, referências e flexões em colunas. A
seção esparsa de notices usa `section_flags = row-major` nos dois perfis,
assim como outras tabelas pequenas/esparsas: ela é consultada depois do hit e
não pertence ao caminho de varredura quente.

O C++ em memória não expõe bitfields dependentes da ABI: usa enums fortes e
`MorphologicalNoticeSet`; somente o wire little-endian usa shifts e máscaras
nomeados em `wwdb_schema.hpp`. Strings de nomes, fontes e URLs não entram no
WWDB/WASM. Os identificadores textuais do ledger são traduzidos por tabelas
`constexpr std::string_view` no packer nativo.
O exportador inclui o ledger no cálculo do `datasetId`; uma mudança de notices
não pode reutilizar silenciosamente a identidade de um dataset anterior.
O demo do Pages recebe apenas os códigos tipados da binding e os converte em
rótulos e explicações em inglês, português e latim dentro de `web/app.js`.
Assim a interface acadêmica mostra as ressalvas sem levar strings de UI ao
WWDB ou ao módulo WASM.

Atestação exata continua sendo outra camada. O desenho aprovado é:
manifesto/corpus rico fora do runtime → compilador nativo determinístico →
seção WWDB compacta → lookup tipado pós-morfologia. O ranker contextual fica
posterior à engine de palavra e recebe todas as alternativas. Não será criada
uma seção vazia nem um flag enganoso antes de existirem formas exatas revisadas.

### Cruzamento de `ACHADOS_RECENTES.md`

O anexo é útil como fila crítica, não como manifesto importável. Ele contém
duas revisões concatenadas e contraditórias quanto a `audetur`: a primeira diz
ter conferido a forma exata no impresso de 1967; a segunda diz não ter obtido
essa atestação. O endereço e o locus são candidatos para a futura camada de
corpus, mas não foram reduzidos a bitflag neste corte. O notice atual continua
afirmando somente uso passivo relacionado.

As correções verificáveis no material local foram incorporadas: Napoleão
também traz `fer` no § 316; Whitaker e o C++ geram o perfeito subjuntivo ativo
`ausim`, o perfeito indicativo ativo `ausi`, a passiva `diffideretur` e as
leituras distintas de `reverti`. `Ausim`/`ausi` agora qualificam o trigger do
perfeito ativo de `audeo`; `diffideretur` qualifica a família passiva de
`diffido`. A qualificação continua no nível `(lexema, trigger)`, não no de
atestação da superfície exata.

O anexo também apontou corretamente uma perda separada em `DO_COMPOUNDS`.
No corte atual, `amata` isolada produz 15 análises, enquanto `amata est`
conserva somente a análise nominativa participante e a construção composta,
reproduzindo o descarte do legado. Já `captum esse` conserva as quatro leituras
participiais porque o ramo infinitivo não impõe nominativo. Corrigir isso
exige uma decisão explícita de contrato multiword: preservar todas as análises
do primeiro token e acrescentar a hipótese composta altera as contagens do
diferencial canônico. O caso ficou registrado para um ciclo TDD próprio; não
foi misturado ao microíndice de evidência nem levado a
`parsers_investigation`.

## Validação e refactor da sessão

- O primeiro estado verde teve 107/107 testes nativos, incluindo o oráculo
  configurado, o diferencial canônico e o corpus da Eneida.
- `clang-tidy` foi executado pelo script oficial antes do refactor. Nos trechos
  novos apontou dois booleanos de regras morfológicas, um ternário de categoria
  ortográfica e inicializadores do fixture; os demais achados reportados eram
  dívida preexistente em lexer, CLI, testes e parsers experimentais.
- O refactor extraiu predicados nomeados para imperativo/depoente, uma função de
  categoria da reescrita e inicializadores designados. A segunda passagem de
  `clang-tidy` não reportou warnings em `src/engine.cpp` nem `src/json.cpp`, nem
  nos fixtures adicionados nesta sessão.
- O wrapper browser passou seu teste Node, incluindo cópia tipada e liberação
  dos handles de assessment/proveniência.
- Os schemas browser v4 validaram 24 documentos reais produzidos pelo artefato
  WASM compilado no host: 9 analysis, 9 search, 3 analysisLine e 3 searchLine.
  JSON continua ausente do grafo WASM.
- O build WASM completo foi executado novamente pelo usuário no host depois
  das alterações do core (`[12/12]`). O exportador montou o bundle full do
  Pages com WWDB 1.9 e `datasetId`
  `sha256:bb89d1c6a7305ecfaf828747018598e7cd97c321570a80da38249c433d131803`;
  o smoke real passou tanto no full-only quanto no par full/search.
- O smoke estava preso ao schema browser v3 e ainda esperava a exclusão de
  `reor`; foi atualizado para o contrato v4 lossless e agora verifica também
  os notices tipados de `audetur`, `diffideretur` e `ausim`. O demo renderiza
  os mesmos códigos como painéis explicativos localizados.
- No corte WWDB 1.9, o teste vermelho primeiro falhou por inexistência do
  lookup e, depois de compilar, por o WWDB 1.8 não conter notices. O verde
  confirmou `audetur` em full/search e uma integração recompilou ambos os
  perfis a partir do ledger.
- O full 1.9 gerado tem 2.731.947 bytes (`profile=2`, SHA-256
  `57f89dca6a7ccd5fc8546343b78fa78322c173c1622ac61da883102329d8c892`);
  o search tem 1.200.388 bytes (`profile=4`, SHA-256
  `fdffcb08213ffcc898255dcca6980c614879e5c1e1879af51845d087cfc3b602`).
  Os headers declaram respectivamente 24 e 19 seções; a seção 24 tem cinco
  registros, stride 3 e flag row-major nos dois.
- A regressão final passou 110/110 testes, incluindo oráculo configurado,
  diferencial, Eneida, wrapper Node e importação que repacka full/search.
- O `clang-tidy` oficial foi executado antes do refactor. Depois dele, uma
  passagem focada ficou limpa em `src/database.cpp`, `src/engine.cpp` e nos
  trechos novos do teste e do packer. Warnings restantes reportados pelo passe
  amplo são dívida anterior fora deste corte; `parsers_investigation` não foi
  editado.
- `WORD.MDV` permaneceu com SHA-256
  `256c9775d007bcaa7ed98b67bb0c2a55d795cfbf5b3470b82113978de3abbe50`.
