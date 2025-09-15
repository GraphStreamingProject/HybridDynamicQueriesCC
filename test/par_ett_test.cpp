#include "ParETT/euler_tour_tree.hpp"


int main(int argc, char** argv) {
    int n = 10;

    parallel_skip_list::AugmentedElement<int>::aggregate_function = [] (int x, int y) {
        return x + y;
    };
    parallel_skip_list::AugmentedElement<int>::default_value = 1;

    parallel_euler_tour_tree::EulerTourTree<int> tree(n);
    for (int i = 0; i < n-1; i++) {
        tree.Link(i, i+1);
        std::cout << "Sum: " << tree.vertices_[0].GetSum() << std::endl;
    }

    return 0; 
}
