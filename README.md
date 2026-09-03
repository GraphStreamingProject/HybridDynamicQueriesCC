This is an anonymized repo for the SIGMOD 2027 submission *Hybrid-Sketching Methods for Dynamic Connectivity on Sparse Graphs*. 

This code solves the dynamic connectivity problem: it takes in a dynamic stream of edge insertions and deletions that define a graph, and can answer connectivity queries during any time in the stream. 
Each query is of the form `(u,v)` and returns `true` if `u` and `v` are currently connected and `false` otherwise.

The main innovation of this system is using lossy graph-sketching techniques to represent the dense-core of the input graph, while representing the sparse periphery losslessly.

The system is built with `C++20` and tested against x86 Linux, with `gcc>=11.5`. `openmpi` is the only explicit dependency. 

### Building

The typical CMake command is:
```
cmake .. -DBUILD_BENCH=no -DCMAKE_BUILD_TYPE=Release -DSKETCH_BUFFER_SIZE=1 -DPARLAY_TBB=Once -DUSE_RESIZEABLE_SKETCH=ON -DUSE_PARALLEL_HYBRID=ON -DENABLE_DIRECT_SKETCH=ON -DUSE_SKETCHLESS_QUERY_ETT=OFF -DLCT_QUERY_MODE=WORST_CASE
```

There are many executables for various versions of our system. There are "bench_speed_" executables which simply get the running time, and "bench_profile_" executables get various properties periodically such as memory usage.

### Dataset CSVs from static graphs

Batch experiments accept a dataset CSV or TSV with these columns:

```text
dataset_name,filepath,num_vertices,num_edges
```

Generate this file from one or more static graphs with `scripts/static_graph_info.py`. It supports binary symmetric CSR and Parlay text graphs, and computes the vertex and edge counts required by the batch drivers:

```bash
python3 scripts/static_graph_info.py \
	--output datasets.csv \
	/path/to/graph_a.bin /path/to/graph_b.bin
```

By default, the generated CSV records absolute paths. Add `--keep-input-paths` to retain the input path spelling instead. See [scripts/configs/datasets.example.csv](scripts/configs/datasets.example.csv) for a minimal example.

### Main experiments

The main configurations run static insertions, 20 million post-insert queries, and deletions. Supply the dataset CSV generated above and `--do-deletions` to each command.

HybridSCALE uses [scripts/configs/optimal_hybridscale_main.json](scripts/configs/optimal_hybridscale_main.json):

```bash
scripts/batch_speed.sh \
	--batch-config scripts/configs/optimal_hybridscale_main.json \
	--dataset-config /path/to/datasets.csv \
	--do-deletions
```

```bash
scripts/batch_profile.sh \
	--batch-config scripts/configs/optimal_hybridscale_main.json \
	--dataset-config /path/to/datasets.csv \
	--do-deletions
```

CUPCaKE uses [scripts/configs/mpi_ett_fixed_main_derived_1p7x.json](scripts/configs/mpi_ett_fixed_main_derived_1p7x.json):

```bash
scripts/batch_speed.sh \
	--batch-config scripts/configs/mpi_ett_fixed_main_derived_1p7x.json \
	--dataset-config /path/to/datasets.csv \
	--do-deletions
```

```bash
scripts/batch_profile.sh \
	--batch-config scripts/configs/mpi_ett_fixed_main_derived_1p7x.json \
	--dataset-config /path/to/datasets.csv \
	--do-deletions
```

Cluster Forest uses [scripts/configs/cluster_forest_main.json](scripts/configs/cluster_forest_main.json):

```bash
scripts/batch_speed.sh \
	--batch-config scripts/configs/cluster_forest_main.json \
	--dataset-config /path/to/datasets.csv \
	--do-deletions
```

```bash
scripts/batch_profile.sh \
	--batch-config scripts/configs/cluster_forest_main.json \
	--dataset-config /path/to/datasets.csv \
	--do-deletions
```

### Interleaved-query experiments

The query-sweep configurations run queries throughout the insert/delete workload at 0.1 and 1.0 queries per update, corresponding to one query per 10 updates and one query per update. These are speed experiments, so use `scripts/batch_speed.sh`:

```bash
# HybridSCALE
scripts/batch_speed.sh \
	--batch-config scripts/configs/optimal_hybridscale_querysweep.json \
	--dataset-config /path/to/datasets.csv \
	--do-deletions

# CUPCaKE
scripts/batch_speed.sh \
	--batch-config scripts/configs/mpi_ett_fixed_main_interleaved_derived_1p7x.json \
	--dataset-config /path/to/datasets.csv \
	--do-deletions

# Cluster Forest
scripts/batch_speed.sh \
	--batch-config scripts/configs/cluster_forest_querysweep.json \
	--dataset-config /path/to/datasets.csv \
	--do-deletions
```


### Hybrid threshold sweep

[scripts/configs/hybrid_threshold_sweep.json](scripts/configs/hybrid_threshold_sweep.json) explicitly runs the MPI-batch LCT resizeable hybrid at threshold multipliers 15, 20, 25, 30, 50, 100, and 200. The runners resolve each multiplier as $M \cdot \text{num_tiers}$ for the dataset. Use the same configuration for time and space experiments:

```
scripts/batch_speed.sh --dataset-config datasets.csv --batch-config scripts/configs/hybrid_threshold_sweep.json
scripts/batch_profile.sh --dataset-config datasets.csv --batch-config scripts/configs/hybrid_threshold_sweep.json
```
## Running Experiments directly

For Cluster Forest:
```
./bench_speed_cf [args]
```

For HybridSCALE:
```
mpirun -np <num_processes> ./bench_speed_mpi_batch_lct_resizeable_hybrid [args]
```

For CUPCaKE:
```
mpirun -np <num_processes> ./bench_speed_mpi_ett_fixed [args]
```



A full command setting dense threshold and batch size and other things may look like this:
```
mpirun -np 23 --oversubscribe ./bench_profile_mpi_batch_lct_resizeable_hybrid /binary_streams/kron_13_stream_binary --batch-size 100 --hybrid-threshold 500 --output-dir results/mpi_batch_lct_resizeable_hybrid_t500 --report-interval 1000000
```
