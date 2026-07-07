#pragma once
#include <string>
#include <vector>

namespace ls {

struct diagnostic {
    bool        is_error{true};
    std::string file;
    int         line{0}, col{0};
    std::string message;

    std::string format() const {
        return file + ":" + std::to_string(line) + ":" + std::to_string(col)
             + ": " + (is_error ? "error" : "warning") + ": " + message;
    }
};

// Collects compile diagnostics; the API surfaces them via generator::error().
struct diagnostics {
    std::vector<diagnostic> all;

    void error(std::string_view file, int line, int col, std::string msg) {
        all.push_back({true, std::string(file), line, col, std::move(msg)});
    }
    void warning(std::string_view file, int line, int col, std::string msg) {
        all.push_back({false, std::string(file), line, col, std::move(msg)});
    }
    bool has_errors() const {
        for (auto const& d : all)
            if (d.is_error) return true;
        return false;
    }
    std::string format_all() const {
        std::string out;
        for (auto const& d : all) { out += d.format(); out += '\n'; }
        return out;
    }
};

}  // namespace ls
