# TLL corpus coverage artifacts

> **Status / Estado:** initial analytical material / material analítico inicial.
> These files are separate inputs for constructing a possible roadmap of new
> WORDS entries; they are not a finished roadmap, an approved editorial list,
> or publication-ready lexical data. / Estes arquivos são insumos separados
> para construir um possível roadmap de novas entradas do WORDS; não são um
> roadmap finalizado, uma lista editorial aprovada nem dados lexicais prontos
> para publicação.

Arquivos gerados pelos comandos documentados em
[`../../docs/cobertura-corpus-tll.md`](../../docs/cobertura-corpus-tll.md).
As listas são fragmentos de evidência para análise e revisão editorial. Elas
podem ajudar a decidir o que investigar em seguida, mas não autorizam importação
automática de lexemas nem, isoladamente, definem o roadmap de expansão.

Os arquivos `tll-cltk-*` são uma segunda camada parcial baseada no piloto
CLTK/Stanza explicitamente identificado no relatório. Eles preservam hipóteses,
ambiguidades, conflitos e proveniência para orientar um possível roadmap.

## Português — como interpretar as filas

A coluna `queue`/`roadmap_queue` é uma classificação editorial para organizar a
revisão. Ela não constitui uma análise linguística definitiva e não autoriza a
inclusão automática de um lema no WORDS.

Em particular, `abbreviation_or_editorial` descreve a **forma superficial do
corpus**, classificada na primeira etapa, e não o lema sugerido pelo
CLTK/Stanza. A forma entra nessa fila quando ocorre pelo menos uma destas
condições:

- tem no máximo duas letras;
- tem no máximo quatro letras e pelo menos 25% das ocorrências são seguidas de
  ponto;
- todas as ocorrências observadas estão em maiúsculas.

Na segunda etapa, um grupo de lema herda `abbreviation_or_editorial` somente
quando todas as formas que contribuíram para ele já pertenciam a essa fila. Por
exemplo, uma linha com lema CLTK `sanctus` pode estar nessa fila porque a forma
real analisada foi `s`, possivelmente uma abreviação. Isso **não** afirma que
`sanctus` seja uma abreviação.

As demais filas do roadmap significam:

- `common_lexeme`: possível lexema comum com evidência dicionarística;
- `proper_name`: possível nome próprio, segundo o CLTK, a capitalização ou a
  evidência estrutural anterior;
- `model_or_pos_review`: hipótese que exige revisão por ambiguidade, conflito de
  classe gramatical ou qualidade da fonte;
- `no_dictionary_match`: lema do CLTK sem correspondência exata nas fontes
  consultadas.

`corpus_occurrences_upper_bound` é apenas o total global das formas associadas
ao lema no piloto; não garante que todas as ocorrências tenham o mesmo lema.
`pilot_lemma_occurrences` conta somente as atribuições efetivamente observadas
no piloto. Consulte `top_forms` e `tll-cltk-lemma-evidence.tsv.gz` para descobrir
qual forma superficial originou cada hipótese.

## English — how to interpret the queues

The `queue`/`roadmap_queue` column is an initial analytical classification for
editorial review. It is not a definitive linguistic analysis, a completed
roadmap, or authorization to add a lemma to WORDS automatically.

In particular, `abbreviation_or_editorial` describes the **surface corpus
form**, classified during stage one, rather than the lemma proposed by
CLTK/Stanza. A form enters this queue when at least one of these conditions is
true:

- it contains at most two letters;
- it contains at most four letters and at least 25% of its occurrences are
  followed by a period;
- every observed occurrence is uppercase.

During stage two, a lemma group inherits `abbreviation_or_editorial` only when
all contributing surface forms already belonged to that queue. For example, a
row whose CLTK lemma is `sanctus` may be in this queue because the actual input
form was `s`, possibly an abbreviation. It does **not** mean that `sanctus`
itself is an abbreviation.

The other roadmap queues mean:

- `common_lexeme`: a possible common lexeme with dictionary evidence;
- `proper_name`: a possible proper name based on CLTK, capitalization, or prior
  structural evidence;
- `model_or_pos_review`: a hypothesis requiring review because of ambiguity, a
  part-of-speech conflict, or source-quality flags;
- `no_dictionary_match`: a CLTK lemma with no exact match in the consulted
  sources.

`corpus_occurrences_upper_bound` is only the global total for surface forms
associated with the lemma in the pilot; it does not assert that every
occurrence has that lemma. `pilot_lemma_occurrences` counts only assignments
actually observed in the pilot. Inspect `top_forms` and
`tll-cltk-lemma-evidence.tsv.gz` to identify the surface form behind each
hypothesis.
