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
