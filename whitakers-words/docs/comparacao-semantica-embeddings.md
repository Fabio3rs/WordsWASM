# Comparação semântica em massa com embeddings

## Papel no fluxo editorial

O Qwen3-Embedding-8B fornece um sinal de ranking entre sentidos de dicionários
diferentes. Similaridade vetorial não constitui prova de identidade: sinônimos,
antônimos e conceitos relacionados também podem ficar próximos. Por isso a
pipeline preserva separadamente lema, morfologia, quantidade, alvo Words e
proveniência, e nunca define `automatic_promotion_allowed` como verdadeiro.

A fila principal compara apenas entradas do mesmo lema ASCII e da mesma classe
próprio/comum. Uma comparação entre lemas distintos só é criada quando a fonte
marca explicitamente uma variante ortográfica ou alias puro. Entradas da mesma
família nunca viram candidatas de merge.

## Artefatos

- `lexical-evidence.sqlite`: entradas, formas básicas, sentidos, hashes,
  execuções do modelo e scores. Não contém os vetores.
- `lexical-embeddings.sqlite`: cache regenerável de vetores normalizados
  `float32` little-endian com 4.096 dimensões.
- `lexical-embedding-gold.jsonl`: 600 decisões editoriais usadas para calibrar
  limiar e margem.
- `lexical-embedding-calibration.json`: gate executável consumido pelo
  analisador semântico v2.

No corte N/V/ADJ medido, há aproximadamente 190.948 documentos semânticos. Os
vetores crus ocupam cerca de 3,13 GB, além do overhead do SQLite. O banco de
evidências permanece muito menor porque guarda apenas textos e resultados.

## Preparação do banco

O dump v2 registra SHA-256 e tamanho de cada fonte, continua abrindo todos os
SQLite externos com `immutable=1` e inclui formas básicas mesmo com
`--no-include-forms`. Essa opção exclui somente os milhões de paradigmas
flexionados.

```bash
python3 poc/compact-db/build_lexical_embedding_db.py \
  /tmp/lexical-comparison-nva.jsonl.gz \
  --input-report /tmp/lexical-comparison-nva-report.json \
  --output /tmp/lexical-evidence.sqlite \
  --report /tmp/lexical-evidence-report.json
```

O construtor recusa sobrescrever um banco existente e publica o arquivo apenas
depois de validar as chaves estrangeiras. Cada entrada conserva a referência
da fonte e a revisão do pacote. Cada sentido recebe hash próprio; definições
sem segmentação são fallback. Remissões curtas são marcadas como
`crossref_only` e não podem sustentar promoção vetorial.

## Geração e ranking

Com o Ollama ativo e `qwen3-embedding:8b` instalado:

```bash
python3 poc/compact-db/generate_lexical_embeddings.py \
  /tmp/lexical-evidence.sqlite \
  --cache /tmp/lexical-embeddings.sqlite \
  --model qwen3-embedding:8b \
  --batch-size 32 \
  --report /tmp/lexical-embeddings-report.json

python3 poc/compact-db/rank_lexical_embeddings.py \
  /tmp/lexical-evidence.sqlite \
  --cache /tmp/lexical-embeddings.sqlite \
  --report /tmp/lexical-embedding-ranking-report.json
```

O preflight consulta `/api/tags`, gera um vetor de prova e exige a dimensão
esperada antes de abrir qualquer saída para escrita. O cache é retomável por
hash do texto formatado, digest do modelo e versão da instrução. O ranking usa
cosseno exato dentro de cada bloco, registra o melhor par de sentidos, média
dos três melhores, ranks recíprocos e margens.

Somente definição/sentido e código de idioma entram no embedding. Lema, formas,
POS e quantidade continuam sinais independentes para que uma grafia comum não
infle artificialmente todos os homógrafos.

## Conjunto-ouro e calibração

Gerar a amostra estratificada:

```bash
python3 poc/compact-db/calibrate_lexical_embeddings.py sample \
  /tmp/lexical-evidence.sqlite \
  --review-queue /tmp/semantic-alignment-review.jsonl.gz \
  --output lexical-embedding-gold.jsonl \
  --report /tmp/lexical-embedding-gold-report.json
```

A amostra contém 200 pares com Words, 150 sem Words, 100 homógrafos/conflitos
de quantidade, 75 arestas rejeitadas e 75 casos sem ponte textual. O revisor
substitui `pending` por `same_lexeme`, `different_lexeme` ou `uncertain` e
preenche `reviewer` e `note`. A revisão não altera `candidate_revision`.

```bash
python3 poc/compact-db/calibrate_lexical_embeddings.py evaluate \
  lexical-embedding-gold.jsonl \
  --output lexical-embedding-calibration.json \
  --report /tmp/lexical-embedding-calibration-report.json
```

O corte desenvolvimento/holdout é determinístico por lema. A configuração só
recebe `promotion_allowed: true` quando desenvolvimento e holdout atingem
precisão empírica mínima de 99,5% com alguma proposta.

## Alinhamento v2

Sem calibração, o banco acrescenta scores informativos, mas não altera
relações nem componentes:

```bash
python3 poc/compact-db/analyze_semantic_alignment.py \
  /tmp/lexical-comparison-nva.jsonl.gz \
  --embedding-evidence /tmp/lexical-evidence.sqlite \
  --output /tmp/semantic-alignment-v2-informative.jsonl.gz \
  --report /tmp/semantic-alignment-v2-informative-report.json
```

Depois que o gate passar, fornecer também a calibração. Apenas pares sem
conflito estrutural, `mutual_top1`, acima do limiar e com margem suficiente
podem formar novas arestas propostas:

```bash
python3 poc/compact-db/analyze_semantic_alignment.py \
  /tmp/lexical-comparison-nva.jsonl.gz \
  --embedding-evidence /tmp/lexical-evidence.sqlite \
  --embedding-calibration lexical-embedding-calibration.json \
  --output /tmp/semantic-alignment-v2.jsonl.gz \
  --report /tmp/semantic-alignment-v2-report.json
```

O validador editorial aceita filas v1 e v2. O ledger de decisões continua
fixado à revisão SHA-256 da análise; nenhuma decisão antiga é promovida ou
reescrita implicitamente.
