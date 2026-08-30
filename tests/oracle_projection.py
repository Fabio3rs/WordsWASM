import copy


def with_deponent_constraints(document: dict) -> dict:
    """Project canonical Ada JSON onto interactive WORDS semantics.

    Initialize_Canonical_Engine disables Trim_Output. In legacy WORDS,
    Allowed_Stem is accidentally nested under that presentation option, so
    words_json retains active finite forms of deponent lexemes that the
    interactive program rejects. Apply only that grammatical constraint; age,
    frequency, and other optional trimming remain disabled.
    """
    projected = copy.deepcopy(document)

    def allowed(item: dict) -> bool:
        properties = item.get("lexeme", {}).get("properties", {})
        morphology = item.get("morphology", {})
        if (item.get("partOfSpeech") != "verb" or
                properties.get("verbKind") != "deponent" or
                morphology.get("voice") != "active"):
            return True
        # List_Sweep retains this one explicit exception for deponents.
        return (morphology.get("mood") == "infinitive" and
                morphology.get("tense") == "future")

    projected["analyses"] = [
        item for item in projected["analyses"] if allowed(item)
    ]
    return projected
