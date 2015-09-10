#ifndef SUCCINCT_GRAPH_H
#define SUCCINCT_GRAPH_H

// FIXME: encouraged to include relative to project's include path
#include "succinct_shard.h"
#include "succinct_file.h"
#include "KeepInputSuccinctFile.h"

#include <sys/time.h>

class SuccinctGraph {
public:

    // TODO: get rid of this, currently succinct-create depends on it.
    // Constructor.  This doesn't actually build the internal data structures.
    SuccinctGraph(std::string succinct_dir = 0,
                  bool construct = false,
                  uint32_t sa_sampling_rate = 32,
                  uint32_t isa_sampling_rate = 32,
                  uint32_t npa_sampling_rate = 128);

    // Loads the previously constructed node table & edge table.
    // The same as load().
    SuccinctGraph(std::string node_succinct_dir, std::string edge_succinct_dir);

    ~SuccinctGraph() {
        if (this->node_table != nullptr) {
            delete this->node_table;
        }
        if (this->edge_table != nullptr) {
            delete this->edge_table;
        }
        if (this->edge_table_with_input_ != nullptr) {
            delete this->edge_table_with_input_;
        }
    }

    // Removes generated files during construction, if any: Succinct data
    // structures and the formatted .edge_table file.
    void remove_generated_files();

    /** Setters that can modify default settings. */
    SuccinctGraph& set_npa_sampling_rate(uint32_t sampling_rate);
    SuccinctGraph& set_sa_sampling_rate(uint32_t sampling_rate);
    SuccinctGraph& set_isa_sampling_rate(uint32_t sampling_rate);

    // Constructs the node/edge tables and Succinct-encodes them, using
    // previously specified (possibly default) settings.
    //
    //   node_file: each row contains attributes (bytes) for the node
    //              whose ID is current row number - 1, separated by
    //              unique delimiters in DELIMITERS
    //   edge_file: each row represents one association, in format
    //              srcId dstId atype time [everything from here to EOL is attr]
    void construct(std::string node_file, std::string edge_file);
    // The two steps in construct().  Intended for greater flexibility: users
    // can construct one table without the other.
    void construct_node_table(std::string node_file);
    void construct_edge_table(std::string edge_file);

    // Loads constructed & Succinct-encoded tables.
    void load(std::string node_succinct_dir, std::string edge_succinct_dir);
    void load_node_table(std::string node_succinct_dir);
    void load_edge_table(std::string edge_succinct_dir);

    std::string succinct_directory();

    int64_t num_nodes();
    int64_t num_edges();
    int64_t num_attributes();

    size_t storage_size();
    size_t serialize();

    /**************** Internal formats ****************/
    // C.f. the LinkBench paper, Sigmoid 2013

    typedef int64_t NodeId;
    typedef int64_t Timestamp;
    typedef int64_t AType;

    typedef std::pair<NodeId, AType> AssocListKey;

    struct Assoc {
        NodeId src_id; // 8 bytes
        NodeId dst_id; // 8 bytes
        AType atype; // 8 bytes
        Timestamp time; // 8 bytes
        std::string attr; // variable bytes
    };

    static bool cmp_assoc_by_decreasing_time(const Assoc &a, const Assoc &b) {
        return a.time > b.time;
    }

    /**************** Primitive APIs ****************/

    // Clears `result` for caller.  Used in query generation and is not
    // efficient enough for general query's use.
    void get_attribute(std::string& result, int64_t node_id, int attr);

    // TODO: decide whether to return set for get_neighbors() as well.

    // Clears `result` for caller.
    void get_neighbors(std::vector<int64_t>& result, int64_t node);

    // Clears `result` for caller.
    void get_neighbors(
        std::vector<int64_t>& result,
        int64_t node,
        int attr,
        const std::string& search_key);

    // Clears `result` for caller.
    void get_neighbors(
        std::vector<int64_t>& result,
        int64_t node,
        int64_t atype);

    // Clears `result` for caller.
    void get_nodes(
        std::set<int64_t>& result,
        int attr,
        const std::string& search_key);

    // Clears `result` for caller.
    void get_nodes(
        std::set<int64_t>& result,
        int attr1,
        const std::string& search_key1,
        int attr2,
        const std::string& search_key2);

    // Clears `result` for caller.
    void filter_nodes(
        std::vector<int64_t>& result,
        const std::vector<int64_t>& node_ids,
        int attr,
        const std::string& search_key);

    /**************** TAO-like APIs ****************/

    static void
    print_assoc_results(const std::vector<Assoc>& assoc_results) {
        for (auto it = assoc_results.begin(); it != assoc_results.end(); ++it) {
            printf("[ %lld-->%lld, atype %lld, time %lld, attr '%s' ]\n",
                it->src_id, it->dst_id, it->atype, it->time, it->attr.c_str());
        }
        printf("\n");
    }

    // Gets all attribute values of node `obj_id` into `result`.  Clears
    // `result` for the caller.  Upon return, the size() of `result` equals
    // to the smallest attr index the range from which to MAX_NUM_NODE_ATTRS
    // (left inclusive, right exclusive) denotes empty attributes for this node.
    //
    // In other words, an empty string for an attribute is treated as if the
    // node doesn't have that attribute set.
    void obj_get(std::vector<std::string>& result, int64_t obj_id);

    // All arguments can be optional (use -1 for none) with the natural
    // semantics.
    std::vector<Assoc> assoc_range(
        int64_t src,
        int64_t atype,
        int32_t off,
        int32_t len);

    // All arguments, except for `dst_id_set`, can be optional (use -1 for
    // none) with the natural semantics.
    std::vector<Assoc> assoc_get(
        int64_t src,
        int64_t atype,
        const std::set<int64_t>& dst_id_set,
        int64_t t_low,
        int64_t t_high);

    // Returns number of associations in the association list (src, atype).
    // Undefined behavior if (src, atype) doesn't exist.
    // All arguments can be optional.
    int64_t assoc_count(int64_t src, int64_t atype);

    // All arguments can be optional.
    std::vector<Assoc> assoc_time_range(
        int64_t src,
        int64_t atype,
        int64_t t_low,
        int64_t t_high,
        int32_t len);

    /**************** Fields ****************/

    // Succinct compression params: currently same for node table & edge table.
    uint32_t sa_sampling_rate = 64;
    uint32_t isa_sampling_rate = 64;
    uint32_t npa_sampling_rate = 256;

    // TODO: consider moving these to GraphFormatter / Serde?
    // Internal node attributes delimiters.  Assumes any char of them doesn't
    // appear in the actual node attributes passed-in by user input.
    const static std::vector<unsigned char> DELIMITERS;

    // Hard assumption: support up to this many # of node attributes.  The
    // character in DELIMITERS indexed by this is used as a special
    // end-of-record delim  appended to every value in node table.
    constexpr static int MAX_NUM_NODE_ATTRS = 40;

    // Recorded inside construct().
    std::string node_file_pathname, edge_file_pathname;

private:

    SuccinctShard* node_table = nullptr;
    SuccinctFile* edge_table = nullptr;
    KeepInputSuccinctFile* edge_table_with_input_ = nullptr;

    std::string succinct_dir;
    int64_t edges;

    // Returns a list of edge table offsets; result is a list since the two
    // arguments can be omitted (i.e. as wildcards, represented as -1 for now).
    // An edge table offset is -1 iff an assoc list doesn't exist.
    std::vector<int64_t> get_edge_table_offsets(NodeId id, AType atype);

    void extract_neighbors(
        std::vector<int64_t>& result,
        const std::vector<int64_t>& offsets,
        int32_t skip_length);

    // Binary search: locates smallest timestamp t, such that t >= t_low.
    // Upon entry, `curr_off` must point to the start of the timestamps of the
    // current association list in the edge table, and `cnt` denotes the number
    // of assocs in this list.  Returns -1 if no such indexes exist.
    int time_range_binary_search_lower_bound(
        Timestamp t_low,
        int64_t cnt,
        int64_t curr_off,
        std::string& tmp_token,
        const int32_t timestamp_width);

    // Binary search: locates largest timestamp t, such that t <= t_high.
    int time_range_binary_search_upper_bound(
        Timestamp t_high,
        int64_t cnt,
        int64_t curr_off,
        std::string& tmp_token,
        const int32_t timestamp_width);

    inline static time_t get_timestamp() {
        struct timeval now;
        gettimeofday (&now, NULL);

        return  now.tv_usec + (time_t)now.tv_sec * 1000000;
    }

    // This can be > 5x faster (loop unroll / static lookup).
    inline static int32_t num_digits(int64_t number) {
       if (number == 0) return 1;
       int32_t digits = 0;
       while (number != 0) {
           number /= 10;
           ++digits;
       }
       return digits;
    }

};

#endif