#pragma once
#include "ls.hpp"
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

// The tools' module resolver (spec §2.6, Appendix A): a `use` path is relative
// to the using module's directory, '/'-separated. The canonical name is the
// lexically normalized path, so every spelling of one file is one module.

inline std::string canonical_module_name(std::string const& path) {
    return std::filesystem::path(path).lexically_normal().generic_string();
}

inline bool read_text_file(std::string const& path, std::string& out) {
    std::ifstream ifs(path, std::ios::binary);
    if (!ifs) return false;
    std::ostringstream buf;
    buf << ifs.rdbuf();
    out = buf.str();
    return true;
}

inline ls::resolver fs_resolver() {
    return [](std::string const& path, std::string const& from)
               -> std::optional<ls::module_source> {
        auto full = std::filesystem::path(from).parent_path() / path;
        std::string name = canonical_module_name(full.string());
        std::string text;
        if (!read_text_file(name, text)) return std::nullopt;
        return ls::module_source{name, std::move(text)};
    };
}
