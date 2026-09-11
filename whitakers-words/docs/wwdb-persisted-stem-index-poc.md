# PoC A/B de índice de stems persistido no WWDB

Executada em 10 de setembro de 2026 e promovida ao formato padrão em 11 de
setembro de 2026. Esta PoC testou somente o maior sort do loader; os outros
índices de lookup continuam reconstruídos em runtime.

## Resultado executivo

Persistir a ordem canônica exata das 62.086 referências de stems no próprio
WWDB reduziu a mediana de `Database::load_poc` em 27,0% no perfil dense e
36,8% no search-only. As instruções caíram 37,8% e 46,9%, respectivamente.
O arquivo não cresceu: a PoC apenas permuta uma seção que não possui IDs
públicos e marca o novo invariante como WWDB 1.10.

O resultado aprovou o índice de stems. O packer gera 1.10 por padrão e o loader
conserva o caminho 1.9 para retrocompatibilidade.

## Desenho

A baseline 1.9 grava `stem_references` na ordem herdada por buckets de até duas
letras. O loader resolve cada referência para sua string, cria um vetor
temporário de `IndexedStem`, ordena pela chave canônica completa e então gera
`stem_groups_` e `stem_references_`.

A variante 1.10 conserva exatamente as mesmas seções, strides e contagens. O
packer ordena cada bucket pela chave completa com case folding e `j/i`, `v/u`,
seguida de `(LexemeId, lexical_slot, stem_key)`. Como os buckets já estão na
ordem dos primeiros caracteres canônicos, o conjunto completo fica ordenado.
O loader decodifica, valida monotonicidade e forma os grupos em uma passagem.

O caminho de produção e a geração legada para testes são:

```sh
wwdb_poc_pack whitakers-words production.wwdb dense
wwdb_poc_pack whitakers-words legacy.wwdb dense --legacy-stem-order
```

Reordenar `stem_references` é seguro porque a referência não possui identidade
pública. A PoC não reordena lexemas, regras ou addons; seus IDs permanecem
estáveis.

## Metodologia

O target `words_database_load_benchmark` lê o arquivo antes da região medida,
faz a cópia necessária para cada ownership independente fora do relógio e
cronometra somente `Database::load_poc`. O banco é destruído depois de parar o
relógio. Foram executadas sete amostras intercaladas, cada uma com dois warmups
e vinte loads, em Clang 21, `RelWithDebInfo`, `-O2` e frame pointers.

Callgrind mediu uma carga sem warmup, delimitada por client requests. DHAT
mediu uma execução completa sem warmup; os dois lados usam executáveis e
arquivos do mesmo tamanho.

## Resultados

### Perfil dense

| Métrica | WWDB 1.9 | Índice persistido 1.10 | Variação |
| --- | ---: | ---: | ---: |
| mediana por load | 25,241 ms | 18,431 ms | -26,98% |
| instruções | 331.284.695 | 206.111.497 | -37,78% |
| bytes DHAT | 12.488.460 | 10.998.398 | -11,93% |
| blocos DHAT | 128 | 127 | -1 |
| pico DHAT | 12.233.051 | 10.962.379 | -10,39% |
| leituras de heap | 56.470.747 | 25.079.066 | -55,59% |
| escritas de heap | 20.560.510 | 15.970.312 | -22,33% |
| tamanho WWDB | 2.731.947 | 2.731.947 | zero |

O Callgrind atribuiu 102.096.161 instruções ao loop principal do introsort e
24.014.269 ao seu insertion sort. A redução total de 125.173.198 instruções
corresponde à remoção desse trabalho e do manejo do vetor temporário.

### Perfil search-only

| Métrica | WWDB 1.9 | Índice persistido 1.10 | Variação |
| --- | ---: | ---: | ---: |
| mediana por load | 19,294 ms | 12,200 ms | -36,77% |
| instruções | 266.925.488 | 141.752.290 | -46,90% |
| bytes DHAT | 8.900.930 | 7.410.868 | -16,74% |
| blocos DHAT | 123 | 122 | -1 |
| pico DHAT | 8.645.521 | 7.374.849 | -14,70% |
| leituras de heap | 52.130.163 | 20.738.482 | -60,22% |
| escritas de heap | 15.178.901 | 10.588.703 | -30,24% |
| tamanho WWDB | 1.200.388 | 1.200.388 | zero |

A economia absoluta de Callgrind e DHAT é idêntica nos dois perfis, como
esperado: ambos contêm as mesmas 62.086 referências e eliminam exatamente o
mesmo `IndexedStem`.

### Custo de compatibilidade

O loader de produção contém os caminhos 1.9 e 1.10. Contra o último snapshot
registrado da mesma build, `.text` do benchmark da engine cresceu 1.796 bytes
(0,258%). O Wasm Release cresceu de 859.224 para 860.438 bytes, 1.214 bytes
(0,141%). Esse é custo de código, não de banco ou memória linear por consulta.

## Equivalência e corrupção

- duas gerações 1.10 produziram bytes idênticos;
- dense e search-only conservaram exatamente seus tamanhos 1.9;
- as 2.726 formas distintas da Eneida IV produziram JSON `search-v2`
  byte a byte idêntico entre 1.9 e 1.10 nos dois perfis;
- dense e search-only continuaram semanticamente idênticos;
- o harness da engine manteve 4.570 unidades, 6 snapshots, 18.245 análises e
  checksum 22.821;
- uma seção 1.10 com primeiro e último registros trocados, mas CRC recalculado,
  foi rejeitada como `invalid-index-order`;
- o build Wasm Release carregou o dense 1.10 e preservou checksum 289 no
  microcorpus de compostos.

O teste `words_persisted_stem_index` regenera ambos os perfis em diretório
temporário, verifica determinismo, compara o corpus e testa a corrupção. O
teste unitário também prova que um arquivo 1.9 não pode apenas declarar a
versão 1.10 sem satisfazer o novo invariante.

## Decisão

A hipótese foi incorporada à produção: WWDB 1.10 é a saída padrão do packer e
dos fixtures dense/search. O runtime aceita 1.9 e 1.10; `--legacy-stem-order`
existe somente para testar a leitura retrocompatível. A versão 1.11 foi
avaliada separadamente e não foi incorporada.

Os artefatos locais ficam em
`build/perf-investigation-clang/profiles/wwdb-stem-index/`.

O follow-up que adiciona somente endings à variante de stems está documentado
em [`wwdb-persisted-ending-index-poc.md`](wwdb-persisted-ending-index-poc.md).
