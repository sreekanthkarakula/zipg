#!/bin/bash
set -e
make bench

dataset=higgs-40attr16each

if [[ "$dataset" == "liveJournal-40attr16each" ]]; then
  edgelist=/mnt/soc-LiveJournal1.txt
  num_nodes=4847571
  num_node_attr=40
  node_attr_freq=1000
  node_attr_size_each=16
  inner_delim='	'
  end_delim='
  assoc_out_file=liveJournal.assoc
  node_out_file=liveJournal-${num_node_attr}attr${node_attr_size_each}each-tpch.node
elif [[ "$dataset" == "higgs-40attr16each" ]]; then
  edgelist=./data/higgs-social_network.assoc
  num_nodes=456627
  num_node_attr=40
  node_attr_freq=1000
  node_attr_size_each=16
  inner_delim='	'
  end_delim='
  assoc_out_file=NOT_NEEDED_FOR_NOW
  node_out_file=${dataset}-tpch.node
else 
  exit 1
fi

./bin/create \
  format-input \
  $edgelist \
  data/data_0 \
  data/${assoc_out_file} \
  data/${node_out_file} \
  $num_nodes \
  $num_node_attr \
  $node_attr_freq \
  $node_attr_size_each \
  "${inner_delim}" \
  "${end_delim}"