#ifndef GRAPH_LOG_STORE_H
#define GRAPH_LOG_STORE_H

#include "KVLogStore.h"
#include "StructuredEdgeTable.h"

#include <set>
#include <string>
#include <vector>

// A graph that is backed by LogStore and thus supports individual node/edge
// insertions.  In other words, the Node Table is backed by KVLogStore, and the
// Edge Table by FileLogStore.
//
// Limitation: it relies on an ngram index to perform search on the node
// properties.  By default, n = 3, and therefore we can't search along an empty
// search string (we could if n is smaller).
class GraphLogStore {
public:

    // The `node_file` here is just the un-delimited node properties (the
    // values file).  The keys are assumed to be 0-based line numbers; the
    // key-value pointers are computed by using newlines as record delimiters.
    GraphLogStore(
        const std::string& node_file,
        const std::string& edge_file)
        : node_file_(node_file),
          edge_file_(edge_file),
          node_pointer_file(""), // FIXME?
          edge_table_(edge_file)
    { }

    void init(int option = 1);

    // TODO: think about where this key should come from; and locking.
    // Limitation: `node_id` must be larger than all current node_id's managed
    // by the current GraphLogStore (because insertion sort is not done).  Note
    // that these id's are local keys.
    //
    // Thread-safe: internally, a lock is used.
    void append_node(int64_t node_id, std::vector<std::string>& attrs);

    // Thread-safe: internally, a lock is used.
    void append_edge(
        int64_t src,
        int64_t atype,
        int64_t dst,
        int64_t timestamp,
        const std::string& attr);

    // An incomplete and/or modified set of Succinct Graph API below

    void get_attribute(std::string& result, int64_t node_id, int attr);

    void get_nodes(
        std::set<int64_t>& result,
        int attr,
        const std::string& search_key);

    void get_nodes(
        std::set<int64_t>& result,
        int attr1,
        const std::string& search_key1,
        int attr2,
        const std::string& search_key2);

    std::vector<SuccinctGraph::Assoc> assoc_range(
        int64_t src,
        int64_t atype,
        int32_t off,
        int32_t len);

private:
    const std::string node_file_, edge_file_;
    std::string node_pointer_file;

    std::shared_ptr<KVLogStore> node_table_ = nullptr;
    StructuredEdgeTable edge_table_;

};

#endif