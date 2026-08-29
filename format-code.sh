#!/bin/bash
# Script para aplicar clang-format em arquivos C++ modificados

echo "Formatando arquivos C++ no staging area..."
git diff --cached --name-only --diff-filter=AM | grep -E "\.(cpp|hpp|h|cc|cxx)$" | xargs -r clang-format -i

echo "Formatando arquivos C++ modificados mas não commitados..."
git diff --name-only --diff-filter=M | grep -E "\.(cpp|hpp|h|cc|cxx)$" | xargs -r clang-format -i

echo "Formatando arquivos C++ não monitorados (novos)..."
git ls-files --others --exclude-standard | grep -E "\.(cpp|hpp|h|cc|cxx)$" | xargs -r clang-format -i

echo "Removendo espaços em branco desnecessários de todos os arquivos de texto..."
find . -type f \( -iname '*.txt' -o -iname '*.md' -o -iname '*.yml' -o -iname '*.yaml' -o -iname '*.json' -o -iname '*.c' -o -iname '*.h' -o -iname '*.cpp' -o -iname '*.hpp' -o -iname '*.py' -o -iname '*.sh' -o -iname '*.js' -o -iname '*.ts' -o -iname '*.css' -o -iname '*.html' -o -iname '*.xml' \) -print0 | xargs -0 sed -i -e 's/[ \t]\+$//' -e 's/[ \t]\+\r$/\r/'

echo "Formatação concluída!"
