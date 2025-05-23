#include "../include/link_cut_tree.h"
#include <cassert>

void LinkCutNode::set_parent(LinkCutNode* parent) { this->parent = parent; }
void LinkCutNode::set_dparent(LinkCutNode* dparent) { this->dparent = dparent; }
void LinkCutNode::set_max(uint32_t weight){ this->max = weight; }
void LinkCutNode::set_reversed(bool reversed){ this->reversed = reversed; }
void LinkCutNode::reverse() { this->reversed = !this->reversed; }

void LinkCutNode::link_left(LinkCutNode* other) {
    this->left = other;
    if (other != nullptr) {
        other->set_parent(this);
    }
    this->rebuild_max();
    assert(this->get_left() == nullptr || this->get_left()->get_parent() == this);
}

void LinkCutNode::link_right(LinkCutNode* other) {
    this->right = other;
    if (other != nullptr) {
        other->set_parent(this);
    }
    this->rebuild_max();
    assert(this->get_right() == nullptr || this->get_right()->get_parent() == this);
}

LinkCutNode* LinkCutNode::get_left() { return this->left; }
LinkCutNode* LinkCutNode::get_right() { return this->right; }
LinkCutNode* LinkCutNode::get_parent() { return this->parent; }
LinkCutNode* LinkCutNode::get_dparent() { return this->dparent; }
LinkCutNode* LinkCutNode::get_head() { return this->head; }
LinkCutNode* LinkCutNode::get_tail() { return this->tail; }
std::pair<edge_id_t, uint32_t> LinkCutNode::get_max_edge() { return {this->max_edge, this->max}; }
bool LinkCutNode::get_reversed() { return this->reversed; }

LinkCutNode* LinkCutNode::recompute_head() {
    LinkCutNode* curr = this;
    LinkCutNode* next;
    bool reversal_state = false;
    while (curr != nullptr) {
        reversal_state = reversal_state != curr->reversed;
        if((next = reversal_state ? curr->right : curr->left) == nullptr) { this->head = curr; }
        curr = next;
    }
    return this->head;
}

LinkCutNode* LinkCutNode::recompute_tail() {
    LinkCutNode* curr = this;
    LinkCutNode* next;
    bool reversal_state = false;
    while (curr != nullptr) {
        reversal_state = reversal_state != curr->reversed;
        if((next = reversal_state ? curr->left : curr->right) == nullptr) { this->tail = curr; }
        curr = next;
    }
    return this->tail;
}

// void inorder(LinkCutNode* node, std::vector<LinkCutNode*>& nodes, bool reversal_state) {
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

// std::vector<LinkCutNode*> get_inorder(LinkCutNode* node) {
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

void LinkCutNode::correct_reversals() {
    //std::vector<LinkCutNode*> inorder = get_inorder(this);
    //Get the XOR of all reversed booleans from this node to root
    bool reversal_state = 0;
    LinkCutNode* curr = this;
    while (curr) {
        reversal_state = reversal_state != curr->reversed;
        curr = curr->parent;
    }
    //Travel back up performing the reversals where needed
    curr = this;
    LinkCutNode* prev = nullptr;
    while (curr) {
        bool next_reversal_state = reversal_state != curr->reversed;
        if (reversal_state) {
            LinkCutNode* temp = curr->left;
            curr->left = curr->right;
            curr->right = temp;
            if (curr->left && curr->left != prev) {
                curr->left->set_reversed(!curr->left->reversed);
            }
            if (curr->right && curr->right != prev) {
                curr->right->set_reversed(!curr->right->reversed);
            }
        }
        curr->set_reversed(false);
        reversal_state = next_reversal_state;
        prev = curr;
        curr = curr->parent;
    }
    //assert(inorder == get_inorder(this));
}

void LinkCutNode::make_preferred_edge(edge_id_t e) {
    assert(this->preferred_edges.first == MAX_UINT64 || this->preferred_edges.second == MAX_UINT64);
    if (this->preferred_edges.first == MAX_UINT64) {
        this->preferred_edges.first = e;
    } else {
        this->preferred_edges.second = e;
    }
}

void LinkCutNode::unmake_preferred_edge(edge_id_t e) {
    assert(this->preferred_edges.first == e || this->preferred_edges.second == e);
    if (this->preferred_edges.first == e) {
        this->preferred_edges.first = MAX_UINT64;
    } else {
        this->preferred_edges.second = MAX_UINT64;
    }
}

void LinkCutNode::insert_edge(edge_id_t e, uint32_t weight) {
    assert(this->edges.count(e) == 0);
    this->edges.insert({e, weight});
}

void LinkCutNode::remove_edge(edge_id_t e) {
    assert(this->edges.count(e) == 1);
    this->edges.erase(e);
}

bool LinkCutNode::has_edge(edge_id_t e) {
    return (this->edges.find(e) != this->edges.end());
}

uint32_t LinkCutNode::get_edge_weight(edge_id_t e) {
    return this->edges[e];
}

void LinkCutNode::rebuild_max() {
    uint32_t max = 0;
    edge_id_t max_edge = 0;

    if (this->preferred_edges.first != MAX_UINT64 && this->edges.find(this->preferred_edges.first) != this->edges.end() && this->edges[this->preferred_edges.first] > max) {
        max = this->edges[this->preferred_edges.first];
        max_edge = this->preferred_edges.first;
    }
    if (this->preferred_edges.second != MAX_UINT64 && this->edges.find(this->preferred_edges.second) != this->edges.end() && this->edges[this->preferred_edges.second] > max) {
        max = this->edges[this->preferred_edges.second];
        max_edge = this->preferred_edges.second;
    }
    if (this->left && this->left->max > max) {
        max = this->left->max;
        max_edge = this->left->max_edge;
    }
    if (this->right && this->right->max > max) {
        max = this->right->max;
        max_edge = this->right->max_edge;
    }

    this->set_max(max);
    this->max_edge = max_edge;
}

void LinkCutNode::rotate_up() {
    LinkCutNode* parent = this->parent;
    LinkCutNode* grandparent = parent->parent;

    if (parent->left == this) {
        parent->link_left(this->right);
        this->link_right(parent);
    } else {
        parent->link_right(this->left);
        this->link_left(parent);
    }

    if (grandparent != nullptr) {
        if (grandparent->left == parent) {
            grandparent->link_left(this);
        } else {
            grandparent->link_right(this);
        }
    } else {
        this->set_parent(nullptr);
    }
}

LinkCutNode* LinkCutNode::splay() {
    this->correct_reversals();

    while (this->parent != nullptr) {
        LinkCutNode* parent = this->parent;
        LinkCutNode* grandparent = parent->parent;
        if (grandparent == nullptr) {
            // zig
            this->rotate_up();
        } else if ((grandparent->left == parent) == (parent->left == this)) {
            // zig-zig
            parent->rotate_up();
            this->rotate_up();
        } else {
            // zig-zag
            this->rotate_up();
            this->rotate_up();
        }
    }
    this->recompute_head();
    this->recompute_tail();
    this->rebuild_max();
    assert(this->get_parent() == nullptr);
    return this;
}

LinkCutTree::LinkCutTree(node_id_t num_nodes) : nodes(num_nodes) {}

LinkCutNode* LinkCutTree::join(LinkCutNode* v, LinkCutNode* w) {
    assert(v != nullptr && w != nullptr && v->get_parent() == nullptr && w->get_parent() == nullptr);
    LinkCutNode* tail = v->get_tail();
    LinkCutNode* head = w->get_head();
    // TODO - this nodes vector is confusing me. 
    // oh it's pointer arithmetic to standardize the node id
    node_id_t tail_id = tail-&(this->nodes[0]);
    node_id_t head_id = head-&(this->nodes[0]);
    edge_id_t edge = (tail_id < head_id) ? (((edge_id_t)tail_id << 32) + head_id) : (((edge_id_t)head_id << 32) + tail_id);
    tail->make_preferred_edge(edge);
    head->make_preferred_edge(edge);
    tail->splay();
    head->splay(); // To recompute the aggregate
    assert(tail->get_right() == nullptr);
    tail->link_right(head);
    tail->recompute_head();
    tail->recompute_tail();
    return tail;
}

std::pair<LinkCutNode*, LinkCutNode*> LinkCutTree::split(LinkCutNode* v) {
    assert(v != nullptr);
    v->splay();
    LinkCutNode* r = v->get_right();
    LinkCutNode* w = nullptr;
    if (r != nullptr) {
        w = r->recompute_head();
        node_id_t v_id = v-&(this->nodes[0]);
        node_id_t w_id = w-&(this->nodes[0]);
        edge_id_t edge = (v_id < w_id) ? (((edge_id_t)v_id << 32) + w_id) : (((edge_id_t)w_id << 32) + v_id);
        v->unmake_preferred_edge(edge);
        w->unmake_preferred_edge(edge);
        v->link_right(nullptr); // This also recomputes the aggregate for v
        r->set_parent(nullptr);
        w->set_dparent(v);
        w->splay(); // Recompute the aggregate for w
        w->recompute_head();
        w->recompute_tail();
    }
    v->recompute_head();
    v->recompute_tail();
    std::pair<LinkCutNode*, LinkCutNode*> paths = {v, w};
    return paths;
}

LinkCutNode* LinkCutTree::splice(LinkCutNode* p) {
    LinkCutNode* v = p->get_head()->get_dparent();
    std::pair<LinkCutNode*, LinkCutNode*> paths = this->split(v);
    p->get_head()->set_dparent(nullptr);
    return this->join(paths.first, p);
}

LinkCutNode* LinkCutTree::expose(LinkCutNode* v) {
    std::pair<LinkCutNode*, LinkCutNode*> paths = this->split(v);
    LinkCutNode* p = paths.first;
    while(p->get_head()->get_dparent() != nullptr) {
        p = this->splice(p);
    }
    return p;
}

LinkCutNode* LinkCutTree::evert(LinkCutNode* v) {
    LinkCutNode* p = this->expose(v);
    p->reverse();
    p->recompute_head();
    p->recompute_tail();
    return p;
}

void LinkCutTree::link(node_id_t v, node_id_t w, uint32_t weight) {
    assert(find_root(v) != find_root(w));
    LinkCutNode* v_node = &this->nodes[v];
    LinkCutNode* w_node = &this->nodes[w];
    edge_id_t edge = (v < w) ? (((edge_id_t)v << 32) + w) : (((edge_id_t)w << 32) + v);
    v_node->insert_edge(edge, weight);
    w_node->insert_edge(edge, weight);
    LinkCutNode* p_v = this->expose(v_node);
    LinkCutNode* p_w = this->evert(w_node);
    assert(p_v->get_tail() == v_node);
    assert(p_w->get_head() == w_node);
    this->join(p_v, p_w);
}

void LinkCutTree::cut(node_id_t v, node_id_t w) {
    assert(find_root(v) == find_root(w));
    LinkCutNode* v_node = &this->nodes[v];
    LinkCutNode* w_node = &this->nodes[w];
    edge_id_t edge = (v < w) ? (((edge_id_t)v << 32) + w) : (((edge_id_t)w << 32) + v);
    v_node->remove_edge(edge);
    w_node->remove_edge(edge);
    this->evert(v_node);
    this->expose(v_node);
    w_node->set_dparent(nullptr);
}

void* LinkCutTree::find_root(node_id_t v) {
    return this->expose(&this->nodes[v])->get_head();
}

std::pair<edge_id_t, uint32_t> LinkCutTree::path_aggregate(node_id_t v, node_id_t w) {
    assert(find_root(v) == find_root(w));
    LinkCutNode* v_node = &this->nodes[v];
    LinkCutNode* w_node = &this->nodes[w];
    this->evert(v_node);
    LinkCutNode* p = this->expose(w_node);
    return p->get_max_edge();
}

bool LinkCutTree::has_edge(node_id_t v1, node_id_t v2) {
    edge_id_t e = VERTICES_TO_EDGE(v1, v2);
    return nodes[v1].has_edge(e);
}

uint32_t LinkCutTree::get_edge_weight(node_id_t v1, node_id_t v2) {
    edge_id_t e = VERTICES_TO_EDGE(v1, v2);
    return nodes[v1].get_edge_weight(e);
}

std::vector<std::set<node_id_t>> LinkCutTree::get_cc() {
	std::map<LinkCutNode*, std::set<node_id_t>> cc_map;
	std::map<LinkCutNode*, LinkCutNode*> visited;
	for (uint32_t i = 0; i < nodes.size(); i++) {
		if (visited.find(&nodes[i]) == visited.end()) {
            std::set<LinkCutNode*> node_component;
            LinkCutNode* curr = &nodes[i];
            while ((curr->get_parent() && visited.find(curr->get_parent()) == visited.end())
            || (curr->get_head()->get_dparent() && visited.find(curr->get_head()->get_dparent()) == visited.end())) {
                node_component.insert(curr);
                curr = curr->get_parent() ? curr->get_parent() : curr->get_head()->get_dparent();
            }
            node_component.insert(curr);
            LinkCutNode* root = curr;
            if (curr->get_parent() || curr->get_head()->get_dparent())
                root = curr->get_parent() ? visited[curr->get_parent()] : visited[curr->get_head()->get_dparent()];

            if (cc_map.find(root) == cc_map.end()) {
                std::set<node_id_t> component;
                cc_map.insert({root, component});
            }
			for (auto node : node_component) {
				cc_map[root].insert(node-&nodes[0]);
				visited.insert({node, root});
			}
		}
	}
    std::vector<std::set<node_id_t>> cc;
    for (auto component : cc_map) {
        cc.push_back(component.second);
    }
	return cc;
}
