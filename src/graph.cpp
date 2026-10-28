#include "graph.hpp"
#include <algorithm>

namespace ls::internal {

graph_data extract_graph(compiled_graph const& g, bool is_number,
                         std::vector<int64_t> const& cells, int width, int height) {
    graph_data d;
    size_t n = (size_t)std::max(width, 0) * (size_t)std::max(height, 0);
    if (cells.size() < n) return d;
    d.width  = width;
    d.height = height;
    d.node_of.assign(n, -1);

    auto empty = [&](int64_t v) { return is_number ? v == num_empty : (v & tag_empty) != 0; };
    // A cell overlapping both masks (a union write) is a node cell.
    auto is_node = [&](size_t i) {
        return !empty(cells[i]) && (is_number || (cells[i] & g.nodes) != 0);
    };
    auto is_connector = [&](size_t i) {
        return !is_number && !empty(cells[i]) && (cells[i] & g.edges) != 0 && !is_node(i);
    };
    // The 4-neighbours of cell i, passed to f.
    auto neighbours = [&](size_t i, auto&& f) {
        int x = (int)(i % width), y = (int)(i / width);
        if (x > 0)          f(i - 1);
        if (x + 1 < width)  f(i + 1);
        if (y > 0)          f(i - width);
        if (y + 1 < height) f(i + width);
    };

    // Value regions, in row-major order of their first cell.
    std::vector<size_t> stack;
    for (size_t i = 0; i < n; ++i) {
        if (d.node_of[i] >= 0 || !is_node(i)) continue;
        int id = (int)d.sizes.size();
        int count = 0;
        d.node_of[i] = id;
        stack.push_back(i);
        while (!stack.empty()) {
            size_t c = stack.back();
            stack.pop_back();
            ++count;
            neighbours(c, [&](size_t j) {
                if (d.node_of[j] < 0 && cells[j] == cells[i] && is_node(j)) {
                    d.node_of[j] = id;
                    stack.push_back(j);
                }
            });
        }
        d.sizes.push_back(count);
        d.values.push_back(cells[i]);
    }

    d.adj.resize(d.sizes.size());
    auto link = [&](int a, int b) {
        if (a == b) return;
        d.adj[(size_t)a].push_back(b);
        d.adj[(size_t)b].push_back(a);
    };
    // Direct contact between two regions.
    for (size_t i = 0; i < n; ++i)
        if (d.node_of[i] >= 0)
            neighbours(i, [&](size_t j) {
                if (j > i && d.node_of[j] >= 0) link(d.node_of[i], d.node_of[j]);
            });
    // Each connector region joins every pair of the nodes it touches.
    if (g.edges != 0) {
        std::vector<char> seen(n, 0);
        std::vector<int> touched;
        for (size_t i = 0; i < n; ++i) {
            if (seen[i] || !is_connector(i)) continue;
            touched.clear();
            seen[i] = 1;
            stack.push_back(i);
            while (!stack.empty()) {
                size_t c = stack.back();
                stack.pop_back();
                neighbours(c, [&](size_t j) {
                    if (d.node_of[j] >= 0) touched.push_back(d.node_of[j]);
                    else if (!seen[j] && is_connector(j)) { seen[j] = 1; stack.push_back(j); }
                });
            }
            std::sort(touched.begin(), touched.end());
            touched.erase(std::unique(touched.begin(), touched.end()), touched.end());
            for (size_t a = 0; a < touched.size(); ++a)
                for (size_t b = a + 1; b < touched.size(); ++b) link(touched[a], touched[b]);
        }
    }
    for (auto& a : d.adj) {
        std::sort(a.begin(), a.end());
        a.erase(std::unique(a.begin(), a.end()), a.end());
    }
    // Regions as one counting sort of the cells by node, row-major within each.
    d.region_start.assign(d.sizes.size() + 1, 0);
    for (size_t i = 0; i < d.sizes.size(); ++i) d.region_start[i + 1] = d.region_start[i] + d.sizes[i];
    d.region_cells.resize((size_t)d.region_start.back());
    std::vector<int> fill(d.region_start.begin(), d.region_start.end() - 1);
    for (size_t i = 0; i < n; ++i)
        if (d.node_of[i] >= 0) d.region_cells[(size_t)fill[(size_t)d.node_of[i]]++] = (int)i;
    return d;
}

}  // namespace ls::internal
