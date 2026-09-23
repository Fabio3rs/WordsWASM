# Depoentes com paradigmas mistos: cadastro e opções de filtragem

Levantamento de 2026-09-22. Esta nota registra o estado da `DICTLINE.GEN`, da
imagem `web/engine/words-full.wwdb` e da engine nesta data. Os exemplos de
saída foram consultados com `words_cli` em `analysis-v3` ou `search-v3`.
`dist/words-web/words-full.wwdb` pode representar outra revisão dos dados.

## Distinção necessária

`deponent-active-form` é hoje um **motivo de incompatibilidade com o filtro
histórico de Whitaker**, não uma prova de que a forma seja impossível. A engine
o aplica a análises verbais ativas finitas ou infinitivas de uma entrada `DEP`,
exceto o infinitivo futuro ativo. A condição usa apenas `VerbKind::deponent` e
a morfologia da análise (`src/engine.cpp`, `is_disallowed_deponent_active_form`
e `assess_morphology`). Não consulta formas atestadas, pares de entradas nem
paradigmas mistos.

Allen & Greenough, §§ 190–191, reconhecem verbos com variantes ativa e
depoente, como `mereō`/`mereor`, e o perfeito ativo histórico de `revertor`.
O Surrey Morphology Group descreve o tipo `revertor`: presente deponente e
perfectum ativo, com `dēvertor`, `pacīscor` e `assentior` entre os exemplos.
Isso impede tratar toda análise ativa associada a `DEP` como apócrifa.

Fontes gramaticais:

- [Allen & Greenough, §§ 190–192](https://grammars.alpheios.net/allen-greenough/conj20.htm)
- [Surrey Morphology Group, Latin: Revertor-type](https://www.smg.surrey.ac.uk/deponency/Examples/Latin_2.htm)

## O que está cadastrado

Os números abaixo são as linhas/entradas de `whitakers-words/DICTLINE.GEN`.
Entradas adjacentes são **lexemas independentes no banco**; a proximidade não
codifica um vínculo entre variantes do mesmo paradigma. `X` é o tipo genérico
do Whitaker e aparece como `verbKind: null` no JSON, não como um novo padrão de
voz.

| Caso | Entradas | Resultado observado na WWDB atual |
|---|---|---|
| `revertor` | 33666 `reverto` (`X`, radical de perfeito `revert-`); 33667 `revertor` (`DEP`, sem radical de perfeito) | `revertī` recebe uma leitura de perfeito indicativo ativo da 33666; também há leituras de infinitivo presente passivo. `reverteram` recebe a leitura ativa da 33666. Não há leitura de perfeito ativo atribuída à 33667. |
| `dēvertor` | 17303 `deverto` (`X`, radical de perfeito `devert-`); há ainda variantes `devort-` em 17349–17350 | `devertor` e `devertī` são gerados pela entrada `X`. Não há entrada `DEP` `devertor` nem classificação específica do padrão misto. |
| `pacīscor` | 29095 `pacisc-` (`X`) e 29096 `pacisc-` (`DEP`), ambos sem radical de perfeito; 29240 `pangō` (`TRANS`) tem radical `pepig-` | `paciscor` aparece nas duas primeiras entradas. `pepigī` aparece sob `pangō`, sem vínculo explícito com `pacīscor`. |
| `adsentior` / `assentior` | 1496–1497 e 5137–5138: em cada grafia há uma entrada `INTRANS` e uma `DEP` | `assēnsī` recebe leitura de perfeito ativo da entrada `assentio` (5137), sem vínculo explícito com a 5138. |
| `mereō` / `mereor` | 26791 `mer-` (`X`, radical `meru-`) e 26792 `mer-` (`DEP`, sem radical de perfeito) | As duas variantes estão representadas. `mereō` recebe uma leitura compatível da 26791 e outra da 26792 com `deponent-active-form`. A primeira não deve ser eliminada por causa da segunda. |

Assim, **há duas entradas para vários casos, mas não duas variações
formalmente ligadas**. Também não se deve supor que a entrada `X` restrinja a
voz por tempo: por ser genérica, ela pode gerar outras leituras além das
formas históricas que motivaram sua inclusão. Em `pacīscor`/`pepigī`, a forma
ativa ainda está sob outro verbo. O cadastro precisa ser avaliado por análise
e por relação lexical, não apenas por superfície igual.

Para `rēs → reor`, a leitura verbal ativa recebe
`deponent-active-form`; a leitura nominal de `rēs` é independente. O paradigma
normal de `reor` é passivo na forma, e não há exceção cadastrada que licencie
essa análise de `rēs`. Isso sustenta ocultá-la na apresentação padrão, sem
converter o motivo histórico em juízo universal sobre todos os depoentes.

## Espaço técnico e limites do esquema

- `VerbKind` usa um campo de quatro bits na WWDB. Os valores atuais vão de 0
  a 11, deixando **12–15** livres no campo. Acrescentar valores exige mudanças
  coordenadas no importador, no packer, na validação do banco, na semântica da
  engine e nas projeções públicas. Espaço numérico não é, por si, uma decisão
  de representar padrão de voz nesse enum.
- Cada registro de `MORPHOLOGICAL_NOTICES.LAT` compilado na WWDB usa três bits
  para o motivo (seis valores em uso), três para avisos (todos em uso) e dois
  reservados. Os bits reservados são rejeitados quando não são zero; não são
  flags livres na API atual. O ledger existente só cobre exceções revisadas
  de `audeo` e `diffido`, não os paradigmas mistos desta nota.
- `AnalysisOptions` não possui opção de filtragem por motivo. O CLI apenas
  expõe a política descritiva `whitakerTrim: "annotate"`. Uma opção nova
  mudaria o conjunto de resultados padrão e exigiria revisão dos contratos,
  testes e wrappers nativo/WebAssembly.

Referências de implementação: `include/words/model.hpp` (`VerbKind`,
`WhitakerTrimReason`, `AnalysisOptions`),
`include/words/detail/wwdb_schema.hpp` (campos da WWDB), `src/database.cpp`
(validação), `src/engine.cpp` (avaliação) e
`whitakers-words/MORPHOLOGICAL_NOTICES.LAT` (ledger editorial).

## Sugestões para evolução

1. **Separar o significado das camadas.** Preservar `whitakerTrim` como
   descrição fiel do corte de Whitaker. Um eventual juízo novo, como
   “incompatível com o paradigma lexical revisado”, deve ter nome e
   proveniência próprios. Reinterpretar `deponent-active-form` como esse juízo
   mudaria o significado atual do campo.
2. **Cadastrar vínculos e escopo antes de filtrar por padrão.** Revisar cada
   família de entradas e registrar quais lexemas compartilham um paradigma,
   quais tempos admitem formas ativas e qual fonte sustenta a afirmação.
   `revertor` e `mereor` não têm o mesmo padrão; `pacīscor → pepigī` ainda
   requer relação com a entrada de `pangō`. Um simples enum de `VerbKind`
   sozinho não expressa essas relações.
3. **Aplicar o filtro por análise.** Depois da avaliação morfológica, ocultar
   apenas análises classificadas como incompatíveis pela política nova. Manter
   as leituras alternativas da mesma superfície; se todas forem removidas,
   recalcular estado e diagnósticos da consulta. Uma opção como
   `filter_deponent_active_forms = true`, com
   `--include-deponent-active-forms` no CLI, só deve ser ativada por padrão
   depois de especificar a semântica e auditar os casos acima.
4. **Verificar a alteração com casos discriminantes.** Cobrir `rēs`/`reor`,
   `mereō`/`mereor`, `revertī`/`reverteram`, `devertī`, `pepigī`, `assēnsī`,
   o infinitivo futuro ativo legítimo dos depoentes e as exceções
   semideponentes (`audetur`, `ausī`). A verificação deve comparar tanto a
   análise lexical quanto seu motivo, não só a presença da superfície.

Nenhuma dessas sugestões está implementada nesta nota.

## Custo de armazenamento medido

Simulação de 2026-09-22, GCC/GNAT 13.3.0, Linux x86-64. **É possível
cadastrar flags de padrão de voz com acréscimo de zero bytes tanto na WWDB
quanto nos registros em RAM e no DICTFILE legado**, escolhendo a posição
adequada dos campos. Isso não inclui novas entradas, radicais, relações
lexicais nem textos de proveniência. Não foi implementada a semântica nova.

Reprodução, a partir da raiz do projeto:

```sh
python3 scripts/measure-deponent-storage.py --ada
```

O script verifica CRC, diretórios e registros das imagens, simula os nove
bits disponíveis em **todos os 7.792 verbos**, recompõe o CRC em memória e
confirma que o tamanho não muda. Compila variantes C++ de `LexemeRecord` e
consulta a representação GNAT das especificações Ada em diretórios
temporários. Não modifica as imagens nem o código de produção. A imagem
simulada com flags seria rejeitada pelo loader atual; a simulação comprova
capacidade física, não suporte funcional ou compatibilidade.

| Imagem medida | Tamanho | Lexemas | Bytes por lexema | Avisos compilados |
|---|---:|---:|---:|---:|
| `web/engine/words-full.wwdb`, 1.10, dense | 2.731.964 B | 39.339 | 16 | 5 registros / 15 B |
| `web/engine/words-search.wwdb`, 1.10, columnar/search | 1.200.406 B | 39.339 | 14 | 5 registros / 15 B |

SHA-256, respectivamente:
`c113075077b91f7da4ec87e5cb35e44d8fb838cd90b5e3730dc46e1679f2c27f`
e `4bf9ae761b5cd83e1d9aff9b9a5ea0e458daa64e5ff4287d10a749c8e9362088`.

### WWDB: bits reservados, sem padding de alinhamento

Os seis bytes de metadados lexicais são serializados explicitamente, não
por `sizeof` de uma struct. Sua composição é:

| Bits (base zero) | Conteúdo | Uso em verbos |
|---|---|---|
| 0–3 | Classe gramatical | 4 bits |
| 4–11 | Paradigma | 8 bits |
| 12–33 | Metadados editoriais | 22 bits |
| 34–46 | Payload da classe | `VerbKind` usa apenas 34–37 |
| 47 | Reservado global | Zero obrigatório |

Portanto, **38–46 são nove bits reservados dentro do payload verbal**.
Podem receber até nove flags independentes, ou enum(s) e flags cuja soma
caiba nesses nove bits. Não podem ser reaproveitados indiscriminadamente nas
outras classes: numerais, por exemplo, usam o payload inteiro. O bit 47 é
uma reserva adicional global e não precisa ser consumido por esta proposta.

| Alternativa | Acréscimo na WWDB | Limite/condição |
|---|---:|---|
| Acrescentar valores 12–15 a `VerbKind` | **0 B** | Quatro categorias novas, não quatro flags combináveis |
| Ampliar `VerbKind` para cinco bits | **0 B** | Mudar máscara/validação; ocupa um dos nove bits reservados |
| Manter `VerbKind` e criar `VoicePattern` ou flags separadas | **0 B** | Até nove bits no payload verbal |
| Acrescentar até duas flags a `MorphologicalNotice` | **0 B por registro existente** | Ampliar máscara de avisos de 3 para até 5 bits; consome os dois bits reservados |
| Acrescentar motivos 6 e 7 a `WhitakerTrimReason` | **0 B por registro existente** | O campo de motivo continua com três bits |
| Novo par `(lexema, motivo)` na seção de avisos existente | **3 B por par** | Avisos do mesmo par são combinados no mesmo registro |
| Novo byte físico em cada registro lexical | **39.339 B** | Cresce em todos os lexemas, inclusive não verbos |
| Nova coluna compactada de um/dois bits por lexema | **4.918 / 9.835 B** | Mais 32 B se criada como seção própria; exige outro esquema |

As duas expansões do ledger têm limites conjuntos: três bits de motivo +
cinco de avisos já esgotam o byte. Um nono motivo requer quatro bits; com
cinco bits de avisos, isso exige mais um byte por registro, ou outro layout.
Um sexto aviso independente com três bits de motivo também ultrapassa o
byte. Não confundir valores de enum com bits de um conjunto de flags.

`make_image` concatena cabeçalho de 40 B, diretório de 32 B por seção e os
payloads, sem arredondamento de offsets. A inspeção confirmou **zero bytes de
padding entre seções** nas duas imagens. Acrescentar uma seção custa
`32 + tamanho_do_payload` bytes; ampliar uma seção existente custa apenas o
payload adicional. O formato columnar transpõe bytes, sem acrescentar padding.

### RAM: a posição do campo determina o custo

`VerbKind`, `WhitakerTrimReason` e `MorphologicalNoticeSet` ocupam um byte
cada. Acrescentar enumeradores dentro da capacidade de `uint8_t` não altera
o tamanho desses tipos. Os limites menores da WWDB são independentes disso.

| Simulação C++ | `sizeof(LexemeRecord)` | Acréscimo para 39.339 registros |
|---|---:|---:|
| Atual | 56 B | — |
| Novo `uint8_t` no fim ou logo após `verb_kind` | 60 B | 157.356 B |
| Novo `uint16_t` no fim ou logo após `verb_kind` | 60 B | 157.356 B |
| Novo `uint8_t` após `dictionary` | 56 B | **0 B** |
| Novo `uint16_t` após `dictionary` | 56 B | **0 B** |

Há três bytes de padding após `dictionary` (offset 20) e antes de
`dictionary_entry` (offset 24). Um byte de flags cabe no offset 21;
um campo de 16 bits cabe no offset 22 e comporta as nove flags possíveis
na WWDB. No fim não há folga: `source` ocupa o offset 55. O crescimento
calculado é do vetor de registros, sem overhead do alocador/capacidade extra.
É uma medição da ABI nativa indicada, não uma medição de WebAssembly;
`sizeof`/offsets devem ser confirmados ao implementar em cada alvo.

O equivalente C++ do registro privado de aviso (`uint32_t key` e
`MorphologicalNoticeSet`) ocupa 8 B por alinhamento, embora use 3 B na WWDB.
Logo, cada novo par no ledger acrescenta 8 B de elementos em RAM, além dos
3 B na imagem mantida pelo loader. Flags adicionais no mesmo par não
aumentam esse registro enquanto o conjunto continuar em um byte.

### Whitaker/Ada e arquivos legados

O GNAT foi consultado sobre cópias das **especificações reais** em
`whitakers-words/src/latin_utils`, com `-gnatc -gnatR3`:

| Variante | `Verb_Entry` (Object_Size) | `Part_Entry` | `Dictionary_Entry` |
|---|---:|---:|---:|
| Atual, 12 valores de `Verb_Kind_Type` | 12 B | 20 B | 180 B |
| 16 valores | 12 B | 20 B | 180 B |
| 17 valores | 12 B | 20 B | 180 B |
| Campo de flags de 1 ou 3 bytes após `Kind` | 12 B | 20 B | 180 B |
| Campo de flags de 4 bytes após `Kind` | 16 B | 20 B | 180 B |

`Kind` fica no offset 8; os offsets 9–11 são padding de `Verb_Entry`.
Mesmo quatro bytes adicionais cabem no registro externo porque a variante
numeral já determina um payload de 16 B. A passagem a 17 enumeradores rompe
o limite de quatro bits da WWDB, mas não aumenta esses registros Ada.
O espelho C em `whitakers-words/src/legacy_data_layout.h` também confirma
`DICTFILE` de 180 B e `STEMFILE` de 56 B. Mantendo o tamanho de `Part_Entry`,
não há crescimento esperado do `STEMFILE` por essas flags.

Padding legado pode conter lixo: campos novos precisam ser inicializados
ao regenerar os dados; não é válido interpretar os bytes de arquivos antigos
como flags. As simulações Ada verificam representação das especificações,
não compilação completa dos importadores, agregados e rotinas de I/O após
uma alteração. `DICTLINE.GEN` é texto: novos tokens/colunas têm custo textual
próprio e exigem ajustar seu parser; esse custo não é padding binário.

### O que as flags não pagam nem resolvem

Um enum separado de padrão de voz, ou flags com escopo presente/perfectum e
estado de revisão, cabe nos nove bits sem sobrecarregar a classificação
`TRANS`/`INTRANS`/`DEP`. Essa é a opção de layout mais flexível dentre as
medidas. Os valores devem distinguir ausência de revisão de proibição.
O ledger atual usa um motivo ativo deponente sem separação por tempo;
um aviso ligado a esse motivo sozinho não licencia apenas o perfectum.

Ligar dois lexemas exige dados adicionais. Como orçamento ilustrativo,
uma seção esparsa nova com `origem:u16 + destino:u16 + relação/escopo:u8`
custaria **32 + 5 × R bytes**, para R relações, com até 65.536 IDs possíveis
e relação/escopo cabendo em oito bits. Cinco relações custariam 57 B.
Isso é uma proposta de formato, não uma seção existente nem uma contagem
editorial definitiva. Fontes bibliográficas e escopos mais ricos têm custo
adicional se forem embarcados; podem permanecer em um ledger editorial externo.

Disponibilizar `revert-` para o lexema `revertor`, por exemplo, também exige
cadastrar/associar o radical e atualizar referências e índices. Alterar um ID
de radical já existente no registro não aumenta esse registro; uma referência
adicional custa 3 B na seção `stem_references`, fora outros dados necessários.
Novas strings/entradas têm custos próprios. Uma flag sozinha não gera a
análise lexical ausente nem estabelece `pacīscor → pepigī`.

Finalmente, **zero bytes adicionais não significa compatibilidade automática**:
o loader atual rejeita os bits reservados e os valores de enum novos.
Será necessário coordenar versão de esquema, packer/importadores, validação,
engine e projeções públicas. Os custos acima referem-se aos bytes sem
compressão; variações em Brotli/gzip e no executável dependem da implementação
e dos dados finais e não foram medidas.

## Avaliação da proposta: paradigma, evidência e relações

Investigação de 2026-09-22. **A separação em três camadas encaixa no projeto;
a atribuição definitiva das nove flags propostas ainda precisa de revisão.**
Esta seção é uma avaliação de arquitetura, não uma implementação nem uma
adjudicação de todas as formas citadas.

### O que as fontes efetivamente sustentam

- [LatInfLexi, 2020, §§ 2.1–2.4](https://aclanthology.org/2020.lt4hala-1.6.pdf)
  distingue defectividade de ausência no corpus e fornece frequências por
  quatro períodos. Contudo, marca também como `#DEF#` células cujo radical
  não consta do LemLat. Além disso, **seleciona uma única forma por célula**,
  descartando alternativas; não conserva toda a sobreabundância. Exclui
  células sempre perifrásticas. É uma inspiração conceitual, não uma tabela
  de proibições históricas pronta para importação. Frequência de superfície
  não resolve sua atribuição a uma análise homógrafa.
- [Allen & Greenough, §§ 205–206](https://dcc.dickinson.edu/grammar/latin/defective-verbs)
  sustenta sistemas defectivos e perfeitos com valor de presente, mas também
  registra formas antigas raras de `coepi` e exceções de `quaeso`. Uma
  restrição do paradigma usual precisa de escopo temporal e de exceções.
- [Allen & Greenough, §§ 209–210](https://grammars.alpheios.net/allen-greenough/conj29.htm)
  documenta ausência de perfeito/supino. A própria lista menciona
  `exstaturus` apesar da ausência de supino de `exsto`: não se deve bloquear
  automaticamente toda forma associada ao quarto radical.
- [Allen & Greenough, §§ 190–192](https://dcc.dickinson.edu/grammar/latin/deponent-verbs)
  é a referência pertinente para `medeor`/`vescor` sem radical de supino,
  sentidos passivos de depoentes e formas ativas com sentido passivo.
  Confirma que voz formal e interpretação semântica são dimensões distintas.
- [Pellegrini, 2023, *Flexemes in theory and in practice*](https://link.springer.com/article/10.1007/s11525-023-09414-7)
  estuda como agrupar variantes coerentes entre células. Uma marca
  `ALTERNATE_PARADIGM_FORM` isolada não representa essas correspondências;
  um agrupamento de variantes pode ser uma evolução posterior, sem exigir
  que todo par de entradas Whitaker seja fundido em um lexema.

### Encaixe no código existente

[`MorphologicalAssessmentIR`](../include/words/model.hpp) já separa geração,
compatibilidade Whitaker e avisos editoriais.
[`docs/morphological-assessment.md`](morphological-assessment.md) já propõe
manifesto de evidência, compilação determinística, seção compacta e anotação
das análises. A proposta desenvolve essa direção existente.

Há limites concretos:

- `MORPHOLOGICAL_NOTICES.LAT` é indexado por **entrada + motivo de trim**,
  não por célula/superfície. `assess_morphology`, em `src/engine.cpp`, consulta
  esse ledger nos dois cortes semideponentes. Adicionar dados ou flags não
  cria automaticamente consultas para depoentes, particípios, supinos ou
  candidatos sem corte. Evidência deve ser consultada independentemente do
  resultado do trim.
- `VerbMorphology`, `ParticipleMorphology` e `SupineMorphology` são tipos
  distintos. Uma camada sobre paradigmas verbais precisa cobrir todos,
  inclusive as construções perifrásticas reconhecidas pela engine.
- `LexemeRecord::age` e `InflectionRule::age` já existem. São metadados
  herdados, não datas de ocorrência. Preservá-los e acrescentar períodos
  de evidência evita reinterpretar a API atual.
- `VerbKind::perfect_definite` já existe, com `memini` e `odi` classificados
  `PERFDEF` na `DICTLINE.GEN`. `src/lexeme.cpp` o usa na construção do lema
  e das formas de dicionário. `PreteritivePerfect` deve ter correspondência
  editorial explícita com esse dado; não criar duas classificações
  independentes potencialmente contraditórias. Há entradas `IMPERS` cujo
  significado menciona também `PERFDEF`, mostrando utilidade de propriedades
  ortogonais ao enum legado.
- A `DICTLINE.GEN` já contém `coepio` antigo (entrada 10809), além de
  `incipio` (23201). Regras globais por grafia de lema perderiam essa distinção.

### Avaliação das marcações propostas

| Marcação | Avaliação para WordsWASM |
|---|---|
| `PARADIGM_CELL_DEFECTIVE` | Útil como afirmação revisada, com célula, escopo e fonte; nunca derivada apenas do trim, de radical vazio ou de busca sem resultado. |
| `DEFECTIVE_PRESENT_SYSTEM` | Útil para resumir um paradigma revisado, desde que período, variante e exceções estejam definidos no ledger. |
| `DEFECTIVE_PERFECT_SYSTEM` | Ambígua: separar ausência de radical de perfeito sintético de ausência de todo o perfectum. Um semidepoente tem perfectum perifrástico. |
| `DEFECTIVE_SUPINE_STEM` | Separar ausência da forma supina, ausência editorialmente afirmada de radical e radical não cadastrado. Não inferir uma proibição de todos os particípios. |
| `PERSON_NUMBER_RESTRICTED` | Pode indicar que há restrições, mas não informa quais. `IMPERS` já cobre parte dos casos; uma regra de escopo deve especificar pessoas, números e modos. |
| `PRETERITIVE_PERFECT` | Útil como propriedade de interpretação, coordenada com `PERFDEF`; manter o tempo morfológico perfeito e anotar o valor semântico separadamente. |
| Quatro flags `*Evidence` | Podem ser resumos derivados do ledger. Precisam definir voz formal, sistema, modos incluídos e significado de zero. Não são permissões de geração. |
| `OVERABUNDANT_CELL` / `ALTERNATE_PARADIGM_FORM` | Representar as realizações alternativas e seu agrupamento; a multiplicidade pode ser calculada. Não significa necessariamente uma forma principal e outra inferior. |
| `ACTIVE_DEPONENT_DOUBLET` | Relação revisada entre entradas, geralmente simétrica; sem transferência automática de atestações. |
| `SUPPLIES_SYSTEM_FOR` | Relação dirigida e com sistema explícito. Não implica copiar todos os radicais ou todas as acepções do fornecedor. |
| Período / registro / qualidade | Pertencem à evidência da análise/ocorrência, não ao payload lexical geral. |
| Sentido passivo em forma ativa/depoente | Informação semântica, frequentemente dependente de acepção/contexto; manter separada da voz morfológica. |

`PassivePerfectEvidence` merece atenção especial: na proposta, a descrição
fala em sentido realmente passivo, enquanto as outras flags se referem a
formas morfológicas. Essas definições não são paralelas. `ausus sum` não
prova sentido passivo por ter forma perifrástica tradicionalmente passiva.
Definir também se `Present` significa o infectum inteiro e se participiais
entram no resumo; do contrário, particípios ativos normais de um depoente
poderiam acionar um bit destinado a exceções finitas/infinitivas.

### Estados separados, em vez de um único enum de validade

Uma célula pode ser possível e estar atestada; uma forma atestada pode ser
disputada; uma restrição clássica pode coexistir com evidência tardia. Por
isso, `DEFECTIVE`, `EXCEPTIONAL_ATTESTED` e `UNKNOWN` não devem disputar o
mesmo campo. Proposta conceitual, ainda sem contrato público:

```text
paradigmAssessment: unreviewed | licensed | defective | disputed
attestation:       not-checked | exact-attested | related-only |
                   not-observed-in-coverage | source-conflict
scope:             célula/sistema + variante + período/contexto aplicável
evidence:          uma ou mais referências e suas avaliações
```

`possible-but-unattested` passa a ser uma combinação: possibilidade
revisada e ausência numa cobertura explicitamente pesquisada. Sem pesquisa,
o estado de atestação continua `not-checked`. `Evidence != license` vale
para flags agregadas: evidência exata sustenta aquela análise no escopo
documentado, sem autorizar todo o subparadigma.

Para `rēs → reor`, preservar `deponent-active-form` e ausência de revisão
exata até haver uma decisão sustentada para essa análise. As fontes gerais
consultadas não bastam para atribuir `DEFECTIVE` absoluto. Uma restrição
revisada ao paradigma clássico pode ser registrada com esse escopo. A
atestação tardia de outra forma ativa não atesta `rēs`; a ocorrência nominal
de `rēs` tampouco atesta sua leitura verbal.

Para `ausī → audeo`, o ledger atual continua sendo resumo de evidência
relacionada. Promover a análise a `exact-attested` exige conferir ocorrência,
atribuição lexical e morfologia, inclusive sua ambiguidade com o particípio.
Para `revertor`, a evidência agregada tampouco cria a análise de perfeito
hoje atribuída à entrada `reverto`.

### Identidade, proveniência e conflitos

O alvo mínimo de evidência exata é **entrada + célula + realização**.
A célula deve codificar características morfológicas, não apenas um `RuleId`:
regras distintas podem realizar a mesma célula, e `UNIQUES` tem morfologia
própria. A realização deve preservar a grafia observada e sua normalização;
uma reescrita de busca não transmite automaticamente atestação à grafia
digitada. Construções de várias palavras precisam de identidade própria.

Períodos são conjuntos, não um enum exclusivo. O manifesto deve conservar
datas/intervalos e identificar o esquema de periodização; os quatro recortes
do LatInfLexi não equivalem aos valores de `Age` nem distinguem por si sós
arcaico de clássico. Registro também pode ser múltiplo.

O enum sugerido `AttestationQuality` mistura duas perguntas. Preferir no
manifesto `sourceKind` (texto, gramático, glossário, lexicógrafo etc.) e
`reviewStatus`/condição textual (confirmada, disputada, conjectural etc.).
Um texto direto também pode ter leitura disputada; um gramático pode citar
literalmente um testemunho. Um resumo compacto pode perder detalhes, mas
deve manter referência à evidência completa e não fabricar uma escala
automática de confiabilidade.

IDs do ledger precisam de vínculo com a revisão do dataset: os atuais números
de entrada são ordinais. A compilação deve verificar a identidade lexical
esperada e resolver os IDs da imagem de destino. Relações não implicam
identidade semântica, transitividade ou transferência de evidência.

Inconsistências de formato devem falhar na compilação; divergências legítimas
entre fontes devem permanecer representáveis. Não basta fazer um OR de
flags e deixar desaparecer o conflito. Uma exceção exata só substitui uma
regra ampla quando essa precedência foi editorialmente definida para o
mesmo escopo; nos demais casos, expor a divergência.

### Recomendação de implementação e custo

**Critério adotado: não duplicar informações já codificadas.** Os nove bits
disponíveis são capacidade, não uma meta de preenchimento. Antes de criar
qualquer campo persistido, identificar sua fonte de verdade e verificar se
o valor pode ser obtido dos dados existentes:

| Informação | Fonte existente / decisão |
|---|---|
| Depoente, semidepoente, impessoal, `PERFDEF` | Preservar `VerbKind`; não repetir essas classificações em flags. Uma propriedade semântica adicional só se justifica quando não for equivalente ao código existente. |
| Radical cadastrado ou ausente | Consultar os quatro slots de radicais; não criar bits `HasPresentStem`, `HasPerfectStem` ou `HasSupineStem`. A ausência histórica revisada é outra afirmação, com fonte e escopo. |
| Época lexical ou da regra | Reutilizar `Age`. Época de uma ocorrência é informação nova e pertence à evidência, sem cópia automática de `Age`. |
| Existência de evidência ativa/passiva por sistema | Derivar dos registros de evidência revisados; não persistir os quatro bits `*Evidence` junto com os mesmos registros. |
| Existência de restrição ou de variantes numa célula | Consultar regras/realizações esparsas; não duplicar com `PersonNumberRestricted` ou `OverabundantCell` apenas para indicar sua presença. |
| Existência de relação lexical | Consultar a tabela de relações; não adicionar uma flag lexical equivalente. |

Uma API pode oferecer propriedades derivadas convenientes sem gastar bits na
WWDB. Uma estrutura auxiliar de consulta em RAM só deve ser introduzida se
medições justificarem; não deve virar uma segunda fonte editorial. Quando
um dado legado for insuficiente, registrar apenas o complemento necessário,
sem replicar toda a classificação. Na ausência de uma necessidade residual
demonstrada, **a recomendação é manter os nove bits reservados** e começar
pelos registros esparsos de informação nova.

1. Começar pelo manifesto com alvos de célula/realização, escopo, fontes e
   relações. Cadastrar poucos casos discriminantes já documentados nesta
   nota; não preencher milhares de ausências por inferência.
2. Manter o caminho gerador e `whitakerTrim`. Consultar a nova avaliação
   independentemente do motivo legado e anotar candidatos. Gerar formas
   hoje ausentes exige trabalho separado nos radicais/variantes/construções.
3. Compilar uma seção esparsa apenas com registros revisados; publicar os
   resumos tipados no IR, JSON e Embind. Qualquer filtragem será uma política
   posterior explícita, não consequência de um bit de evidência.
4. Só então avaliar se resta alguma propriedade lexical nova, não derivável
   dos dados existentes, que justifique bits próprios. Resumos derivados
   ficam como projeções, sem duplicação persistida de `PERFDEF`, `IMPERS`,
   radicais, evidências ou relações.

Os nove bits continuam custando **0 B adicionais na WWDB**. Porém, nove
booleanos só expressam nove afirmações positivas: zero deve significar
“sem afirmação compilada”, não “revisado e falso”. Se cada uma das nove
propriedades precisar de três estados autônomos, a codificação conjunta
exige pelo menos `ceil(log2(3^9)) = 15` bits; campos individuais de dois bits
usariam 18. O ledger esparso evita exigir esses estados em todo lexema.

A fórmula anterior `32 + 5R` continua válida somente para relações simples
com dois IDs de 16 bits e tipo/escopo de um byte. Para evidência exata, não
se deve anunciar 3 B por registro: esse é o tamanho do aviso atual, que não
carrega célula, superfície, período ou referência. O orçamento correto será
`32 + N × stride + strings/escopos/fontes adicionais` por nova seção, após
fixar os limites dos IDs e quais detalhes ficarão externos. Nenhuma medição
de um formato novo de evidência foi feita nesta investigação.

Validação futura deve distinguir: `rēs` nominal/verbal; evidência relacionada
de `audeo` versus atestação exata; perfectum perifrástico versus radical
sintético ausente; supino ausente versus particípio disponível; restrição
temporal versus exceção antiga; e relações lexicais versus identidade de
análise. Esta investigação alterou apenas documentação.
