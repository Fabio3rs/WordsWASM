#!/usr/bin/env python3
"""Benchmark native execution and simulate memory/network traffic.

The benchmark intentionally invokes WORDS with two file arguments.  That path
sets Output_Screen_Size to Integer'Last, so legacy output pagination cannot
block a long batch waiting for newlines on stdin.
"""

from __future__ import annotations

import argparse
import datetime as dt
import gzip
import hashlib
import os
from pathlib import Path
import platform
import random
import re
import shutil
import statistics
import subprocess
import tempfile
import time


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_ASSETS = (
    "DICTFILE.GEN",
    "STEMFILE.GEN",
    "INDXFILE.GEN",
    "INFLECTS.SEC",
    "UNIQUES.LAT",
    "ADDONS.LAT",
)
CONTROL_WORDS = ("rosae", "rosarum", "amo")
REGULAR_INFLECTION_ENDINGS = frozenset("acdeimnorstu")
INFLECTION_SECTION_BYTES = 22_800


def parse_int_list(value: str) -> list[int]:
    values = sorted({int(item) for item in value.split(",")})
    if not values or values[0] < 0:
        raise argparse.ArgumentTypeError("expected comma-separated non-negative integers")
    return values


def corpus_words(path: Path, seed: int, maximum: int) -> tuple[list[str], int]:
    tokens = re.findall(r"[A-Za-z]+", path.read_text(encoding="utf-8"))
    unique = sorted({token.lower() for token in tokens})
    controls = [word for word in CONTROL_WORDS if word not in unique]
    random.Random(seed).shuffle(unique)
    ordered = list(CONTROL_WORDS) + [word for word in unique if word not in CONTROL_WORDS]
    if maximum > len(ordered):
        raise ValueError(f"requested {maximum} words, but corpus supplies {len(ordered)}")
    return ordered[:maximum], len(unique)


def wait_with_rusage(command: list[str]) -> dict[str, float]:
    started = time.perf_counter_ns()
    process = subprocess.Popen(
        command,
        cwd=ROOT,
        stdout=subprocess.DEVNULL,
        stderr=subprocess.PIPE,
    )
    pid, status, usage = os.wait4(process.pid, 0)
    elapsed_ns = time.perf_counter_ns() - started
    assert pid == process.pid
    process.returncode = os.waitstatus_to_exitcode(status)
    stderr = process.stderr.read().decode("utf-8", errors="replace")
    process.stderr.close()
    if process.returncode != 0:
        raise RuntimeError(
            f"command failed ({process.returncode}): {' '.join(command)}\n{stderr}"
        )
    return {
        "wall_ms": elapsed_ns / 1_000_000,
        "user_ms": usage.ru_utime * 1_000,
        "system_ms": usage.ru_stime * 1_000,
        "cpu_ms": (usage.ru_utime + usage.ru_stime) * 1_000,
        "max_rss_kib": float(usage.ru_maxrss),
        "minor_faults": float(usage.ru_minflt),
        "major_faults": float(usage.ru_majflt),
        "block_inputs": float(usage.ru_inblock),
        "block_outputs": float(usage.ru_oublock),
    }


def median_measurements(measurements: list[dict[str, float]]) -> dict[str, float]:
    return {
        key: statistics.median(item[key] for item in measurements)
        for key in measurements[0]
    }


def measure_max_rss(command: list[str], temporary: Path) -> float:
    """Measure target RSS without inheriting Python's pre-exec high-water mark."""
    output_path = temporary / "gnu-time.txt"
    completed = subprocess.run(
        ["/usr/bin/time", "-f", "%M", "-o", str(output_path), *command],
        cwd=ROOT,
        stdout=subprocess.DEVNULL,
        stderr=subprocess.PIPE,
        text=True,
        check=False,
    )
    if completed.returncode != 0:
        raise RuntimeError(
            f"GNU time command failed ({completed.returncode}): {' '.join(command)}\n"
            f"{completed.stderr}"
        )
    return float(output_path.read_text(encoding="ascii").strip())


def native_benchmark(
    executable: Path,
    words: list[str],
    sample_sizes: list[int],
    repeats: int,
    temporary: Path,
) -> dict[int, dict[str, float]]:
    results: dict[int, dict[str, float]] = {}
    for size in sample_sizes:
        input_path = temporary / f"sample-{size}.txt"
        input_path.write_text("".join(f"{word}\n" for word in words[:size]), encoding="ascii")
        command = [str(executable), str(input_path), "/dev/null"]
        runs = [wait_with_rusage(command) for _ in range(repeats)]
        results[size] = median_measurements(runs)
        results[size]["max_rss_kib"] = measure_max_rss(command, temporary)
    return results


def parse_counter(log: str, label: str) -> int:
    match = re.search(rf"^==\d+== {re.escape(label)}:\s+([0-9,]+)", log, re.MULTILINE)
    if match is None:
        raise RuntimeError(f"Cachegrind counter not found: {label}")
    return int(match.group(1).replace(",", ""))


def cachegrind_benchmark(
    executable: Path,
    words: list[str],
    sample_sizes: list[int],
    temporary: Path,
) -> dict[int, dict[str, int]]:
    if shutil.which("valgrind") is None:
        return {}

    results: dict[int, dict[str, int]] = {}
    for size in sample_sizes:
        input_path = temporary / f"sample-{size}.txt"
        if not input_path.exists():
            input_path.write_text(
                "".join(f"{word}\n" for word in words[:size]), encoding="ascii"
            )
        output_path = temporary / f"cachegrind-{size}.out"
        command = [
            "valgrind",
            "--tool=cachegrind",
            "--cache-sim=yes",
            "--branch-sim=yes",
            f"--cachegrind-out-file={output_path}",
            str(executable),
            str(input_path),
            "/dev/null",
        ]
        completed = subprocess.run(
            command,
            cwd=ROOT,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.PIPE,
            text=True,
            check=True,
        )
        log = completed.stderr
        i_refs = parse_counter(log, "I refs")
        i1_misses = parse_counter(log, "I1  misses")
        d_refs = parse_counter(log, "D refs")
        d1_misses = parse_counter(log, "D1  misses")
        ll_misses = parse_counter(log, "LL misses")
        branches = parse_counter(log, "Branches")
        mispredicts = parse_counter(log, "Mispredicts")
        results[size] = {
            "instruction_refs": i_refs,
            "data_refs": d_refs,
            "l1_misses": i1_misses + d1_misses,
            "ll_misses": ll_misses,
            "branches": branches,
            "branch_mispredicts": mispredicts,
            "estimated_l1_fill_bytes": (i1_misses + d1_misses) * 64,
            "estimated_ll_fill_bytes": ll_misses * 64,
        }
    return results


def asset_sizes() -> list[tuple[str, int, int]]:
    sizes = []
    for name in DEFAULT_ASSETS:
        data = (ROOT / name).read_bytes()
        sizes.append((name, len(data), len(gzip.compress(data, compresslevel=9, mtime=0))))
    return sizes


def fmt_int(value: float | int) -> str:
    return f"{int(round(value)):,}".replace(",", ".")


def fmt_float(value: float, digits: int = 3) -> str:
    return f"{value:.{digits}f}".replace(".", ",")


def render_report(
    args: argparse.Namespace,
    words: list[str],
    corpus_unique: int,
    native: dict[int, dict[str, float]],
    cachegrind: dict[int, dict[str, int]],
    assets: list[tuple[str, int, int]],
) -> str:
    sample_hash = hashlib.sha256(
        "".join(f"{word}\n" for word in words).encode("ascii")
    ).hexdigest()
    baseline_native = native.get(0)
    largest_native_size = max(native)
    largest_native = native[largest_native_size]
    quick_read: list[str] = []
    if baseline_native is not None and largest_native_size > 0:
        incremental_wall_ms = max(
            0.0, largest_native["wall_ms"] - baseline_native["wall_ms"]
        )
        incremental_cpu_ms = max(
            0.0, largest_native["cpu_ms"] - baseline_native["cpu_ms"]
        )
        quick_read.extend(
            [
                f"- Inicialização em processo vazio: `{fmt_float(baseline_native['cpu_ms'])} ms` de CPU.",
                f"- No lote de {fmt_int(largest_native_size)}: `{fmt_float(incremental_cpu_ms / largest_native_size)} ms` de CPU incremental por consulta e aproximadamente `{fmt_int(largest_native_size * 1000 / incremental_wall_ms)}` consultas/s.",
                f"- RSS máximo observado: `{fmt_int(max(item['max_rss_kib'] for item in native.values()))} KiB`; ele não cresce proporcionalmente ao lote.",
            ]
        )
    if 0 in cachegrind and max(cachegrind) > 0:
        largest_cache_size = max(cachegrind)
        largest_cache = cachegrind[largest_cache_size]
        baseline_cache = cachegrind[0]
        quick_read.extend(
            [
                f"- Cachegrind, descontando o lote vazio: cerca de `{fmt_int((largest_cache['instruction_refs'] - baseline_cache['instruction_refs']) / largest_cache_size)}` instruções e `{fmt_int((largest_cache['data_refs'] - baseline_cache['data_refs']) / largest_cache_size)}` acessos a dados por consulta.",
                f"- Preenchimentos simulados: `{fmt_float((largest_cache['estimated_l1_fill_bytes'] - baseline_cache['estimated_l1_fill_bytes']) / largest_cache_size / 2**20)} MiB` para L1 e `{fmt_float((largest_cache['estimated_ll_fill_bytes'] - baseline_cache['estimated_ll_fill_bytes']) / largest_cache_size / 2**10)} KiB` desde o último nível por consulta, em média.",
            ]
        )
    quick_read.append("- Rede por consulta depois de carregar o Worker: `0 bytes`.")
    largest_regular_reads = sum(
        bool(word) and word[-1] in REGULAR_INFLECTION_ENDINGS
        for word in words[:largest_native_size]
    )
    quick_read.append(
        f"- O passe regular do Ada lê `{fmt_int(largest_regular_reads * INFLECTION_SECTION_BYTES)}` bytes lógicos de seções de `INFLECTS.SEC` no lote maior; releituras especiais não estão incluídas."
    )
    lines = [
        "# Benchmark de tráfego de memória",
        "",
        f"Executado em `{dt.datetime.now(dt.timezone.utc).isoformat(timespec='seconds')}`.",
        "",
        "## Configuração",
        "",
        f"- Plataforma: `{platform.platform()}`",
        f"- Executável: `{args.executable}`",
        f"- Corpus: `{args.corpus}` ({fmt_int(corpus_unique)} tokens únicos)",
        f"- Semente: `{args.seed}`",
        f"- SHA-256 da maior amostra: `{sample_hash}`",
        f"- Repetições nativas por tamanho: `{args.repeats}`; tabela usa a mediana",
        f"- Primeiras palavras: `{', '.join(words[:10])}`",
        "- Invocação: `words entrada.txt /dev/null`; o modo de dois arquivos desativa a paginação legada.",
        "- Cada linha é uma consulta; o mesmo processo atende todo o lote.",
        "",
        "## Leitura rápida",
        "",
        *quick_read,
        "",
        "## CPU e RAM — medição nativa",
        "",
        "Cada linha inicia um processo novo, mas consultas dentro do lote reutilizam a engine.",
        "O cache de arquivos do sistema operacional não foi descartado.",
        "",
        "| Consultas | Parede total (ms) | CPU total (ms) | CPU incremental/consulta (ms) | RSS máximo (KiB) | Falhas menores |",
        "| ---: | ---: | ---: | ---: | ---: | ---: |",
    ]
    for size, item in native.items():
        if size and baseline_native is not None:
            incremental = max(0.0, item["cpu_ms"] - baseline_native["cpu_ms"]) / size
            incremental_text = fmt_float(incremental)
        else:
            incremental_text = "—"
        lines.append(
            f"| {fmt_int(size)} | {fmt_float(item['wall_ms'])} | "
            f"{fmt_float(item['cpu_ms'])} | {incremental_text} | "
            f"{fmt_int(item['max_rss_kib'])} | {fmt_int(item['minor_faults'])} |"
        )

    lines.extend(
        [
            "",
            "`CPU incremental/consulta` subtrai o lote vazio, portanto separa aproximadamente",
            "o custo de inicialização do custo marginal de análise e formatação.",
            "",
            "## Leituras lógicas de `INFLECTS.SEC`",
            "",
            "Na inicialização, o Ada lê as cinco seções, totalizando 114.000 bytes. No passe",
            "regular de cada palavra com uma letra final suportada, ele relê uma seção inteira",
            "de 22.800 bytes. A tabela é um mínimo estrutural: packons, pronomes e novas",
            "tentativas ortográficas podem provocar leituras adicionais.",
            "",
            "| Consultas | Leituras regulares de seção | Bytes lógicos |",
            "| ---: | ---: | ---: |",
        ]
    )
    for size in native:
        regular_reads = sum(
            bool(word) and word[-1] in REGULAR_INFLECTION_ENDINGS
            for word in words[:size]
        )
        lines.append(
            f"| {fmt_int(size)} | {fmt_int(regular_reads)} | "
            f"{fmt_int(regular_reads * INFLECTION_SECTION_BYTES)} |"
        )
    lines.extend(
        [
            "",
            "Esses bytes normalmente vêm do page cache, portanto não equivalem a tráfego de",
            "disco nem aos preenchimentos de cache simulados abaixo.",
            "",
            "## Tráfego CPU ↔ caches ↔ RAM — simulação Cachegrind",
            "",
            "Cachegrind conta referências executadas e simula caches com linhas de 64 bytes.",
            "Os volumes são `misses * 64`: representam preenchimentos de linha, não incluem",
            "write-backs e não são uma medição do controlador de memória físico.",
            "",
            "| Consultas | Instruções | Acessos a dados | Misses L1 (I+D) | Misses último nível | Linhas para L1 (MiB) | Linhas desde RAM (MiB) |",
            "| ---: | ---: | ---: | ---: | ---: | ---: | ---: |",
        ]
    )
    for size, item in cachegrind.items():
        lines.append(
            f"| {fmt_int(size)} | {fmt_int(item['instruction_refs'])} | "
            f"{fmt_int(item['data_refs'])} | {fmt_int(item['l1_misses'])} | "
            f"{fmt_int(item['ll_misses'])} | "
            f"{fmt_float(item['estimated_l1_fill_bytes'] / 2**20)} | "
            f"{fmt_float(item['estimated_ll_fill_bytes'] / 2**20)} |"
        )
    if not cachegrind:
        lines.append("| — | Cachegrind não disponível | — | — | — | — | — |")

    raw_total = sum(raw for _, raw, _ in assets)
    gzip_total = sum(compressed for _, _, compressed in assets)
    lines.extend(
        [
            "",
            "## Rede — modelo WebAssembly",
            "",
            "Na arquitetura proposta, as consultas acontecem no Worker depois do carregamento",
            "do banco. Portanto, uma consulta adicional transfere **0 bytes de rede**. O custo",
            "de rede é do carregamento inicial e do cache HTTP, não de cada palavra.",
            "",
            "| Ativo legado usado como referência | Bruto (bytes) | gzip-9 determinístico (bytes) |",
            "| --- | ---: | ---: |",
        ]
    )
    for name, raw, compressed in assets:
        lines.append(f"| `{name}` | {fmt_int(raw)} | {fmt_int(compressed)} |")
    lines.extend(
        [
            f"| **Total lexical atual** | **{fmt_int(raw_total)}** | **{fmt_int(gzip_total)}** |",
            "",
            "Esse total é apenas um substituto mensurável enquanto `words.wwdb` não existe.",
            "`words.wasm`, loader JavaScript, cabeçalhos HTTP e interface web não estão incluídos.",
            "Com Worker persistente: primeira sessão paga esse download uma vez; todos os lotes",
            "da tabela continuam com 0 bytes adicionais de rede.",
            "",
            "## Limites",
            "",
            "- Processo novo não significa cache de disco frio; os dados podem estar no page cache.",
            "- RSS mede páginas residentes do processo, não todo o page cache do kernel.",
            "- Cachegrind é determinístico, mas simula caches; não mede largura de banda real.",
            "- O executável inclui formatação da saída, mesmo gravando-a em `/dev/null`.",
            "- A amostra é aleatória sobre palavras únicas de um corpus, não sobre todas as formas possíveis do léxico.",
            "- Para medir o futuro WebAssembly será necessário repetir no navegador com `words.wasm` e `words.wwdb` reais.",
        ]
    )
    return "\n".join(lines) + "\n"


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--executable", type=Path, default=ROOT / "bin/words")
    parser.add_argument("--corpus", type=Path, default=ROOT / "test/01_aeneid/input.txt")
    parser.add_argument("--seed", type=int, default=20260828)
    parser.add_argument("--sample-sizes", type=parse_int_list, default=parse_int_list("0,1,10,100,1000"))
    parser.add_argument("--cachegrind-sizes", type=parse_int_list, default=parse_int_list("0,1,10,100"))
    parser.add_argument("--repeats", type=int, default=7)
    parser.add_argument("--skip-cachegrind", action="store_true")
    args = parser.parse_args()

    args.executable = args.executable.resolve()
    args.corpus = args.corpus.resolve()
    if args.repeats < 1:
        parser.error("--repeats must be positive")
    maximum = max(args.sample_sizes + ([] if args.skip_cachegrind else args.cachegrind_sizes))
    words, corpus_unique = corpus_words(args.corpus, args.seed, maximum)

    with tempfile.TemporaryDirectory(prefix="ww-memory-traffic-") as temp_name:
        temporary = Path(temp_name)
        native = native_benchmark(
            args.executable, words, args.sample_sizes, args.repeats, temporary
        )
        cachegrind = {}
        if not args.skip_cachegrind:
            cachegrind = cachegrind_benchmark(
                args.executable, words, args.cachegrind_sizes, temporary
            )
    print(render_report(args, words, corpus_unique, native, cachegrind, asset_sizes()), end="")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
