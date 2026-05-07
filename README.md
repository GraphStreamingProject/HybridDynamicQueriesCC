
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
