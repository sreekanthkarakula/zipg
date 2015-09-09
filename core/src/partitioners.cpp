#include "partitioners.hpp"

#include <memory>
#include <unistd.h>

#include "utils.h"

void RangePartitioner::partition(
    const std::string& node_file_in,
    const std::string& edge_file_in)
{

    std::ifstream node_file_stream(node_file_in);
    std::string line, curr_split_filename;

    std::vector<std::string> lines;
    while (std::getline(node_file_stream, line)) {
        lines.push_back(line);
    }

    assert(static_cast<size_t>(this->num_shards_) <= lines.size()); // we can relax this assumption
    int lines_per_split = lines.size() / this->num_shards_;
    int num_shards_digits = num_digits(this->num_shards_);
    int diff = lines.size() - lines_per_split * this->num_shards_;
    int shard_idx = 0;

    size_t split_start_line = 0;
    while (split_start_line < lines.size()) {
        std::string s(format_out_name(
            node_file_in, num_shards_digits, shard_idx, this->num_shards_));
        std::ofstream curr_split_ofstream(s);
        size_t j = split_start_line;
        for ( ; j < split_start_line + lines_per_split && j < lines.size(); ++j)
            curr_split_ofstream << lines.at(j) << std::endl;

        // first diff shards output one more line
        if (shard_idx < diff) {
            ++split_start_line;
            curr_split_ofstream << lines.at(j) << std::endl;
        }
        split_start_line += lines_per_split;
        ++shard_idx;
    }

    /*********** edge file ***********/

    std::ifstream edge_file_stream(edge_file_in);
    int64_t curr_id_limit = lines_per_split;
    if (diff > 0) ++curr_id_limit;

    std::string src_id_str;
    shard_idx = 0;
    std::ofstream curr_split_ofstream(format_out_name(
        edge_file_in, num_shards_digits, 0, num_shards_));

    while (std::getline(edge_file_stream, line)) {
        std::stringstream ss(line);
        std::getline(ss, src_id_str, ' ');
        while (std::stol(src_id_str) >= curr_id_limit) {
            // initialize new stream
            ++shard_idx;
            curr_id_limit += lines_per_split;
            if (shard_idx < diff) ++curr_id_limit; // leftovers
            if (std::stol(src_id_str) < curr_id_limit) {
                curr_split_ofstream.close();
                curr_split_ofstream.open(format_out_name(
                    edge_file_in, num_shards_digits, shard_idx, num_shards_));
            }
        }
        curr_split_ofstream << line << std::endl;
    }
}

// TODO: partition two files in parallel
void HashPartitioner::partition(
    const std::string& node_file_in,
    const std::string& edge_file_in)
{
    int32_t N = this->num_shards_;
    auto id_to_shard = [N](int64_t node_id) { return node_id % N; };
//    std::vector< std::vector<std::string> > node_splits_per_shard(num_shards_);
    std::vector< std::vector<std::string> > edge_splits_per_shard(num_shards_);
    std::string line;

    int num_shards_digits = num_digits(this->num_shards_);
    int num = this->num_shards_;
    std::vector<std::shared_ptr<std::ofstream>> shard_edge_outs;
    for (int i = 0; i < num_shards_; ++i) {
        std::string out_name(
            format_out_name(edge_file_in, num_shards_digits, i, num));
        shard_edge_outs.push_back(std::make_shared<std::ofstream>(out_name));
    }

//    // reads node files
//    std::ifstream file_ifstream(node_file_in);
//    int64_t line_idx = 0;
//    while (std::getline(file_ifstream, line)) {
//        node_splits_per_shard[id_to_shard(line_idx)].push_back(line);
//        ++line_idx;
//    }

    // reads edge files
    std::ifstream edgefile_ifstream(edge_file_in);
    std::string src_id_str;
    while (std::getline(edgefile_ifstream, line)) {
        std::stringstream ss(line);
        std::getline(ss, src_id_str, ' ');
        *(shard_edge_outs[id_to_shard(std::stoll(src_id_str))]) << line
            << std::endl;
    }
    edgefile_ifstream.close();

    // output, selectively
    auto output_nonempty_shards = [num_shards_digits, num](
        const std::vector< std::vector<std::string> >& lines,
        const std::string& file_prefix)
    {
        for (size_t i = 0; i < lines.size(); ++i) {
            auto shard_lines = lines[i];
            if (!shard_lines.empty()) {
                std::string out_name(
                    format_out_name(file_prefix, num_shards_digits, i, num));
                std::ofstream file_ofstream(out_name);
                LOG_E("Writing split '%s' for shard %d\n", out_name.c_str(), i);
                for (const auto& line : shard_lines) {
                    file_ofstream << line << std::endl;
                }
            }
        }
    };
//    output_nonempty_shards(node_splits_per_shard, node_file_in);
}

int main(int argc, char **argv) {
    if (argc < 2) {
        LOG_E("partitioners: [-n total_num_shards=1] node_file edge_file\n");
        return -1;
    }

    int c;
    int total_num_shards = 1;
    while ((c = getopt(argc, argv, "n:")) != -1) {
        switch(c) {
        case 'n':
            total_num_shards = atoi(optarg);
            break;
        }
    }
    assert(optind + 2 >= argc);
    std::string node_file(argv[optind]);
    std::string edge_file(argv[optind + 1]);

    GraphPartitioner* partitioner = new HashPartitioner(total_num_shards);
    partitioner->partition(node_file, edge_file);
}
