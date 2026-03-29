#pragma once

#include <mpi.h>

static void bcast(void* message, int size, int root) {
    MPI_Bcast(message, size, MPI_BYTE, root, MPI_COMM_WORLD);
}

static void gather(void* send_data, int send_size, void* recv_data, int recv_size, int root) {
    MPI_Gather(send_data, send_size, MPI_BYTE, recv_data, recv_size, MPI_BYTE, root, MPI_COMM_WORLD);
}

static void allgather(void* send_data, int send_size, void* recv_data, int recv_size) {
    MPI_Allgather(send_data, send_size, MPI_BYTE, recv_data, recv_size, MPI_BYTE, MPI_COMM_WORLD);
}

static void allreduce(void* send_data, void* recv_data) {
    MPI_Allreduce(send_data, recv_data, 1, MPI_UINT32_T, MPI_MIN, MPI_COMM_WORLD);
}

static void barrier() {
    MPI_Barrier(MPI_COMM_WORLD);
}
