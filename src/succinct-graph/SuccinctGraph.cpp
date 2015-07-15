#include "succinct-graph/SuccinctGraph.hpp"

#include <limits>
#include <sstream>
#include <thread>

#include "succinct-graph/SuccinctGraphSerde.hpp"
#include "succinct-graph/utils.h"

#ifdef LOG_DEBUG
    #define LOG(fmt, ...) printf(fmt, ##__VA_ARGS__)
#else
    #define LOG(fmt, ...)
#endif

#define WIDTH_TIMESTAMP SuccinctGraphSerde::WIDTH_TIMESTAMP

// Hacky: represents not-specified query arguments
#define NONE -1

// Used in edge table layout only.
constexpr char NODE_ID_DELIM = '\x02';
constexpr char ATYPE_DELIM = '\x03';
constexpr char DST_ID_WIDTH_DELIM = '\x04'; // delim right after atype
constexpr char DATA_WIDTH_DELIM = '\x05'; // delim right before data width
constexpr char METADATA_DELIM = '\x06'; // delim after all these header metadata

// Used in node table layout only.
// *****Note that it is important the delim is not in DELIMITERS.*****
constexpr char NODE_TABLE_HEADER_DELIM = '\x1F';
const std::vector<unsigned char> SuccinctGraph::DELIMITERS = {
    // non-ASCII delims (ord >= 128)
    128, 129, 130, 131, 132, 133, 134, 135, 136, 137, 138, 139, 140, 141, 142,
    143, 144, 145, 146, 147,
    // ASCII delims that are not alphanumeric (unlikely to be used), ord < 128
    '\x02', '\x03', '\x04', '\x05', '\x06', '\x07', '\x08', '\x0C', '\x0D',
    '\x0E', '\x0F', '\x10', '\x11', '\x12', '\x13', '\x14', '\x15', '\x16',
    '\x17', '\x18', '\x19', '\x1A', '\x1B', '\x1C', '\x1D', '\x1E'
};


// TODO: lots of code duplication among the TAO-like functions

SuccinctGraph::SuccinctGraph(
    std::string succinct_dir,
    bool construct,
    uint32_t sa_sampling_rate,
    uint32_t isa_sampling_rate,
    uint32_t npa_sampling_rate) {
}

SuccinctGraph::SuccinctGraph(
    std::string node_succinct_dir,
    std::string edge_succinct_dir)
{
    load(node_succinct_dir, edge_succinct_dir);
}

void SuccinctGraph::load(
    std::string node_succinct_dir,
    std::string edge_succinct_dir)
{
    this->load_node_table(node_succinct_dir);
    this->load_edge_table(edge_succinct_dir);
}

void SuccinctGraph::load_node_table(std::string node_succinct_dir) {
    this->node_table = new SuccinctShard(
        0,
        node_succinct_dir,
        SuccinctMode::LOAD_MEMORY_MAPPED);
}

void SuccinctGraph::load_edge_table(std::string edge_succinct_dir) {
    this->edge_table = new SuccinctFile(
        edge_succinct_dir,
        SuccinctMode::LOAD_MEMORY_MAPPED);
}

SuccinctGraph& SuccinctGraph::set_npa_sampling_rate(uint32_t sampling_rate) {
    this->npa_sampling_rate = sampling_rate;
    return *this;
}

SuccinctGraph& SuccinctGraph::set_sa_sampling_rate(uint32_t sampling_rate) {
    this->sa_sampling_rate = sampling_rate;
    return *this;
}

SuccinctGraph& SuccinctGraph::set_isa_sampling_rate(uint32_t sampling_rate) {
    this->isa_sampling_rate = sampling_rate;
    return *this;
}

void SuccinctGraph::construct_node_table(std::string node_file) {
    LOG_E("Constructing node table with npa %d, sa %d, isa %d\n",
        npa_sampling_rate, sa_sampling_rate, isa_sampling_rate);

    // TODO: correct thing to do is use a temp file for this
    // TODO: also, the Succinct dir will have the postfix in it -- not clean?
    std::string formatted_node_file(node_file + "WithPtrs");

    std::ifstream in_stream(node_file);
    std::ofstream out_stream(formatted_node_file);
    std::string line, token;
    std::vector<int64_t> attr_lengths(MAX_NUM_NODE_ATTRS);

    // Output format:
    //
    // distance [delim] len1 [delim] ... lenMAX-1 [delim] [delim1] attr1 ... [delimMAX] attrMAX
    //
    // NOTE:
    //   (1) distance jumps us from right after its [delim] to start of attr1.
    //   (2) to jump to attrK, read from distance up to (& including) len(K-1).

    while (std::getline(in_stream, line)) {
        std::stringstream ss(line);
        attr_lengths.clear();
        int distance = 0;

        std::getline(ss, token, static_cast<char>(DELIMITERS[0])); // skip first delim

        // Need to reach DELIMITERS[MAX] as well
        for (int i = 1; i <= MAX_NUM_NODE_ATTRS; ++i) {
            // assumes consecutive use of the delimiters
            if (!std::getline(ss, token, static_cast<char>(DELIMITERS[i])))
                break;
            attr_lengths.push_back(token.length());
            // account for one delimiter after each len here
            distance += num_digits(token.length()) + 1;
        }
        out_stream << distance << NODE_TABLE_HEADER_DELIM;
        for (int64_t len : attr_lengths)
            out_stream << len << NODE_TABLE_HEADER_DELIM;
        out_stream << line << "\n";
    }
    out_stream.close();

    this->node_table = new SuccinctShard(
        0,
        formatted_node_file,
        SuccinctMode::CONSTRUCT_IN_MEMORY,
        sa_sampling_rate,
        isa_sampling_rate,
        npa_sampling_rate
    );
    this->node_table->serialize();
    LOG_E("Node table constructed and serialized\n");

    // FIXME: rely on some dtor to clean up
    char cmd[99];
    sprintf(cmd, "rm -rf %s", formatted_node_file.c_str());
    LOG_E("Running cmd: '%s'\n", cmd);
    system(cmd);
}

void SuccinctGraph::construct_edge_table(std::string edge_file) {
    LOG_E("Initializing edge table (SuccinctFile)\n");
    std::map<AssocListKey, std::vector<Assoc>> assoc_map;
    std::string line, token;
    std::ifstream edge_file_stream(edge_file);
    AType atype;
    Timestamp time;
    NodeId src_id, dst_id;

    while (std::getline(edge_file_stream, line)) {
        std::stringstream ss(line);
        int token_idx = 0;
        while (std::getline(ss, token, ' ')) {
            ++token_idx;
            if (token_idx == 1) src_id = std::stol(token);
            else if (token_idx == 2) dst_id = std::stol(token);
            else if (token_idx == 3) atype = std::stol(token);
            else if (token_idx == 4) time = std::stol(token);
            token.clear();
            if (token_idx == 4) break;
        }
        std::getline(ss, token); // rest of the data is attr
        Assoc assoc = { src_id, dst_id, atype, time, token };
        assoc_map[std::make_pair(src_id, atype)].push_back(assoc);
    }
    edge_file_stream.close();

    for (auto it = assoc_map.begin(); it != assoc_map.end(); ++it) {
        std::sort(it->second.begin(),
                  it->second.end(),
                  cmp_assoc_by_decreasing_time);
    }

    // Serialize to an .edge_table file (flat file layout)
    size_t postfix_pos = edge_file.rfind(".assoc");
    std::string edge_file_name = edge_file + ".edge_table";
    if (postfix_pos != std::string::npos)
        edge_file_name = edge_file.replace(postfix_pos, 6, ".edge_table");
    std::ofstream edge_file_out(edge_file_name);

    for (auto it = assoc_map.begin(); it != assoc_map.end(); ++it) {
        auto src_id_and_atype = it->first;

        edge_file_out << NODE_ID_DELIM
            << src_id_and_atype.first;

        edge_file_out << ATYPE_DELIM
            << src_id_and_atype.second
            << DST_ID_WIDTH_DELIM;

        std::vector<Assoc> assoc_list = it->second;

        NodeId maxDstId = -1;
        for (auto it2 = assoc_list.begin(); it2 != assoc_list.end(); ++it2) {
            maxDstId = std::max(maxDstId, it2->dst_id);
        }

        int32_t dst_id_width = num_digits(maxDstId);
        int32_t edge_width = assoc_list.begin()->attr.length();
        int64_t data_width = assoc_list.size() *
            (WIDTH_TIMESTAMP +
             dst_id_width +
             edge_width);

        edge_file_out
            << SuccinctGraphSerde::pad_dst_id_width(dst_id_width)
            << std::to_string(edge_width)
            << DATA_WIDTH_DELIM
            << std::to_string(data_width)
            << METADATA_DELIM;

        // timestamps
        for (auto it2 = assoc_list.begin(); it2 != assoc_list.end(); ++it2) {
            std::string encoded = SuccinctGraphSerde::encode_timestamp(
                it2->time);

            if (SuccinctGraphSerde::decode_timestamp(encoded) != it2->time) {
                printf("Failed: time = [%lld], encoded = [%s], decoded = [%lld]\n",
                    it2->time, encoded.c_str(), SuccinctGraphSerde::decode_timestamp(encoded));
                assert(0);
            }

            edge_file_out << encoded;
        }
        // dst node ids
        for (auto it2 = assoc_list.begin(); it2 != assoc_list.end(); ++it2) {
            std::string encoded = SuccinctGraphSerde::encode_node_id(
                it2->dst_id, dst_id_width);

            assert(SuccinctGraphSerde::decode_node_id(encoded) == it2->dst_id);

            edge_file_out << encoded;
        }
        // edge attributes
        for (auto it2 = assoc_list.begin(); it2 != assoc_list.end(); ++it2) {
            std::string attr = it2->attr; // note: no encoding
            assert(attr.length() == edge_width);
            edge_file_out << attr;
        }
    }
    edge_file_out << "\n"; // FIXME: without this, SuccinctCore ctor segfaults
    edge_file_out.close();
    LOG_E("Edge table written out to disk, now to Succinct-encode it\n");

    LOG_E("constructing edge table with npa %d, sa %d, isa %d\n",
        npa_sampling_rate, sa_sampling_rate, isa_sampling_rate);
    this->edge_table = new SuccinctFile(
        edge_file_name,
        SuccinctMode::CONSTRUCT_IN_MEMORY,
        sa_sampling_rate,
        isa_sampling_rate,
        npa_sampling_rate);
    size_t num_bytes = this->edge_table->serialize();
    LOG_E("Succinct-encoded edge table, number of bytes written: %zu\n",
        num_bytes);
}

void SuccinctGraph::construct(std::string node_file, std::string edge_file) {
    // construct in parallel
    std::thread node_table_thread(
        &SuccinctGraph::construct_node_table, this, node_file);
    this->construct_edge_table(edge_file);
    node_table_thread.join();

    this->node_file_pathname = node_file;
    this->edge_file_pathname = edge_file;
}

// Note: this is supposed to be used in testing only.
void SuccinctGraph::remove_generated_files() {
    char* cmd = new char[999];
    sprintf(cmd, "rm -rf %s", (this->node_file_pathname + ".succinct").c_str());
    system(cmd);

    sprintf(cmd, "rm -rf %s",
        (this->edge_file_pathname + ".edge_table").c_str());
    system(cmd);

    sprintf(cmd, "rm -rf %s",
        (this->edge_file_pathname + ".edge_table.succinct").c_str());
    system(cmd);

    delete[] cmd;
}

std::vector<int64_t>
SuccinctGraph::get_edge_table_offsets(NodeId id, AType atype) {
    std::vector<int64_t> res;
    std::string key(1, NODE_ID_DELIM);

    if (id == NONE && atype == NONE) {
        this->edge_table->search(res, key);
    } else if (atype == NONE) {
        this->edge_table->search(
            res, key + std::to_string(id) + ATYPE_DELIM);
    } else if (id == NONE) {
        // NOTE: since srcIds are variable-length, this case is same as first
        this->edge_table->search(res, key);
        // filter by atype
        std::string atype_str;
        for (auto it = res.begin(); it != res.end(); ) {
            int64_t curr_off = this->edge_table->skipping_extract_until(
                *it, ATYPE_DELIM);
            // TODO: extract_compare() similar to SuccinctShard can be nice here
            this->edge_table->extract_until(atype_str, curr_off, DST_ID_WIDTH_DELIM);
            if (std::stol(atype_str) != atype) {
                it = res.erase(it);
            } else {
                ++it;
            }
        }
    } else {
        // case: id & atype are specified.
        key += std::to_string(id) +
            ATYPE_DELIM +
            std::to_string(atype) +
            DST_ID_WIDTH_DELIM;
        this->edge_table->search(res, key);
        assert(res.size() <= 1);
    }
    return res;
}

void SuccinctGraph::obj_get(std::string& result, int64_t obj_id) {
    this->node_table->get(result, obj_id);
}

std::vector<SuccinctGraph::Assoc> SuccinctGraph::assoc_range(
    int64_t src, int64_t atype, int32_t off, int32_t len) {

    printf("assoc_range(src = %lld, atype = %lld, off = %d, len = %d)\n",
        src, atype, off, len);

    std::vector<int64_t> eoffs = get_edge_table_offsets(src, atype);

    std::vector<Assoc> result;
    std::string edge_width_str, data_width, timestamps, dst_ids, attrs;
    std::string dst_id_width_str;
    std::string src_id_str, atype_str;
    int32_t edge_width, dst_id_width;
    int64_t cnt, curr_off;

    if (off == NONE) off = 0; // extract from head
    int32_t len_saved = len;

    for (auto it = eoffs.begin(); it != eoffs.end(); ++it) {
        curr_off = *it;
        LOG("edge table offset = %llu\n", curr_off);
        if (curr_off == -1) continue;

        // Since the passed-in src and atype can be None, extract nonetheless
        curr_off = this->edge_table->extract_until(
            src_id_str, curr_off + 1, ATYPE_DELIM); // +1 for skip NODE_DELIM
        src = std::stol(src_id_str);

        curr_off = this->edge_table->extract_until(
            atype_str, curr_off, DST_ID_WIDTH_DELIM);
        atype = std::stol(atype_str);

        this->edge_table->extract(
            dst_id_width_str,
            curr_off,
            SuccinctGraphSerde::WIDTH_DST_ID_WIDTH_PADDED);
        LOG("extracted dst id width = '%s'\n", dst_id_width_str.c_str());
        dst_id_width = std::stoi(dst_id_width_str);
        curr_off += SuccinctGraphSerde::WIDTH_DST_ID_WIDTH_PADDED;

        curr_off = this->edge_table->extract_until(
            edge_width_str, curr_off, DATA_WIDTH_DELIM);
        LOG("extracted edge width = '%s'\n", edge_width_str.c_str());
        edge_width = std::stoi(edge_width_str);

        curr_off = this->edge_table->extract_until(
            data_width, curr_off, METADATA_DELIM);
        LOG("extracted data width = '%s'\n", data_width.c_str());

        assert(std::stol(data_width) %
            (WIDTH_TIMESTAMP + dst_id_width + edge_width) == 0);
        cnt = std::stol(data_width) /
            (WIDTH_TIMESTAMP + dst_id_width + edge_width);
        LOG("cnt = %llu\n", cnt);

        // if len is wildcard, extract all that's left
        len = std::min((int64_t) len_saved, cnt - off);
        if (len_saved == NONE) len = cnt - off;
        assert(off + len <= cnt);
        if (len <= 0) continue;

        this->edge_table->extract(
            timestamps,
            curr_off + off * WIDTH_TIMESTAMP,
            len * WIDTH_TIMESTAMP);
        LOG("extracted timestamps = '%s'\n", timestamps.c_str());

        curr_off += cnt * WIDTH_TIMESTAMP;
        this->edge_table->extract(
            dst_ids,
            curr_off + off * dst_id_width,
            len * dst_id_width);
        LOG("extracted dst ids: '%s'\n", dst_ids.c_str());

        curr_off += cnt * dst_id_width;
        this->edge_table->extract(attrs,
                                  curr_off + off * edge_width,
                                  len * edge_width);
        LOG("extracted attrs = '%s'\n", attrs.c_str());

        std::vector<int64_t> decoded_timestamps =
            SuccinctGraphSerde::decode_multi_timestamps(timestamps);

        std::vector<int64_t> decoded_dst_ids =
            SuccinctGraphSerde::decode_multi_node_ids(dst_ids, dst_id_width);

        for (int i = 0; i < decoded_timestamps.size(); ++i) {
            result.push_back(
                { src,
                  decoded_dst_ids[i],
                  atype,
                  decoded_timestamps[i],
                  attrs.substr(i * edge_width, edge_width)
                });
        }
        LOG("\n");
    }
    return result;
}

// Basic impl idea: performs binary search on the timestamps.
std::vector<SuccinctGraph::Assoc> SuccinctGraph::assoc_get(
    int64_t src,
    int64_t atype,
    std::set<int64_t> dst_id_set,
    int64_t t_low,
    int64_t t_high) {

    printf("assoc_get(src = %lld, atype = %lld, dstIdSet = ..., tLow = %lld, tHigh = %lld)\n",
        src, atype, t_low, t_high);
    std::vector<int64_t> eoffs = get_edge_table_offsets(src, atype);

    std::vector<Assoc> result;
    std::string edge_width_str, data_width, timestamps, dst_ids, attrs;
    std::string dst_id_width_str;
    std::string src_id_str, atype_str, timestamp;
    Timestamp ts;
    int32_t edge_width, dst_id_width;
    int64_t cnt, curr_off;

    for (auto it = eoffs.begin(); it != eoffs.end(); ++it) {
        curr_off = *it;
        LOG("edge table offset = %llu\n", curr_off);
        if (curr_off == -1) continue;

        // Since the passed-in src and atype can be None, extract nonetheless
        curr_off = this->edge_table->extract_until(
            src_id_str, curr_off + 1, ATYPE_DELIM); // +1 for skip NODE_DELIM
        src = std::stol(src_id_str);

        curr_off = this->edge_table->extract_until(
            atype_str, curr_off, DST_ID_WIDTH_DELIM);
        atype = std::stol(atype_str);

        this->edge_table->extract(
            dst_id_width_str,
            curr_off,
            SuccinctGraphSerde::WIDTH_DST_ID_WIDTH_PADDED);
        LOG("extracted dst id width = '%s'\n", dst_id_width_str.c_str());
        dst_id_width = std::stoi(dst_id_width_str);
        curr_off += SuccinctGraphSerde::WIDTH_DST_ID_WIDTH_PADDED;

        curr_off = this->edge_table->extract_until(
            edge_width_str, curr_off, DATA_WIDTH_DELIM);
        LOG("extracted edge width = '%s'\n", edge_width_str.c_str());
        edge_width = std::stoi(edge_width_str);

        curr_off = this->edge_table->extract_until(
            data_width, curr_off, METADATA_DELIM);
        LOG("extracted data width = '%s'\n", data_width.c_str());

        assert(std::stol(data_width) %
            (WIDTH_TIMESTAMP + dst_id_width + edge_width) == 0);
        cnt = std::stol(data_width) /
            (WIDTH_TIMESTAMP + dst_id_width + edge_width);
        LOG("cnt = %llu\n", cnt);

        int range_left, range_right; // in-range: [left, right]

        // binary search: locates smallest t s.t. t >= t_low
        // invariant: target in [l, r)
        int l = 0, r = cnt, m;
        if (t_low != NONE) {
            // check t_l >= t_low
            this->edge_table->extract(timestamp, curr_off, WIDTH_TIMESTAMP);
            ts = SuccinctGraphSerde::decode_timestamp(timestamp);
            if (ts < t_low) continue;

            while (l + 1 < r) {
                m = (l + r) / 2;
                this->edge_table->extract(
                    timestamp, curr_off + m * WIDTH_TIMESTAMP, WIDTH_TIMESTAMP);
                ts = SuccinctGraphSerde::decode_timestamp(timestamp);
                if (ts >= t_low) l = m; // note timestamps are decreasing
                else r = m;
            }
            assert(l + 1 == r);
            range_right = l;
        } else {
            // if no time lower bound, just extract all early edges
            range_right = cnt - 1;
        }

        // binary search: locates largest t s.t. t <= t_high
        // invariant: target in (l, r]
        if (t_high != NONE) {
            l = -1;
            r = cnt - 1;

            // check t_r <= t_high
            this->edge_table->extract(timestamp, curr_off + r * WIDTH_TIMESTAMP, WIDTH_TIMESTAMP);
            ts = SuccinctGraphSerde::decode_timestamp(timestamp);
            if (ts > t_high) continue;

            while (l + 1 < r) {
                m = (l + r) / 2;
                this->edge_table->extract(
                    timestamp, curr_off + m * WIDTH_TIMESTAMP, WIDTH_TIMESTAMP);
                ts = SuccinctGraphSerde::decode_timestamp(timestamp);
                if (ts > t_high) l = m;
                else r = m;
            }
            assert(l + 1 == r);
            range_left = r;
        } else {
            // if no time lower bound, just extract all latest edges
            range_left = 0;
        }

        LOG("range left: %d, range right: %d, cnt: %lld\n", range_left, range_right, cnt);
        if (range_left > range_right) continue;

        // extract in-range timestamps
        this->edge_table->extract(
            timestamps,
            curr_off + range_left * WIDTH_TIMESTAMP,
            (range_right - range_left + 1) * WIDTH_TIMESTAMP);
        LOG("extracted timestamps = '%s'\n", timestamps.c_str());

        // extract in-range dst ids: i.e. whose idx in [range_left, range_right]
        curr_off += cnt * WIDTH_TIMESTAMP;
        this->edge_table->extract(
            dst_ids,
            curr_off + range_left * dst_id_width,
            (range_right - range_left + 1) * dst_id_width);
        LOG("extracted dst ids: '%s'\n", dst_ids.c_str());

        std::vector<int64_t> decoded_dst_ids =
            SuccinctGraphSerde::decode_multi_node_ids(dst_ids, dst_id_width);

        // filter
        std::vector<int64_t> in_set_indexes;
        for (int i = 0; i < decoded_dst_ids.size(); ++i) {
            LOG("decoded_dst_id[i=%d] = %lld\n", i, decoded_dst_ids[i]);

            if (dst_id_set.count(decoded_dst_ids[i]) != 0) {
                in_set_indexes.push_back(range_left + i);
                LOG("pass filter id: %d, decoded_dst_id[i] = %lld\n", range_left + i, decoded_dst_ids[i]);
            }
        }

        // TODO: another choice is to do a single extract then filter; evaluate?
        // Now extract only the in-set (and in-range) attrs
        curr_off += cnt * dst_id_width;
        std::vector<std::string> valid_attrs;
        int idx;
        for (int i = 0; i < in_set_indexes.size(); ++i) {
            idx = in_set_indexes[i];
            std::string attr;
            this->edge_table->extract(attr, curr_off + idx * edge_width, edge_width);
            valid_attrs.push_back(attr);
        }

        std::vector<int64_t> decoded_timestamps =
            SuccinctGraphSerde::decode_multi_timestamps(timestamps);
        for (int i = 0; i < in_set_indexes.size(); ++i) {
            idx = in_set_indexes[i];
            // decoded dst ids and timestamps start w/ absolute idx range_left
            result.push_back(
                { src,
                  decoded_dst_ids[idx - range_left],
                  atype,
                  decoded_timestamps[idx - range_left],
                  valid_attrs[i]
                });
        }
    }
    return result;
}

int64_t SuccinctGraph::assoc_count(int64_t src, int64_t atype) {
    std::vector<int64_t> eoffs = get_edge_table_offsets(src, atype);
    int64_t total_cnt = 0;
    std::string edge_width_str, data_width, dst_id_width_str;
    int32_t dst_id_width;

    for (auto curr_off : eoffs) {
        if (curr_off == -1) continue;

        curr_off = this->edge_table->skipping_extract_until(
            curr_off, DST_ID_WIDTH_DELIM);

        this->edge_table->extract(
            dst_id_width_str,
            curr_off,
            SuccinctGraphSerde::WIDTH_DST_ID_WIDTH_PADDED);
        LOG("extracted dst id width = '%s'\n", dst_id_width_str.c_str());
        dst_id_width = std::stoi(dst_id_width_str);
        curr_off += SuccinctGraphSerde::WIDTH_DST_ID_WIDTH_PADDED;

        curr_off = this->edge_table->extract_until(
            edge_width_str, curr_off, DATA_WIDTH_DELIM);
        int32_t edge_width = std::stoi(edge_width_str);

        curr_off = this->edge_table->extract_until(
            data_width, curr_off, METADATA_DELIM);

        assert(std::stol(data_width) %
            (WIDTH_TIMESTAMP + dst_id_width + edge_width) == 0);
        total_cnt += std::stol(data_width) /
            (WIDTH_TIMESTAMP + dst_id_width + edge_width);
    }
    return total_cnt;
}

std::vector<SuccinctGraph::Assoc> SuccinctGraph::assoc_time_range(
    int64_t src,
    int64_t atype,
    int64_t t_low,
    int64_t t_high,
    int32_t len) {

    printf("assoc_time_range(src = %lld, atype = %lld, tLow = %lld, tHigh = %lld, len = %d)\n",
        src, atype, t_low, t_high, len);

    std::vector<int64_t> eoffs = get_edge_table_offsets(src, atype);

    std::vector<Assoc> result;
    std::string edge_width_str, data_width, timestamps, dst_ids, attrs;
    std::string dst_id_width_str;
    std::string src_id_str, atype_str, timestamp;
    Timestamp ts;
    int32_t edge_width, dst_id_width;
    int64_t cnt;

    int32_t len_saved = len;

    for (int64_t curr_off : eoffs) {
        LOG("edge table offset = %llu\n", curr_off);
        if (curr_off == -1) continue;

        // Since the passed-in src and atype can be None, extract nonetheless
        curr_off = this->edge_table->extract_until(
            src_id_str, curr_off + 1, ATYPE_DELIM); // +1 for skip NODE_DELIM
        src = std::stol(src_id_str);

        curr_off = this->edge_table->extract_until(
            atype_str, curr_off, DST_ID_WIDTH_DELIM);
        atype = std::stol(atype_str);

        this->edge_table->extract(
            dst_id_width_str,
            curr_off,
            SuccinctGraphSerde::WIDTH_DST_ID_WIDTH_PADDED);
        LOG("extracted dst id width = '%s'\n", dst_id_width_str.c_str());
        dst_id_width = std::stoi(dst_id_width_str);
        curr_off += SuccinctGraphSerde::WIDTH_DST_ID_WIDTH_PADDED;

        curr_off = this->edge_table->extract_until(
            edge_width_str, curr_off, DATA_WIDTH_DELIM);
        LOG("extracted edge width = '%s'\n", edge_width_str.c_str());
        edge_width = std::stoi(edge_width_str);

        curr_off = this->edge_table->extract_until(
            data_width, curr_off, METADATA_DELIM);
        LOG("extracted data width = '%s'\n", data_width.c_str());

        assert(std::stol(data_width) %
            (WIDTH_TIMESTAMP + dst_id_width + edge_width) == 0);
        cnt = std::stol(data_width) /
            (WIDTH_TIMESTAMP + dst_id_width + edge_width);
        LOG("cnt = %llu\n", cnt);

        // if len is wildcard, extract all that's left
        if (len_saved == NONE) len = cnt;

        int range_left, range_right; // in-range: [left, right]
        int l, r, m;

        if (t_low != NONE) {
            l = 0;
            r = cnt;

            // check t_l >= t_low
            this->edge_table->extract(timestamp, curr_off, WIDTH_TIMESTAMP);
            ts = SuccinctGraphSerde::decode_timestamp(timestamp);
            if (ts < t_low) continue;

            // binary search: locates smallest t s.t. t >= t_low
            // invariant: target in [l, r)
            while (l + 1 < r) {
                m = (l + r) / 2;
                this->edge_table->extract(
                    timestamp, curr_off + m * WIDTH_TIMESTAMP, WIDTH_TIMESTAMP);
                ts = SuccinctGraphSerde::decode_timestamp(timestamp);
                if (ts >= t_low) l = m; // note timestamps are decreasing
                else r = m;
            }
            assert(l + 1 == r);
            range_right = l;
        } else {
            // if no time lower bound, just extract all early edges
            range_right = cnt - 1;
        }

        // binary search: locates largest t s.t. t <= t_high
        // invariant: target in (l, r]
        if (t_high != NONE) {
            l = -1;
            r = cnt - 1;

            // check t_r <= t_high
            this->edge_table->extract(timestamp, curr_off + r * WIDTH_TIMESTAMP, WIDTH_TIMESTAMP);
            ts = SuccinctGraphSerde::decode_timestamp(timestamp);
            if (ts > t_high) continue;

            while (l + 1 < r) {
                m = (l + r) / 2;
                this->edge_table->extract(
                    timestamp, curr_off + m * WIDTH_TIMESTAMP, WIDTH_TIMESTAMP);
                ts = SuccinctGraphSerde::decode_timestamp(timestamp);
                if (ts > t_high) l = m;
                else r = m;
            }
            assert(l + 1 == r);
            range_left = r;
        } else {
            // if no time lower bound, just extract all latest edges
            range_left = 0;
        }

        // limit to first `len` edges
        range_right = std::min(range_right, range_left + len - 1);
        LOG("range left: %d, range right: %d, cnt: %lld\n", range_left, range_right, cnt);
        if (range_left > range_right) continue;

        // extract in-range timestamps
        std::string timestamps;
        this->edge_table->extract(timestamps,
                                  curr_off + range_left * WIDTH_TIMESTAMP,
                                  (range_right - range_left + 1) * WIDTH_TIMESTAMP);
        LOG("extracted timestamps = '%s'\n", timestamps.c_str());

        // extract in-range dst ids: i.e. whose idx in [range_left, range_right]
        curr_off += cnt * WIDTH_TIMESTAMP;
        std::string dst_ids;
        this->edge_table->extract(dst_ids,
                                  curr_off + range_left * dst_id_width,
                                  (range_right - range_left + 1) * dst_id_width);
        LOG("extracted dst ids: '%s'\n", dst_ids.c_str());

        curr_off += cnt * dst_id_width;
        std::string attrs;
        this->edge_table->extract(attrs,
                                  curr_off + range_left * edge_width,
                                  (range_right - range_left + 1) * edge_width);
        LOG("extracted attrs = '%s'\n", attrs.c_str());

        std::vector<int64_t> decoded_timestamps =
            SuccinctGraphSerde::decode_multi_timestamps(timestamps);
        std::vector<int64_t> decoded_dst_ids =
            SuccinctGraphSerde::decode_multi_node_ids(dst_ids, dst_id_width);
        for (int i = 0; i < decoded_timestamps.size(); ++i) {
            result.push_back(
                { src,
                  decoded_dst_ids[i],
                  atype,
                  decoded_timestamps[i],
                  attrs.substr(i * edge_width, edge_width)
                });
        }
    }
    return result;
}

std::string SuccinctGraph::succinct_directory() {
    return this->succinct_dir;
}

int64_t SuccinctGraph::num_nodes() {
    return this->node_table->num_keys() - 1; // FIXME: a bug in SuccinctShard
}

int64_t SuccinctGraph::num_edges() {
    return edges;
}

size_t SuccinctGraph::storage_size() {
}

size_t SuccinctGraph::serialize() {
}

/******* Primitive APIs *******/

// This might not be most efficient as we don't jump over the lengths.
void SuccinctGraph::get_attribute(
    std::string& result,
    int64_t node_id,
    int attr) {

    assert(attr < MAX_NUM_NODE_ATTRS);
    uint64_t suf_arr_idx = -1;

    this->node_table->extract_until(
        result, suf_arr_idx, node_id, static_cast<char>(DELIMITERS[attr]));

    this->node_table->extract_until(
        result, suf_arr_idx, node_id, static_cast<char>(DELIMITERS[attr + 1]));
}

inline void SuccinctGraph::extract_neighbors(
    std::vector<int64_t>& result,
    const std::vector<int64_t>& offsets,
    int32_t skip_length) {

#ifdef BYTES_EXTRACTED
    int64_t bytes_extracted = 0;
    bytes_extracted += offsets.size() *
        (SuccinctGraphSerde::WIDTH_EDGE_WIDTH_PADDED +
         SuccinctGraphSerde::WIDTH_DATA_WIDTH_PADDED);
#endif

    result.clear();
    std::string edge_width_str, data_width, dst_ids, dst_id_width_str;
    uint64_t suf_arr_idx;
    LOG("in extract_nhbrs()\n");
    for (auto it = offsets.begin(); it != offsets.end(); ++it) {
        suf_arr_idx = -1;

        int64_t curr_off = this->edge_table->skipping_extract_until(
            suf_arr_idx, (*it) + skip_length, DST_ID_WIDTH_DELIM);

        this->edge_table->extract(
            dst_id_width_str,
            suf_arr_idx,
            curr_off,
            SuccinctGraphSerde::WIDTH_DST_ID_WIDTH_PADDED);
        LOG("dst id width = '%s'\n", dst_id_width_str.c_str());

        curr_off = this->edge_table->extract_until(
            edge_width_str,
            suf_arr_idx,
            curr_off + SuccinctGraphSerde::WIDTH_DST_ID_WIDTH_PADDED,
            DATA_WIDTH_DELIM);
        LOG("edge width = '%s'\n", edge_width_str.c_str());

        curr_off = this->edge_table->extract_until(
            data_width,
            suf_arr_idx,
            curr_off,
            METADATA_DELIM);
        LOG("data width = '%s'\n", data_width.c_str());

        int32_t dst_id_width = std::stoi(dst_id_width_str);
        int32_t edge_width = std::stoi(edge_width_str);

        assert(std::stol(data_width) %
            (WIDTH_TIMESTAMP + dst_id_width + edge_width) == 0);

        int64_t cnt = std::stol(data_width) /
            (WIDTH_TIMESTAMP + dst_id_width + edge_width);
        LOG("curr off = %lld, cnt = %lld\n", curr_off, cnt);

        curr_off += cnt * WIDTH_TIMESTAMP;

        this->edge_table->extract(dst_ids, curr_off, cnt * dst_id_width);
        LOG("dst ids = '%s'\n", dst_ids.c_str());

#ifdef BYTES_EXTRACTED
        bytes_extracted += cnt * dst_id_width;
#endif

        std::vector<int64_t> decoded(SuccinctGraphSerde::decode_multi_node_ids(
            dst_ids, dst_id_width));

        result.insert(result.end(), decoded.begin(), decoded.end());
    }

#ifdef BYTES_EXTRACTED
    printf(",%lld\n", bytes_extracted);
#endif
}

void SuccinctGraph::get_neighbors(std::vector<int64_t>& result, int64_t node) {

#ifdef DEBUG_TIME_NHBR1
    auto t1 = get_timestamp();
#endif

    std::vector<int64_t> offsets;
    this->edge_table->search(
        offsets, NODE_ID_DELIM + std::to_string(node) + ATYPE_DELIM);

#ifdef DEBUG_TIME_NHBR1
    auto t2 = get_timestamp();
    printf(".%lld\n", t2 - t1);
    t1 = get_timestamp();
#endif

    // skip node delim, node, atype delim
    extract_neighbors(result, offsets, num_digits(node) + 2);

#ifdef DEBUG_TIME_NHBR1
    t2 = get_timestamp();
    printf(",%lld\n", t2 - t1);
#endif
}

inline std::string mk_node_attr_key(int attr, const std::string& query_key) {
    assert(attr < SuccinctGraph::MAX_NUM_NODE_ATTRS);
    return static_cast<char>(SuccinctGraph::DELIMITERS[attr]) +
        query_key +
        static_cast<char>(SuccinctGraph::DELIMITERS[attr + 1]);
}

void SuccinctGraph::filter_nodes(
    std::vector<int64_t>& result,
    const std::vector<int64_t>& node_ids,
    int attr,
    const std::string& search_key)
{
    assert(attr < SuccinctGraph::MAX_NUM_NODE_ATTRS);
    result.clear();
    const char next_attr_delim = static_cast<char>(DELIMITERS[attr + 1]);
    std::string tmp;

    for (int64_t node_id : node_ids) {
        uint64_t suf_arr_idx = -1;
        int64_t start_offset = this->node_table->extract_until(
            tmp, suf_arr_idx, node_id, NODE_TABLE_HEADER_DELIM);
        if (start_offset == -1) continue; // key doesn't exist
        // +(attr + 1) to account for delims after each of the lengths
        int32_t dist = std::stoi(tmp) + (attr + 1);

        for (int i = 1; i <= attr; ++i) {
            this->node_table->extract_until(
                tmp, suf_arr_idx, node_id, NODE_TABLE_HEADER_DELIM);
            dist += std::stoi(tmp);
        }

        // jump!
        if (this->node_table->extract_compare_until(
                start_offset + dist, next_attr_delim, search_key))
        {
            result.push_back(node_id);
        }
    }
}

// Scans neighbors and looks for those that contain the desired attr.
void SuccinctGraph::get_neighbors(
    std::vector<int64_t>& result,
    int64_t node_id,
    int attr,
    const std::string& search_key) {

#ifdef DEBUG_TIME_NHBR2
    auto t1 = get_timestamp();
#endif

    assert(attr < SuccinctGraph::MAX_NUM_NODE_ATTRS);
    std::vector<int64_t> nbhrs;
    get_neighbors(nbhrs, node_id);

#ifdef DEBUG_TIME_NHBR2
    auto t2 = get_timestamp();
    printf(".%lld\n", t2 - t1);
    if (t2 - t1 == 0) assert(nbhrs.empty());
    t1 = get_timestamp();
#endif

    filter_nodes(result, nbhrs, attr, search_key);

#ifdef DEBUG_TIME_NHBR2
    t2 = get_timestamp();
    printf(",%lld\n", t2 - t1);
#endif
}

void SuccinctGraph::get_neighbors(
    std::vector<int64_t>& result,
    int64_t node,
    int64_t atype) {

#ifdef DEBUG_TIME_NHBR3
    auto t1 = get_timestamp();
#endif

    std::vector<int64_t> offsets;
    this->edge_table->search(
        offsets,
        NODE_ID_DELIM + std::to_string(node) +
        ATYPE_DELIM + std::to_string(atype) +
        DST_ID_WIDTH_DELIM);

#ifdef DEBUG_TIME_NHBR3
    auto t2 = get_timestamp();
    printf(".%lld\n", t2 - t1);
    t1 = get_timestamp();
#endif

    // skip 2 delims & node & atype, i.e. first ISA lookup will hit dst id delim
    extract_neighbors(result, offsets, num_digits(node) + num_digits(atype) + 2);

#ifdef DEBUG_TIME_NHBR3
    t2 = get_timestamp();
    printf(",%lld\n", t2 - t1);
#endif
}

void SuccinctGraph::get_nodes(
    std::set<int64_t>& result,
    int attr,
    const std::string& search_key) {

    result.clear();
    this->node_table->search(result, mk_node_attr_key(attr, search_key));
}

void SuccinctGraph::get_nodes(
    std::set<int64_t>& result,
    int attr1,
    const std::string& search_key1,
    int attr2,
    const std::string& search_key2) {

    result.clear();
    std::set<int64_t> s1, s2;
    this->node_table->search(s1, mk_node_attr_key(attr1, search_key1));
    this->node_table->search(s2, mk_node_attr_key(attr2, search_key2));
    // result.end() is a hint that supposedly is faster than .begin()
    std::set_intersection(s1.begin(), s1.end(), s2.begin(), s2.end(),
                          std::inserter(result, result.end()));
}
