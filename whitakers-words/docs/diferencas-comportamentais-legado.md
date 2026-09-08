# Diferenças comportamentais deliberadas em relação ao legado

Este documento registra diferenças conhecidas entre o processamento de texto
do WORDS em Ada e a engine C++23. Elas não são necessariamente defeitos de
compatibilidade: servem como referência caso um perfil estritamente histórico
seja necessário no futuro.

## Dois hífens não iniciam comentário

O procedimento Ada `Analyse_Line` chama `String_Before_Dash` antes da
tokenização. Quando encontra `--`, ele ignora o restante da linha. Assim, a
entrada `amo -- amare` analisa somente `amo`.

O `TextTokenCursor` nativo trata hífens como fronteiras tipadas e continua
percorrendo a entrada. A mesma linha produz duas unidades, `amo` e `amare`.
Essa diferença é deliberada: a API nativa recebe texto para análise lexical e
não atribui aos dois hífens a semântica de comentário de uma interface
interativa histórica.

Se um perfil de compatibilidade estrita vier a ser criado, o corte deve ser
uma política de pré-processamento de linha anterior ao `TextTokenCursor`. Ele
não deve ser incorporado à decodificação UTF-8 nem ao `LatinLexer`, pois esses
componentes também atendem entradas nas quais `--` é somente pontuação.
