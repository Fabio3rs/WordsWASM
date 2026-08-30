# Proposta de migração do Whitaker's WORDS para WebAssembly

Este documento apresenta uma arquitetura possível para executar o analisador
morfológico do Whitaker's WORDS inteiramente no navegador. Ele complementa a
descrição do algoritmo em
[`algoritmo-analise-morfologica.md`](algoritmo-analise-morfologica.md).
As decisões de normalização Unicode e representação futura de quantidade
vocálica estão em
[`plano-unicode-quantidade-vocalica.md`](plano-unicode-quantidade-vocalica.md).
O detalhamento de módulos, classes, ownership, targets CMake e fluxo de uma
consulta está em
[`arquitetura-engine-cpp23.md`](arquitetura-engine-cpp23.md).

A recomendação principal é preservar os dados linguísticos históricos, manter
o executável Ada atual como referência de comportamento e portar somente o
motor morfológico para uma biblioteca C++ compilável tanto nativamente quanto
para WebAssembly.

## Objetivos

- Executar a análise sem servidor e, depois do primeiro carregamento, permitir
  uso offline.
- Preservar todas as análises ambíguas produzidas pelo programa original.
- Manter `DICTLINE.GEN`, `INFLECTS.LAT`, `UNIQUES.LAT` e `ADDONS.LAT` como
  fontes canônicas dos dados linguísticos.
- Separar o motor morfológico da interface de terminal e da formatação textual.
- Validar o novo motor por comparação sistemática com o executável Ada.
- Permitir que o mesmo núcleo C++ seja testado nativamente e compilado para o
  navegador.
- Aceitar futuramente mácrons e breves sem alterar o índice lexical: entrada
  ASCII permanece sem restrição de quantidade e compatível com o legado.

## Alternativas

| Caminho | Vantagem | Risco principal |
| --- | --- | --- |
| Ada → GNAT-LLVM → WASM | Preserva a implementação quase integralmente | Runtime Ada e camada de I/O para o alvo WASM |
| C++ → Emscripten → WASM | Toolchain web madura e API simples com JavaScript | Necessidade de reproduzir fielmente as regras |
| Ada em servidor + API HTTP | Menor esforço inicial | Não funciona localmente/offline no navegador |

O [GNAT-LLVM](https://github.com/AdaCore/gnat-llvm) consegue gerar LLVM
bitcode, e o LLVM possui um
[backend WebAssembly](https://www.llvm.org/docs/doxygen/dir_96ba75976c22f2500bfbc06f8c4c2b70.html).
Isso torna plausível uma prova de conceito Ada → WASM. Não significa, porém,
que esta aplicação possa ser compilada sem adaptações: seria necessário um
runtime Ada adequado e suporte suficiente às operações usadas pelo programa.

Neste repositório, os principais obstáculos são:

- uso intenso de `Ada.Text_IO` e `Ada.Direct_IO`;
- arquivos binários gerados a partir da representação de tipos Ada;
- estado global em diversos pacotes;
- interface de terminal acoplada à preparação da saída;
- dependência de exceções e de partes relativamente amplas do runtime Ada.

Assim, Ada → WASM é indicado como experimento de preservação. Para uma
aplicação web mantível, a rota C++ → Emscripten é mais previsível.

## Arquitetura proposta

```text
Interface web
     │
     │ postMessage({ operation: "analyze" | "search", word: "rosae" })
     ▼
Web Worker
     │
     │ ABI C: ponteiros, tamanhos e handles
     ▼
words.wasm
  ┌─────────────────────────────┐
  │ normalização                │
  │ enumeração de desinências   │
  │ busca de radicais           │
  │ compatibilidade morfológica │
  │ construção do lema          │
  └─────────────────────────────┘
     │
     │ span somente para leitura
     ▼
words.wwdb
banco lexical portátil e versionado
```

A arquitetura oferece duas superfícies sem misturá-las: análise completa,
carregada com significados e metadados, e busca enxuta, ligada por `datasetId`
ao índice editorial do site. O perfil `search-only` atende apenas a segunda.

A aplicação pode ser distribuída como arquivos estáticos:

```text
index.html
app.js
words-worker.js
words.js
words.wasm
words.wwdb
```

O Worker impede que o carregamento do banco e análises extensas bloqueiem a
interface. O próprio Worker pode baixar o módulo e o banco em paralelo.

Emscripten suporta módulos assíncronos e isolados por meio de `MODULARIZE`,
assim como diferentes formas de conectar C++ e JavaScript. Consulte a
[documentação de modularização](https://emscripten.org/docs/compiling/Modularized-Output.html)
e a
[documentação de integração C++/JavaScript](https://emscripten.org/docs/porting/connecting_cpp_and_javascript/Interacting-with-code.html).

## O banco lexical

### Por que não usar diretamente os binários atuais

Os artefatos de dados usados pela análise latina ocupam exatamente 10.747.938
bytes nesta cópia do repositório:

| Arquivo | Bytes brutos | Bytes com gzip-9 |
| --- | ---: | ---: |
| `DICTFILE.GEN` | 7.081.020 | 1.215.369 |
| `STEMFILE.GEN` | 3.476.816 | 380.460 |
| `INDXFILE.GEN` | 32.338 | 3.506 |
| `INFLECTS.SEC` | 114.000 | 9.061 |
| `UNIQUES.LAT` | 9.067 | 1.726 |
| `ADDONS.LAT` | 34.697 | 9.184 |
| **Total dos dados** | **10.747.938** | **1.619.306** |

O total comprimido da tabela corresponde à soma de seis respostas HTTP
comprimidas separadamente. A concatenação dos seis arquivos em um único fluxo
e sua compressão com gzip-9 resultou em 1.622.139 bytes. A diferença pequena é
causada pelos contextos e cabeçalhos de compressão. O futuro `words.wwdb` terá
outro layout e, portanto, seu tamanho comprimido deverá ser medido novamente.

### Orçamento de fetch da engine

`bin/words` é o executável Ada que contém a engine atual. Seu tamanho também
foi medido:

| Artefato | Bytes brutos | Bytes com gzip-9 |
| --- | ---: | ---: |
| `bin/words`, como está no repositório | 2.755.536 | 768.760 |
| `bin/words`, cópia sem símbolos de debug | 1.613.824 | 484.272 |

Somando o executável atual, sem alterá-lo, aos seis arquivos de dados:

| Referência | Bytes brutos | Bytes com gzip-9 em respostas separadas |
| --- | ---: | ---: |
| engine Ada atual + dados latinos | **13.503.474** | **2.388.066** |

Esses 2.388.066 bytes são uma referência de compressibilidade do sistema
atual, não uma previsão exata do download WebAssembly. Há três motivos:

1. `bin/words` é um ELF x86-64, e o navegador não fará fetch nem executará esse
   arquivo; ele será substituído por `words.wasm` e por um pequeno loader JS.
2. O ELF atual contém informações de debug e não está stripped.
3. Ele é dinamicamente ligado a `libgnat-13`, `libgcc_s`, `libc` e `libm`.
   Logo, seus 2.755.536 bytes não incluem todo o runtime necessário a um módulo
   WASM autocontido. Por outro lado, o linker WebAssembly poderá eliminar
   partes não usadas do runtime.

O orçamento real de transferência será:

```text
fetch_total = wire(words.js)
            + wire(words.wasm)
            + wire(words.wwdb)
            + wire(interface web)
```

Para o núcleo, sem contar a interface web, enquanto `words.wasm` e
`words.wwdb` não existirem a única fórmula mensurável é:

```text
fetch_núcleo ≈ tamanho_comprimido(words.wasm)
             + tamanho_comprimido(words.wwdb)
             + loader JavaScript
```

Usando os dados atuais sem repacotamento, a parcela lexical dessa fórmula é
aproximadamente **1.619.306 bytes com gzip-9**. O tamanho do módulo WASM só
poderá ser informado corretamente depois que uma das duas rotas — GNAT-LLVM ou
C++/Emscripten — produzir um módulo de release.

Por isso, o primeiro build WebAssembly deve registrar pelo menos:

- tamanho bruto e comprimido de `words.wasm`;
- tamanho bruto e comprimido de `words.wwdb`;
- tamanho do loader JS;
- total transferido observado no painel de rede do navegador;
- pico de memória durante o carregamento;
- memória retida depois que o buffer JavaScript original for coletado.

Uma medição reproduzível da engine Ada atual, com lotes aleatórios, RSS e
simulação Cachegrind, está em
[`benchmark-trafego-memoria.md`](benchmark-trafego-memoria.md). Ela serve como
linha de base; os números WebAssembly ainda dependerão dos artefatos
`words.wasm` e `words.wwdb` reais.

Os valores comprimidos acima foram obtidos localmente com `gzip -9`. Brotli
não foi medido neste ambiente; se for habilitado no servidor, seus números
devem ser registrados separadamente em vez de estimados a partir do gzip.

`DICTFILE.GEN`, `STEMFILE.GEN` e `INFLECTS.SEC` são produzidos e lidos com
instanciações de `Ada.Direct_IO`. Sua representação pode depender do
compilador, do runtime, do tamanho de enums, do alinhamento e de outras
decisões de ABI. Ler os arquivos diretamente como estruturas C++ ou estruturas
Ada compiladas para outro alvo seria frágil.

O layout exato dos arquivos presentes nesta cópia, incluindo structs C/C++
para auxiliar uma conversão controlada, está registrado em
[`formato-binario-dados.md`](formato-binario-dados.md). Essa descrição é do
legado observado, não uma especificação portátil para `words.wwdb`.

Também não se deve resolver o problema serializando `struct` C++ diretamente,
pois isso apenas transfere o risco para padding, alinhamento e endianness do
compilador C++.

### Gerador de banco portátil

Um programa nativo, provisoriamente chamado `wwpack`, transformaria os dados
textuais em uma imagem binária explicitamente definida:

```text
DICTLINE.GEN ─┐
INFLECTS.LAT ─┤
UNIQUES.LAT ──┼── wwpack ──> words.wwdb
ADDONS.LAT ───┘
```

O formato deve especificar:

- magic number, por exemplo `WWDB`;
- versão do formato;
- tamanho total;
- checksum da imagem inteira, calculado com o próprio campo zerado;
- inteiros de tamanho fixo;
- endianness definida, preferencialmente little-endian;
- offsets relativos de 32 bits em vez de ponteiros;
- alinhamento de cada seção;
- quantidade e tamanho de todos os registros;
- validação obrigatória de limites no carregamento.

Uma organização possível é:

```text
+--------------------------+
| Header                   |
| magic / version          |
| file size / checksum     |
+--------------------------+
| Section directory        |
| offset + count/size      |
+--------------------------+
| Inflection records       |
+--------------------------+
| Ending indexes           |
+--------------------------+
| First-two-letter index   |
+--------------------------+
| Sorted stem records      |
+--------------------------+
| Lexeme records           |
+--------------------------+
| Addons and uniques       |
+--------------------------+
| UTF-8 string pool        |
+--------------------------+
```

Um registro de radical poderia ser representado conceitualmente como:

```cpp
struct StemRecord {
    std::uint32_t text_offset;
    std::uint32_t lexeme_index;
    std::uint8_t stem_key;
    std::uint8_t part_of_speech;
};
```

Esse exemplo não implica gravar a memória bruta da `struct`. Cada campo deve
ser escrito e lido segundo o formato especificado.

O pool de strings reduz a duplicação de radicais, formas de dicionário e
significados. Índices equivalentes aos atuais intervalos de duas letras podem
ser mantidos, pois já se ajustam bem ao algoritmo existente.

## Carregamento direto na memória linear

Não é necessário expor `words.wwdb` como arquivo dentro do WASM. O JavaScript
pode baixar o banco, pedir que o módulo aloque uma região e copiar os bytes
para `WebAssembly.Memory`.

```text
fetch("words.wwdb")
       │
       ▼
ArrayBuffer do navegador
       │ uma cópia
       ▼
WebAssembly.Memory
       │ ponteiro + tamanho
       ▼
Engine C++ lê diretamente
```

Um módulo WASM não pode, em geral, dereferenciar diretamente um `ArrayBuffer`
JavaScript arbitrário, pois ele só endereça sua memória linear. A integração
requer uma cópia para `WebAssembly.Memory`. Depois dessa cópia, o C++ pode
manter uma visão zero-copy do banco durante toda a vida do motor.

### ABI de carregamento

É preferível dar ao módulo a responsabilidade pela alocação e pelo tempo de
vida do banco:

```cpp
extern "C" {

std::uint8_t* ww_database_allocate(std::uint32_t size);

int ww_database_commit(
    const std::uint8_t* data,
    std::uint32_t size
);

void ww_database_release();

}
```

`ww_database_release` libera tanto uma alocação ainda não confirmada quanto o
banco ativo. Assim, todos os caminhos de falha depois de `allocate` possuem uma
operação de limpeza definida.

Fluxo JavaScript:

```js
const response = await fetch("/assets/words.wwdb");
if (!response.ok) {
  throw new Error(`Falha ao baixar banco lexical: HTTP ${response.status}`);
}
const database = new Uint8Array(await response.arrayBuffer());

const pointer = module._ww_database_allocate(database.byteLength);

if (pointer === 0) {
  throw new Error("Não foi possível alocar o banco lexical");
}

// Obter HEAPU8 depois da alocação, porque ela pode aumentar a memória.
module.HEAPU8.set(database, pointer);

const status = module._ww_database_commit(
  pointer,
  database.byteLength
);

if (status !== 0) {
  module._ww_database_release();
  throw new Error(`Banco lexical inválido: ${status}`);
}
```

Depois do `commit`, o `ArrayBuffer` original pode ser coletado pelo JavaScript.
O banco permanece na memória do WASM até `ww_database_release`.

### Leitura sem segunda desserialização

O motor pode manter um `std::span` sobre a imagem binária:

```cpp
class Database {
public:
    explicit Database(std::span<const std::byte> bytes)
        : bytes_(bytes)
    {
        validate_header();
        initialize_sections();
    }

private:
    std::span<const std::byte> bytes_;
};
```

Para máxima segurança, os acessores devem ler campos explicitamente:

```cpp
std::uint32_t read_u32_le(
    std::span<const std::byte> data,
    std::size_t offset
);
```

Isso continua sendo zero-copy. Somente os campos consultados são decodificados;
o banco inteiro não é recriado em objetos C++.

É possível usar `span<const Record>` sobre determinadas seções se o formato
garantir alinhamento, layout e representação. Mesmo nesse caso, offsets devem
ser validados antes da criação dos spans.

### Crescimento da memória

Se o módulo for compilado com `-sALLOW_MEMORY_GROWTH=1`, uma alocação pode
executar `memory.grow`. Os endereços lineares usados pelo código WASM continuam
válidos, mas uma `TypedArray` JavaScript obtida anteriormente pode apontar para
um `ArrayBuffer` antigo.

Evitar:

```js
const heap = module.HEAPU8;
const pointer = module._ww_database_allocate(size);
heap.set(bytes, pointer); // `heap` pode estar desatualizado
```

Preferir:

```js
const pointer = module._ww_database_allocate(size);
module.HEAPU8.set(bytes, pointer);
```

A mesma regra deve ser aplicada ao ler resultados depois de uma chamada ao
motor: a view deve ser obtida novamente do `Module`.

### Escopo do carregamento inicial

A primeira versão usa `response.arrayBuffer()` e uma única cópia para a memória
linear. Carregamento incremental não é requisito do projeto. Além de aumentar a
complexidade, um fluxo baseado diretamente em `Content-Length` seria incorreto
sob `Content-Encoding`, pois o tamanho transferido pode diferir do tamanho
decodificado entregue por Fetch. Qualquer otimização futura deve partir de uma
medição de pico de memória e do `file_size` validado no próprio WWDB.

O filesystem virtual e `--preload-file` continuam úteis para uma prova de
conceito. O Emscripten documenta o mecanismo em
[Packaging Files](https://emscripten.org/docs/porting/files/packaging_files.html).
Para a versão final, a passagem direta de ponteiro e tamanho é mais simples e
evita `open`, `read`, `seek` e a montagem de MEMFS.

## Motor C++

Esta seção mantém a visão resumida. O desenho implementável e atualizado está
em [`arquitetura-engine-cpp23.md`](arquitetura-engine-cpp23.md).

O núcleo deve ser uma biblioteca sem dependência de navegador:

```cpp
class Engine {
public:
    explicit Engine(Database database);

    std::vector<Analysis>
    analyze(std::string_view word) const;
};
```

Estrutura sugerida:

```text
cpp/
  include/words/
    database.hpp
    engine.hpp
    result.hpp
    types.hpp

  src/
    normalization.cpp
    orthography.cpp
    inflections.cpp
    lexicon.cpp
    compatibility.cpp
    dictionary_form.cpp
    addons.cpp
    engine.cpp
    wasm_api.cpp

  tools/
    wwpack.cpp
```

### Escopo inicial do port

Portar:

- enumeração de radical e desinência de `Run_Inflections`;
- busca de radicais de `Dictionary_Search`;
- compatibilidade morfológica de `Reduce_Stem_List`;
- construção do lema de `Dictionary_Form`;
- tipos de regra, entrada lexical e análise;
- normalização `u`/`v` e `i`/`j`;
- validação UTF-8, NFC/NFD e representação triestado de quantidade vocálica,
  ainda que todas as regras do primeiro banco comecem como desconhecidas.

Adiar:

- interface interativa;
- paginação e formatação de terminal;
- busca inglês → latim;
- estatísticas;
- parâmetros históricos de apresentação;
- composição de múltiplas palavras.

Depois da análise regular, adicionar em etapas:

- `UNIQUES`;
- pronomes especiais;
- prefixos e sufixos;
- enclíticos;
- síncope;
- transformações ortográficas (*tricks*).

### Estado do motor

A base lexical deve ser imutável depois do carregamento. Dados temporários de
uma consulta devem ficar em um workspace local:

```cpp
struct AnalysisWorkspace {
    std::vector<InflectionCandidate> inflections;
    std::vector<StemCandidate> stems;
    std::vector<Analysis> results;
};
```

Isso substitui arrays globais e limites históricos como 80, 100 e 250
registros, além de facilitar consultas concorrentes ou múltiplas instâncias do
módulo.

## API de análise

Para a primeira versão, uma ABI C pequena com dois resultados JSON separados é
suficiente:

```cpp
extern "C" {

std::uint32_t ww_analyze_json(
    const char* word,
    std::uint32_t word_size
);

std::uint32_t ww_search_json(
    const char* word,
    std::uint32_t word_size
);

const char* ww_result_data(std::uint32_t handle);
std::uint32_t ww_result_size(std::uint32_t handle);
void ww_result_free(std::uint32_t handle);
int ww_last_error();

}
```

O contrato de `ww_analyze_json` está em
[`formato-json-canonico.md`](formato-json-canonico.md), com schema validável em
[`../../schemas/analysis-v1.schema.json`](../../schemas/analysis-v1.schema.json).
Uma resposta possui este envelope:

```json
{
  "schema": "whitakers-words.analysis",
  "schemaVersion": 1,
  "query": {
    "text": "zzzzzz",
    "normalized": "zzzzzz",
    "mode": "latin"
  },
  "status": "unknown",
  "analyses": [],
  "diagnostics": []
}
```

Cada item separa identidade e metadados do lexema, segmentação da forma,
morfologia resolvida e caminho de derivação. A forma de citação reconstruída é
chamada de `dictionaryForm`, não `lemma`. Na base atual, `rosae` produz nove
análises — quatro participiais de `rodo` e cinco nominais de `rosa` — e todas
devem ser preservadas no resultado completo.

`ww_search_json` usa o contrato enxuto de
[`formato-json-busca.md`](formato-json-busca.md), validável por
[`../../schemas/search-v1.schema.json`](../../schemas/search-v1.schema.json).
Ele devolve IDs densos locais ao `datasetId`, sem repetir significados ou
conteúdo editorial. Não é substituto do documento completo no *harness*
diferencial.

Consulta vazia, com espaços internos ou UTF-8 inválido produz, quando houver
memória para serializá-la, um envelope JSON com `status = "error"`. Falhas da
própria ABI, como banco ausente, handle inválido ou falta de memória, usam um
handle nulo e um código consultável separadamente; não se deve fingir que foi
possível construir um documento JSON nesses casos.

JSON cria algum custo de serialização e parsing, mas oferece uma fronteira
simples, inspecionável e fácil de testar. Caso medições demonstrem que essa
fronteira é relevante, ela pode ser substituída por buffers binários ou
`TypedArray` sem alterar o motor.

[Embind](https://emscripten.org/docs/porting/connecting_cpp_and_javascript/embind.html)
também pode expor classes, vetores e strings. Uma ABI C é preferível para o
núcleo porque torna ownership, alocação e compatibilidade mais explícitos e
reduz o acoplamento ao Emscripten.

Uma função `analyze_many` deve ser considerada para analisar várias palavras
com uma única travessia JavaScript/WASM.

## Inicialização no Worker

O módulo e o banco podem ser baixados em paralelo:

```js
import createWordsModule from "./words.js";

const [module, response] = await Promise.all([
  createWordsModule(),
  fetch("/assets/words.wwdb")
]);

if (!response.ok) {
  throw new Error(`Falha ao baixar words.wwdb: HTTP ${response.status}`);
}

const bytes = new Uint8Array(await response.arrayBuffer());
const pointer = module._ww_database_allocate(bytes.byteLength);

if (pointer === 0) {
  throw new Error("Falha de memória ao carregar words.wwdb");
}

module.HEAPU8.set(bytes, pointer);

if (module._ww_database_commit(pointer, bytes.byteLength) !== 0) {
  module._ww_database_release();
  throw new Error("words.wwdb inválido ou incompatível");
}

postMessage({ type: "ready" });
```

É melhor executar o `fetch` no próprio Worker. Caso a thread principal já
possua o `ArrayBuffer`, ele pode ser transferido ao Worker sem cópia entre
agentes JavaScript:

```js
worker.postMessage(
  { type: "database", buffer },
  [buffer]
);
```

Ainda será necessária a cópia final para `WebAssembly.Memory`.

## Estratégia de validação

O maior risco da migração não é a geração do WASM, mas uma reimplementação que
elimine ou introduza análises morfológicas.

O executável Ada deve ser tratado como oráculo:

```text
entrada ──> Ada atual ──> documento canônico A
   │
   └──────> C++ nativo ─> documento canônico B

                  comparar A e B
```

Antes do port, definir um formato canônico de resultado, preferencialmente
JSON. A comparação deve validar o schema, aplicar a ordenação canônica e
comparar multiconjuntos: a ordem incidental e a formatação de terminal não
importam, mas a multiplicidade e caminhos distintos de derivação importam.

Etapas propostas:

1. Capturar resultados do Ada para os testes existentes.
2. Ampliar os testes já existentes do leitor `dense` do PoC e da validação de
   limites para os demais perfis e para o WWDB definitivo.
3. Implementar substantivos e adjetivos regulares.
4. Comparar lema, classe, declinação, variante, caso, número e gênero.
5. Implementar verbos, particípios, pronomes e numerais.
6. Adicionar formas únicas, afixos, enclíticos, síncope e *tricks*.
7. Executar o mesmo conjunto no C++ nativo e no WASM.
8. Investigar qualquer diferença antes de mudar as expectativas.

Além dos casos existentes, podem ser geradas formas de teste a partir da
combinação de entradas lexicais e regras de `INFLECTS.LAT`. Casos ambíguos,
como `rosae`, devem obrigatoriamente manter todas as análises válidas.

## Fases de implementação

### Fase 0 — contrato de comportamento

- Definir o modelo `Analysis`.
- Definir o JSON completo de análise e o JSON enxuto de busca.
- Criar um harness que invoque o executável atual.
- Registrar casos conhecidos de ambiguidade e de afixos.

### Fase 1 — banco portátil

- Especificar normativamente `words.wwdb`, sem depender do código do packer.
- Implementar `wwpack`.
- Validar magic, versão, checksum da imagem inteira, offsets e tamanhos.
- Evoluir o leitor C++23 nativo já existente para o WWDB definitivo e para os
  perfis de busca/significados separados.

### Fase 2 — motor C++ nativo (implementada)

- Validar e normalizar Unicode e portar as flexões.
- Portar índice e busca de radicais.
- Portar compatibilidade morfológica.
- Portar construção da forma de dicionário.
- Obter paridade com os casos regulares.

### Fase 3 — comportamento histórico

- Formas únicas.
- Afixos e enclíticos.
- Pronomes especiais.
- Síncope e *tricks*.
- Filtros de época e frequência, se desejados na API.

### Fase 4 — WebAssembly

- Adicionar a ABI C para análise completa e busca enxuta.
- Compilar com Emscripten.
- Carregar `words.wwdb` por ponteiro e tamanho.
- Executar no Worker.
- Comparar os resultados nativos e WASM.

### Fase 5 — otimização

- Medir tempo de inicialização, memória e latência por consulta.
- Compactar o pool de strings.
- Avaliar `analyze_many`.
- Servir `words.wwdb` com compressão HTTP e cache versionado.
- Só substituir JSON se as medições justificarem.

## Decisão recomendada

O caminho de produção recomendado é:

```text
dados históricos preservados
        +
packer determinístico
        +
motor C++ nativo com testes diferenciais
        +
Emscripten/WebAssembly em Web Worker
```

Uma prova de conceito GNAT-LLVM → WASM ainda é útil para medir o estado do
runtime Ada e documentar a possibilidade de preservação integral. Ela não deve
bloquear o desenvolvimento do formato portátil, pois `words.wwdb` é útil nos
dois cenários.

A passagem direta `{ponteiro, tamanho}` deve ser a interface principal do banco
lexical. Ela elimina a dependência de filesystem no navegador, permite leitura
zero-copy depois da transferência para a memória linear e mantém o módulo WASM
independente da forma como o navegador baixa ou armazena o arquivo.
