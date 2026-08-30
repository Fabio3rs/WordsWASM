#ifndef WHITAKERS_WORDS_LEGACY_DATA_LAYOUT_H
#define WHITAKERS_WORDS_LEGACY_DATA_LAYOUT_H

/*
 * Layout dos arquivos Ada.Direct_IO gerados nesta copia do WORDS.
 *
 * ABI observada:
 *   GNAT 13.3.0, x86-64 Linux, little-endian, Character de 8 bits,
 *   Integer de 32 bits e Ada.Direct_IO.Count de 64 bits.
 *
 * Isto descreve DICTFILE.GEN, STEMFILE.GEN e INFLECTS.SEC existentes no
 * repositorio. Nao e um formato portatil nem a especificacao de words.wwdb.
 * Os campos pad_* podem conter bytes nao inicializados e devem ser ignorados.
 * Ao ler de um buffer de bytes, prefira memcpy para um objeto alinhado e
 * valide tamanho, faixas dos enums e endianness antes de usar os valores.
 */

#include <stddef.h>
#include <stdint.h>

#if defined(__cplusplus)
#define WW_LEGACY_STATIC_ASSERT(condition, message) static_assert(condition, message)
#else
#define WW_LEGACY_STATIC_ASSERT(condition, message) _Static_assert(condition, message)
#endif

typedef uint8_t ww_legacy_pofs_t;
enum {
    WW_LEGACY_POFS_X = 0,
    WW_LEGACY_POFS_N = 1,
    WW_LEGACY_POFS_PRON = 2,
    WW_LEGACY_POFS_PACK = 3,
    WW_LEGACY_POFS_ADJ = 4,
    WW_LEGACY_POFS_NUM = 5,
    WW_LEGACY_POFS_ADV = 6,
    WW_LEGACY_POFS_V = 7,
    WW_LEGACY_POFS_VPAR = 8,
    WW_LEGACY_POFS_SUPINE = 9,
    WW_LEGACY_POFS_PREP = 10,
    WW_LEGACY_POFS_CONJ = 11,
    WW_LEGACY_POFS_INTERJ = 12,
    WW_LEGACY_POFS_TACKON = 13,
    WW_LEGACY_POFS_PREFIX = 14,
    WW_LEGACY_POFS_SUFFIX = 15
};

typedef uint8_t ww_legacy_gender_t;
enum {
    WW_LEGACY_GENDER_X = 0,
    WW_LEGACY_GENDER_M = 1,
    WW_LEGACY_GENDER_F = 2,
    WW_LEGACY_GENDER_N = 3,
    WW_LEGACY_GENDER_C = 4
};

typedef uint8_t ww_legacy_case_t;
enum {
    WW_LEGACY_CASE_X = 0,
    WW_LEGACY_CASE_NOM = 1,
    WW_LEGACY_CASE_VOC = 2,
    WW_LEGACY_CASE_GEN = 3,
    WW_LEGACY_CASE_LOC = 4,
    WW_LEGACY_CASE_DAT = 5,
    WW_LEGACY_CASE_ABL = 6,
    WW_LEGACY_CASE_ACC = 7
};

typedef uint8_t ww_legacy_number_t;
enum {
    WW_LEGACY_NUMBER_X = 0,
    WW_LEGACY_NUMBER_S = 1,
    WW_LEGACY_NUMBER_P = 2
};

typedef uint8_t ww_legacy_comparison_t;
enum {
    WW_LEGACY_COMPARISON_X = 0,
    WW_LEGACY_COMPARISON_POS = 1,
    WW_LEGACY_COMPARISON_COMP = 2,
    WW_LEGACY_COMPARISON_SUPER = 3
};

typedef uint8_t ww_legacy_numeral_sort_t;
enum {
    WW_LEGACY_NUMERAL_X = 0,
    WW_LEGACY_NUMERAL_CARD = 1,
    WW_LEGACY_NUMERAL_ORD = 2,
    WW_LEGACY_NUMERAL_DIST = 3,
    WW_LEGACY_NUMERAL_ADVERB = 4
};

typedef uint8_t ww_legacy_tense_t;
enum {
    WW_LEGACY_TENSE_X = 0,
    WW_LEGACY_TENSE_PRES = 1,
    WW_LEGACY_TENSE_IMPF = 2,
    WW_LEGACY_TENSE_FUT = 3,
    WW_LEGACY_TENSE_PERF = 4,
    WW_LEGACY_TENSE_PLUP = 5,
    WW_LEGACY_TENSE_FUTP = 6
};

typedef uint8_t ww_legacy_voice_t;
enum {
    WW_LEGACY_VOICE_X = 0,
    WW_LEGACY_VOICE_ACTIVE = 1,
    WW_LEGACY_VOICE_PASSIVE = 2
};

typedef uint8_t ww_legacy_mood_t;
enum {
    WW_LEGACY_MOOD_X = 0,
    WW_LEGACY_MOOD_IND = 1,
    WW_LEGACY_MOOD_SUB = 2,
    WW_LEGACY_MOOD_IMP = 3,
    WW_LEGACY_MOOD_INF = 4,
    WW_LEGACY_MOOD_PPL = 5
};

typedef uint8_t ww_legacy_noun_kind_t;
enum {
    WW_LEGACY_NOUN_KIND_X = 0,
    WW_LEGACY_NOUN_KIND_S = 1,
    WW_LEGACY_NOUN_KIND_M = 2,
    WW_LEGACY_NOUN_KIND_A = 3,
    WW_LEGACY_NOUN_KIND_G = 4,
    WW_LEGACY_NOUN_KIND_N = 5,
    WW_LEGACY_NOUN_KIND_P = 6,
    WW_LEGACY_NOUN_KIND_T = 7,
    WW_LEGACY_NOUN_KIND_L = 8,
    WW_LEGACY_NOUN_KIND_W = 9
};

typedef uint8_t ww_legacy_pronoun_kind_t;
enum {
    WW_LEGACY_PRONOUN_X = 0,
    WW_LEGACY_PRONOUN_PERS = 1,
    WW_LEGACY_PRONOUN_REL = 2,
    WW_LEGACY_PRONOUN_REFLEX = 3,
    WW_LEGACY_PRONOUN_DEMONS = 4,
    WW_LEGACY_PRONOUN_INTERR = 5,
    WW_LEGACY_PRONOUN_INDEF = 6,
    WW_LEGACY_PRONOUN_ADJECT = 7
};

typedef uint8_t ww_legacy_verb_kind_t;
enum {
    WW_LEGACY_VERB_X = 0,
    WW_LEGACY_VERB_TO_BE = 1,
    WW_LEGACY_VERB_TO_BEING = 2,
    WW_LEGACY_VERB_GEN = 3,
    WW_LEGACY_VERB_DAT = 4,
    WW_LEGACY_VERB_ABL = 5,
    WW_LEGACY_VERB_TRANS = 6,
    WW_LEGACY_VERB_INTRANS = 7,
    WW_LEGACY_VERB_IMPERS = 8,
    WW_LEGACY_VERB_DEP = 9,
    WW_LEGACY_VERB_SEMIDEP = 10,
    WW_LEGACY_VERB_PERFDEF = 11
};

typedef uint8_t ww_legacy_age_t;
enum {
    WW_LEGACY_AGE_X = 0,
    WW_LEGACY_AGE_A = 1,
    WW_LEGACY_AGE_B = 2,
    WW_LEGACY_AGE_C = 3,
    WW_LEGACY_AGE_D = 4,
    WW_LEGACY_AGE_E = 5,
    WW_LEGACY_AGE_F = 6,
    WW_LEGACY_AGE_G = 7,
    WW_LEGACY_AGE_H = 8
};

typedef uint8_t ww_legacy_frequency_t;
enum {
    WW_LEGACY_FREQ_X = 0,
    WW_LEGACY_FREQ_A = 1,
    WW_LEGACY_FREQ_B = 2,
    WW_LEGACY_FREQ_C = 3,
    WW_LEGACY_FREQ_D = 4,
    WW_LEGACY_FREQ_E = 5,
    WW_LEGACY_FREQ_F = 6,
    WW_LEGACY_FREQ_I = 7,
    WW_LEGACY_FREQ_M = 8,
    WW_LEGACY_FREQ_N = 9
};

typedef uint8_t ww_legacy_area_t;
enum {
    WW_LEGACY_AREA_X = 0,
    WW_LEGACY_AREA_A = 1,
    WW_LEGACY_AREA_B = 2,
    WW_LEGACY_AREA_D = 3,
    WW_LEGACY_AREA_E = 4,
    WW_LEGACY_AREA_G = 5,
    WW_LEGACY_AREA_L = 6,
    WW_LEGACY_AREA_P = 7,
    WW_LEGACY_AREA_S = 8,
    WW_LEGACY_AREA_T = 9,
    WW_LEGACY_AREA_W = 10,
    WW_LEGACY_AREA_Y = 11
};

typedef uint8_t ww_legacy_geo_t;
enum {
    WW_LEGACY_GEO_X = 0,
    WW_LEGACY_GEO_A = 1,
    WW_LEGACY_GEO_B = 2,
    WW_LEGACY_GEO_C = 3,
    WW_LEGACY_GEO_D = 4,
    WW_LEGACY_GEO_E = 5,
    WW_LEGACY_GEO_F = 6,
    WW_LEGACY_GEO_G = 7,
    WW_LEGACY_GEO_H = 8,
    WW_LEGACY_GEO_I = 9,
    WW_LEGACY_GEO_J = 10,
    WW_LEGACY_GEO_K = 11,
    WW_LEGACY_GEO_N = 12,
    WW_LEGACY_GEO_P = 13,
    WW_LEGACY_GEO_Q = 14,
    WW_LEGACY_GEO_R = 15,
    WW_LEGACY_GEO_S = 16,
    WW_LEGACY_GEO_U = 17
};

typedef uint8_t ww_legacy_source_t;
enum {
    WW_LEGACY_SOURCE_X = 0,
    WW_LEGACY_SOURCE_A = 1,
    WW_LEGACY_SOURCE_B = 2,
    WW_LEGACY_SOURCE_C = 3,
    WW_LEGACY_SOURCE_D = 4,
    WW_LEGACY_SOURCE_E = 5,
    WW_LEGACY_SOURCE_F = 6,
    WW_LEGACY_SOURCE_G = 7,
    WW_LEGACY_SOURCE_H = 8,
    WW_LEGACY_SOURCE_I = 9,
    WW_LEGACY_SOURCE_J = 10,
    WW_LEGACY_SOURCE_K = 11,
    WW_LEGACY_SOURCE_L = 12,
    WW_LEGACY_SOURCE_M = 13,
    WW_LEGACY_SOURCE_N = 14,
    WW_LEGACY_SOURCE_O = 15,
    WW_LEGACY_SOURCE_P = 16,
    WW_LEGACY_SOURCE_Q = 17,
    WW_LEGACY_SOURCE_R = 18,
    WW_LEGACY_SOURCE_S = 19,
    WW_LEGACY_SOURCE_T = 20,
    WW_LEGACY_SOURCE_U = 21,
    WW_LEGACY_SOURCE_V = 22,
    WW_LEGACY_SOURCE_W = 23,
    WW_LEGACY_SOURCE_Y = 24,
    WW_LEGACY_SOURCE_Z = 25
};

typedef struct ww_legacy_decn_record {
    uint32_t which;
    uint32_t variant;
} ww_legacy_decn_record;

typedef struct ww_legacy_dictionary_noun_entry {
    ww_legacy_decn_record decl;
    ww_legacy_gender_t gender;
    ww_legacy_noun_kind_t kind;
    uint8_t pad_10[2];
} ww_legacy_dictionary_noun_entry;

typedef struct ww_legacy_dictionary_pronoun_entry {
    ww_legacy_decn_record decl;
    ww_legacy_pronoun_kind_t kind;
    uint8_t pad_9[3];
} ww_legacy_dictionary_pronoun_entry;

typedef struct ww_legacy_dictionary_adjective_entry {
    ww_legacy_decn_record decl;
    ww_legacy_comparison_t comparison;
    uint8_t pad_9[3];
} ww_legacy_dictionary_adjective_entry;

typedef struct ww_legacy_dictionary_numeral_entry {
    ww_legacy_decn_record decl;
    ww_legacy_numeral_sort_t sort;
    uint8_t pad_9[3];
    uint32_t value;
} ww_legacy_dictionary_numeral_entry;

typedef struct ww_legacy_dictionary_verb_entry {
    ww_legacy_decn_record conjugation;
    ww_legacy_verb_kind_t kind;
    uint8_t pad_9[3];
} ww_legacy_dictionary_verb_entry;

typedef union ww_legacy_dictionary_part_payload {
    ww_legacy_dictionary_noun_entry noun;
    ww_legacy_dictionary_pronoun_entry pronoun;
    ww_legacy_dictionary_pronoun_entry pack;
    ww_legacy_dictionary_adjective_entry adjective;
    ww_legacy_dictionary_numeral_entry numeral;
    ww_legacy_comparison_t adverb_comparison;
    ww_legacy_dictionary_verb_entry verb;
    ww_legacy_case_t preposition_case;
    uint8_t raw[16];
} ww_legacy_dictionary_part_payload;

typedef struct ww_legacy_dictionary_part {
    ww_legacy_pofs_t pofs;
    uint8_t pad_1[3];
    ww_legacy_dictionary_part_payload payload;
} ww_legacy_dictionary_part;

typedef struct ww_legacy_translation_record {
    ww_legacy_age_t age;
    ww_legacy_area_t area;
    ww_legacy_geo_t geo;
    ww_legacy_frequency_t frequency;
    ww_legacy_source_t source;
} ww_legacy_translation_record;

/* Um registro de DICTFILE.GEN: 180 bytes. */
typedef struct ww_legacy_dictionary_entry {
    char stems[4][18];
    ww_legacy_dictionary_part part;
    ww_legacy_translation_record translation;
    char meaning[80];
    uint8_t pad_177[3];
} ww_legacy_dictionary_entry;

/* Um registro de STEMFILE.GEN: 56 bytes. MNPC e um indice Ada 1-based. */
typedef struct ww_legacy_dictionary_stem {
    char stem[18];
    uint8_t pad_18[2];
    ww_legacy_dictionary_part part;
    uint32_t stem_key;
    uint8_t pad_44[4];
    uint64_t mnpc;
} ww_legacy_dictionary_stem;

typedef struct ww_legacy_tense_voice_mood {
    ww_legacy_tense_t tense;
    ww_legacy_voice_t voice;
    ww_legacy_mood_t mood;
} ww_legacy_tense_voice_mood;

typedef struct ww_legacy_inflection_nominal {
    ww_legacy_decn_record decl;
    ww_legacy_case_t grammatical_case;
    ww_legacy_number_t number;
    ww_legacy_gender_t gender;
    uint8_t pad_11;
} ww_legacy_inflection_nominal;

typedef struct ww_legacy_inflection_adjective {
    ww_legacy_decn_record decl;
    ww_legacy_case_t grammatical_case;
    ww_legacy_number_t number;
    ww_legacy_gender_t gender;
    ww_legacy_comparison_t comparison;
} ww_legacy_inflection_adjective;

typedef struct ww_legacy_inflection_numeral {
    ww_legacy_decn_record decl;
    ww_legacy_case_t grammatical_case;
    ww_legacy_number_t number;
    ww_legacy_gender_t gender;
    ww_legacy_numeral_sort_t sort;
} ww_legacy_inflection_numeral;

typedef struct ww_legacy_inflection_verb {
    ww_legacy_decn_record conjugation;
    ww_legacy_tense_voice_mood tvm;
    uint8_t person;
    ww_legacy_number_t number;
    uint8_t pad_13[3];
} ww_legacy_inflection_verb;

typedef struct ww_legacy_inflection_vpar {
    ww_legacy_decn_record conjugation;
    ww_legacy_case_t grammatical_case;
    ww_legacy_number_t number;
    ww_legacy_gender_t gender;
    ww_legacy_tense_voice_mood tvm;
    uint8_t pad_14[2];
} ww_legacy_inflection_vpar;

typedef union ww_legacy_inflection_quality_payload {
    ww_legacy_inflection_nominal noun;
    ww_legacy_inflection_nominal pronoun;
    ww_legacy_inflection_nominal pack;
    ww_legacy_inflection_adjective adjective;
    ww_legacy_inflection_numeral numeral;
    ww_legacy_comparison_t adverb_comparison;
    ww_legacy_inflection_verb verb;
    ww_legacy_inflection_vpar vpar;
    ww_legacy_inflection_nominal supine;
    ww_legacy_case_t preposition_case;
    uint8_t raw[16];
} ww_legacy_inflection_quality_payload;

typedef struct ww_legacy_inflection_quality {
    ww_legacy_pofs_t pofs;
    uint8_t pad_1[3];
    ww_legacy_inflection_quality_payload payload;
} ww_legacy_inflection_quality;

typedef struct ww_legacy_ending_record {
    uint32_t size;
    char suffix[7];
    uint8_t pad_11;
} ww_legacy_ending_record;

/* Um registro logico de INFLECTS.SEC: 40 bytes. */
typedef struct ww_legacy_inflection_record {
    ww_legacy_inflection_quality quality;
    uint32_t stem_key;
    ww_legacy_ending_record ending;
    ww_legacy_age_t age;
    ww_legacy_frequency_t frequency;
    uint8_t pad_38[2];
} ww_legacy_inflection_record;

enum {
    WW_LEGACY_DICTIONARY_ENTRY_SIZE = 180,
    WW_LEGACY_DICTIONARY_STEM_SIZE = 56,
    WW_LEGACY_INFLECTION_RECORD_SIZE = 40,
    WW_LEGACY_INFLECTIONS_PER_SECTION = 570,
    WW_LEGACY_INFLECTION_SECTION_COUNT = 5,
    WW_LEGACY_INFLECTION_SECTION_SIZE = 22800
};

WW_LEGACY_STATIC_ASSERT(sizeof(ww_legacy_decn_record) == 8, "decn layout");
WW_LEGACY_STATIC_ASSERT(sizeof(ww_legacy_dictionary_part) == 20, "dictionary part layout");
WW_LEGACY_STATIC_ASSERT(sizeof(ww_legacy_dictionary_entry) == 180, "DICTFILE record layout");
WW_LEGACY_STATIC_ASSERT(offsetof(ww_legacy_dictionary_entry, part) == 72, "DICTFILE part offset");
WW_LEGACY_STATIC_ASSERT(offsetof(ww_legacy_dictionary_entry, translation) == 92, "DICTFILE translation offset");
WW_LEGACY_STATIC_ASSERT(offsetof(ww_legacy_dictionary_entry, meaning) == 97, "DICTFILE meaning offset");
WW_LEGACY_STATIC_ASSERT(sizeof(ww_legacy_dictionary_stem) == 56, "STEMFILE record layout");
WW_LEGACY_STATIC_ASSERT(offsetof(ww_legacy_dictionary_stem, part) == 20, "STEMFILE part offset");
WW_LEGACY_STATIC_ASSERT(offsetof(ww_legacy_dictionary_stem, stem_key) == 40, "STEMFILE key offset");
WW_LEGACY_STATIC_ASSERT(offsetof(ww_legacy_dictionary_stem, mnpc) == 48, "STEMFILE MNPC offset");
WW_LEGACY_STATIC_ASSERT(sizeof(ww_legacy_inflection_quality) == 20, "quality layout");
WW_LEGACY_STATIC_ASSERT(sizeof(ww_legacy_ending_record) == 12, "ending layout");
WW_LEGACY_STATIC_ASSERT(sizeof(ww_legacy_inflection_record) == 40, "INFLECTS record layout");
WW_LEGACY_STATIC_ASSERT(offsetof(ww_legacy_inflection_record, stem_key) == 20, "INFLECTS key offset");
WW_LEGACY_STATIC_ASSERT(offsetof(ww_legacy_inflection_record, ending) == 24, "INFLECTS ending offset");
WW_LEGACY_STATIC_ASSERT(offsetof(ww_legacy_inflection_record, age) == 36, "INFLECTS age offset");

#undef WW_LEGACY_STATIC_ASSERT

#endif
