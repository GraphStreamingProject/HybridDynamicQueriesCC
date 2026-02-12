#pragma once
#include <iostream>
#include <math.h>
#include <parlay/parallel.h>
#include <parlay/primitives.h>
#include <parlay/sequence.h>
#include "ufo_tree/ufo_types.h"


namespace ufo {

template <class ET>
inline bool CAS(ET *ptr, ET oldv, ET newv) {
    if (sizeof(ET) == 1) {
        return __sync_bool_compare_and_swap((bool*)ptr, *((bool*)&oldv), *((bool*)&newv));
    } else if (sizeof(ET) == 4) {
        return __sync_bool_compare_and_swap((int*)ptr, *((int*)&oldv), *((int*)&newv));
    } else if (sizeof(ET) == 8) {
        return __sync_bool_compare_and_swap((long*)ptr, *((long*)&oldv), *((long*)&newv));
    } else {
        std::cout << "CAS bad length : " << sizeof(ET) << std::endl;
        abort();
    }
}

template <class ET>
inline ET AtomicLoad(ET *ptr) {
    return __atomic_load_n(ptr, __ATOMIC_SEQ_CST);
}

template <class ET>
inline void AtomicStore(ET *ptr, ET val) {
    __atomic_store_n(ptr, val, __ATOMIC_SEQ_CST);
}

template <class ET>
inline ET AtomicExchange(ET *ptr, ET val) {
    return __sync_lock_test_and_set(ptr, val);
}

#define MAX_VERTEX_T (std::numeric_limits<uint32_t>::max())

#define VERTICES_TO_EDGE(U, V) (edge_t) U + (((edge_t) V) << 32)
#define EDGE_TYPE_TO_STRUCT(E) {(vertex_t) E, (vertex_t) (E >> 32)}

static int max_tree_height(vertex_t n) {
    return ceil(log2(n) / log2(1.2));
}

#define TAG(P,T) (Cluster*)((uintptr_t) P | (uintptr_t) T)
#define UNTAG(P) (Cluster*)((uintptr_t) P & (uintptr_t) ~0x7)
#define GET_TAG(P) (int)((uintptr_t) P & (uintptr_t) 0x7)

// #define START_TIMER(X) auto X = std::chrono::high_resolution_clock::now()
// #define STOP_TIMER(X, T) T += std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::high_resolution_clock::now()-X).count()
// #define PRINT_TIMER(S, T) std::cout << "    " << S << " (ms): " << T/1000000 << std::endl

#define START_TIMER(X) ;
#define STOP_TIMER(X, T) ;
#define PRINT_TIMER(S, T) ;

}
