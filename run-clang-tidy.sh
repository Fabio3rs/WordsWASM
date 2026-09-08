#!/usr/bin/env bash

set -euo pipefail

project_root="$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)"
readonly project_root
readonly build_directory="${1:-${project_root}/build}"
readonly jobs="${2:-20}"
readonly source_filter="^${project_root}/(src|parsers_investigation|tests|wasmsrc)/.*\\.(c|cc|cpp|cxx)$"

if [[ ! "${jobs}" =~ ^[1-9][0-9]*$ ]]; then
    echo "jobs must be a positive integer" >&2
    exit 2
fi

exec run-clang-tidy \
    -p "${build_directory}" \
    -j "${jobs}" \
    -source-filter "${source_filter}"
