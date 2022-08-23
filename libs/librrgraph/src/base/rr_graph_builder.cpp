#include "vtr_log.h"
#include "rr_graph_builder.h"
#include "vtr_time.h"
#include <queue>
#include <random>
//#include <algorithm>

//#include "globals.h"

RRGraphBuilder::RRGraphBuilder() {
    is_edge_dirty_ = true;
    is_incoming_edge_dirty_ = true;
}

t_rr_graph_storage& RRGraphBuilder::rr_nodes() {
    return node_storage_;
}

RRSpatialLookup& RRGraphBuilder::node_lookup() {
    return node_lookup_;
}

MetadataStorage<int>& RRGraphBuilder::rr_node_metadata() {
    return rr_node_metadata_;
}

MetadataStorage<std::tuple<int, int, short>>& RRGraphBuilder::rr_edge_metadata() {
    return rr_edge_metadata_;
}

vtr::vector<RRNodeId, std::vector<RREdgeId>>& RRGraphBuilder::node_in_edge_storage() {
    return node_in_edges_;
}

void RRGraphBuilder::add_node_to_all_locs(RRNodeId node) {
    t_rr_type node_type = node_storage_.node_type(node);
    short node_ptc_num = node_storage_.node_ptc_num(node);
    for (int ix = node_storage_.node_xlow(node); ix <= node_storage_.node_xhigh(node); ix++) {
        for (int iy = node_storage_.node_ylow(node); iy <= node_storage_.node_yhigh(node); iy++) {
            switch (node_type) {
                case SOURCE:
                case SINK:
                case CHANY:
                    node_lookup_.add_node(node, ix, iy, node_type, node_ptc_num, SIDES[0]);
                    break;
                case CHANX:
                    /* Currently need to swap x and y for CHANX because of chan, seg convention 
                     * TODO: Once the builders is reworked for use consistent (x, y) convention,
                     * the following swapping can be removed
                     */
                    node_lookup_.add_node(node, iy, ix, node_type, node_ptc_num, SIDES[0]);
                    break;
                case OPIN:
                case IPIN:
                    for (const e_side& side : SIDES) {
                        if (node_storage_.is_node_on_specific_side(node, side)) {
                            node_lookup_.add_node(node, ix, iy, node_type, node_ptc_num, side);
                        }
                    }
                    break;
                default:
                    VTR_LOG_ERROR("Invalid node type for node '%lu' in the routing resource graph file", size_t(node));
                    break;
            }
        }
    }
}

RRNodeId RRGraphBuilder::create_node(int x, int y, t_rr_type type, int ptc, e_side side) {
    e_side node_side = SIDES[0];
    /* Only OPIN and IPIN nodes have sides, otherwise force to use a default side */
    if (OPIN == type || IPIN == type) {
        node_side = side;
    }
    node_storage_.emplace_back();
    RRNodeId new_node = RRNodeId(node_storage_.size() - 1);
    node_storage_.set_node_type(new_node, type);
    node_storage_.set_node_coordinates(new_node, x, y, x, y);
    node_storage_.set_node_ptc_num(new_node, ptc);
    if (OPIN == type || IPIN == type) {
        node_storage_.add_node_side(new_node, node_side);
    }
    /* Special for CHANX, being consistent with the rule in find_node() */
    if (CHANX == type) {
        node_lookup_.add_node(new_node, y, x, type, ptc, node_side);
    } else {
        node_lookup_.add_node(new_node, x, y, type, ptc, node_side);
    }

    return new_node;
}

void RRGraphBuilder::clear() {
    node_lookup_.clear();
    node_storage_.clear();
    rr_node_metadata_.clear();
    rr_edge_metadata_.clear();
    rr_segments_.clear();
    rr_switch_inf_.clear();
    edges_to_build_.clear();
    is_edge_dirty_ = true;
    is_incoming_edge_dirty_ = true;
}

void RRGraphBuilder::reorder_nodes(e_rr_node_reorder_algorithm reorder_rr_graph_nodes_algorithm,
                                   int reorder_rr_graph_nodes_threshold,
                                   int reorder_rr_graph_nodes_seed) {
    size_t v_num = node_storage_.size();
    if (reorder_rr_graph_nodes_threshold < 0 || v_num < (size_t)reorder_rr_graph_nodes_threshold) return;
    vtr::ScopedStartFinishTimer timer("Reordering rr_graph nodes");
    vtr::vector<RRNodeId, RRNodeId> src_order(v_num); // new id -> old id
    size_t cur_idx = 0;
    for (RRNodeId& n : src_order) { // Initialize to [0, 1, 2 ...]
        n = RRNodeId(cur_idx++);
    }

    // This method works well. The intution is that highly connected nodes are enumerated first (together),
    // and since there will be a lot of nodes with the same degree, they are then ordered based on some
    // distance from the starting node.
    if (reorder_rr_graph_nodes_algorithm == DEGREE_BFS) {
        vtr::vector<RRNodeId, size_t> bfs_idx(v_num);
        vtr::vector<RRNodeId, size_t> degree(v_num);
        std::queue<RRNodeId> que;

        // Compute both degree (in + out) and an index based on the BFS traversal
        cur_idx = 0;
        for (size_t i = 0; i < v_num; ++i) {
            if (bfs_idx[RRNodeId(i)]) continue;
            que.push(RRNodeId(i));
            bfs_idx[RRNodeId(i)] = cur_idx++;
            while (!que.empty()) {
                RRNodeId u = que.front();
                que.pop();
                degree[u] += node_storage_.num_edges(u);
                for (RREdgeId edge = node_storage_.first_edge(u); edge < node_storage_.last_edge(u); edge = RREdgeId(size_t(edge) + 1)) {
                    RRNodeId v = node_storage_.edge_sink_node(edge);
                    degree[v]++;
                    if (bfs_idx[v]) continue;
                    bfs_idx[v] = cur_idx++;
                    que.push(v);
                }
            }
        }

        // Sort by degree primarily, and BFS order secondarily
        sort(src_order.begin(), src_order.end(),
             [&](auto a, auto b) -> bool {
                 auto deg_a = degree[a];
                 auto deg_b = degree[b];
                 return deg_a > deg_b || (deg_a == deg_b && bfs_idx[a] < bfs_idx[b]);
             });
    } else if (reorder_rr_graph_nodes_algorithm == RANDOM_SHUFFLE) {
        std::mt19937 g(reorder_rr_graph_nodes_seed);
        std::shuffle(src_order.begin(), src_order.end(), g);
    }
    vtr::vector<RRNodeId, RRNodeId> dest_order(v_num);
    cur_idx = 0;
    for (auto u : src_order)
        dest_order[u] = RRNodeId(cur_idx++);

    VTR_ASSERT_SAFE(node_storage_.validate(rr_switch_inf_));
    node_storage_.reorder(dest_order, src_order);
    VTR_ASSERT_SAFE(node_storage_.validate(rr_switch_inf_));

    node_lookup().reorder(dest_order);

    rr_node_metadata().remap_keys([&](int node) { return size_t(dest_order[RRNodeId(node)]); });
    rr_edge_metadata().remap_keys([&](std::tuple<int, int, short> edge) {
        return std::make_tuple(size_t(dest_order[RRNodeId(std::get<0>(edge))]),
                               size_t(dest_order[RRNodeId(std::get<1>(edge))]),
                               std::get<2>(edge));
    });
}

void RRGraphBuilder::create_edge(RRNodeId src, RRNodeId dest, RRSwitchId edge_switch) {
    edges_to_build_.emplace_back(src, dest, size_t(edge_switch));
    is_edge_dirty_ = true; /* Adding a new edge revokes the flag */
    is_incoming_edge_dirty_ = true;
}

void RRGraphBuilder::build_edges() {
    std::sort(edges_to_build_.begin(), edges_to_build_.end());
    edges_to_build_.erase(std::unique(edges_to_build_.begin(), edges_to_build_.end()), edges_to_build_.end());
    alloc_and_load_edges(&edges_to_build_);
    edges_to_build_.clear(); 
    is_edge_dirty_ = false;
}

void RRGraphBuilder::build_in_edges() {
    VTR_ASSERT(validate());
    node_in_edges_.clear();
    node_in_edges_.resize(node_storage_.size());

    for (const RRNodeId& src_node: vtr::StrongIdRange<RRNodeId>(RRNodeId(0), RRNodeId(node_storage_.size()))) {
        for (size_t iedge = size_t(node_storage_.first_edge(src_node)); iedge < size_t(node_storage_.last_edge(src_node)); iedge++) {
            RRNodeId des_node = node_storage_.edge_sink_node(RREdgeId(iedge));
            node_in_edges_[des_node].push_back(RREdgeId(iedge));
        }
    }
    is_incoming_edge_dirty_ = false;
}

std::vector<RREdgeId> RRGraphBuilder::node_in_edges(RRNodeId node) const {
  VTR_ASSERT(size_t(node) < node_storage_.size());
  if (is_incoming_edge_dirty_) {
    VTR_LOG_ERROR("Incoming edges are built yet in routing resource graph. Please call build_in_edges().");
    return std::vector<RREdgeId>();
  }
  if (node_in_edges_.empty()) {
    return std::vector<RREdgeId>();
  }
  return node_in_edges_[node];
}
