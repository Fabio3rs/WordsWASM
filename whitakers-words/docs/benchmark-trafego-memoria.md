# Benchmark de tráfego de memória

Executado em `2026-08-28T18:11:16+00:00`.

## Configuração

- Plataforma: `Linux-6.14.0-1020-oem-x86_64-with-glibc2.39`
- Executável: `/mnt/projects/Projects/textllm/whitakers-words/bin/words`
- Corpus: `/mnt/projects/Projects/textllm/whitakers-words/test/01_aeneid/input.txt` (2.726 tokens únicos)
- Semente: `20260828`
- SHA-256 da maior amostra: `de2994261fdf01d5a98f76f6b438800d7970c5d60771ce53d8ab8f4d757dd7d2`
- Repetições nativas por tamanho: `7`; tabela usa a mediana
- Primeiras palavras: `rosae, rosarum, amo, procul, suo, venerem, timet, iove, cingunt, vade`
- Invocação: `words entrada.txt /dev/null`; o modo de dois arquivos desativa a paginação legada.
- Cada linha é uma consulta; o mesmo processo atende todo o lote.

O benchmark pode ser reproduzido com
[`benchmarks/memory_traffic.py`](../benchmarks/memory_traffic.py).

## Leitura rápida

- Inicialização em processo vazio: `2,943 ms` de CPU.
- No lote de 1.000: `0,438 ms` de CPU incremental por consulta e aproximadamente `2.284` consultas/s.
- RSS máximo observado: `6.812 KiB`; ele não cresce proporcionalmente ao lote.
- Cachegrind, descontando o lote vazio: cerca de `1.564.911` instruções e `1.397.952` acessos a dados por consulta.
- Preenchimentos simulados: `1,614 MiB` para L1 e `6,884 KiB` desde o último nível por consulta, em média.
- Rede por consulta depois de carregar o Worker: `0 bytes`.
- O passe regular do Ada lê `22.594.800` bytes lógicos de seções de `INFLECTS.SEC` no lote maior; releituras especiais não estão incluídas.

## CPU e RAM — medição nativa

Cada linha inicia um processo novo, mas consultas dentro do lote reutilizam a engine.
O cache de arquivos do sistema operacional não foi descartado.

| Consultas | Parede total (ms) | CPU total (ms) | CPU incremental/consulta (ms) | RSS máximo (KiB) | Falhas menores |
| ---: | ---: | ---: | ---: | ---: | ---: |
| 0 | 3,020 | 2,943 | — | 6.260 | 244 |
| 1 | 3,643 | 3,570 | 0,627 | 6.812 | 344 |
| 10 | 6,268 | 6,184 | 0,324 | 6.616 | 344 |
| 100 | 43,692 | 43,482 | 0,405 | 6.744 | 347 |
| 1.000 | 440,907 | 440,473 | 0,438 | 6.736 | 348 |

`CPU incremental/consulta` subtrai o lote vazio, portanto separa aproximadamente
o custo de inicialização do custo marginal de análise e formatação.

## Leituras lógicas de `INFLECTS.SEC`

Na inicialização, o Ada lê as cinco seções, totalizando 114.000 bytes. No passe
regular de cada palavra com uma letra final suportada, ele relê uma seção inteira
de 22.800 bytes. A tabela é um mínimo estrutural: packons, pronomes e novas
tentativas ortográficas podem provocar leituras adicionais.

| Consultas | Leituras regulares de seção | Bytes lógicos |
| ---: | ---: | ---: |
| 0 | 0 | 0 |
| 1 | 1 | 22.800 |
| 10 | 9 | 205.200 |
| 100 | 99 | 2.257.200 |
| 1.000 | 991 | 22.594.800 |

Esses bytes normalmente vêm do page cache, portanto não equivalem a tráfego de
disco nem aos preenchimentos de cache simulados abaixo.

## Tráfego CPU ↔ caches ↔ RAM — simulação Cachegrind

Cachegrind conta referências executadas e simula caches com linhas de 64 bytes.
Os volumes são `misses * 64`: representam preenchimentos de linha, não incluem
write-backs e não são uma medição do controlador de memória físico.

| Consultas | Instruções | Acessos a dados | Misses L1 (I+D) | Misses último nível | Linhas para L1 (MiB) | Linhas desde RAM (MiB) |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 0 | 13.421.571 | 4.658.497 | 72.306 | 23.956 | 4,413 | 1,462 |
| 1 | 15.508.819 | 6.259.999 | 102.601 | 32.850 | 6,262 | 2,005 |
| 10 | 27.855.574 | 17.915.130 | 328.955 | 33.418 | 20,078 | 2,040 |
| 100 | 169.912.704 | 144.453.672 | 2.716.452 | 34.970 | 165,799 | 2,134 |

## Rede — modelo WebAssembly

Na arquitetura proposta, as consultas acontecem no Worker depois do carregamento
do banco. Portanto, uma consulta adicional transfere **0 bytes de rede**. O custo
de rede é do carregamento inicial e do cache HTTP, não de cada palavra.

| Ativo legado usado como referência | Bruto (bytes) | gzip-9 determinístico (bytes) |
| --- | ---: | ---: |
| `DICTFILE.GEN` | 7.081.020 | 1.216.820 |
| `STEMFILE.GEN` | 3.476.816 | 380.852 |
| `INDXFILE.GEN` | 32.338 | 3.493 |
| `INFLECTS.SEC` | 114.000 | 9.048 |
| `UNIQUES.LAT` | 9.067 | 1.714 |
| `ADDONS.LAT` | 34.697 | 9.173 |
| **Total lexical atual** | **10.747.938** | **1.621.100** |

Esse total é apenas um substituto mensurável enquanto `words.wwdb` não existe.
`words.wasm`, loader JavaScript, cabeçalhos HTTP e interface web não estão incluídos.
Com Worker persistente: primeira sessão paga esse download uma vez; todos os lotes
da tabela continuam com 0 bytes adicionais de rede.

## Limites

- Processo novo não significa cache de disco frio; os dados podem estar no page cache.
- RSS mede páginas residentes do processo, não todo o page cache do kernel.
- Cachegrind é determinístico, mas simula caches; não mede largura de banda real.
- O executável inclui formatação da saída, mesmo gravando-a em `/dev/null`.
- A amostra é aleatória sobre palavras únicas de um corpus, não sobre todas as formas possíveis do léxico.
- Para medir o futuro WebAssembly será necessário repetir no navegador com `words.wasm` e `words.wwdb` reais.
