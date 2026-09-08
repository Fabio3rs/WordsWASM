# Roadmap de expansão dos dicionários

Data-base da análise: 2026-09-03.

## Estado de partida

O corte de substantivos, verbos e adjetivos contém 54.802 pacotes de lema. A
comparação estrutural encontrou 30.318 pacotes ausentes do Words, 20.216
presentes sem ambiguidade e 4.268 com múltiplos alvos possíveis. A análise
semântica produziu 34.259 registros de revisão e propôs 18.895 componentes com
mais de uma entrada; 14.286 deles incluem um alvo Words. Permanecem 73.274
ocorrências isoladas nas filas de POS, número que não equivale a lexemas únicos
porque testemunhos sem POS podem aparecer em mais de uma fila.

O algoritmo selecionou 25.547 arestas semanticamente mútuas e únicas e rejeitou
1.879 por empate ou margem insuficiente. Há ainda 31.087 pares apoiados apenas
por quantidade e 26.526 sem ponte textual entre idiomas. Nenhuma dessas
contagens representa autorização automática de merge ou importação.

## Estratégia

O trabalho segue duas trilhas independentes até o momento da compilação:

```text
fontes imutáveis ── dump ── análise semântica ── decisões editoriais ─┐
                                                                    ├─ compilação ─ testes ─ release
formato WWDB atual ── benchmark u24/u32 ── formato escalável ────────┘
```

As decisões usam referências estáveis das fontes e revisões SHA-256, portanto
podem avançar antes da atribuição dos `LexemeId`s definitivos. Entradas novas só
chegam ao WWDB depois da migração dos IDs e offsets que hoje usam `u16`.

## M0 — Congelar o corte reproduzível

Entregas:

- manifesto com caminho lógico, tamanho, schema e SHA-256 de cada fonte;
- comando exato de geração dos dumps e versões dos scripts;
- relatórios N/V/ADJ arquivados fora de `/tmp` como artefatos regeneráveis;
- distinção explícita entre fonte independente, derivada e auxiliar;
- teste que abre todo SQLite externo com `immutable=1`.

Gate de saída: duas execuções sobre o mesmo corte produzem os mesmos IDs de
pacote, revisões e contagens.

## M1 — Calibrar o alinhamento semântico

Criar um conjunto-ouro estratificado, evitando medir apenas casos fáceis:

- homógrafos clássicos com oposição de quantidade;
- pares aceitos com score forte, moderado e próximo do limiar;
- 1.879 arestas rejeitadas por empate/margem;
- pares com alvo Words, sem alvo Words e com múltiplos alvos;
- substantivos, verbos e adjetivos em proporções conhecidas;
- pares PT–PT, EN–EN e casos sem ponte textual.

Metas iniciais para o conjunto-ouro:

- precisão mínima de 99,5% em componentes que sugerem merge com Words;
- precisão mínima de 99% nos demais componentes semanticamente propostos;
- 100% de bloqueio quando há empate entre alvos da mesma família;
- nenhuma fusão de dois homógrafos de uma mesma fonte;
- recall apenas como métrica de ranking, nunca à custa da precisão de merge.

Gate de saída: limiares e stopwords fixados em configuração versionada, com
relatório de falsos positivos e falsos negativos.

A camada vetorial implementada em
[`comparacao-semantica-embeddings.md`](comparacao-semantica-embeddings.md)
materializa essa calibração: Qwen3-Embedding-8B ranqueia sentidos dentro de
blocos lexicais e só altera componentes quando desenvolvimento e holdout
atingem a precisão exigida.

## M2 — Resolver primeiro os componentes ligados ao Words

Fila prioritária: os 14.286 componentes propostos que contêm exatamente um
alvo Words. Essa onda tende a melhorar qualidade sem aumentar o número de
lexemas.

Para cada decisão aceita:

- confirmar o sentido comum e o alvo Words;
- registrar `sense_summary` e fontes usadas;
- aplicar o consenso por atributo somente dentro do grupo confirmado;
- registrar dissenso de gênero, morfologia ou quantidade sem descartá-lo;
- separar correção de dado existente de criação de lexema novo.

Subfilas:

1. homógrafos diferenciados por quantidade e significado;
2. componentes com três autoridades primárias;
3. componentes com duas autoridades e alvo Words inequívoco;
4. merges dependentes de apenas uma ponte textual;
5. múltiplos alvos Words, que permanecem revisão de alta prioridade.

Gate de saída: ledger válido, sem revisões obsoletas, membros sobrepostos ou
colisões de família; todo merge possui exatamente um alvo Words.

## M3 — Migrar a capacidade do WWDB

Executar antes de compilar entradas novas em massa:

- benchmark comparativo de `u24` packed, ID de 19 bits no registro de 24 bits e
  `u32` alinhado;
- migrar `LexemeId`, referências de radical, boundaries/offsets e qualquer
  contador transitivamente limitado a 65.536;
- versionar o formato, mantendo erro claro para leitores incompatíveis;
- testar os perfis full e search-only, linhas e colunas;
- medir tamanho bruto/comprimido, tempo de carga, lookup e tráfego de memória;
- reservar capacidade explícita para novas ondas, não apenas para o corte
  atual.

Recomendação de gate: suportar pelo menos 250 mil lexemas e um milhão de
referências de radical sem nova mudança de formato. `u24` comporta essa margem;
o benchmark decide se o custo de decodificação é aceitável frente a `u32`.

Gate de saída: packer e runtime leem o novo formato, a suíte diferencial passa
e a simulação do maior lote editorial não alcança 80% da capacidade escolhida.

## M4 — Preparar novos lexemas corroborados

Ponto de partida: os 4.609 componentes multientrada propostos sem alvo Words
(18.895 totais menos 14.286 ligados ao Words). Essa subtração é uma estimativa
de fila, não uma contagem final de lexemas.

Uma entrada só avança se tiver:

- identidade semântica confirmada;
- POS e paradigma normalizados;
- radicais completos exigidos pela classe;
- gênero e flags especiais resolvidos ou explicitamente desconhecidos;
- quantidade por posição com consenso, fonte única ou conflito declarado;
- formas geradas validadas contra as tabelas morfológicas disponíveis;
- significado canônico curto e proveniência por campo.

Ondas sugeridas:

1. comuns, três fontes primárias e estrutura completa;
2. comuns, duas fontes primárias mais validação Latim–Alemão;
3. comuns sem quantidade completa, mantendo posições desconhecidas;
4. próprios, tardios, medievais e técnicos;
5. variantes, remissões e entradas com classificação contestada.

Gate de saída: o compilador aceita a onda inteira ou falha sem produzir WWDB
parcial; nenhuma entrada depende de inferência sem proveniência.

## M5 — Atacar a cauda sem ponte textual

Não usar tradução improvisada dentro do analisador determinístico. Para os
26.526 pares sem ponte textual:

- propagar pontes somente através de componentes já confirmados;
- avaliar embeddings multilíngues locais apenas como ranking de revisão;
- preservar modelo, versão e score como evidência reproduzível;
- usar quantidade e morfologia para desempate, nunca como prova isolada de
  identidade semântica;
- priorizar os 31.087 pares apoiados por quantidade quando ela distingue
  homógrafos conhecidos;
- manter o restante como singleton até surgir autoridade adicional.

Gate de saída: qualquer método novo é medido no mesmo conjunto-ouro de M1 e não
reduz a precisão da fila principal.

## M6 — Releases incrementais

Cada release deve conter uma única onda editorial identificável:

- manifesto das fontes e decisões;
- relatório de merges, novos lexemas, variantes, rejeições e adiamentos;
- variação de lexemas, referências de radical, strings e meanings;
- cobertura antes/depois num corpus fixo;
- regressão diferencial contra o snapshot anterior;
- tamanho e desempenho dos perfis WASM;
- amostra humana dos resultados novos e alterados.

Critérios de rollback: regressão morfológica, colisão estrutural, decisão
obsoleta, conflito quantitativo promovido ou perda relevante de desempenho.

## Indicadores permanentes

| Indicador | Objetivo |
| --- | --- |
| decisões presas à revisão atual | 100% |
| componentes aceitos com duas entradas da mesma família | 0 |
| merges com mais de um alvo Words | 0 |
| campos canônicos sem proveniência | 0 |
| vogais não marcadas convertidas implicitamente em breves | 0 |
| precisão do conjunto-ouro para merge | ≥ 99,5% |
| uso da capacidade escolhida após cada onda | < 80% |
| regressões no corpus diferencial | 0 não justificadas |

## Próxima ação concreta

Construir o conjunto-ouro de M1 antes de revisar os 18.895 componentes em
massa. Uma primeira amostra de 600 decisões é suficiente para calibrar:

- 200 componentes com alvo Words;
- 150 componentes sem alvo Words;
- 100 homógrafos ou conflitos de quantidade;
- 75 arestas rejeitadas por empate/margem;
- 75 casos sem ponte textual.

Depois da calibração, revisar a onda M2 em lotes pequenos e versionados enquanto
M3 mede e implementa o formato de IDs escalável.
