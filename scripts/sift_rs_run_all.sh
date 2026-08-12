#!/bin/bash

# Define dataset sizes
SIZES=("10K" "20K" "30K" "40K" "50K" "60K")

echo "Starting benchmarks..."

# 1. Run bench_profile_cf
echo "Running bench_profile_cf suite..."
mkdir -p results/cf
for size in "${SIZES[@]}"; do
    out_dir="results/cf/$size"
    mkdir -p "$out_dir"
    ./bench_profile_cf "${BASE_PATH}/sift_RS-${size}_sym.bin" --static-graph --output-dir "$out_dir"
done

# 2. Run bench_profile_mpi_batch_lct_resizeable_hybrid
echo "Running mpi_hybrid suite..."
mkdir -p results/mpi_hybrid
for size in "${SIZES[@]}"; do
    out_dir="results/mpi_hybrid/$size"
    mkdir -p "$out_dir"
    mpirun -np 21 ./bench_profile_mpi_batch_lct_resizeable_hybrid \
        "${BASE_PATH}/sift_RS-${size}_sym.bin" \
        --static-graph \
        --output-dir "$out_dir" \
        --batch-size 10000 \
        --hybrid-threshold 500
done

# 3. Run bench_profile_mpi_ett_fixed
echo "Running mpi_ett suite..."
mkdir -p results/mpi_ett
for size in "${SIZES[@]}"; do
    out_dir="results/mpi_ett/$size"
    mkdir -p "$out_dir"
    mpirun -np 21 ./bench_profile_mpi_ett_fixed \
        "${BASE_PATH}/sift_RS-${size}_sym.bin" \
        --static-graph \
        --output-dir "$out_dir" \
        --batch-size 100
done

echo "All benchmarks completed."
