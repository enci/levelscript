#pragma once
#include "sema.hpp"
#include <vector>

namespace ls::internal {

// One extraction of a graph layer: its nodes, the cells they cover, and
// their adjacency. A pure function of the source grid's cells.
struct graph_data {
    int width{0}, height{0};
    std::vector<int>              node_of;   // per cell, row-major; -1 = no node
    std::vector<int>              sizes;     // per node: its region's cell count
    std::vector<int64_t>          values;    // per node: the value its cells hold
    std::vector<std::vector<int>> adj;       // per node: adjacent nodes, ascending
    // Each node's cells, row-major flat indices: node n's are
    // region_cells[region_start[n] .. region_start[n + 1]).
    std::vector<int>              region_start, region_cells;

    int node_count() const { return (int)sizes.size(); }
    int const* region_begin(int n) const { return region_cells.data() + region_start[(size_t)n]; }
    int const* region_end(int n) const   { return region_cells.data() + region_start[(size_t)n + 1]; }
    int node_at(int x, int y) const {
        if (x < 0 || x >= width || y < 0 || y >= height) return -1;
        return node_of[(size_t)y * width + x];
    }
};

// Extract graph `g` from its source's cells (`width` x `height`, row-major).
// Nodes are 4-connected regions of one exact value, numbered in row-major
// order of their first cell; a connector region joins every node it touches.
graph_data extract_graph(compiled_graph const& g, bool is_number,
                         std::vector<int64_t> const& cells, int width, int height);

}  // namespace ls::internal
