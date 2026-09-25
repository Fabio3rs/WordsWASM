# Inspeção local de frases

Este diretório é uma investigação local, fora do código de produção. Depois de
compilar o alvo `parsers_investigation`, passe uma frase para ver a melhor análise
morfológica e as dependências em formato legível:

```sh
cmake --build build/parsers --target parsers_investigation
build/parsers/parsers_investigation/parsers_investigation \
  --text 'Puella rosam amat.'
```

Com `--text`, o programa usa `dependency-mst` por padrão. A frase é analisada
sem abrir o corpus e sem comparar com gold. Use `--strategy NOME` para investigar
outra estratégia, ou `--strategy all` para exibir todas. `--fragment` permite
uma frase sem verbo. `--json` retorna o NDJSON completo; `--include-nbest` acrescenta
todas as alternativas ao NDJSON.

Quando o produto das alternativas morfológicas de um texto ad hoc excede
`--max-product`, os decodificadores `dependency-mst` e `dependency-eisner`
usam um feixe de até 512 análises parciais. A busca é aproximada e o estado
indica `approximate` (ou `approximate-no-parse`); o diagnóstico mostra a
largura do feixe. A propagação e os candidatos originais continuam visíveis
no JSON. Nesse caso, `--include-nbest` lista somente as análises retidas pelo
feixe. As execuções de corpus e as demais estratégias mantêm o limite e a
enumeração exata.

Para listar os estados parciais rejeitados antes de construir árvores, use
`--text '...' --json --include-rejections`. Cada registro contém o prefixo de
candidatos, a alternativa que levou à rejeição e o motivo. O motivo
`beam-score-limit` indica apenas que o prefixo saiu dos 512 melhores por score;
ele pode ser gramaticalmente válido. Para reproduzir a planilha da frase longa:

```sh
python3 parsers_investigation/generate_rejection_spreadsheet.py
```

O script local requer `openpyxl` e grava
`parsers_investigation/reports/quae_res_rejeicoes.xlsx`.

Para comparar uma frase anotada, passe explicitamente o corpus JSON. O texto
precisa coincidir exatamente com o campo `text` de uma única fixture:

```sh
build/parsers/parsers_investigation/parsers_investigation \
  --text 'Petrus est bonus.' \
  --corpus parsers_investigation/corpus/agreement_fixtures.json
```

Sem `--text`, o executável mantém a execução por lote no corpus padrão e a
saída NDJSON existente. `--human` mantém a tabela compacta para comparar
estratégias.
