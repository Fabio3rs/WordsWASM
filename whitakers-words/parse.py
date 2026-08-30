import re
import os
import sys
import json


def parse_whitaker_output(output: str) -> dict:
    noun_re = re.compile(
        r"^(\S+)\s+N\s+"  # forma + POS
        r"(\d+)\s+\d+\s+"  # declinação + ignore
        r"([A-Z]{3})\s+"  # caso (NOM, ACC, VOC…)
        r"([SP])\s+"  # número
        r"([MF])\b"  # gênero
    )
    verb_re = re.compile(
        r"^(\S+)\s+V\s+"  # forma + POS
        r"(\d+)\s+\d+\s+"  # conjugação + ignore
        r"([A-Z]+)\s+"  # tempo (PRES, IMPF…)
        r"([A-Z]+)\s+"  # voz (ACTIVE, PASSIVE)
        r"([A-Z]{2,3})\s+"  # modo (IND, IMP, SUBJ)
        r"(\d+)\s+"  # pessoa
        r"([SP])\b"  # número
    )
    adj_re = re.compile(
        r"^(\S+)\s+ADJ\s+"  # forma + POS
        r"(\d+)\s+\d+\s+"  # declinação + ignore
        r"([A-Z]{3})\s+"  # caso
        r"([SP])\s+"  # número
        r"(POS|COMP|SUP)\b"  # grau (positivo, comparativo, superlativo)
    )
    pp_re = re.compile(
        r"^(\w+),\s*([^ ]+)\s+"  # nom, gen
        r"([NVAD])\s+"  # POS
        r"\((\d+)(?:st|nd|rd|th)\)"  # decl/class
    )

    morphologia = []
    principal_parts = []
    gloss = []

    for line in output.splitlines():
        ln = line.strip()
        if not ln or ln.startswith("MORE") or ln.startswith("*"):
            continue

        # 1) noun
        m = noun_re.match(ln)
        if m:
            forma, decl, caso, num, gen = m.groups()
            morphologia.append(
                {
                    "forma": forma,
                    "pos": "N",
                    "declinação": int(decl),
                    "caso": caso,
                    "número": "sing" if num == "S" else "plur",
                    "gênero": "masc" if gen == "M" else "fem",
                }
            )
            continue

        # 2) verb
        m = verb_re.match(ln)
        if m:
            forma, conj, tempo, voz, modo, pessoa, num = m.groups()
            morphologia.append(
                {
                    "forma": forma,
                    "pos": "V",
                    "conjugação": int(conj),
                    "tempo": tempo,
                    "voz": voz,
                    "modo": modo,
                    "pessoa": int(pessoa),
                    "número": "sing" if num == "S" else "plur",
                }
            )
            continue

        # 3) adjective
        m = adj_re.match(ln)
        if m:
            forma, decl, caso, num, grau = m.groups()
            morphologia.append(
                {
                    "forma": forma,
                    "pos": "ADJ",
                    "declinação": int(decl),
                    "caso": caso,
                    "número": "sing" if num == "S" else "plur",
                    "grau": grau,
                }
            )
            continue

        # 4) principal parts
        m = pp_re.match(ln)
        if m:
            nom, gen, pos, decl = m.groups()
            principal_parts = [nom, gen]
            continue

        # 5) gloss / definição
        # linhas que começam com letra minúscula ou contêm ';'
        if ln[0].islower() or ";" in ln:
            # remove colchetes explicativos mas preserve conteúdo entre parênteses
            parts = [p.strip(" []") for p in ln.rstrip(";").split(";") if p.strip()]
            gloss.extend(parts)
            continue

        # 6) casos especiais (TACKON, enclíticos etc)
        # Se quiser, capture como raw_others para debug
        # out.setdefault("raw_others", []).append(ln)

    return {
        "morphologia": morphologia,
        "principal_parts": principal_parts,
        "gloss": gloss,
    }


# exec bin/words "word" and parses it
def parse_word(word):
    if not word:
        return None

    # Execute the command and capture the output
    cmd = f"bin/words {word}"
    try:
        output = os.popen(cmd).read().strip()
    except Exception as e:
        print(f"Error executing command: {e}", file=sys.stderr)
        return None

    parsed_data = parse_whitaker_output(output)

    return parsed_data


if __name__ == "__main__":
    if len(sys.argv) != 2:
        print("Usage: python parse.py <word>")
        sys.exit(1)

    word = sys.argv[1]
    result = parse_word(word)

    print(json.dumps(result, indent=2, ensure_ascii=False))
