#include <emscripten/bind.h>
#include <emscripten/val.h>

using namespace emscripten;

/*

EMSCRIPTEN_BINDINGS(my_module) {
    emscripten::class_<QueryEngine>("QueryEngine")
        .constructor<>()
        .function("tokenize", &QueryEngine::tokenize)
        .function("load_index_map_raw", &QueryEngine::load_index_map_raw)
        .function("get_shard_files", &QueryEngine::get_shard_files)
        .function("get_docs_per_shard", &QueryEngine::get_docs_per_shard)
        .function("query_shard_docs_raw_limited",
                  &QueryEngine::query_shard_docs_raw_limited)
        .function("get_documents_blob_raw",
                  &QueryEngine::get_documents_blob_raw);

    emscripten::register_vector<std::string>("VectorString");
    emscripten::register_vector<uint32_t>("VectorUint32");

    emscripten::value_object<Importance>("Importance")
        .field("term_freq", &Importance::term_freq)
        .field("local_importance", &Importance::local_importance);

    emscripten::value_object<DocumentView>("Document")
        .field("id", &DocumentView::id)
        .field("name", &DocumentView::name)
        .field("url", &DocumentView::url)
        .field("tokens", &DocumentView::tokens);

    emscripten::register_vector<DocumentView>("VectorDocument");

    emscripten::value_array<std::pair<uint32_t, Importance>>(
        "PairUint32Importance")
        .element(&std::pair<uint32_t, Importance>::first)
        .element(&std::pair<uint32_t, Importance>::second);

    emscripten::register_vector<std::pair<uint32_t, Importance>>(
        "VectorPairUint32Importance");

    emscripten::value_object<WordResult>("WordResult")
        .field("word", &WordResult::word)
        .field("docImp", &WordResult::docImp)
        .field("global_importance", &WordResult::global_importance);
    emscripten::register_vector<WordResult>("VectorWordResult");
}

*/
