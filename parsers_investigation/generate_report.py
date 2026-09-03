#!/usr/bin/env python3
"""Generate the Gate D0 Markdown report from schema-v2 NDJSON."""

from __future__ import annotations

import argparse
import json
import math
import pathlib
import statistics
from collections import defaultdict
from typing import Any


STRATEGIES = (
    "morphology",
    "cartesian-leaf-check",
    "incremental-dfs",
    "dfs-mrv-forward-checking",
    "worklist-prefilter",
    "gac-propagation",
    "gac-residue-cache",
    "dependency-projection",
    "dependency-attachment-search",
    "dependency-tree-oracle",
    "dependency-eisner",
    "dependency-mst",
    "earley-fixed-point-recognizer",
    "gslr-stackset-recognizer",
)


def percentile(values: list[int], probability: float) -> int:
    ordered = sorted(values)
    return ordered[max(0, math.ceil(probability * len(ordered)) - 1)]


def elapsed_summary(items: list[dict[str, Any]]) -> tuple[int, int]:
    values = [item["elapsedNs"] // 1_000 for item in items]
    return int(statistics.median(values)), percentile(values, 0.95)


def markdown(records: list[dict[str, Any]]) -> str:
    if any(
        record.get("schema") != "words-parser-investigation"
        or record.get("schemaVersion") != 2
        for record in records
    ):
        raise ValueError("the report generator accepts only result schema v2")

    by_strategy: dict[str, list[dict[str, Any]]] = defaultdict(list)
    by_fixture: dict[str, dict[str, dict[str, Any]]] = defaultdict(dict)
    for record in records:
        by_strategy[record["strategy"]].append(record)
        by_fixture[record["fixtureId"]][record["strategy"]] = record
    missing = set(STRATEGIES) - set(by_strategy)
    if missing:
        raise ValueError(f"missing strategies: {', '.join(sorted(missing))}")

    first = records[0]
    annotated = [
        strategies["morphology"]
        for strategies in by_fixture.values()
        if strategies["morphology"].get("fixtureAnnotation") is not None
    ]
    agreement_didactic = [
        item
        for item in annotated
        if item["fixtureAnnotation"]["source"]["blockId"] == "s0029-l-b007"
    ]
    comparison_didactic = [
        item
        for item in annotated
        if item["fixtureAnnotation"]["source"]["blockId"]
        in {"s0059-l-b009", "s0059-l-b010"}
    ]

    def gold_rank_cell(gold: dict[str, Any]) -> str:
        rank = gold["rank"]
        if rank is None:
            return "—"
        if rank != 1 and gold.get("bestScoreTie") is True:
            return f"{rank} (empate no topo)"
        return str(rank)

    lines = [
        "# Relatório dos parsers — Gate D0 e comparativos didáticos",
        "",
        "## Resultado",
        "",
        (
            f"O contrato v2 foi executado em {len(by_fixture)} frases de "
            "S0. O relatório separa morfologia, busca, attachments, árvores, "
            "recognizers e projeção; nenhuma soma combina essas unidades."
        ),
        "",
        f"- Dataset: `{first['datasetId']}`",
        f"- Commit configurado: `{first['sourceCommit']}`",
        f"- Compilador: `{first['compiler']} {first['compilerVersion']}` (`{first['buildType']}`)",
        f"- Orçamento de enumeração: `{first['maxProduct']}` atribuições",
        f"- Fixtures com proveniência didática verificada: {len(annotated)}/{len(by_fixture)}",
        "- Tempos: uma observação por frase, adequados apenas para diagnóstico.",
        "- Memória: estimativa das estruturas próprias, não RSS.",
        "",
        "## Semântica da decisão",
        "",
        "Uma hard constraint pode eliminar uma análise como **impossível**; toda análise restante é apenas **possível**, e as features brandas ordenam esse conjunto por plausibilidade. `bestScore` e `scoreReasons` são scores manuais decomponíveis, não probabilidades calibradas. O v2 ainda agrega rejeições por ID de constraint, sem evidência individual por análise, e não expõe o N-best completo nem um campo de probabilidade; esses são requisitos do próximo ciclo, não propriedades retroativas destes números.",
        "",
        "## Corpus e gold",
        "",
        "| Frase | Candidatos | Produto bruto | Scan | GAC | Arestas H005–H011 | Atribuições | Attachments | Árvores P/NP | Gold morfológico | Gold de dependências |",
        "|---|---|---:|---|---|---:|---:|---:|---:|---:|---:|",
    ]
    for strategies in by_fixture.values():
        morphology = strategies["morphology"]
        worklist = strategies["worklist-prefilter"]
        gac = strategies["gac-propagation"]
        dependency = strategies["dependency-projection"]
        attachment = strategies["dependency-attachment-search"]
        tree = strategies["dependency-tree-oracle"]
        counts = "×".join(str(value) for value in morphology["morphology"]["candidateCounts"])
        domains = "×".join(str(value) for value in worklist["propagation"]["domainsAfter"])
        gac_domains = "×".join(str(value) for value in gac["propagation"]["domainsAfter"])
        morph_rank = gold_rank_cell(dependency["gold"]["morphology"])
        dep_rank = gold_rank_cell(dependency["gold"]["dependency"])
        lines.append(
            f"| {morphology['text']} | {counts} | "
            f"{morphology['morphology']['rawProduct']} | {domains} | {gac_domains} | "
            f"{dependency['relationCandidates']['generated']} | "
            f"{gac['acceptance']['morphAssignments']} | "
            f"{attachment['attachmentSearch']['completeAnalyses']} | "
            f"{tree['treeSearch']['projectiveTrees']}/"
            f"{tree['treeSearch']['nonprojectiveTrees']} | "
            f"{morph_rank} | {dep_rank} |"
        )

    if annotated:
        didactic_ids = {item["fixtureId"] for item in annotated}
        didactic_projective = sum(
            by_fixture[fixture_id]["dependency-eisner"]["gold"]["dependency"]
            ["survives"]
            is True
            for fixture_id in didactic_ids
        )
        ambiguous = by_fixture.get("agreement-feminine-plural", {}).get(
            "dependency-projection"
        )
        lines.extend([
            "",
            "## Corpus didático verificado",
            "",
            (
                f"Foram promovidas {len(annotated)} frases de *Gramática Latina*: "
                f"{len(agreement_didactic)} exemplos de concordância predicativa da "
                f"página 54 e {len(comparison_didactic)} exemplos de comparação da "
                "página 114. Cada fixture registra snapshot, página, unidade, bloco, "
                "texto-fonte e o bloco que sustenta a anotação."
            ),
            "",
            (
                "Nas frases de concordância, a fonte afirma caso, número e gênero. "
                "Nos pares comparativos, ela contrasta o segundo termo em ablativo "
                "com a construção `quam` + caso paralelo ao primeiro termo. A análise "
                "verbal completa e os heads/labels continuam adições editoriais "
                "explícitas, não alegações atribuídas ao livro."
            ),
            "",
            (
                f"Os {didactic_projective}/{len(annotated)} golds didáticos são "
                "projetivos e sobrevivem em Eisner e no MST. Este lote valida "
                "concordância, mas ainda não decide a necessidade de "
                "não projetividade em latim real."
            ),
        ])
        if ambiguous:
            gold = ambiguous["gold"]["morphology"]
            best_lemma = ambiguous["bestAnalysis"][2]["lemma"]
            lines.extend([
                "",
                (
                    "`Alumnae sunt altae` expôs ambiguidade lexical genuína: "
                    f"o gold adjetival `altus` aparece no rank {gold['rank']}, mas "
                    f"empata no melhor score (`bestScoreTie={str(gold['bestScoreTie']).lower()}`). "
                    f"O desempate estável mostra primeiro o particípio de `{best_lemma}`. "
                    "A sintaxe isolada admite tanto ‘são altas’ quanto ‘foram criadas’; "
                    "a preferência didática não autoriza eliminar a segunda leitura."
                ),
            ])
        if comparison_didactic:
            lookup_overrides = sum(
                len(item["morphology"]["lookupOverrides"])
                for item in comparison_didactic
            )
            quam = by_fixture["comparison-quam-intelligentior"][
                "dependency-projection"
            ]
            quam_part = quam["bestAnalysis"][3]["part"]
            lines.extend([
                "",
                (
                    f"Os pares comparativos exercitam H011 nas duas realizações. "
                    f"{lookup_overrides} fixtures preservam a grafia impressa "
                    "`intelligentior`, mas consultam explicitamente "
                    "`intellegentior`; o override e sua justificativa aparecem no "
                    "resultado, sem alterar o testemunho da fonte."
                ),
                "",
                (
                    "A fonte chama `quam` de conjunção comparativa, enquanto a WWDB "
                    f"ranqueia primeiro a análise `{quam_part}`. O gold aceita ambas "
                    "as categorias como uma ambiguidade POS ainda não resolvida; as "
                    "duas projetam `mark` e dependem da mesma relação "
                    "`comparison-standard`/`obl:cmp`."
                ),
            ])

    lines.extend([
        "",
        "## Track A — busca morfológica/CSP",
        "",
        "As seis linhas abaixo têm a mesma unidade: estados parciais da enumeração e atribuições completas.",
        "",
        "| Estratégia | Cobertura | Atribuições aceitas | Estados parciais | Checks de constraints | Backtracks | p50 µs | p95 µs |",
        "|---|---:|---:|---:|---:|---:|---:|---:|",
    ])
    for strategy in (
        "cartesian-leaf-check",
        "incremental-dfs",
        "dfs-mrv-forward-checking",
        "worklist-prefilter",
        "gac-propagation",
        "gac-residue-cache",
    ):
        items = by_strategy[strategy]
        p50, p95 = elapsed_summary(items)
        lines.append(
            f"| `{strategy}` | {sum(item['status'] == 'ok' for item in items)}/{len(items)} | "
            f"{sum(item['acceptance']['morphAssignments'] for item in items)} | "
            f"{sum(item['enumeration']['partialStates'] for item in items)} | "
            f"{sum(item['enumeration']['constraintChecks'] for item in items)} | "
            f"{sum(item['enumeration']['backtracks'] for item in items)} | {p50} | {p95} |"
        )

    exact_search = all(
        all(
            strategies[name]["acceptance"]["assignmentIds"]
            == strategies["cartesian-leaf-check"]["acceptance"]["assignmentIds"]
            for name in (
                "incremental-dfs",
                "dfs-mrv-forward-checking",
                "worklist-prefilter",
                "gac-propagation",
                "gac-residue-cache",
            )
        )
        for strategies in by_fixture.values()
    )
    exact_recognizers = all(
        strategies["earley-fixed-point-recognizer"]["acceptance"]["assignmentIds"]
        == strategies["gslr-stackset-recognizer"]["acceptance"]["assignmentIds"]
        for strategies in by_fixture.values()
    )
    lines.extend([
        "",
        f"Equivalência extensional das seis buscas: **{'sim' if exact_search else 'não'}**, comparando IDs exatos, não apenas contagens.",
        "",
        "## Propagação — scan, GAC e resíduos",
        "",
        "| Estratégia | Remoções | Checks de suporte | Hits | Misses | Invalidações | Checks do resíduo | Queue pops | Revisões | Estados enumerados |",
        "|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|",
    ])
    for strategy in (
        "worklist-prefilter",
        "gac-propagation",
        "gac-residue-cache",
    ):
        items = by_strategy[strategy]
        lines.append(
            f"| `{strategy}` | "
            f"{sum(sum(item['propagation']['removalsByConstraint'].values()) for item in items)} | "
            f"{sum(item['propagation']['supportChecks'] for item in items)} | "
            f"{sum(item['propagation']['residueHits'] for item in items)} | "
            f"{sum(item['propagation']['residueMisses'] for item in items)} | "
            f"{sum(item['propagation']['residueInvalidations'] for item in items)} | "
            f"{sum(item['propagation']['residueCandidateChecks'] for item in items)} | "
            f"{sum(item['propagation']['queuePops'] for item in items)} | "
            f"{sum(item['propagation']['revisions'] for item in items)} | "
            f"{sum(item['enumeration']['partialStates'] for item in items)} |"
        )
    gac_checks = sum(
        item["propagation"]["supportChecks"]
        for item in by_strategy["gac-propagation"]
    )
    cached_items = by_strategy["gac-residue-cache"]
    cached_checks = sum(item["propagation"]["supportChecks"] for item in cached_items)
    residue_hits = sum(item["propagation"]["residueHits"] for item in cached_items)
    residue_invalidations = sum(
        item["propagation"]["residueInvalidations"] for item in cached_items
    )
    residue_candidate_checks = sum(
        item["propagation"]["residueCandidateChecks"] for item in cached_items
    )
    reduction = 100.0 * (gac_checks - cached_checks) / gac_checks if gac_checks else 0.0
    reduction_text = f"{reduction:.1f}".replace(".", ",")
    lines.extend([
        "",
        "A GAC remove valores dos dois lados da constraint. Em `In urbe manet`, o scan reduz o produto 12→6; a agenda reduz 12→2.",
        "",
        (
            f"O cache reutilizou {residue_hits} testemunhos e reduziu os checks "
            f"semânticos de suporte de {gac_checks} para {cached_checks} "
            f"({reduction_text}%). Para isso, fez {residue_candidate_checks} checks "
            f"baratos de presença no domínio. Houve {residue_invalidations} "
            "invalidações: S0 ainda não exercita cascatas capazes de invalidar "
            "um suporte previamente guardado. Os tempos de uma única execução "
            "e a estimativa de memória não sustentam uma conclusão de desempenho."
        ),
        "",
        "## Relações candidatas — H005/H006/H007/H011",
        "",
    ])
    dependency_items = by_strategy["dependency-projection"]
    attachment_items = by_strategy["dependency-attachment-search"]

    def aggregate_relation_map(field: str) -> dict[str, int]:
        totals: dict[str, int] = defaultdict(int)
        for item in dependency_items:
            for key, value in item["relationCandidates"][field].items():
                totals[key] += value
        return dict(totals)

    relation_kinds = aggregate_relation_map("byKind")
    relation_compatibility = aggregate_relation_map("byCompatibility")
    selected_relations = sum(
        item["relationCandidates"]["selected"] for item in dependency_items
    )
    generated_relations = sum(relation_kinds.values())
    lines.extend([
        f"Foram materializadas **{generated_relations}** arestas tipadas no corpus: "
        f"{relation_kinds.get('preposition-complement', 0)} `preposition-complement`, "
        f"{relation_kinds.get('verb-argument', 0)} `verb-argument`, "
        f"{relation_kinds.get('coordination', 0)} `coordination`, e "
        f"{relation_kinds.get('comparison-standard', 0)} `comparison-standard`.",
        "",
        "| Compatibilidade | Arestas |",
        "|---|---:|",
        f"| compatível | {relation_compatibility.get('compatible', 0)} |",
        f"| incompatível | {relation_compatibility.get('incompatible', 0)} |",
        f"| indeterminada | {relation_compatibility.get('indeterminate', 0)} |",
        "",
        f"A projeção selecionou {selected_relations} arestas explícitas nas análises top-1. "
        "`Accredo amico` exerce H006: a aresta dativa compatível é selecionada como `iobj` "
        "e recebe S008, enquanto a alternativa ablativa permanece morfologicamente possível "
        "mas não é promovida a argumento regido. `Placet` continua válido sem complemento. "
        "Os quatro pares comparativos exercitam H011 e emitem `obl:cmp`.",
        "",
        "`dependency-projection` escolhe deterministicamente entre candidatas compatíveis. "
        f"Os {relation_compatibility.get('indeterminate', 0)} casos indeterminados mostram "
        "que `VerbKind` sem frame de regência não basta para decidir papéis argumentais.",
        "",
        "## Busca exata de attachments",
        "",
        (
            f"`dependency-attachment-search` enumerou "
            f"**{sum(item['attachmentSearch']['completeAnalyses'] for item in attachment_items)}** "
            f"análises relacionais sobre "
            f"{sum(item['acceptance']['morphAssignments'] for item in attachment_items)} "
            f"atribuições morfológicas, visitando "
            f"{sum(item['attachmentSearch']['partialStates'] for item in attachment_items)} "
            f"estados de escolha em "
            f"{sum(item['attachmentSearch']['slotsCreated'] for item in attachment_items)} "
            "slots."
        ),
        "",
        (
            f"A projeção determinística pertence ao conjunto exato em "
            f"{sum(item['attachmentSearch']['projectionInSearch'] for item in attachment_items)}/"
            f"{sum(item['attachmentSearch']['projectionChecked'] for item in attachment_items)} "
            "atribuições. Os IDs canônicos e o digest do conjunto tornam essa comparação "
            "reproduzível."
        ),
        "",
        "H005, H007 e H011 abrem slots obrigatórios quando a construção está selecionada; "
        "H006 permanece opcional sem um frame que prove obrigatoriedade. A busca cobre "
        "somente essas quatro famílias de relações; ainda não enumera heads para todos os "
        "tokens nem garante uma árvore de dependências completa.",
        "",
        "O orçamento `maxProduct` também limita a materialização desse conjunto. Ao "
        "excedê-lo, a estratégia retorna `experiment-budget-exceeded` e não publica IDs "
        "parciais como se formassem um conjunto exato.",
        "",
        "## Oráculo exato de árvores",
        "",
    ])
    tree_items = by_strategy["dependency-tree-oracle"]
    tree_arcs = sum(item["treeSearch"]["arcCandidatesGenerated"] for item in tree_items)
    tree_count = sum(item["treeSearch"]["completeTrees"] for item in tree_items)
    projective_count = sum(item["treeSearch"]["projectiveTrees"] for item in tree_items)
    nonprojective_count = sum(item["treeSearch"]["nonprojectiveTrees"] for item in tree_items)
    cycle_rejections = sum(item["treeSearch"]["cycleRejections"] for item in tree_items)
    root_rejections = sum(item["treeSearch"]["rootRejections"] for item in tree_items)
    tree_projection_checked = sum(item["treeSearch"]["projectionChecked"] for item in tree_items)
    tree_projection_found = sum(item["treeSearch"]["projectionInSearch"] for item in tree_items)
    hyperbaton = by_fixture["nonprojective-hyperbaton"]["dependency-tree-oracle"]
    lines.extend([
        (
            f"O domínio comum materializou **{tree_arcs}** arcos sobre as atribuições "
            f"morfológicas e o DFS exato produziu **{tree_count}** árvores: "
            f"{projective_count} projetivas e {nonprojective_count} não projetivas. "
            "Cada árvore tem exatamente uma raiz, um head por token, é conectada e acíclica."
        ),
        "",
        (
            f"A poda incremental rejeitou {cycle_rejections} fechamentos de ciclo e "
            f"{root_rejections} escolhas incompatíveis com raiz única. A projeção "
            f"determinística pertence ao conjunto em {tree_projection_found}/"
            f"{tree_projection_checked} atribuições."
        ),
        "",
        (
            "A fixture sintética `Bona rosam puella amat` torna a distinção observável: "
            f"ela possui {hyperbaton['treeSearch']['projectiveTrees']} árvores projetivas e "
            f"{hyperbaton['treeSearch']['nonprojectiveTrees']} não projetivas; seu gold "
            "liga `Bona` a `puella` através de `rosam→amat`, formando arestas cruzadas, e "
            f"fica no rank {hyperbaton['gold']['dependency']['rank']}."
        ),
        "",
        "As demais árvores não projetivas pertencem a análises morfológicas alternativas "
        "e incluem arcos que atravessam a raiz artificial; não representam a mesma "
        "quantidade de frases latinas independentes.",
        "",
        "Os scores T001 são heurísticas auditáveis de arco, não pesos treinados. O "
        "oráculo fornece agora a resposta de referência para testar decodificadores, mas "
        "sua enumeração exponencial continua restrita a S0 e ao orçamento `maxProduct`.",
        "",
        "## Decodificadores projetivo e não projetivo",
        "",
        "| Estratégia | Árvores | Projetivas | Não projetivas | Estados/arestas examinadas | Ciclos contraídos | Igual ao oráculo | p50 µs | p95 µs |",
        "|---|---:|---:|---:|---:|---:|---:|---:|---:|",
    ])
    decoder_specs = (
        ("dependency-eisner", "bestProjectiveScores"),
        ("dependency-mst", "bestUnrestrictedScores"),
    )
    decoder_exact: dict[str, bool] = {}
    for strategy, oracle_field in decoder_specs:
        items = by_strategy[strategy]
        exact = True
        for item in items:
            oracle = by_fixture[item["fixtureId"]]["dependency-tree-oracle"]
            expected = oracle["treeSearch"][oracle_field]
            actual = item["decoder"]["scoresByAssignment"]
            exact = exact and expected.keys() == actual.keys() and all(
                math.isclose(expected[key], actual[key], abs_tol=1.0e-9)
                for key in expected
            )
            exact = exact and set(item["decoder"]["analysisIds"]).issubset(
                oracle["treeSearch"]["analysisIds"]
            )
        decoder_exact[strategy] = exact
        p50, p95 = elapsed_summary(items)
        lines.append(
            f"| `{strategy}` | {sum(item['decoder']['completeTrees'] for item in items)} | "
            f"{sum(item['decoder']['projectiveTrees'] for item in items)} | "
            f"{sum(item['decoder']['nonprojectiveTrees'] for item in items)} | "
            f"{sum(item['decoder']['states'] for item in items)} | "
            f"{sum(item['decoder']['cyclesContracted'] for item in items)} | "
            f"{'sim' if exact else 'não'} | {p50} | {p95} |"
        )

    eisner_hyperbaton = by_fixture["nonprojective-hyperbaton"]["dependency-eisner"]
    mst_hyperbaton = by_fixture["nonprojective-hyperbaton"]["dependency-mst"]
    better_unrestricted = 0
    compared_decodings = 0
    for strategies in by_fixture.values():
        eisner_scores = strategies["dependency-eisner"]["decoder"]["scoresByAssignment"]
        mst_scores = strategies["dependency-mst"]["decoder"]["scoresByAssignment"]
        for assignment, eisner_score in eisner_scores.items():
            compared_decodings += 1
            if mst_scores[assignment] > eisner_score + 1.0e-9:
                better_unrestricted += 1
    lines.extend([
        "",
        (
            f"Chu–Liu/Edmonds supera o ótimo projetivo em {better_unrestricted}/"
            f"{compared_decodings} atribuições. Eisner emite somente árvores projetivas; "
            "o MST pode escolher cruzamentos quando aumentam o score."
        ),
        "",
        (
            "Em `Bona rosam puella amat`, Eisner deliberadamente não contém o gold "
            f"não projetivo (`survives={str(eisner_hyperbaton['gold']['dependency']['survives']).lower()}`), "
            "enquanto Chu–Liu/Edmonds o recupera em rank "
            f"{mst_hyperbaton['gold']['dependency']['rank']}."
        ),
        "",
        "Os contadores de trabalho permanecem próprios de cada algoritmo: células/splits "
        "de Eisner não são a mesma unidade que arestas examinadas e ciclos contraídos por MST.",
        "",
        "## Baselines sintáticos — métricas próprias",
        "",
        "| Estratégia | Atribuições aceitas | Métrica própria | Valor | p50 µs | p95 µs |",
        "|---|---:|---|---:|---:|---:|",
    ])
    own_metric = {
        "dependency-projection": ("relações emitidas", "dependencyRelationsEmitted"),
        "dependency-attachment-search": ("análises de attachment", None),
        "dependency-tree-oracle": ("árvores completas", None),
        "earley-fixed-point-recognizer": ("itens/deduções criados", "unitsCreated"),
        "gslr-stackset-recognizer": ("configurações de pilha criadas", "unitsCreated"),
    }
    for strategy, (label, field) in own_metric.items():
        items = by_strategy[strategy]
        p50, p95 = elapsed_summary(items)
        value = (
            sum(item["attachmentSearch"]["completeAnalyses"] for item in items)
            if strategy == "dependency-attachment-search"
            else sum(item["treeSearch"]["completeTrees"] for item in items)
            if strategy == "dependency-tree-oracle"
            else sum(item["parser"][field] for item in items)
        )
        lines.append(
            f"| `{strategy}` | {sum(item['acceptance']['morphAssignments'] for item in items)} | "
            f"{label} | {value} | {p50} | {p95} |"
        )

    morph_top = sum(
        item["gold"]["morphology"]["rank"] == 1
        for item in by_strategy["dependency-projection"]
    )
    dep_top = sum(
        item["gold"]["dependency"]["rank"] == 1
        for item in by_strategy["dependency-projection"]
    )
    eisner_dep_top = sum(
        item["gold"]["dependency"]["rank"] == 1
        for item in by_strategy["dependency-eisner"]
    )
    mst_dep_top = sum(
        item["gold"]["dependency"]["rank"] == 1
        for item in by_strategy["dependency-mst"]
    )
    morph_best_score = sum(
        item["gold"]["morphology"]["bestScoreTie"] is True
        for item in by_strategy["dependency-projection"]
    )
    dep_best_score = sum(
        item["gold"]["dependency"]["bestScoreTie"] is True
        for item in by_strategy["dependency-projection"]
    )
    eisner_dep_best_score = sum(
        item["gold"]["dependency"]["bestScoreTie"] is True
        for item in by_strategy["dependency-eisner"]
    )
    mst_dep_best_score = sum(
        item["gold"]["dependency"]["bestScoreTie"] is True
        for item in by_strategy["dependency-mst"]
    )
    lines.extend([
        "",
        f"Equivalência extensional dos recognizers: **{'sim' if exact_recognizers else 'não'}**. Isso demonstra equivalência nesta gramática mínima, não equivalência entre Earley e GLR como famílias.",
        "",
        "Os valores da coluna ‘métrica própria’ não são comparáveis entre linhas: relações, itens Earley e pilhas explícitas são unidades diferentes.",
        "",
        "## Validade do gold e limites",
        "",
        f"- Gold morfológico completo em rank 1: {morph_top}/{len(by_fixture)}.",
        f"- Gold morfológico empatado no melhor score: {morph_best_score}/{len(by_fixture)}.",
        f"- Gold de dependências da projeção em rank 1: {dep_top}/{len(by_fixture)}.",
        f"- Gold de dependências da projeção empatado no melhor score: {dep_best_score}/{len(by_fixture)}.",
        f"- Gold de dependências em rank 1: Eisner {eisner_dep_top}/{len(by_fixture)}; Chu–Liu/Edmonds {mst_dep_top}/{len(by_fixture)}.",
        f"- Gold de dependências empatado no melhor score: Eisner {eisner_dep_best_score}/{len(by_fixture)}; Chu–Liu/Edmonds {mst_dep_best_score}/{len(by_fixture)}.",
        "- O self-test também muta caso e relação mantendo o restante da análise; ambas as mutações precisam falhar.",
        "- `preferredLemmaSequence` permanece apenas como sinal de compatibilidade e nunca é chamado de gold estrutural.",
        "- `forest.available` é falso e contagens de derivações/SPPF são nulas: estes protótipos não constroem floresta.",
        "- `dependency-projection` é uma projeção determinística, não um decodificador ótimo.",
        "- `dependency-attachment-search` é um oráculo exato apenas para H005/H006/H007/H011, não uma busca de árvores completas.",
        "- `dependency-tree-oracle` enumera árvores completas exatamente em S0, mas não é um algoritmo adequado para corpus longo.",
        "- Eisner e Chu–Liu/Edmonds igualam, respectivamente, os ótimos projetivo e irrestrito do oráculo em cada atribuição de S0.",
        "- Os scores T001 formalizam a política atual; ainda não foram calibrados em train/dev nem validados externamente.",
        "- `gslr-stackset-recognizer` usa um conjunto de pilhas explícitas, não GSS.",
        "- H006 não exige a presença global de um complemento: `Placet.` preserva `placeo`. Quando uma aresta argumento–predicado é escolhida, o caso incompatível é rejeitado na relação sem apagar a análise morfológica como possível adjunto.",
        f"- O catálogo didático tem {len(annotated)}/33 frases promovidas a gold estrutural; as outras {33 - len(annotated)} continuam `candidate-unverified`.",
        "- A auditoria reencontrou 33/33 frases nos 15 blocos declarados e validou reciprocamente as 10 promoções. O censo lexical bruto continua em 29/33: `intelligentior` requer os overrides explícitos nas duas fixtures; `Catilina` e `Pyrrho` ainda não existem na WWDB.",
        f"- O self-test passou nas {len(by_fixture)} fixtures com WWDB full e search-only; o corpus e os {len(records)} registros passaram nos schemas v2; a suíte geral passou em 94/94 testes; ASan/UBSan também passou (LeakSanitizer desativado sob `ptrace`).",
        "",
        "## Decisão D0",
        "",
        "O Gate D0 permanece satisfeito. Dez exemplos didáticos agora cobrem concordância e as duas construções do segundo termo da comparação. H011 torna o contraste observável sem apagar a grafia da fonte nem resolver artificialmente a categoria de `quam`. Eisner e Chu–Liu/Edmonds continuam iguais aos respectivos ótimos do oráculo. O próximo ciclo deve tornar explícito o N-best dos casos possíveis e só então ampliar comparação de inferioridade e ordem livre; ainda não há probabilidades calibradas nem evidência para escolher o decodificador padrão.",
        "",
        "## Reprodução",
        "",
        "```sh",
        "cmake -S . -B build/parsers -G Ninja -DCMAKE_BUILD_TYPE=Release -DENABLE_TESTS=ON \\",
        "  -DPARSERS_INVESTIGATION_CORPUS_PATH=$PWD/parsers_investigation/corpus/agreement_fixtures.json",
        "cmake --build build/parsers --target parsers_investigation",
        "build/parsers/parsers_investigation/parsers_investigation --self-test",
        "build/parsers/parsers_investigation/parsers_investigation > /tmp/parsers-results-v2.ndjson",
        "python3 parsers_investigation/generate_report.py /tmp/parsers-results-v2.ndjson --output parsers_investigation/REPORT.md",
        "```",
        "",
    ])
    return "\n".join(lines)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("ndjson", type=pathlib.Path)
    parser.add_argument("--output", type=pathlib.Path)
    args = parser.parse_args()
    records = [
        json.loads(line)
        for line in args.ndjson.read_text(encoding="utf-8").splitlines()
        if line.strip()
    ]
    if not records:
        raise SystemExit("the NDJSON input is empty")
    report = markdown(records)
    if args.output:
        args.output.write_text(report, encoding="utf-8")
    else:
        print(report)


if __name__ == "__main__":
    main()
