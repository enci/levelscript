#include "modules.hpp"
#include "parser.hpp"
#include <algorithm>

namespace ls {

namespace {

struct loader {
    resolver const& resolve;
    diagnostics&    diags;
    module_closure  out;
    std::vector<ast_file> asts;    // by module id
    std::vector<int>      state;   // 0 loading (on the walk), 1 done
    std::vector<int>      path;    // the walk's in-progress chain
    std::vector<std::vector<int>> uses_of;   // direct uses, by module id

    int find(std::string const& name) const {
        for (int i = 0; i < (int)out.names.size(); ++i)
            if (out.names[i] == name) return i;
        return -1;
    }

    int load(std::string const& name, std::string_view source) {
        int id = (int)out.names.size();
        out.names.push_back(name);
        state.push_back(0);
        asts.push_back(*parse(source, name, diags, id));
        path.push_back(id);

        std::vector<int> used;
        // copy: asts may reallocate while dependencies load
        std::vector<use_decl> uses = asts[id].uses;
        for (auto const& u : uses) {
            auto loc_err = [&](std::string msg) {
                diags.error(name, u.loc.line, u.loc.col, std::move(msg));
            };
            std::optional<module_source> src;
            if (resolve) src = resolve(u.path, name);
            if (!src) {   // section 7.3, check 9
                loc_err("cannot resolve module \"" + u.path + "\"");
                continue;
            }
            int dep = find(src->name);
            if (dep >= 0 && std::find(used.begin(), used.end(), dep) != used.end()) {
                loc_err("module '" + src->name + "' is already used by this module");   // #41
                continue;
            }
            if (dep >= 0 && state[dep] == 0) {   // section 7.3, check 40: on the walk = a cycle
                std::string chain;
                auto from = std::find(path.begin(), path.end(), dep);
                for (auto it = from; it != path.end(); ++it)
                    chain += "'" + out.names[*it] + "' -> ";
                loc_err("module cycle: " + chain + "'" + src->name + "'");
                continue;
            }
            if (dep < 0) dep = load(src->name, src->source);
            used.push_back(dep);
        }

        if ((int)uses_of.size() < id + 1) uses_of.resize(id + 1);
        uses_of[id] = used;
        state[id] = 1;
        path.pop_back();
        out.order.push_back(id);   // post-order: after everything it uses
        return id;
    }

    void finish() {
        int n = (int)out.names.size();
        out.sees.assign(n, std::vector<char>(n, 0));
        for (int i = 0; i < n; ++i) {
            out.sees[i][i] = 1;
            for (int d : uses_of[i]) out.sees[i][d] = 1;   // direct uses only (D3)
        }
        // merge in canonical order
        auto& m = out.merged;
        for (int id : out.order) {
            ast_file& a = asts[id];
            for (auto& x : a.tags)          m.tags.push_back(std::move(x));
            for (auto& x : a.layers.layers) m.layers.layers.push_back(std::move(x));
            for (auto& x : a.params)        m.params.push_back(std::move(x));
            for (auto& x : a.rules)         m.rules.push_back(std::move(x));
            for (auto& x : a.sequences)     m.sequences.push_back(std::move(x));
            m.has_layers = m.has_layers || a.has_layers;
            m.has_params = m.has_params || a.has_params;
        }
        m.uses = std::move(asts[out.root].uses);
    }
};

}  // namespace

module_closure load_closure(std::string_view root_source, std::string const& root_name,
                            resolver const& resolve, diagnostics& diags) {
    loader l{resolve, diags, {}, {}, {}, {}, {}};
    l.out.root = l.load(root_name, root_source);
    l.finish();
    return std::move(l.out);
}

module_closure single_module(ast_file ast, std::string const& name) {
    module_closure c;
    c.names = {name};
    c.sees = {{1}};
    c.order = {0};
    c.merged = std::move(ast);
    return c;
}

}  // namespace ls
