# Estudo de backends de texto nativo/WASM

## Status e escopo

Este documento aprofunda uma possível arquitetura com utf8proc no build nativo
e componentes finitos no build WebAssembly. É um estudo; nenhuma seleção de
backend, binding, API pública, tokenização ou regra de produção foi alterada.

O estudo separa dois problemas independentes:

1. normalização e validação da palavra latina com quantidade;
2. decoding e classificação de fronteiras no `TextTokenCursor`.

A primeira parte já possui uma biblioteca C++23 experimental e evidência
diferencial no relatório [investigacao-latin-unicode.md](investigacao-latin-unicode.md).
A segunda ainda não possui implementação candidata integrada.

## Conclusão executiva

A combinação “nativo completo com utf8proc, WASM finito” é tecnicamente viável
como etapa de transição, desde que ambos implementem um contrato observável
versionado e sejam comparados continuamente. Ela não exige que JavaScript seja
a autoridade de validade: o browser pode rejeitar cedo e melhorar a mensagem
ao usuário, enquanto o decoder C++ finito continua sendo a última fronteira.

Esta é a recomendação para um experimento integrado futuro:

- build nativo: utf8proc como backend completo e oracle;
- build WASM: normalizador latino finito mais uma tabela determinística de
  fronteiras Unicode 17;
- wrapper JavaScript: gate para `String` Unicode bem formada e, quando a origem
  forem bytes, `TextDecoder` UTF-8 em modo fatal;
- C++: validação autoritativa em ambos os builds;
- contrato e versão Unicode publicados no manifesto do artefato.

Usar apenas as propriedades Unicode do motor JavaScript para tokenização é
possível, mas não preserva por si só um contrato fixo: a versão do Unicode é a
do browser/engine instalado. Também não remove automaticamente a necessidade de
tokenização C++, porque offsets em bytes e flags de fronteira influenciam a
análise de compostos.

## 1. O que entra no browser não é necessariamente UTF-8

Há duas origens com contratos diferentes.

### 1.1 Entrada de controles HTML e APIs JavaScript

Um `input.value` é uma `String` ECMAScript: uma sequência de code units UTF-16.
Ela não preserva bytes UTF-8 originais e pode, por construção programática,
conter um surrogate isolado. Portanto não existe “UTF-8 malformado original” a
validar nessa rota. O contrato apropriado é:

1. rejeitar `String` que não seja Unicode bem formada;
2. converter seus escalares para UTF-8;
3. validar o domínio latino finito;
4. normalizar e analisar.

`String.prototype.isWellFormed()` fornece a primeira operação. Não se deve usar
`toWellFormed()` nessa fronteira fail-fast, pois seu propósito é reparar a
string substituindo surrogates isolados. A especificação ECMAScript define
também `normalize("NFC")`, mas NFC é uma transformação separada: não faz
casefold, não aplica a allowlist latina e não atribui quantidade.

### 1.2 Arquivos, respostas de rede e outros bytes

Quando há bytes reais, o browser oferece um bom decoder-oracle de ingresso:

```js
const decoder = new TextDecoder("utf-8", {
  fatal: true,
  ignoreBOM: true,
});
const text = decoder.decode(bytes);
```

`fatal: true` faz erro de decoding lançar `TypeError`. `ignoreBOM: true` é
importante para equivalência com o contrato atual: apesar do nome, faz U+FEFF
permanecer no texto. Com o valor padrão `false`, um BOM UTF-8 inicial é
consumido e uma entrada que o C++ rejeitaria poderia virar uma palavra aceita.

`TextEncoder` não é um validador de `String`. Seu argumento Web IDL é
`USVString`; um surrogate isolado é convertido em U+FFFD antes da codificação.
Assim, a ordem correta para texto JavaScript é `isWellFormed()` e só depois
encoding. Para bytes externos, a ordem correta é `TextDecoder` fatal e só
depois validação de domínio.

Referências normativas: [ECMAScript, processamento de
texto](https://tc39.es/ecma262/multipage/text-processing.html), [WHATWG Encoding
Standard](https://encoding.spec.whatwg.org/) e [Unicode UAX
#15](https://www.unicode.org/reports/tr15/).

## 2. Modelo formal das duas rotas

Sejam:

- `B*`, todas as sequências finitas de bytes;
- `J*`, todas as sequências finitas de code units UTF-16;
- `S*`, todas as sequências finitas de escalares Unicode;
- `D : B* ⇀ S*`, o decoder UTF-8 estrito parcial;
- `W : J* ⇀ S*`, a interpretação parcial de uma `String` bem formada;
- `E : S* -> B*`, o encoder UTF-8 canônico;
- `L : S* ⇀ Surface`, a transformação latina finita;
- `C17 : S -> BoundaryFlag`, a classificação de fronteira fixada em Unicode
  17 e com precedência das regras editoriais atuais.

As condições desejáveis são:

```text
aceita_bytes(b)  <=> D(b) está definido e o pipeline de C17/L aceita D(b)
aceita_string(j) <=> W(j) está definido e o pipeline de C17/L aceita W(j)
```

Para toda `j` bem formada, deve valer:

```text
D(E(W(j))) = W(j)
```

Para toda entrada latina aceita, os backends completo e finito devem produzir
a mesma superfície, tokens, flags e offsets. Isso permite que nativo e WASM
tenham implementações diferentes sem terem contratos diferentes.

O normalizador finito já satisfaz a parte local de `L` para seu alfabeto de 76
escalares. A futura tabela de fronteiras deve tornar `C17` uma função total
sobre os 1.112.064 escalares e deve ser exaustivamente comparada com o oracle.

## 3. Auditoria da fronteira JS/Embind atual

O wrapper `wasmsrc/words-engine.mjs` apenas verifica
`typeof text === "string"` e chama métodos Embind que recebem
`const std::string&`. A interface da aplicação ainda chama `.trim()` antes de
`analyzeLine`; esse já é um pequeno contrato do cliente, não do lexer.

A documentação do Emscripten registra que um `std::string` ligado por Embind
aceita `String`, `ArrayBuffer`, `Uint8Array`, `Uint8ClampedArray` ou `Int8Array`.
O setting `EMBIND_STD_STRING_IS_UTF8` é verdadeiro por padrão. Portanto:

- o wrapper público atual não permite bytes arbitrários;
- a classe `module.AnalysisEngine` exposta pelo módulo bruto permite;
- uma verificação apenas no wrapper não pode justificar retirar a validação do
  C++ enquanto o binding bruto estiver acessível ou for usado por outro host.

Referências: [conversões built-in do
Embind](https://emscripten.org/docs/porting/connecting_cpp_and_javascript/embind.html#built-in-type-conversions)
e [settings de strings do
Emscripten](https://emscripten.org/docs/tools_reference/settings_reference.html#embind-std-string-is-utf8).

### 3.1 Experimento no artefato WASM existente

O artefato `dist/words-web` foi carregado no Node 20.18.0, com seu banco
`words-search.wwdb`. Foram exercitados wrapper e binding bruto, sem editar o
artefato:

| Rota | Entrada | Resultado C++ |
| --- | --- | --- |
| wrapper | `puella` | analisada |
| wrapper | high surrogate isolado | `invalid-utf8` |
| wrapper | low surrogate isolado | `invalid-utf8` |
| wrapper | par válido representando 😀 | UTF-8 válido, `unsupported-character` |
| binding bruto | bytes `C0 80` | `invalid-utf8` |
| binding bruto | byte `80` | `invalid-utf8` |
| binding bruto | byte `61` | analisado como `a` |

O glue gerado nesse build calcula quatro bytes para qualquer code unit
surrogate e usa `codePointAt` para escrever. Para um surrogate isolado ele
escreveu a sequência inválida `ED A0 80`, deixou também o byte NUL da capacidade
calculada dentro do `std::string` e o lexer rejeitou corretamente. Isso é uma
observação do artefato atual, não uma API na qual se deva confiar. O valor
devolvido a JavaScript também não preservou de modo útil os bytes malformados.

Conclusão: adicionar `isWellFormed()` ao wrapper seria uma correção de contrato
e UX plausível em tarefa futura. Não é argumento para remover o decoder C++.

### 3.2 Experimentos com as APIs do host

No mesmo runtime, que declara Unicode 15.1/ICU 75.1:

- `TextEncoder().encode("\uD800")` produziu `EF BF BD`;
- `TextDecoder("utf-8", {fatal:true})` rejeitou `C0 80`, `ED A0 80` e
  `F4 90 80 80` com `TypeError`;
- `TextDecoder` padrão removeu `EF BB BF`, enquanto `ignoreBOM:true` preservou
  U+FEFF;
- `"a\u0304".normalize("NFC")` produziu `ā`;
- `"y\u0306".normalize("NFC")` permaneceu em dois codepoints.

Esses resultados coincidem com as especificações, mas os testes de regressão
devem continuar existindo porque o glue Emscripten e a matriz de browsers são
partes reais do produto.

## 4. Quanto JavaScript pode filtrar

### 4.1 Gate recomendado

Um gate JavaScript pequeno pode rejeitar antes da travessia do WASM:

- `String` não bem formada;
- codepoint fora das 52 letras ASCII, 22 precompostos e duas marcas;
- marca no início, sobre consoante, repetida ou conflitante;
- opcionalmente mais de um token quando a operação exige uma palavra.

Esse gate deve ser tratado como duplicação deliberada para mensagem rápida, não
como autoridade. O C++ continua aplicando as mesmas condições. Para não criar
duas semânticas, os vetores da PoC devem alimentar testes compartilhados JS/C++.

Não é necessário chamar `normalize("NFC")` no JS para obter a economia da PoC.
Fazer isso antes da validação original tornaria menos clara a garantia de que
caracteres inicialmente fora do domínio não são aceitos após transformação. A
normalização finita C++ já converge precomposto e decomposto e produz os offsets
exigidos.

### 4.2 Gate por regex Unicode

Expressões como `\p{General_Category=Dash_Punctuation}` funcionam, mas consultam
os dados Unicode do engine. O ECMAScript exige a propriedade, não uma versão de
UCD fixa para todos os hosts instalados.

Uma enumeração local sobre todos os escalares comparou Node 20/Unicode 15.1 com
utf8proc 2.11.3/Unicode 17:

| Grupo | Node 15.1 | utf8proc 17 | Diferença |
| --- | ---: | ---: | ---: |
| Pi/Pf | 22 | 22 | 0 |
| Pd | 26 | 27 | 1 |
| Ps/Pe | 156 | 156 | 0 |
| Pc/Po | 638 | 651 | 13 |

Os 14 escalares conhecidos apenas pelo oracle Unicode 17 nesse comparativo são
U+1B4E, U+1B4F, U+1B7F, U+10D6E, U+10ED0, U+113D4, U+113D5, U+113D7,
U+113D8, U+11BE1, U+16D6D, U+16D6E, U+16D6F e U+1E5FF. U+10D6E pertence a
`Pd`; os demais entram no grupo `Pc/Po` usado pelo cursor.

Isso não demonstra divergência entre todos os browsers atuais; demonstra que a
semântica acompanha a versão do host. Se essa variação for aceitável, deve ser
nomeada como contrato “host Unicode”. Se a saída precisar ser reproduzível
entre browsers, Node, nativo e dados persistidos, propriedades do host não são
uma base suficiente.

## 5. A categorização necessária sem utf8proc

O `TextTokenCursor` não precisa de categorização Unicode geral. Seu contrato é:

1. reconhecer dez ranges explícitos da propriedade `White_Space`;
2. aplicar switches editoriais específicos para vírgula, ponto e outros sinais;
3. aplicar `Pi/Pf -> quote`;
4. aplicar `Pd -> dash`;
5. aplicar `Ps/Pe -> bracket`;
6. aplicar `Pc/Po -> other_punctuation`;
7. produzir `none` para todo o resto.

A precedência importa: U+2019, por exemplo, é `apostrophe` pela política local
antes de sua General Category ser consultada.

Na versão Unicode 17 vendorizada pelo oracle, os quatro grupos gerais totalizam
856 codepoints em 263 ranges mesclados. Endpoints `uint32_t` simples ocupariam
no máximo 2.104 bytes, antes de alinhamento e código. Isso é pequeno comparado
às tabelas gerais do utf8proc e ainda permite uma busca binária simples e
auditável.

Uma candidata futura deveria:

- gerar os ranges offline a partir de dados Unicode 17 fixados;
- versionar os arrays `constexpr`, não baixar dados no build;
- provar por `static_assert` ordenação, disjunção, limites e ausência de ranges
  vazios;
- comparar todos os escalares com `utf8proc_category` no target de testes;
- manter switches editoriais e whitespace como políticas distintas dos dados
  de General Category.

Uma allowlist só dos 35 caracteres gerais encontrados no corpus da Latin
Library seria ainda menor, mas mudaria a semântica. Ela pode ser uma política
editorial nova em pré-release, desde que seja assumida e versionada como tal;
não deve ser apresentada como equivalente a Unicode 17.

## 6. Por que não mover já o tokenizador para JavaScript

O cursor atual devolve views e offsets em bytes UTF-8, acumula flags da fronteira
posterior e essas flags decidem se dois tokens podem formar um composto. Uma
iteração JavaScript fornece normalmente índices em code units UTF-16. Mover o
cursor exigiria definir e testar:

- tradução UTF-16 -> offsets UTF-8 para cada token e fronteira;
- um ABI em lote para texto, offsets e bitmask de flags;
- ownership e lifetime do texto codificado;
- precedência de todas as regras editoriais;
- comportamento diante de bytes malformados, que só existem na rota binária;
- composição entre tokens sem transformar cada token em uma chamada Embind.

Analisar tokens isoladamente no JS não é equivalente a `analyzeLine`: perderia
o contexto de fronteira usado por compostos. Fazer uma chamada WASM por token
também tende a custar mais que uma tabela C++ de aproximadamente 2 KiB.

Uma implementação JS determinística e gerada com os mesmos ranges é possível,
mas duplica lógica e dados. Deve ser considerada apenas se um perfil integrado
mostrar que retirar também o cursor C++ traz economia material. Até lá, JS é
mais útil como gate de entrada que como tokenizer autoritativo.

## 7. Opções arquiteturais

| Opção | Nativo | WASM | Contrato | Avaliação |
| --- | --- | --- | --- | --- |
| A | utf8proc | utf8proc | Unicode 17 atual | baseline simples, maior WASM |
| B | utf8proc | finito + ranges U17 | igual e determinístico | melhor transição para A/B |
| C | utf8proc | finito + allowlist editorial | explicitamente diferente | viável em pré-release, exige perfil/versionamento |
| D | utf8proc | finito + categorias do host JS | varia com o host | não recomendada para resultados reproduzíveis |
| E | finito + ranges U17 | finito + ranges U17 | igual e determinístico | destino possível após evidência integrada |

A opção B preserva um oracle independente no nativo e permite medir o delta
real do WASM. Se a motivação de produto justificar um contrato editorial menor,
a opção C também é defensável; a divergência deve ser uma decisão do produto,
não um efeito acidental do toolchain.

### 7.1 Separar seleção de dois backends

Não existe um único “backend Unicode”. Uma futura configuração deveria manter
as escolhas ortogonais, conceitualmente:

```text
WORDS_LATIN_BACKEND     = utf8proc | finite
WORDS_BOUNDARY_BACKEND  = utf8proc | unicode17_ranges | editorial
```

Os nomes são ilustrativos. O ponto arquitetural é permitir quatro combinações
nos testes sem fazer o normalizador conhecer pontuação e sem fazer o cursor
conhecer quantidade vocálica.

A seleção deve ocorrer no link/compile time por artefato. Colocar ambos os
backends dentro do mesmo WASM e escolher em runtime tende a conservar as duas
tabelas e invalida o principal objetivo de tamanho. Um executável de teste pode
ligar ambos; cada artefato de distribuição deve ligar somente o backend eleito.

### 7.2 Identidade do contrato no artefato

Se builds diferentes forem publicados, o manifesto ou metadata da API deveria
expor pelo menos:

```text
latinInputProfile: latin-quantity-v1
boundaryProfile: unicode-17.0-exact | words-editorial-v1 | host-unicode
unicodeDataVersion: 17.0.0 | host
textBackend: utf8proc | finite
```

`textBackend` é diagnóstico de implementação; os profiles são o contrato. Dois
backends com profiles iguais precisam produzir resultados iguais. Um cache ou
resultado persistido não deve depender silenciosamente do browser que o gerou.

## 8. Alocações e ABI

A PoC demonstrou zero alocações em `normalize_into` sob DHAT, mas isso não
significa zero alocações de ponta a ponta no browser. Hoje existem, no mínimo:

- alocação/cópia feita pelo conversor Embind de `String` para `std::string`;
- ownership das strings/vetores de `SurfaceForm` e do resultado;
- conversão dos value objects e vetores devolvidos a JavaScript.

Há três graus possíveis numa tarefa futura:

1. **Integração simples.** Usar a lógica finita e preencher os tipos próprios
   atuais. Remove utf8proc do caminho, mas conserva as alocações necessárias à
   API proprietária.
2. **Scratch reutilizável C++.** Calcular `requirements`, reservar buffers por
   engine/thread e usar `normalize_into`. Pode zerar alocações transitórias após
   aquecimento, embora o resultado público ainda tenha ownership.
3. **ABI linear reutilizável.** JS verifica `isWellFormed`, codifica com
   `TextEncoder.encodeInto` diretamente numa região reutilizada da memória
   WASM e passa ponteiro+tamanho. Isso evita o `std::string` temporário do
   Embind, mas exige API, growth/lifetime e testes novos; o C++ ainda valida.

O terceiro desenho só vale a complexidade se profiling do produto completo
mostrar custo relevante na travessia. O microbenchmark do normalizador não mede
essa fronteira. Também não se deve aplicar `TextEncoder` antes de rejeitar
surrogates isolados, pois isso converteria erro em U+FFFD.

Para resultados, um formato plano em memória poderia reduzir handles e cópias,
mas seria uma revisão bem maior do ABI e está fora do problema Unicode. Não é
pré-requisito para experimentar a opção B.

## 9. Evidência de tamanho disponível e lacuna restante

O snapshot histórico `dist/words-web/words_wasm.wasm`, datado de 30/08, mede
767.066 bytes brutos, 218.268 com gzip e 149.448 com Brotli. Ele não deve ser
confundido com o build local mais recente, que já mede 859.224 bytes e contém
outras mudanças da engine. Nos micro-WASM isolados:

| Núcleo | RAW | Brotli-11 |
| --- | ---: | ---: |
| normalizador finito com buffers | 5.349 B | 2.636 B |
| transformação utf8proc usada como comparação | 332.520 B | 54.494 B |

Uma medição adicional com somente `utf8proc_category` produziu 285.496 bytes
brutos e 36.551 bytes Brotli. Assim, conservar o tokenizador atual retém 85,9%
do RAW e 67,1% do Brotli do micro-WASM utf8proc. Remover apenas as chamadas de
normalização pode beneficiar CPU e alocações, mas não remove a maior parte dos
dados do runtime.

Esses valores indicam uma oportunidade, não um delta aditivo garantido de 52
KiB Brotli. O WASM atual ainda usa `utf8proc_category`, LTO pode compartilhar ou
eliminar código, e a tabela futura de fronteiras acrescentará dados. A medida
decisiva é um A/B do mesmo commit e opções, produzindo:

- `native-utf8proc`;
- `native-finite`;
- `wasm-utf8proc`;
- `wasm-finite-unicode17`.

Somente então devem ser comparados CODE, DATA, RAW, gzip, Brotli, tempo de
instanciação, heap inicial e throughput end-to-end.

## 10. Plano de verificação antes de qualquer migração

### 10.1 Matriz C++

- executar todos os testes do lexer e engine com cada backend;
- ligar candidato e utf8proc juntos apenas nos testes diferenciais;
- comparar a superfície latina como já feito pela PoC;
- comparar `boundary_flag` para todos os escalares;
- comparar sequências completas de `TextToken`, bytes, offsets e flags;
- testar cada fronteira antes, entre e depois de palavras;
- testar combinações de flags que permitem/bloqueiam compostos;
- preservar os vetores de UTF-8 malformado e a precedência diagnóstica;
- repetir corpus Latin Library e o corpus morfológico do produto.

### 10.2 Matriz JavaScript/WASM

- high e low surrogate isolados em todas as posições;
- pares de surrogate válidos fora do domínio;
- precomposto/decomposto e `y + breve`;
- BOM, NUL e controles;
- `Uint8Array` com cada vetor UTF-8 malformado no binding bruto;
- rota de bytes com `TextDecoder` fatal e `ignoreBOM:true`;
- equivalência wrapper/binding para todas as entradas válidas;
- Chrome, Firefox e WebKit se qualquer semântica do host for adotada;
- teste que prova que o artefato WASM finito não contém/linka utf8proc.

### 10.3 Profiling integrado

- DHAT/heaptrack ou contadores de alocação no nativo;
- instrumentação de `malloc/free` e snapshots do heap no WASM;
- `fprofile-instr-generate` para confirmar os hot paths reais;
- benchmark separado de encoding/ABI, tokenização, normalização e construção
  do resultado;
- corpora ASCII, quantitativo e linhas editoriais reais;
- memória/tempo sob chamadas unitárias e `analyzeLine` em lote.

Callgrind e LLVM fprofile já ajudaram a localizar custo dentro da PoC; para a
decisão de arquitetura, a próxima medição útil é no binário integrado, incluindo
o glue JS.

## 11. Critérios de decisão

Antes de selecionar a opção B, C ou E, devem estar respondidas explicitamente:

1. O contrato de fronteiras é Unicode 17 exato ou uma política editorial?
2. Nativo e WASM precisam ser semanticamente idênticos?
3. A classe Embind bruta é API suportada ou detalhe que pode ser escondido?
4. Qual economia mínima de Brotli/heap justifica dois backends?
5. O wrapper só melhora UX ou é uma fronteira de segurança documentada?
6. Qual versão mínima de browser suporta o gate escolhido?
7. Como o profile de entrada aparece em manifests, diagnósticos e caches?
8. Qual processo revisa diffs ao atualizar a versão Unicode?

## 12. Recomendação técnica

Para a próxima tarefa de implementação, a sequência de menor risco é:

1. manter produção inalterada e concluir a PoC de ranges Unicode 17 para
   `boundary_flag`;
2. provar equivalência escalar e no cursor completo contra utf8proc;
3. criar builds experimentais integrados, ainda fora do default, para a opção
   B;
4. adicionar o gate `isWellFormed()` ao wrapper somente junto de testes
   end-to-end e decisão explícita de contrato;
5. medir o A/B integrado, inclusive alocações da fronteira;
6. decidir depois se o build nativo permanece completo ou converge para o
   backend finito.

O browser deve ser usado onde é forte: decoding fatal de bytes externos,
detecção de `String` mal formada e feedback antecipado. A pequena validação C++
deve continuar autoritativa no WASM: é barata, já é exaustivamente testável e
cobre chamadas que contornem o wrapper. Para classificação reproduzível, uma
tabela C++ Unicode 17 é preferível às propriedades dependentes do host.

Essa arquitetura mantém aberta a possibilidade mencionada — biblioteca nativa
completa e WASM especializado — sem transformar variação de browser em mudança
morfológica silenciosa e sem presumir de antemão que utf8proc precisa sair.
