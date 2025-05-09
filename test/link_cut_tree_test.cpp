#include <gtest/gtest.h>
#include <iostream>
#include <string>
#include "link_cut_tree.h"

static bool validate(LinkCutNode* v) {
    bool valid = true;
    if (!(v->get_parent() == nullptr || v->get_parent()->get_left() == v  || v->get_parent()->get_right() == v)) {
        valid = false;
        std::cout << "Node " << v << " invalid" << std::endl
        << "Parent's left: " << v->get_parent()->get_left()
        << " Parent's right: " << v->get_parent()->get_right() << std::endl;
    }
    if (!(v->get_left() == nullptr || v->get_left()->get_parent() == v)) {
        valid = false;
        std::cout << "Node " << v << " invalid" << std::endl
        << "Left's parent: " << v->get_left()->get_parent() << std::endl;
    }
    if (!(v->get_right() == nullptr || v->get_right()->get_parent() == v)) {
        valid = false;
        std::cout << "Node " << v << " invalid" << std::endl
        << "Rights's parent: " << v->get_right()->get_parent() << std::endl;
    }
    return valid;
}

// static void inorder(LinkCutNode* node, std::vector<LinkCutNode*>& nodes, bool reversal_state) {
//     if (node != nullptr) {
//         reversal_state = reversal_state != node->get_reversed();
//         if (!reversal_state) {
//             inorder(node->get_left(), nodes, reversal_state);
//             nodes.push_back(node);
//             inorder(node->get_right(), nodes, reversal_state);
//         } else {
//             inorder(node->get_right(), nodes, reversal_state);
//             nodes.push_back(node);
//             inorder(node->get_left(), nodes, reversal_state);
//         }
//     }
// }

// static std::vector<LinkCutNode*> get_inorder(LinkCutNode* node) {
//     LinkCutNode* curr = node;
//     LinkCutNode* root;
//     while (curr) {
//         if (curr->get_parent() == nullptr) { root = curr; }
//         curr = curr->get_parent();
//     }
//     std::vector<LinkCutNode*> nodes;
//     inorder(root, nodes, false);
//     return nodes;
// }

// static void print_paths(std::vector<LinkCutNode>* nodes) {
//     std::set<LinkCutNode*> paths;
//     std::cout << "Paths: " << std::endl;
//     for (uint32_t i = 0; i < (*nodes).size(); i++) {
//         LinkCutNode* curr = &(*nodes)[i];
//         while (curr) {
//             if (curr->get_parent() == nullptr) {
//                 if (paths.find(curr) == paths.end()) {
//                     paths.insert(curr);
//                     std::vector<LinkCutNode*> inorder = get_inorder(&(*nodes)[i]);
//                     for (uint32_t i = 0; i < inorder.size(); i++) {
//                         std::cout << inorder[i]-&(*nodes)[0] << " ";
//                     }
//                     std::cout << "dparent: " << (curr->get_head()->get_dparent()==nullptr ? "null" : std::to_string(curr->get_head()->get_dparent()-&(*nodes)[0]));
//                     std::cout << " agg: " << curr->get_max_edge().second << " edge: " << curr->get_max_edge().first << std::endl;
//                 }
//             }
//             curr = curr->get_parent();
//         }
//     }
//     std::cout << std::endl;
// }

TEST(LinkCutTreeSuite, join_split_test) {
    // power of 2 node count
    int nodecount = 1024;
    LinkCutTree lct(nodecount);
    // Join every 2,4,8,16... nodes
    for (int i = 2; i <= nodecount; i*=2) {
        for (int j = 0; j < nodecount; j+=i) {
            lct.nodes[j].splay();
            lct.nodes[j+i/2].splay();
            //std::cout << "Join nodes: " << &nodes[j] << " and " << &nodes[j+i/2] << "\n";
            LinkCutNode* p = lct.join(&lct.nodes[j], &lct.nodes[j+i/2]);
            EXPECT_EQ(p->get_head(), &lct.nodes[j]);
            EXPECT_EQ(p->get_tail(), &lct.nodes[j+i-1]);
        }
        // Validate all nodes
        for (int i = 0; i < nodecount; i++) {
            validate(&lct.nodes[i]);
        }
    }
    // Split Every ...16,8,4,2 nodes
    for (int i = nodecount; i > 1; i/=2) {
        for (int j = 0; j < nodecount; j+=i) {
            //std::cout << "Split on node: " << &nodes[j+i/2-1] << "\n";
            std::pair<LinkCutNode*, LinkCutNode*> paths = lct.split(&lct.nodes[j+i/2-1]);
            EXPECT_EQ(paths.first->get_head(), &lct.nodes[j]);
            EXPECT_EQ(paths.first->get_tail(), &lct.nodes[j+i/2-1]);
            EXPECT_EQ(paths.second->get_head(), &lct.nodes[j+i/2]);
            EXPECT_EQ(paths.second->get_tail(), &lct.nodes[j+i-1]);
        }
        // Validate all nodes
        for (int i = 0; i < nodecount; i++) {
            validate(&lct.nodes[i]);
        }
    }
}

TEST(LinkCutTreeSuite, expose_simple_test) {
    int pathcount = 100;
    int nodesperpath = 100;
    LinkCutTree lct(nodesperpath*pathcount);
    // Link all the nodes in each path together
    for (int path = 0; path < pathcount; path++) {
        for (int node = 0; node < nodesperpath-1; node++) {
            lct.nodes[path*nodesperpath+node].splay();
            lct.join(&lct.nodes[path*nodesperpath+node], &lct.nodes[path*nodesperpath+node+1]);
        }
    }
    // Link all the paths together with dparent pointers half way up the previous path
    for (int path = 1; path < pathcount; path++) {
        lct.nodes[path*nodesperpath].set_dparent(&lct.nodes[path*nodesperpath-nodesperpath/2]);
    }
    // Call expose on the node half way up the bottom path
    LinkCutNode* p = lct.expose(&lct.nodes[pathcount*nodesperpath-nodesperpath/2]);

    // Validate all nodes
    for (int i = 0; i < pathcount*nodesperpath; i++) {
        validate(&lct.nodes[i]);
    }
    // Validate head and tail of returned path
    EXPECT_EQ(p->get_head(), &lct.nodes[0]);
    EXPECT_EQ(p->get_tail(), &lct.nodes[pathcount*nodesperpath-nodesperpath/2]) << "Exposed node not tail of path";
    // Validate all dparent pointers
    for (int path = 0; path < pathcount; path++) {
        EXPECT_EQ(lct.nodes[(path+1)*nodesperpath-nodesperpath/2+1].get_dparent(), &lct.nodes[(path+1)*nodesperpath-nodesperpath/2]);
    }
}

TEST(LinkCutTreeSuite, random_links_and_cuts) {
    int nodecount = 1000;
    LinkCutTree lct(nodecount);
    int seed = time(NULL);
    // Link all nodes
    for (int i = 0; i < nodecount-1; i++) {
        lct.link(i,i+1, rand()%100);
        ASSERT_TRUE(std::all_of(lct.nodes.begin(), lct.nodes.end(), [](auto& node){return validate(&node);}))
          << "One or more invalid nodes found" << std::endl;
    }
    // Cut every node
    for (int i = 0; i < nodecount-1; i+=1) {
        lct.cut(i,i+1);
        ASSERT_TRUE(std::all_of(lct.nodes.begin(), lct.nodes.end(), [](auto& node){return validate(&node);}))
          << "One or more invalid nodes found" << std::endl;
    }
    // Do random links and cuts
    int n = 5000;
    std::cout << "Seeding random links and cuts test with " << seed << std::endl;
    srand(seed);
    for (int i = 0; i < n; i++) {
        node_id_t a = rand() % nodecount, b = rand() % nodecount;
        if (a != b) {
            if (lct.find_root(a) != lct.find_root(b)) {
                uint32_t weight = rand()%100;
                //std::cout << i << ": Linking " << a << " and " << b << " weight " << weight << std::endl;
                lct.link(a, b, weight);
                //print_paths(&lct.nodes);
            } else if (lct.nodes[a].edges.find(&lct.nodes[b]-&lct.nodes[0]) != lct.nodes[a].edges.end()) {
                //std::cout << i << ": Cutting " << a << " and " << b << std::endl;
                lct.cut(a, b);
                //print_paths(&lct.nodes);
            }
            ASSERT_TRUE(std::all_of(lct.nodes.begin(), lct.nodes.end(), [](auto& node){return validate(&node);}))
             << "One or more invalid nodes found" << std::endl;
        }
    }
    // Manually compute the aggregates for each aux tree
    std::map<LinkCutNode*, uint32_t> path_aggregates;
    for (int i = 0; i < nodecount; i++) {
        uint32_t nodemax = std::max(lct.nodes[i].edges[lct.nodes[i].preferred_edges.first],
                lct.nodes[i].edges[lct.nodes[i].preferred_edges.second]);
        LinkCutNode* curr = &lct.nodes[i];
        while (curr) {
            if (curr->get_parent() == nullptr) {
                if (path_aggregates.find(curr) != path_aggregates.end()) {
                    path_aggregates[curr] = std::max(path_aggregates[curr], nodemax);
                } else {
                    path_aggregates.insert({curr, nodemax});
                }
            }
            curr = curr->get_parent();
        }
    }
    // Compare all root aggregates with manually computed ones
    for (auto agg : path_aggregates) {
        EXPECT_EQ(agg.second, agg.first->max) << "Aggregate incorrect" << std::endl;
    }
}
