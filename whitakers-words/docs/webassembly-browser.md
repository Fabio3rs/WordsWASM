# Engine de análise no navegador

Data do corte: 2026-08-29.

A mesma `words::Engine` C++23 usada pelo CLI está disponível no navegador por
uma fronteira Embind pequena. A camada WebAssembly não reimplementa lexer,
morfologia, quantidade vocálica ou serialização: ela carrega um snapshot WWDB
e chama os exportadores canônico e de busca do núcleo.

## Fluxo e ownership

```mermaid
flowchart LR
    F[fetch WWDB] --> U[Uint8Array]
    U -->|uma cópia no load| V[vector de byte no WASM]
    V --> D[Database imutável]
    D --> E[words::Engine]
    Q[string UTF-8] --> E
    E --> A[analysis JSON string]
    E --> S[search JSON string]
    A --> P1[JSON.parse]
    S --> P2[JSON.parse]
```

O único bloco binário da API é o dataset serializado. O JavaScript entrega um
`Uint8Array`; o C++ aloca o `std::vector<std::byte>` e usa uma
`typed_memory_view` temporária para copiá-lo. Depois do `load`, o `Database`
mantém ownership dessa imagem e os seus `std::span`/`std::string_view`
continuam válidos durante toda a vida da engine.

Não são publicados `_malloc`, `_free`, `HEAPU8`, `ccall` ou `cwrap`. O
allocator e a memória linear continuam existindo internamente no módulo, mas
o host não administra ponteiros. Isso elimina pares públicos `pointer +
length` e a possibilidade de consulta depois de `free`.

O carregamento nativo é transacional: um candidato só substitui a base ativa
depois de `Engine::create` validar a imagem completa. Uma tentativa de reload
inválida não descarta o snapshot anterior.

## API de alto nível

[`wasmsrc/words-engine.mjs`](../../wasmsrc/words-engine.mjs) expõe
`createWordsAnalysisEngine`. Ela carrega o módulo, busca ou recebe o banco,
verifica o resultado do loader e converte os JSONs do C++ em objetos
JavaScript.

```javascript
import {createWordsAnalysisEngine} from "./words-engine.mjs";

const release = await fetch("./manifest.json").then((response) => response.json());
const engine = await createWordsAnalysisEngine({
  databaseUrl: release.databases.full.file,
  datasetId: release.datasetId,
});

const complete = engine.analyze("mālum");
const compact = engine.search("anaticulus");
const suggestion = engine.analyze("texto", {twoWords: true});

engine.dispose();
```

Para uma interface que só precisa de resultados enxutos, basta trocar o
arquivo selecionado no manifesto:

```javascript
const searchEngine = await createWordsAnalysisEngine({
  databaseUrl: release.databases.search.file,
  datasetId: release.datasetId,
});
console.log(searchEngine.databaseKind); // "search"
const compactOnly = searchEngine.search("cuique");
```

`analyze` devolve o contrato `whitakers-words.analysis`, versão 1;
`search` devolve `whitakers-words.search`, versão 1. O segundo argumento
`{twoWords: true}` habilita somente a sugestão legada opt-in. Consultas de dois
tokens pertencentes à gramática fechada de compostos continuam sendo
reconhecidas normalmente pelo núcleo.

`databaseKind` informa `"full"` ou `"search"`. `analyze()` exige o banco
full e falha antes de consultar o núcleo quando a projeção search está ativa;
`search()` funciona nas duas projeções com o mesmo contrato e espaço de IDs.

`dispose()` é idempotente e deve ser chamado quando a aplicação não precisar
mais do snapshot. A wrapper bloqueia consultas depois do descarte. Erros
linguísticos, inclusive UTF-8 ou caractere não suportado, continuam sendo
resultados estruturados. Configuração inválida, download malsucedido e WWDB
corrompido rejeitam a criação da engine.

Também é possível passar `databaseBytes: Uint8Array|ArrayBuffer`, útil para
cache próprio, Service Worker ou teste. `moduleFactory`, `moduleOptions` e
`fetchImpl` são pontos de integração opcionais; a morfologia não é
customizável por JavaScript.

## Build e artefatos

```bash
emcmake cmake -S . -B build/wasm -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DENABLE_TESTS=OFF \
  -DENABLE_SANITIZERS=OFF
cmake --build build/wasm
```

O diretório contém:

- `words_wasm.mjs`: glue ES module gerado pelo Emscripten;
- `words_wasm.wasm`: engine e bindings nativos;
- `words_wasm.d.ts`: interface Embind de baixo nível gerada;
- `words-engine.mjs`: wrapper estável de alto nível;
- `words-engine.d.ts`: interface de alto nível da aplicação;
- `words-full.wwdb`: morfologia, metadados e significados;
- `words-search.wwdb`: morfologia e índices, sem textos editoriais;
- `dataset-manifest.json`: fontes canônicas que definem o espaço de IDs;
- `manifest.json`: mapa, tamanhos e hashes dos artefatos publicados.

`words-full.wwdb` é o perfil denso atual renomeado pela finalidade e pode
produzir análise completa ou busca enxuta. `words-search.wwdb` é a projeção
física `search-only`, sem significados, para clientes que só consomem
`search-v1`. O loader lê suas colunas diretamente. Os dois bancos WWDB 1.8
compartilham IDs e regras; metadados tipados de PACKON evitam qualquer
dependência indireta dos textos omitidos.

O exportador de assets usa o `node:zlib` e produz `.br` (Brotli nível 11) e
`.gz` (gzip nível 9) para glue, WASM, tipos e os dois WWDB. Não depende de um
compressor externo; `--no-compress` existe apenas para builds de diagnóstico.
O CMake também pode pré-comprimir seus artefatos quando `brotli` e `gzip`
estiverem instalados e `ENABLE_WEB_COMPRESSION=ON`.

O servidor deve selecionar essas representações
por `Accept-Encoding` e enviar `Content-Encoding` correto; a wrapper sempre
deve receber os bytes WWDB já descomprimidos pelo stack HTTP.

Tipos MIME recomendados:

| Extensão | `Content-Type` |
| --- | --- |
| `.mjs` | `text/javascript; charset=utf-8` |
| `.wasm` | `application/wasm` |
| `.wwdb` | `application/octet-stream` |

O `datasetId` é o SHA-256 de um manifesto canônico de fontes e semântica do
packer, não o hash de `words-full.wwdb`. Assim `words-full.wwdb`,
`words-search.wwdb` e o índice externo compartilham o mesmo espaço de
IDs. Cada arquivo também tem seu SHA-256 físico em `manifest.json`.

O módulo foi gerado para `web`, `worker` e `node`. Análises isoladas podem ser
feitas na thread principal; uma interface interativa com lotes deve hospedar
uma engine persistente em Web Worker para não bloquear renderização. O proxy
de mensagens do Worker e a política de cache offline são cortes de produto
separados da engine.

## Verificação

O teste unitário
[`tests/wasm_wrapper_test.mjs`](../../tests/wasm_wrapper_test.mjs) usa um módulo
controlado para verificar load, contratos, modo `Two_Words`, falha e descarte.
O smoke test
[`tests/wasm_smoke_test.mjs`](../../tests/wasm_smoke_test.mjs) carrega os
artefatos compilados e o WWDB real:

```bash
node scripts/export-wasm-assets.mjs \
  --search-database \
    whitakers-words/poc/compact-db/output/words-poc-search-only.wwdb \
  --out-dir dist/words-web
dataset_id="$(node -p "require('./dist/words-web/manifest.json').datasetId")"
node tests/wasm_smoke_test.mjs \
  build/wasm/words_wasm.mjs \
  dist/words-web/words-full.wwdb \
  "${dataset_id}" \
  dist/words-web/words-search.wwdb
```

Ele cobre `mālum`, `anaticulus`, os dois contratos JSON, a rejeição de `ß`,
reload transacional e equivalência de busca entre full e search. Também
confere que `_malloc`/`HEAPU8` não fazem parte da API pública e que o banco
search recusa análise rica.
