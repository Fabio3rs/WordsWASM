# Proveniência da investigação Markoviana

Este documento separa quatro relações que não devem ser confundidas:

1. **entrada executada**: bytes ou anotações realmente consumidos pelo
   experimento;
2. **evidência textual**: fonte usada somente para confirmar que uma frase
   ocorre num texto;
3. **inspiração de projeto**: artigo ou programa estudado para escolher uma
   hipótese, sem incorporar código, modelo ou dados;
4. **candidato futuro**: material local ainda não consumido.

O ranker em C++23 foi escrito para o WordsWASM. Nenhum trecho de RFTagger,
Latin Macronizer, NLTK ou dos artigos abaixo foi copiado ou ligado ao binário.

## Artefatos realmente consumidos

| Artefato | Origem e identidade local | Licença/proveniência | Uso efetivo |
|---|---|---|---|
| `web/engine/words-full.wwdb` | build corrente do banco Whitaker/WordsWASM; o relatório registra `datasetId` e o perfil completo de análise | termos do próprio projeto/dados Whitaker | tokenizer oficial, lattice morfológico, assessments, análises artificiais e compostos; |
| `corpus/agreement_fixtures.json` | fixtures do projeto; SHA-256 `8087427b1f54f83701df2c187e5fbbf37cd4ea08e55f6a33de8e50fc6491c41a` no início desta fase | cada fixture contém `annotation.source`, commit, bloco, alegação e adições editoriais | treino gold didático, treino sintético e avaliação; |
| `corpus/common_phrases.tsv` | lista editorial local; SHA-256 `306fe1ce7b258b907f093da6d575a4efaf5a01bbb424002d715cfc96073c109d` | não é apresentada como corpus gold nem como edição citável | treino *silver*: escolhe apenas o melhor empate manual compatível com lemas preferidos; |
| `corpus/attested_gold_fixtures.json`, itens `verified-attested` | texto conferido no espelho local de The Latin Library; cada item registra obra, locus, texto-fonte e revisão | o espelho não contém uma licença global identificada; usam-se somente frases curtas como evidência de ocorrência, sem redistribuição do corpus | cinco textos atestados; morfologia e dependências são adições editoriais explicitadas; |
| `corpus/attested_gold_fixtures.json`, itens `treebank-gold` | [`treebank_data-master/v2.1/Latin`](https://github.com/PerseusDL/treebank_data/tree/master/v2.1/Latin), arquivo `texts/phi0474.phi013.perseus-lat1.tb.xml`, SHA-256 `55b12883b48e5c8489e8cd7ebc0f34fc1892642571121f1783bae5f1df43d24a` | repositório Perseus Treebank Data, CC BY-SA 3.0 US, com ressalva de direitos de terceiros; o README do LDT 2.1 descreve anotação semiautomática e revisões | cinco sentenças de *In Catilinam*; tokens, lemas e morfologia foram mapeados ao esquema WordsWASM; as dependências LDT ainda não são tratadas como gold local; |
| saída N-best do `dependency-projection` | implementação local em `parser.cpp`/`parser.hpp` | código do projeto | domínio de candidatos, árvores determinísticas, score manual e hard constraints; |

O arquivo de fixtures preserva a proveniência por item. `commit` vale
`unversioned-local-snapshot` quando o pacote copiado não traz metadados Git; a
ausência não é substituída por um hash de commit inventado.

## Materiais locais inspecionados, mas não consumidos

| Material local | Origem/licença observável | O que foi aproveitado | O que não foi usado |
|---|---|---|---|
| `Schmid-Laws.pdf` | Schmid e Laws, COLING 2008; CC BY-NC-SA 3.0 no próprio PDF; SHA-256 `55af5b5231b49254039fc452ab6fbb53abb297564d07e439cfded4bebe2d2484` | inspiração para expor etiquetas como atributos e para a ablação de suavização hierárquica | nenhum texto ou código foi copiado; o experimento local usa uma hierarquia explícita de sufixos, não as árvores de decisão do artigo, e ainda não escolhe uma fatoração de atributos; |
| `RFTagger/` | pacote de Helmut Schmid/IMS Stuttgart; o snapshot não inclui licença autônoma; a [página oficial](https://www.cis.uni-muenchen.de/~schmid/tools/RFTagger/) limita a distribuição anunciada a educação, pesquisa e outros usos não comerciais | inspeção da arquitetura e confirmação de contexto configurável | biblioteca, executáveis, fontes e parâmetros não são compilados, ligados nem executados pelo WordsWASM; |
| `latin-macronizer-master/` | [Johan Winge, Latin Macronizer](https://alatius.com/macronizer/), 2015--2023, GPLv3; inclui modelo `rftagger-ldt.model` de SHA-256 `52039e59d1102903bf447b68a264dc87b8f0157e531e82da718887b136a41973` | inspiração para uma possível fonte *silver* e confirmação da combinação RFTagger + Morpheus | nenhum código, banco, modelo ou saída entrou no treino atual; |
| `proiel-treebank-master/` | [PROIEL](https://github.com/proiel/proiel-treebank); CC BY-NC-SA 3.0; o README cita Haug e Jøhndal (2008) e declara o XML PROIEL como fonte autoritativa | candidato ao importador e a splits por obra/autor | nenhuma sentença PROIEL entrou no treino ou teste atual; |
| `10-06-2020_all_resources_all_formats/` | pacote Index Thomisticus Treebank de 2020, identificável pelos nomes `IT-TB_*`; a [distribuição oficial](https://itreebank.marginalia.it/view/download.php) declara CC BY-NC-SA 3.0 | candidato para latim medieval, valência e camadas PML/CoNLL | nenhum ZIP foi extraído para o corpus experimental e nenhum item entrou no treino; |
| `treebank_data-master/`, demais textos | Perseus Treebank Data, CC BY-SA 3.0 US com ressalvas no README | fonte candidata para expansão reprodutível | somente as cinco sentenças de Cícero identificadas na seção anterior foram mapeadas; |
| `../markov_demo/` | demonstração local escrita para esta investigação; referência conceitual à documentação `nltk.lm` | contas didáticas de n-gramas, início/fim, geração e log-probabilidade | não é ligada ao ranker integrado e seu corpus sintético não entra automaticamente no treino integrado; |

## Genealogia científica e efeito concreto sobre o desenho

“Contexto” abaixo significa que o trabalho sustenta a discussão, mas não
originou dados nem código. “Decisão” identifica a parte efetivamente refletida
na implementação.

| Fonte primária | Papel nesta investigação |
|---|---|
| [Markov (1906), *Extension of the Law of Large Numbers...*](https://eudml.org/doc/128778); [Markov (1913), estudo de *Eugene Onegin*](https://www.mathnet.ru/eng/im6612) | contexto histórico para dependência local e cadeias de ordem superior; |
| [Shannon (1948), *A Mathematical Theory of Communication*](https://people.math.harvard.edu/~ctm/home/text/others/shannon/entropy/entropy.pdf) | contexto e decisão: fatoração sequencial, aproximações de contexto finito e marcadores de texto; |
| [Baum e Petrie (1966)](https://projecteuclid.org/journals/annals-of-mathematical-statistics/volume-37/issue-6/Statistical-Inference-for-Probabilistic-Functions-of-Finite-State-Markov-Chains/10.1214/aoms/1177699147.full); [Viterbi (1967)](https://essrl.wustl.edu/~jao/itrg/viterbi.pdf) | contexto para estados ocultos e melhor caminho; o ranker atual não é HMM e não executa Viterbi; |
| [Chomsky (1956), *Three Models for the Description of Language*](https://chomsky.info/wp-content/uploads/195609-.pdf) | contexto para os limites de memória finita; |
| [Diaconis e Freedman (1980), *De Finetti's Theorem for Markov Chains*](https://projecteuclid.org/journals/annals-of-probability/volume-8/issue-1/De-Finettis-Theorem-for-Markov-Chains/10.1214/aop/1176994828.full) | contexto matemático para equivalências determinadas por contagens de transição; |
| [Schütze e Singer (1994), memória variável](https://aclanthology.org/P94-1025.pdf) | candidato futuro; nenhuma árvore de sufixos foi implementada; |
| [Kneser e Ney (1995)](https://www-i6.informatik.rwth-aachen.de/publications/download/951/Kneser-ICASSP-1995.pdf) | contexto para esparsidade; o baseline usa suavização aditiva, não Kneser--Ney; |
| [Brants (2000), TnT](https://aclanthology.org/A00-1031.pdf) | contexto e decisão: baseline Markoviano curto, interpolação como alternativa futura e separação de palavras desconhecidas; |
| [Lafferty, McCallum e Pereira (2001), CRF](https://www.cs.columbia.edu/~jebara/6772/papers/crf.pdf) | alternativa condicional ainda não implementada; |
| [McDonald et al. (2005), árvores geradoras não projetivas](https://aclanthology.org/H05-1066.pdf) | contexto estrutural; o parser local continua sendo a autoridade sobre candidatos; |
| [Palanisamy e Devi (2006), HMM para tâmil de ordem relativamente livre](https://www.rcs.cic.ipn.mx/2006_18/HMM%20based%20POS%20Tagger%20for%20a%20Relatively%20Free%20Word%20Order%20Language.html) | precedente comparativo; não fornece dados nem código ao experimento; |
| [Hellwig (2009), SanskritTagger](https://sanskrit.inria.fr/Symposium/DOC/Hellwig.pdf) | precedente de etiquetagem estocástica em língua flexiva; |
| [Schmid e Laws (2008), RFTagger](https://aclanthology.org/C08-1098/) | decisão futura: decompor etiquetas finas em vetores de atributos; nenhuma árvore de decisão foi incorporada; |
| [Lao e Cohen (2010), *path-constrained random walks*](https://dblp.uni-trier.de/rec/journals/ml/LaoC10.html) | inspiração futura para features de caminhos relacionais; |
| [Lee, Naradowsky e Smith (2011)](https://aclanthology.org/P11-1089.pdf) | decisão: preservar N-best morfológico para a estrutura ajudar no ranking; |
| [Gubbins e Vlachos (2013), *Dependency Language Models*](https://aclanthology.org/D13-1143.pdf) | decisão: comparar vizinhança superficial com percurso canônico do parser; |
| [Pickhardt et al. (2014), *skipped n-grams*](https://aclanthology.org/P14-1108.pdf) | alternativa de alcance maior ainda não implementada; |
| [Eger, vor der Brück e Mehler (2015)](https://aclanthology.org/W15-3716/) | evidência empírica de taggers Markovianos e assistência lexical em latim; |
| [Futrell, Mahowald e Gibson (2015)](https://aclanthology.org/W15-2112.pdf) | contexto para medir liberdade de ordem por entropia condicionada à construção; |
| [Gulordava e Merlo (2015)](https://aclanthology.org/W15-2115.pdf) | decisão metodológica futura: separar autor, gênero e período; |
| [Krishna et al. (2016)](https://aclanthology.org/C16-1048.pdf) e [Krishna et al. (2018)](https://aclanthology.org/D18-1276.pdf) | decisão: experimentar candidatos em grafo/estrutura antes de apenas aumentar a ordem do n-grama; |
| [Motavallian e Komeily (2023)](https://dialnet.unirioja.es/descarga/articulo/9165317.pdf) | cautela: aumento sintético por reordenação precisa de ablação e pode piorar generalização; |
| [NLTK `nltk.lm`](https://www.nltk.org/api/nltk.lm.html) | documentação secundária usada somente para conferir nomenclatura e comportamento da demonstração isolada; não é dependência do C++; |

As referências bibliográficas clicáveis permanecem em `MARKOV.md`. Os artigos
foram usados como fundamentação de hipóteses, não como autorização para copiar
implementações.

## Transformações auditáveis

Para os cinco itens LDT atualmente usados:

1. selecionou-se uma sentença curta por ID e `subdoc` no XML 2.1;
2. removeu-se pontuação;
3. removeram-se sufixos de sentido dos lemas quando necessário;
4. converteu-se a etiqueta posicional LDT para campos explícitos WordsWASM;
5. verificou-se que ao menos uma atribuição gold sobrevive às hard constraints;
6. não se importaram as relações LDT como se fossem relações nativas do parser.

Para as cinco frases TLL, somente a ocorrência textual é externa. A análise
morfológica é uma afirmação editorial do experimento e está rotulada como tal.

## Regras para as próximas fases

- Todo novo corpus deve registrar URL/repositório, release ou hash verificável,
  licença, unidade documental e transformação aplicada.
- Treino, desenvolvimento e teste devem registrar IDs de fonte; uma divisão
  aleatória de sentenças não substitui split por obra/autor.
- Saída automática de tagger é `silver`, nunca `gold`, salvo revisão explícita.
- Código ou modelo externo só pode ser incorporado após uma decisão separada de
  licença e compatibilidade; “inspecionado” não autoriza reutilização.
- O relatório experimental deve registrar o controle negativo, a semente, o
  grau de exposição do alvo e a política de desempate. `observed`,
  `shuffle-within-sequence` e `counterfactual-analysis` têm semânticas
  distintas e nunca devem ser agregados silenciosamente.
