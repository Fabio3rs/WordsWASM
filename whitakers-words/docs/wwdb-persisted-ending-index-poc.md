# PoC A/B/C de stems e endings persistidos no WWDB

Executada em 10 de setembro de 2026. Esta PoC compara o WWDB 1.9 original, o
1.10 com stems persistidos e o 1.11 com stems mais endings persistidos. Os
outros seis índices de lookup continuam reconstruídos pelo loader.

> Status final em 11 de setembro de 2026: experimento arquivado. O runtime e o
> packer de produção terminam em WWDB 1.10; 1.11 permanece somente neste
> relatório e nos artefatos de benchmark.

## Resultado executivo

Endings entregou um ganho incremental pequeno, porém confirmado no nativo e no
Wasm: 0,63% a 1,41% no benchmark nativo e 0,55% a 1,57% nas duas rodadas
Node/Wasm. O Callgrind mediu reduções de 0,85% e 1,23%. O custo é 3.602 bytes
por WWDB e 2.323 bytes no módulo Wasm Release.

O resultado é marginal, bem diferente do ganho de stems. A variante 1.11 fica
útil como opção experimental, mas não cria evidência para persistir os outros
seis índices.

## Desenho do formato 1.11

A seção `ending_index_ranks` contém um `uint16` por regra, na ordem original
dos registros. Cada valor informa a posição da regra no índice canônico de
endings. Assim, inflections continuam em ordem legada e `RuleId` permanece
idêntico.

O loader aloca o vetor final, espalha os IDs diretamente pelos ranks, rejeita
ranks repetidos ou fora do intervalo e valida a ordenação por spelling
canônico seguida de `RuleId`. O vetor temporário `IndexedRule`, seu sort e
42.840 bytes transitórios deixam de existir.

Durante a PoC, o packer expunha as três variantes abaixo. As flags experimentais
de 1.10/1.11 não fazem parte da interface atual:

```sh
wwdb_poc_pack whitakers-words v9.wwdb dense
wwdb_poc_pack whitakers-words v10.wwdb dense --persist-stem-index
wwdb_poc_pack whitakers-words v11.wwdb dense \
  --persist-stem-and-ending-index
```

## Metodologia

O benchmark cronometra somente `Database::load_poc`; leitura do arquivo e
cópia de ownership ficam fora da região medida. A confirmação incremental
usou nove amostras intercaladas, com cinco warmups e duzentas cargas por
amostra, fixadas no CPU 0. Callgrind e DHAT mediram uma carga cada com o mesmo
executável Clang 21 `RelWithDebInfo`, `-O2` e frame pointers.

## Resultado incremental: 1.10 para 1.11

| Métrica | WWDB 1.10 | WWDB 1.11 | Variação |
| --- | ---: | ---: | ---: |
| mediana dense | 18,206 ms | 18,090 ms | -0,63% |
| mediana search-only | 11,848 ms | 11,681 ms | -1,41% |
| instruções dense | 206.111.353 | 204.367.046 | -0,85% |
| instruções search-only | 141.752.176 | 140.007.861 | -1,23% |
| bytes alocados dense | 10.998.282 | 10.962.710 | -0,32% |
| bytes alocados search-only | 7.410.776 | 7.375.204 | -0,48% |
| leituras de heap dense | 25.079.036 | 24.270.912 | -3,22% |
| escritas de heap dense | 15.970.182 | 15.687.308 | -1,77% |

A redução absoluta foi de 1.744.307 instruções no dense e 1.744.315 no
search-only. O arquivo maior acrescenta trabalho ao CRC, portanto a economia
bruta da remoção do sort é um pouco maior que a diferença líquida.

O tempo de parede é próximo do ruído: uma rodada A/B/C sem afinidade chegou a
mostrar regressão de 0,31% no search-only. Por isso a decisão se apoia também
na redução determinística de instruções, não somente na mediana fixada.

## Confirmação integrada em Node/Wasm

O runner `scripts/benchmark-wasm-database-load.mjs` instancia uma única vez a
build Wasm `Release` com `WORDS_WASM_PROFILING=ON` e mede
`AnalysisEngine.loadDatabase`. A região inclui a cópia `Uint8Array` -> Wasm, a
fronteira Embind e a criação da engine; exclui leitura do arquivo, instanciação
do módulo e `reset`. Cada rodada teve dez warmups e 101 pares A/B, em ordem
alternada, fixados no CPU 0, com Node 22.16.0 e V8 12.4.254.21.

| Perfil/rodada | WWDB 1.10 | WWDB 1.11 | Variação |
| --- | ---: | ---: | ---: |
| dense 1 | 34,347 ms | 33,940 ms | -1,19% |
| dense 2 | 31,743 ms | 31,243 ms | -1,57% |
| search-only 1 | 25,473 ms | 25,264 ms | -0,82% |
| search-only 2 | 25,549 ms | 25,408 ms | -0,55% |

O Inspector do V8 amostrou vinte cargas adicionais por variante. Os frames do
sort de `IndexedRule` apareceram em 14 samples dense e 11 search-only no 1.10,
e em zero samples no 1.11. Essa contagem é evidência qualitativa do hotpath;
os percentuais temporais vêm das coletas sem Inspector.

### Memória Wasm com a engine pronta

`--measure-memory` carregou cada banco em uma instância Wasm isolada e leu
`mallinfo()` depois da criação da engine. O perfil 1.11 mantém a seção de ranks
no `image_` e reconstrói o mesmo vetor final de `RuleId`s do 1.10; portanto não
há economia residente neste desenho.

| Perfil | Heap C++ 1.10 | Heap C++ 1.11 | Variação | Memória linear |
| --- | ---: | ---: | ---: | ---: |
| dense | 7.047.624 B | 7.051.232 B | +3.608 B (+0,051%) | sem alteração |
| search-only | 5.253.976 B | 5.257.584 B | +3.608 B (+0,069%) | sem alteração |

A memória linear permaneceu em 17.235.968 bytes nas quatro instâncias. No
DHAT nativo, a remoção do temporário de 42.840 bytes reduz os bytes alocados ao
longo da carga em 35.572 e elimina uma alocação, mas o máximo simultaneamente
vivo cresce 7.268 bytes por causa das cópias do arquivo maior. Assim, a versão
1.11 reduz churn de allocator, não o pico nem a memória de runtime.

O wrapper público não guarda o `Uint8Array` na engine retornada. Depois que a
Promise de criação termina, a cópia JavaScript pode ser coletada; os 3.608
bytes acima são o delta persistente dentro do Wasm.

Exemplo reproduzível:

```sh
node scripts/benchmark-wasm-database-load.mjs \
  --baseline build/perf-investigation-clang/profiles/wwdb-stem-ending-index/wwdb-v10.wwdb \
  --candidate build/perf-investigation-clang/profiles/wwdb-stem-ending-index/wwdb-v11.wwdb \
  --warmup 10 --samples 101 --profile-iterations 20 --measure-memory
```

## Comparação A/B/C estrutural

| Perfil | WWDB 1.9 | WWDB 1.10 stems | WWDB 1.11 stems + endings |
| --- | ---: | ---: | ---: |
| bytes dense | 2.731.947 | 2.731.947 | 2.735.549 |
| bytes search-only | 1.200.388 | 1.200.388 | 1.203.990 |
| instruções dense | 331.333.014 | 206.111.353 | 204.367.046 |
| instruções search-only | 266.973.837 | 141.752.176 | 140.007.861 |

De 1.9 para 1.11, a redução acumulada de instruções é 38,32% no dense e
47,56% no search-only. Quase todo esse resultado continua vindo de stems.

O aumento de 3.602 bytes corresponde a 3.570 bytes de ranks e uma entrada de
diretório de 32 bytes: +0,13% no dense e +0,30% no search-only. Contra a build
1.10, o código do benchmark nativo cresceu 2.972 bytes e o Wasm Release cresceu
de 860.438 para 862.761 bytes, +2.323 bytes ou 0,27%.

## Equivalência e guardrails

- duas gerações 1.11 produziram bytes idênticos;
- as 2.726 formas distintas da Eneida IV produziram JSON `search-v2` byte a
  byte idêntico entre 1.9, 1.10 e 1.11 nos dois perfis;
- dense e search-only continuaram semanticamente idênticos;
- trocar os ranks zero e 1.784, com CRC recalculado, foi rejeitado como
  `invalid-index-order`;
- o smoke test Wasm carregou os WWDBs dense e search-only 1.11.

## Decisão final

Stems foi promovido como WWDB 1.10. Endings foi rejeitado para produção: o
ganho determinístico fica ao redor de 1%, mas acrescenta formato, código e
memória residente. O schema, loader e opção de geração 1.11 foram removidos;
este documento preserva a implementação avaliada, os números e a justificativa.

Não persistir uniques ou addons com base neste resultado. Se o objetivo
imediato for performance, o CRC32 e a canonicalização continuam alvos maiores.

Os artefatos locais ficam em
`build/perf-investigation-clang/profiles/wwdb-stem-ending-index/` e
`build/wasm-profile/profiles/wwdb-stem-ending-index/`.
