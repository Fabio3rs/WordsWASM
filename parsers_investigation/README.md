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
