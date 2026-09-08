# Plano de expansão e reconciliação dos dicionários

## Objetivo

Construir uma base editorial rastreável para ampliar o Words sem colapsar
homógrafos, inventar morfologia ou transformar ausência de marca vocálica em
vogal breve. O resultado final deve ser reprodutível a partir dos bancos-fonte
somente-leitura e de um ledger de decisões humanas ou assistidas.

## Princípio central: primeiro identidade, depois maioria

Uma grafia não identifica sozinha um lexema. Cada verbete-fonte permanece
atômico, com seu número de homógrafo, classe, significado e proveniência. A
maioria só pode ser calculada depois que verbetes de obras diferentes forem
alinhados como o mesmo lexema pelo sentido.

O painel primário é Lewis & Short, Gaffiot e Faria. Cada família tem no máximo
um voto por atributo. O Latim–Alemão valida morfologia e fornece evidência
adicional, mas não vira silenciosamente um quarto voto. Fontes derivadas, como
Collatinus, podem apoiar uma decisão, nunca aumentar o quórum independente.

## Fase 1 — dump factual

`poc/compact-db/dump_lexical_comparison.py` produz um JSONL, preferencialmente
comprimido como `.jsonl.gz`, com um pacote por `(lema ASCII, próprio/comum)`.
Ele lê todos os SQLite com `immutable=1` e reúne:

- todas as entradas-fonte, sem deduplicar homógrafos;
- cabeçalho, definição, sentidos hierárquicos e idioma da glosa;
- POS, gênero, indeclinabilidade, classe morfológica e descritores crus;
- todas as formas flexionadas quando `--include-forms` está ativo;
- quantidade explícita, posição por posição; ausência de marca é `unknown`;
- confiança, proveniência, etimologia, notas e flags especiais disponíveis;
- candidatos do `DICTFILE.GEN`, com quatro radicais, paradigma, atributos
  especiais, metadados de idade/área/geografia/frequência/fonte e significado;
- quantidades já canonizadas em `QUANTITIES.LAT`.

O casamento com o Words é estrutural e conservador: radical de citação,
terminação plausível, POS, gênero e condição próprio/comum. O dump não afirma
que um casamento estrutural prova identidade semântica.

## Fase 2 — alinhamento semântico

`poc/compact-db/analyze_semantic_alignment.py` faz duas passagens sobre o dump.
Na primeira calcula frequência documental por idioma; na segunda compara
seções e sentidos por TF–IDF com containment, além de expor gênero, quantidade
por posição e alvos estruturais do Words. Ele não compara diretamente textos
em idiomas diferentes sem uma ponte textual. Lewis & Short fornece essa ponte
em português para o Faria e em inglês para o Words.

Uma aresta só entra num componente proposto quando é a preferência mútua única
entre as duas famílias. Candidatos moderados exigem ainda margem sobre a segunda
opção. Empates, como um verbete contra dois registros Words semanticamente
iguais, permanecem sem escolha. O algoritmo nunca coloca duas entradas da
mesma família no mesmo componente e nunca promove os componentes sozinho.

O ledger separado, validado por
`poc/compact-db/validate_semantic_alignment.py`, registra grupos de entradas
que representam o mesmo lexema. Cada decisão contém:

- chave estável e `revision` exata do pacote;
- membros como `(fonte, source_entry_id)`;
- resumo curto do sentido que justifica o alinhamento;
- destino: novo lexema, merge com Words, variante, rejeição ou adiamento;
- revisor, método e nota de incerteza.

O schema executável é
`schemas/semantic-alignment-decision-v1.schema.json`. O validador fixa cada
decisão à revisão SHA-256 da análise, rejeita membros inexistentes, sobreposição
entre grupos aceitos e colisão de família. Sua saída `resolved` calcula o
consenso de gênero, indeclinabilidade e quantidade somente entre os membros
semanticamente confirmados e sugere `merge_existing`, `new_lexeme_candidate`
ou revisão de múltiplos alvos Words.

Entradas do mesmo dicionário podem ocupar grupos diferentes. Números de
homógrafo ajudam na triagem, mas não são tratados como equivalentes entre
obras. Comparação lexical automática entre inglês, francês, português e
alemão é apenas sinal de ranking; a decisão deve enxergar o texto dos sentidos.

## Fase 3 — consenso por atributo

Dentro de um grupo semanticamente alinhado, cada atributo é votado
separadamente:

1. normalizar o vocabulário do atributo sem apagar o valor cru;
2. reduzir várias observações da mesma família a um voto; dissenso interno
   deixa a família em conflito nesse atributo;
3. aceitar `2 de 3` quando duas autoridades primárias concordam;
4. registrar a terceira discordância e todas as fontes auxiliares;
5. deixar `unknown` quando não houver quórum, sem preencher por ausência.

POS, gênero e indeclinabilidade podem usar igualdade normalizada. Declinação,
conjugação, variantes e slots exigem crosswalk empírico entre os sistemas.
Quantidade vocálica é decidida por posição lógica da palavra; duas marcas
iguais vencem uma oposta, mas uma única marca nunca estabelece consenso.

## Fase 4 — compilação e validação

Somente decisões validadas geram o ledger canônico consumido pelo compilador.
Antes da escrita do WWDB, validar:

- nenhuma entrada-fonte usada em dois homógrafos incompatíveis;
- estrutura completa para a classe gramatical;
- formas geradas atestadas quando houver léxico morfológico;
- quantidade em NFC e máscaras coerentes com os radicais ASCII;
- proveniência por campo e conflito preservado no relatório;
- capacidade das seções após a migração dos IDs e offsets além de 16 bits.

## Execução inicial

Uma amostra pequena, com todas as formas, pode ser produzida assim:

```bash
python3 poc/compact-db/dump_lexical_comparison.py \
  DICTFILE.GEN \
  --ls-database /caminho/ls_dict.db \
  --gaffiot-database /caminho/gaffiot.db \
  --faria-v3 /caminho/faria-v3-quality.sqlite \
  --latin-german /caminho/token_latim_german.sqlite \
  --quantities QUANTITIES.LAT \
  --part NOUN --part VERB --part ADJ \
  --lemma malum --lemma levis \
  --output /tmp/lexical-comparison-sample.jsonl.gz \
  --report /tmp/lexical-comparison-sample-report.json
```

Para o corte completo, retirar `--lemma` e manter a saída comprimida. Como o
Latim–Alemão sozinho contém milhões de formas, `--no-include-forms` serve para
uma primeira auditoria leve; a execução editorial definitiva deve preservar
as formas completas.

Gerar a fila semântica:

```bash
python3 poc/compact-db/analyze_semantic_alignment.py \
  /tmp/lexical-comparison.jsonl.gz \
  --output /tmp/semantic-alignment-review.jsonl.gz \
  --report /tmp/semantic-alignment-report.json
```

Validar um ledger e materializar apenas alinhamentos aceitos:

```bash
python3 poc/compact-db/validate_semantic_alignment.py \
  /tmp/semantic-alignment-review.jsonl.gz \
  SEMANTIC_ALIGNMENT_DECISIONS.jsonl \
  --output /tmp/semantic-alignment-resolved.jsonl.gz \
  --report /tmp/semantic-alignment-validation.json
```
