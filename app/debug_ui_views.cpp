#include "debug_ui_internal.hpp"
#include "phosphor_icons.hpp"

#include <SDL3/SDL.h>
#include <imgui_impl_sdlrenderer3.h>
#include <stb_image.h>

#include <algorithm>
#include <cfloat>
#include <cstdio>
#include <cmath>
#include <climits>

namespace ls {

// ── palette ───────────────────────────────────────────────────────────────────

// Same 12-color cycle as the VS Code extension's decorations.ts, so a tag
// value painted in the editor and one drawn here read as the same color.
// Indexed as (value_id + tag_id * 12) % 12, matching tagColor() there.
static constexpr ImU32 k_palette_dark[12] = {
    IM_COL32(133,  50,  50, 255), IM_COL32(133,  91,  50, 255),
    IM_COL32(133, 133,  50, 255), IM_COL32( 67, 133,  50, 255),
    IM_COL32( 50, 133,  91, 255), IM_COL32( 50, 133, 133, 255),
    IM_COL32( 50,  91, 133, 255), IM_COL32( 50,  50, 133, 255),
    IM_COL32( 91,  50, 133, 255), IM_COL32(133,  50, 133, 255),
    IM_COL32(133,  50,  91, 255), IM_COL32(102,  77,  59, 255),
};
static constexpr ImU32 k_palette_light[12] = {
    IM_COL32(255, 173, 173, 255), IM_COL32(255, 214, 173, 255),
    IM_COL32(255, 255, 173, 255), IM_COL32(190, 255, 173, 255),
    IM_COL32(173, 255, 214, 255), IM_COL32(173, 255, 255, 255),
    IM_COL32(173, 214, 255, 255), IM_COL32(173, 173, 255, 255),
    IM_COL32(214, 173, 255, 255), IM_COL32(255, 173, 255, 255),
    IM_COL32(255, 173, 214, 255), IM_COL32(255, 209, 158, 255),
};

// The debugger has no theme flag threaded through the view code; read it back off
// the style, the same way the app already probes ImGuiCol_WindowBg for the
// title bar tint and clear color (debug_ui.cpp).
static bool is_dark_theme() {
    return ImGui::GetStyle().Colors[ImGuiCol_WindowBg].x < 0.5f;
}

// Black or white text, whichever reads better on `bg` - the light palette
// above is pale enough that white text (draw_pattern's old fixed color)
// loses most of its contrast.
static ImU32 contrast_text_color(ImU32 bg) {
    float r = ((bg >> IM_COL32_R_SHIFT) & 0xFF) / 255.f;
    float g = ((bg >> IM_COL32_G_SHIFT) & 0xFF) / 255.f;
    float b = ((bg >> IM_COL32_B_SHIFT) & 0xFF) / 255.f;
    float luma = 0.299f * r + 0.587f * g + 0.114f * b;
    return luma > 0.6f ? IM_COL32(25, 25, 25, 230) : IM_COL32(255, 255, 255, 220);
}

ImU32 tag_color(int tag_id, int value_id,
                std::unordered_map<int, uint32_t> const* colors) {
    if (colors) {
        auto it = colors->find(value_id);
        if (it != colors->end()) return it->second;
    }
    if (tag_id >= 0 && value_id >= 0) {
        auto const& pal = is_dark_theme() ? k_palette_dark : k_palette_light;
        return pal[(value_id + tag_id * 12) % 12];
    }
    // Number/where cells have no tagset and so no editor-side color to
    // match - keep the old hash-based fallback for those.
    uint32_t h = ((uint32_t)(tag_id + 1) * 2654435761u)
               ^ ((uint32_t)(value_id + 1) * 2246822519u);
    h ^= h >> 16;
    float hue = (float)(h & 0xFFFFu) / 65536.f;
    float sat = 0.5f + 0.2f * ((float)((h >> 16) & 0xFFu) / 255.f);
    float val = 0.65f + 0.2f * ((float)((h >> 24) & 0xFFu) / 255.f);
    float r, g, b;
    ImGui::ColorConvertHSVtoRGB(hue, sat, val, r, g, b);
    return IM_COL32((ImU8)(r * 255.f + 0.5f),
                    (ImU8)(g * 255.f + 0.5f),
                    (ImU8)(b * 255.f + 0.5f), 255);
}

ImU32 with_alpha(ImU32 col, float opacity) {
    return (col & 0x00FFFFFFu) | ((ImU32)(opacity * 255.f + 0.5f) << 24);
}

ImU32 heatmap_color(float t, float opacity) {
    struct stop { float t; ImU8 r, g, b; };
    static constexpr stop stops[] = {
        {0.00f,   0,   0, 220},
        {0.33f,   0, 210, 210},
        {0.66f, 220, 210,   0},
        {1.00f, 220,   0,   0},
    };
    auto lerp8 = [](ImU8 a, ImU8 b, float s) -> ImU8 {
        return (ImU8)((float)a + ((float)b - (float)a) * s + 0.5f);
    };
    ImU8 alpha = (ImU8)(opacity * 255.f + 0.5f);
    for (int i = 0; i < 3; ++i) {
        if (t <= stops[i + 1].t) {
            float s = (t - stops[i].t) / (stops[i + 1].t - stops[i].t);
            return IM_COL32(lerp8(stops[i].r, stops[i + 1].r, s),
                            lerp8(stops[i].g, stops[i + 1].g, s),
                            lerp8(stops[i].b, stops[i + 1].b, s), alpha);
        }
    }
    return IM_COL32(220, 0, 0, alpha);
}

void col3_from_u32(uint32_t c, float out[3]) {
    out[0] = ((c >> 0) & 0xFF) / 255.f;
    out[1] = ((c >> 8) & 0xFF) / 255.f;
    out[2] = ((c >> 16) & 0xFF) / 255.f;
}

uint32_t u32_from_col3(float const c[3]) {
    return IM_COL32((ImU8)(c[0] * 255.f + 0.5f),
                    (ImU8)(c[1] * 255.f + 0.5f),
                    (ImU8)(c[2] * 255.f + 0.5f), 255);
}

palette layer_palette(compiled const& meta, project_config const& cfg, int grid_id) {
    palette p;
    if (grid_id >= 0 && grid_id < (int)meta.layers.size())
        p.tag_id = meta.layers[grid_id].tag_id;
    if (p.tag_id >= 0 && p.tag_id < (int)meta.tag_names.size()) {
        auto it = cfg.tag_colors.find(meta.tag_names[p.tag_id]);
        if (it != cfg.tag_colors.end()) p.colors = &it->second;
    }
    return p;
}

std::string cell_glyph(compiled const& meta, int tag_id, int v) {
    if (v < 0) return "\xc2\xb7";   // middle dot
    if (tag_id < 0 || tag_id >= (int)meta.tag_values.size())
        return std::to_string(v);
    auto const& vals = meta.tag_values[tag_id];
    if (v < (int)vals.size()) return vals[v].substr(0, 1);
    // union slots follow the values (see mask_to_slot)
    int ui = v - (int)vals.size();
    if (tag_id < (int)meta.tag_unions.size() && ui < (int)meta.tag_unions[tag_id].size())
        return meta.tag_unions[tag_id][ui].first.substr(0, 1);
    return "?";
}

// grid::at() returns a value MASK for a tag layer (bit 1..30 - a single bit
// for a normal cell, spec section 3), but tag_color()/cell_glyph() and the tag-color
// config's vmap all still key by the 0-based value id. Converts a mask back
// to that id (the lowest set value bit); -1 (the public API's empty/invalid
// sentinel) passes through unchanged.
static int mask_to_vid(int mask) {
    if (mask < 0) return mask;
    for (int b = 1; b <= 30; ++b)
        if (mask & (1 << b)) return b - 1;
    return -1;
}

// The display slot for a tag-layer mask - what tag_color(), cell_glyph() and
// the tag-color overrides key by. A mask that is exactly a named union takes
// that union's slot, numbered after the tagset's values (the same numbering
// the VS Code extension's decorations use); anything else falls back to its
// lowest value bit.
static int mask_to_slot(compiled const& meta, int tag_id, int mask) {
    if (mask > 0 && tag_id >= 0 && tag_id < (int)meta.tag_unions.size()) {
        auto const& us = meta.tag_unions[tag_id];
        for (int i = 0; i < (int)us.size(); ++i)
            if (us[i].second == mask)
                return (int)meta.tag_values[tag_id].size() + i;
    }
    return mask_to_vid(mask);
}

// Tooltip label for a tag-layer mask: the value name, the union name when
// the mask is exactly a named union, else the member names joined by '|'.
static std::string mask_label(compiled const& meta, int tag_id, int mask) {
    if (tag_id < 0 || tag_id >= (int)meta.tag_values.size()) return "?";
    int slot = mask_to_slot(meta, tag_id, mask);
    auto const& vals = meta.tag_values[tag_id];
    if (slot >= (int)vals.size())
        return meta.tag_unions[tag_id][slot - (int)vals.size()].first;
    std::string out;
    for (int vi = 0; vi < (int)vals.size() && vi < 30; ++vi)
        if (mask & (1 << (vi + 1))) {
            if (!out.empty()) out += '|';
            out += vals[vi];
        }
    return out.empty() ? "?" : out;
}

void draw_centered_text(ImDrawList* dl, ImVec2 p0, float box_px, ImU32 col,
                        char const* text) {
    ImVec2 ts = ImGui::CalcTextSize(text);
    dl->AddText({ p0.x + (box_px - ts.x) * 0.5f,
                  p0.y + (box_px - ImGui::GetTextLineHeight()) * 0.5f },
                col, text);
}

// ── tileset texture ───────────────────────────────────────────────────────────

bool tile_texture::sync(SDL_Renderer* renderer, std::string const& abs_path) {
    if (abs_path == loaded_path) return sdl_tex != nullptr;
    if (sdl_tex) { SDL_DestroyTexture(sdl_tex); sdl_tex = nullptr; }
    loaded_path.clear();
    if (abs_path.empty()) return false;
    int w, h, ch;
    unsigned char* px = stbi_load(abs_path.c_str(), &w, &h, &ch, 4);
    if (!px) return false;
    SDL_Surface* surf = SDL_CreateSurfaceFrom(w, h, SDL_PIXELFORMAT_RGBA32, px, w * 4);
    sdl_tex = SDL_CreateTextureFromSurface(renderer, surf);
    SDL_DestroySurface(surf);
    stbi_image_free(px);
    if (!sdl_tex) return false;
    // Point sampling, not SDL's default bilinear - most tilesets here are
    // pixel art, and linear filtering blurs it when the viewport upscales.
    SDL_SetTextureScaleMode(sdl_tex, SDL_SCALEMODE_NEAREST);
    width = w;
    height = h;
    loaded_path = abs_path;
    return true;
}

tile_texture::~tile_texture() {
    if (sdl_tex) SDL_DestroyTexture(sdl_tex);
}

std::string resolve_path(std::string const& ls_path, std::string const& rel) {
    if (rel.empty()) return {};
    size_t pos = ls_path.find_last_of("/\\");
    return (pos == std::string::npos) ? rel : ls_path.substr(0, pos + 1) + rel;
}

void sync_tilesets(SDL_Renderer* renderer, std::string const& ls_path,
                   project_config const& cfg,
                   std::unordered_map<std::string, tile_texture>& textures) {
    for (auto const& ts : cfg.tilesets)
        textures[ts.name].sync(renderer, resolve_path(ls_path, ts.path));
    for (auto it = textures.begin(); it != textures.end(); ) {
        bool live = std::any_of(cfg.tilesets.begin(), cfg.tilesets.end(),
                                [&](tileset_config const& ts) { return ts.name == it->first; });
        if (live) ++it; else it = textures.erase(it);
    }
}

// ── statement display ─────────────────────────────────────────────────────────
//
// Labels derive from the parsed AST (display metadata only). Operation names
// and argument shapes mirror the operation table in src/sema.cpp (op_table(),
// spec section 6.0) -- a new operation added there shows up here automatically
// as `name(arg, ...)`.

static std::string op_arg_desc(op_arg const& a) {
    std::string s = a.name.empty() ? "" : a.name + "=";
    switch (a.what) {
        case op_arg::kind::int_:  s += std::to_string(a.int_val); break;
        case op_arg::kind::ident: s += a.ident;                   break;
        case op_arg::kind::expr:  s += "(...)";                   break;
    }
    return s;
}

std::string stmt_desc(program_stmt const& s) {
    if (s.what == program_stmt::kind::op_call) {
        std::string d = s.op_name + "(";
        for (size_t i = 0; i < s.op_args.size(); ++i) {
            if (i) d += ", ";
            d += op_arg_desc(s.op_args[i]);
        }
        return d + ")";
    }
    static char const* const names[] = {"once", "scatter", "everywhere", "grow", "settle"};
    std::string p = names[(int)s.mode];
    if (s.count) {
        p += s.count->kind == expr_kind::int_lit ? "(" + std::to_string(s.count->int_val)
                                                 : std::string("((...)");
        p += s.count_percent ? "%)" : ")";
    }
    p += s.inline_rule ? std::string(" { ... }") : " " + s.rule_name;
    if (s.guard) p += "  when (...)";
    return p;
}

char const* stmt_icon(program_stmt const& s, compiled_stmt const& cs) {
    if (s.what == program_stmt::kind::apply)
        return cs.seq_id >= 0 ? phosphor::PH_LIST_NUMBERS   // a sequence (section 6.10)
                              : phosphor::PH_LIGHTNING;     // a rule
    // Operation calls (spec section 6.0): icon per operation name.
    if (s.op_name == "resize")  return phosphor::PH_FRAME_CORNERS;
    if (s.op_name == "upscale") return phosphor::PH_MAGNIFYING_GLASS_PLUS;
    if (s.op_name == "trim")    return phosphor::PH_SCISSORS;
    if (s.op_name == "mirror")  return phosphor::PH_ARROWS_LEFT_RIGHT;
    if (s.op_name == "pad")     return phosphor::PH_ARROWS_OUT_SIMPLE;
    if (s.op_name == "path")    return phosphor::PH_PATH;
    return phosphor::PH_QUESTION;
}

// ── pattern mini-grid (Rule window) ──────────────────────────────────────────

static std::string pattern_grid_name(compiled const& meta, compiled_pattern const& pat) {
    if (pat.is_where || pat.grid_id == where_grid) return "where";
    if (pat.grid_id >= 0 && pat.grid_id < (int)meta.layers.size())
        return meta.layers[pat.grid_id].name;
    return "?";
}

struct cell_view {
    ImU32       bg;
    std::string glyph;
};

static cell_view pattern_cell_view(compiled const& meta, compiled_pattern const& pat,
                                   compiled_cell const& cc, palette const& pal) {
    switch (cc.what) {
        case compiled_cell::kind::wildcard:
            return { IM_COL32(50, 50, 50, 255), "*" };
        case compiled_cell::kind::expr:
            return { IM_COL32(70, 70, 95, 255), "()" };
        case compiled_cell::kind::variable:
            return { IM_COL32(70, 95, 70, 255), "?" };
        case compiled_cell::kind::value:
            break;
    }
    if (pat.is_number || pat.is_where) {
        if (cc.ne) return { IM_COL32(80, 80, 80, 255), "!." };
        if (cc.val == num_empty) return { IM_COL32(80, 80, 80, 255), "." };
        return { tag_color(-1, (int)cc.val, nullptr), std::to_string(cc.val) };
    }
    // Tag mask: empty-only reads as '.', otherwise show the lowest value bit
    // (a union mask previews as its first member).
    int vid = mask_to_slot(meta, pal.tag_id, (int)cc.val);
    if (vid < 0) return { IM_COL32(80, 80, 80, 255), "." };
    return { tag_color(pal.tag_id, vid, pal.colors), cell_glyph(meta, pal.tag_id, vid) };
}

static void draw_pattern(compiled const& meta, compiled_pattern const& pat,
                         float px, project_config const& cfg) {
    palette pal = layer_palette(meta, cfg, pat.grid_id);

    float stride   = px + 1.f;
    float rounding = std::min(px * 0.2f, 4.f);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 origin  = ImGui::GetCursorScreenPos();
    for (int r = 0; r < pat.rows; ++r) {
        for (int c = 0; c < pat.cols; ++c) {
            cell_view cv = pattern_cell_view(meta, pat, pat.at(r, c), pal);
            ImVec2 p0 = { origin.x + c * stride, origin.y + r * stride };
            dl->AddRectFilled(p0, { p0.x + px, p0.y + px }, cv.bg, rounding);
            draw_centered_text(dl, p0, px, contrast_text_color(cv.bg),
                               cv.glyph.c_str());
        }
    }
    ImGui::Dummy({ pat.cols * stride - 1.f, pat.rows * stride - 1.f });
}

// Total height (name label + mini-grid) that draw_pattern's group occupies.
static float pattern_group_height(compiled_pattern const& pat, float mini_px) {
    return ImGui::GetTextLineHeightWithSpacing() + pat.rows * (mini_px + 1.f) - 1.f;
}

static void draw_pattern_group(compiled const& meta, compiled_pattern const& pat,
                               float mini_px, project_config const& cfg) {
    ImGui::BeginGroup();
    ImGui::TextDisabled("%s", pattern_grid_name(meta, pat).c_str());
    draw_pattern(meta, pat, mini_px, cfg);
    ImGui::EndGroup();
}

// "rot 90", "flip h", "rot 90 + flip v" - "" for the identity variant.
static std::string variant_label(compiled_pair const& pr) {
    using mirror = compiled_pair::mirror;
    std::string r = pr.rotation ? "rot " + std::to_string(pr.rotation) : "";
    std::string f = pr.flip == mirror::h    ? "flip h"
                  : pr.flip == mirror::v    ? "flip v"
                  : pr.flip == mirror::both ? "flip h+v" : "";
    return r.empty() ? f : f.empty() ? r : r + " + " + f;
}

// Width of draw_pattern_group: the grid-name label or the mini-grid.
static float pattern_group_width(compiled const& meta, compiled_pattern const& pat,
                                 float mini_px) {
    float grid_w = pat.cols * (mini_px + 1.f) - 1.f;
    return std::max(grid_w, ImGui::CalcTextSize(pattern_grid_name(meta, pat).c_str()).x);
}

// One variant as a flow of pieces - its match patterns, the arrow, each write
// alternative (separated by an alternatives mark) - packed into lines that
// fit the panel, each line centred vertically. A small rule reads as one
// row; a big one wraps instead of running off the right edge.
static void draw_rule_variant(compiled const& meta, compiled_pair const& pair,
                              float mini_px, project_config const& cfg) {
    std::vector<compiled_pattern const*> rhs_leaves;
    collect_write_leaves(pair.rhs, rhs_leaves);
    if (pair.lhs.empty() && rhs_leaves.empty()) return;

    enum class piece_kind { pattern, arrow, alt };
    struct piece { piece_kind kind; compiled_pattern const* pat; float w, h, gap; };
    std::vector<piece> pieces;
    float icon_w = ImGui::CalcTextSize(phosphor::PH_ARROW_RIGHT).x;
    float icon_h = ImGui::GetTextLineHeight();
    auto add_pat = [&](compiled_pattern const& p, float gap) {
        pieces.push_back({piece_kind::pattern, &p, pattern_group_width(meta, p, mini_px),
                          pattern_group_height(p, mini_px), gap});
    };
    for (size_t k = 0; k < pair.lhs.size(); ++k) add_pat(pair.lhs[k], k ? 8.f : 0.f);
    pieces.push_back({piece_kind::arrow, nullptr, icon_w, icon_h, 20.f});
    for (size_t k = 0; k < rhs_leaves.size(); ++k) {
        if (k) pieces.push_back({piece_kind::alt, nullptr, icon_w, icon_h, 10.f});
        add_pat(*rhs_leaves[k], k ? 10.f : 20.f);
    }

    // pass 1: pack into lines
    float avail = std::max(ImGui::GetContentRegionAvail().x, 1.f);
    std::vector<std::pair<size_t, size_t>> lines;   // [begin, end)
    float x = 0.f;
    size_t begin = 0;
    for (size_t k = 0; k < pieces.size(); ++k) {
        float need = (k == begin ? 0.f : pieces[k].gap) + pieces[k].w;
        // a separator (arrow, alternatives mark) travels with the pattern
        // after it, so no line ends on one
        bool sep = pieces[k].kind != piece_kind::pattern;
        float with_next = need + (sep && k + 1 < pieces.size()
                                  ? pieces[k + 1].gap + pieces[k + 1].w : 0.f);
        if (k > begin && x + with_next > avail) {
            lines.push_back({begin, k});
            begin = k;
            x = pieces[k].w;
        } else {
            x += need;
        }
    }
    lines.push_back({begin, pieces.size()});

    // pass 2: draw, each line centred on its tallest piece
    ImVec4 arrow_col(0.40f, 0.68f, 0.98f, 1.f);
    for (auto [b, e] : lines) {
        float line_h = 0.f;
        for (size_t k = b; k < e; ++k) line_h = std::max(line_h, pieces[k].h);
        for (size_t k = b; k < e; ++k) {
            auto const& pc = pieces[k];
            if (k > b) ImGui::SameLine(0.f, pc.gap);
            ImGui::BeginGroup();
            if (line_h > pc.h) ImGui::Dummy({0.f, (line_h - pc.h) * 0.5f});
            if (pc.kind == piece_kind::pattern) {
                draw_pattern_group(meta, *pc.pat, mini_px, cfg);
            } else if (pc.kind == piece_kind::arrow) {
                ImGui::PushStyleColor(ImGuiCol_Text, arrow_col);
                ImGui::TextUnformatted(phosphor::PH_ARROW_RIGHT);
                ImGui::PopStyleColor();
            } else {
                ImGui::TextDisabled("%s", phosphor::PH_ARROWS_LEFT_RIGHT);
            }
            ImGui::EndGroup();
        }
    }
}

// The leaf statement the last step worked on, followed down the statement
// stack through nested sequences (section 6.10). `trail` collects "in <sequence>
// (iteration k)" for each level. Null when there is no step yet.
static std::pair<program_stmt const*, compiled_stmt const*>
shown_leaf(script const& sc, debug_run const& run, std::string& trail) {
    auto frames = run.gen.stmt_stack();
    if (frames.empty()) return {nullptr, nullptr};
    if (sc.entry < 0 || sc.entry >= (int)sc.ast.sequences.size() ||
        sc.entry >= (int)sc.meta.sequences.size())
        return {nullptr, nullptr};
    auto const* ast_list  = &sc.ast.sequences[(size_t)sc.entry].stmts;
    auto const* meta_list = &sc.meta.sequences[(size_t)sc.entry].stmts;
    for (size_t k = 0; k < frames.size(); ++k) {
        int idx = frames[k].index;
        if (idx < 0 || idx >= (int)ast_list->size() || idx >= (int)meta_list->size())
            return {nullptr, nullptr};
        auto const& ps = (*ast_list)[(size_t)idx];
        auto const& cs = (*meta_list)[(size_t)idx];
        if (k + 1 == frames.size()) return {&ps, &cs};
        int sid = cs.seq_id;
        if (sid < 0 || sid >= (int)sc.meta.sequences.size() ||
            sid >= (int)sc.ast.sequences.size())
            return {nullptr, nullptr};
        if (!trail.empty()) trail += " > ";
        trail += sc.meta.sequences[(size_t)sid].name + " (iteration " +
                 std::to_string(frames[k + 1].iteration + 1) + ")";
        ast_list  = &sc.ast.sequences[(size_t)sid].stmts;
        meta_list = &sc.meta.sequences[(size_t)sid].stmts;
    }
    return {nullptr, nullptr};
}

void draw_rule_window(script const& sc, debug_run const& run, float mini_px,
                      project_config const& cfg) {
    std::string trail;
    auto [leaf, cleaf] = shown_leaf(sc, run, trail);
    if (!leaf) {
        ImGui::TextDisabled("(none yet)");
        return;
    }
    if (!trail.empty()) ImGui::TextDisabled("in %s", trail.c_str());
    auto const& stmt = *leaf;
    if (stmt.what != program_stmt::kind::apply) {
        ImGui::TextDisabled("%s", stmt_desc(stmt).c_str());
        return;
    }
    int rid = cleaf->rule_id;
    if (rid < 0 || rid >= (int)sc.meta.rules.size()) {
        ImGui::TextDisabled("?");
        return;
    }

    auto const& cr = sc.meta.rules[(size_t)rid];
    ImGui::TextUnformatted(cr.name.c_str());   // an inline rule's is its position
    rule_decl const* decl = stmt.inline_rule.get();
    for (auto const& rd : sc.ast.rules)
        if (!decl && rd.name == stmt.rule_name) decl = &rd;
    if (decl) {
        auto const& rd = *decl;
        std::string attrs;
        if (rd.symmetry != "none") attrs += " sym=" + rd.symmetry;
        if (!rd.rotation_angles.empty()) {
            attrs += " rot={";
            for (size_t i = 0; i < rd.rotation_angles.size(); ++i) {
                if (i) attrs += ",";
                attrs += std::to_string(rd.rotation_angles[i]);
            }
            attrs += "}";
        }
        if (rd.body == body_combinator::ordered) attrs += " ordered";
        if (!attrs.empty()) ImGui::TextDisabled("%s", attrs.c_str());
    }
    ImGui::Separator();
    if (cr.pairs.empty()) return;

    int n = (int)cr.pairs.size();
    for (int pi = 0; pi < n; ++pi) {
        ImGui::PushID(pi);
        // which sub-rule, and which rotation/flip of it (section 5.6), this is
        auto const& pr = cr.pairs[(size_t)pi];
        std::string tag;
        if (cr.body != body_combinator::none)
            tag += "sub-rule " + std::to_string(pr.sub_rule_idx + 1);
        std::string variant = variant_label(pr);
        if (!variant.empty()) tag += (tag.empty() ? "" : " · ") + variant;
        if (n > 1 || !tag.empty())
            ImGui::TextDisabled("%s %d / %d%s%s", phosphor::PH_STACK, pi + 1, n,
                                tag.empty() ? "" : "  ·  ", tag.c_str());
        draw_rule_variant(sc.meta, cr.pairs[(size_t)pi], mini_px, cfg);
        if (pi + 1 < n) {
            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();
        }
        ImGui::PopID();
    }
}

// ── program window ────────────────────────────────────────────────────────────

// One statement list - the entry's body, or a sequence's - as tree rows.
// Sequence applications are tree nodes unfolding into their bodies (section 6.10;
// the compiler rejects cycles, so the tree is finite). `cur` is the row the
// run is on in this list (-1: none); `all_past` dims a body whose applying
// statement already finished. `path` is the run's statement stack: the rows
// it names, level by level, are the ones being executed.
struct program_tree {
    script&                        sc;   // breakpoints are toggled here
    std::vector<stmt_frame> const& path;
    bool                           follow;   // the step moved: open + reveal its path
    ImVec4                         accent;

    // `prefix`: the dotted path of the applying statements ("2.0."), so a row
    // reads as its full stmt_stack position - 2.0.1 = body item 1 of the
    // sequence at 2.0.
    void list(std::vector<program_stmt> const& ast_list,
              std::vector<compiled_stmt> const& meta_list,
              int depth, int cur, bool all_past, std::string const& prefix) {
        int n = (int)std::min(ast_list.size(), meta_list.size());
        for (int i = 0; i < n; ++i) {
            auto const& stmt = ast_list[(size_t)i];
            auto const& cs   = meta_list[(size_t)i];
            bool is_current = i == cur;
            bool is_past    = all_past || (cur >= 0 && i < cur);
            int  sid        = cs.what == compiled_stmt::kind::apply ? cs.seq_id : -1;
            bool is_seq     = sid >= 0 && sid < (int)sc.ast.sequences.size() &&
                              sid < (int)sc.meta.sequences.size() && depth < 64;
            // the run is inside this sequence: the stack reaches below this row
            bool inside = is_current && is_seq && (int)path.size() > depth + 1 &&
                          path[(size_t)depth].index == i;

            ImGui::TableNextRow();
            if (is_current)
                ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0,
                                       ImGui::GetColorU32(ImGuiCol_Header));
            ImGui::PushID(i);
            if (is_current)   ImGui::PushStyleColor(ImGuiCol_Text, accent);
            else if (is_past) ImGui::PushStyleColor(ImGuiCol_Text,
                                  ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));

            std::string num = prefix + std::to_string(i);

            // breakpoint gutter (VS Code): a red dot; a faint one on hover;
            // click toggles. Keyed by the dotted path, so a breakpoint in a
            // sequence body hits on every iteration.
            ImGui::TableNextColumn();
            {
                float h = ImGui::GetTextLineHeight();
                ImVec2 p0 = ImGui::GetCursorScreenPos();
                bool set = sc.breakpoints.count(num) != 0;
                if (ImGui::InvisibleButton("##bp", {h, h})) {
                    if (set) sc.breakpoints.erase(num); else sc.breakpoints.insert(num);
                    set = !set;
                }
                bool hover = ImGui::IsItemHovered();
                if (hover) ImGui::SetTooltip(set ? "Remove breakpoint" : "Add breakpoint");
                if (set || hover) {
                    ImU32 red = set ? IM_COL32(229, 20, 0, 255)    // #E51400
                                    : IM_COL32(229, 20, 0, 90);
                    ImGui::GetWindowDrawList()->AddCircleFilled(
                        {p0.x + h * 0.5f, p0.y + h * 0.5f}, h * 0.32f, red, 16);
                }
            }

            // dotted position: one stmt_stack frame index per level
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(num.c_str());

            ImGui::TableNextColumn();
            std::string label = std::string(stmt_icon(stmt, cs)) + "  " + stmt_desc(stmt);
            // Indent this cell only: a tree push would indent the whole row,
            // shifting the index column too. Children nest under PushID(i).
            ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_SpanAvailWidth |
                                       ImGuiTreeNodeFlags_NoTreePushOnOpen;
            if (is_seq) flags |= ImGuiTreeNodeFlags_DefaultOpen;   // unfolded
            else        flags |= ImGuiTreeNodeFlags_Leaf;
            float indent = depth * ImGui::GetStyle().IndentSpacing;
            if (indent > 0.f) ImGui::Indent(indent);
            if (is_seq && follow && inside) ImGui::SetNextItemOpen(true);
            bool open = ImGui::TreeNodeEx("##stmt", flags, "%s", label.c_str());
            if (inside) {   // which iteration of this sequence is running
                int it = path[(size_t)depth + 1].iteration + 1;
                ImGui::SameLine();
                if (cs.count_lit >= 0)
                    ImGui::TextDisabled("iteration %d / %d", it, cs.count_lit);
                else
                    ImGui::TextDisabled("iteration %d", it);
            }
            if (indent > 0.f) ImGui::Unindent(indent);
            bool leaf_here = is_current && !inside;
            if (leaf_here && (ImGui::IsWindowAppearing() || (follow && !ImGui::IsItemVisible())))
                ImGui::SetScrollHereY(0.25f);

            if (is_current || is_past) ImGui::PopStyleColor();

            if (is_seq && open) {
                int child_cur = inside ? path[(size_t)depth + 1].index : -1;
                list(sc.ast.sequences[(size_t)sid].stmts, sc.meta.sequences[(size_t)sid].stmts,
                     depth + 1, child_cur, is_past, num + ".");
            }
            ImGui::PopID();
        }
    }
};

void draw_program_window(script& sc, debug_run const& run) {
    if (sc.entry < 0 || sc.entry >= (int)sc.ast.sequences.size() ||
        sc.entry >= (int)sc.meta.sequences.size() ||
        sc.ast.sequences[(size_t)sc.entry].stmts.empty()) {
        ImGui::TextDisabled("(empty entry)");
        return;
    }
    auto const& body      = sc.ast.sequences[(size_t)sc.entry].stmts;
    auto const& meta_body = sc.meta.sequences[(size_t)sc.entry].stmts;

    // Follow the run: when the step moves, open and reveal its path. Between
    // moves the user may fold anything, the running sequence included.
    std::vector<stmt_frame> path = run.started ? run.gen.stmt_stack()
                                               : std::vector<stmt_frame>{};
    static std::vector<stmt_frame> last_path;
    bool follow = path.size() != last_path.size();
    for (size_t k = 0; !follow && k < path.size(); ++k)
        follow = path[k].index != last_path[k].index ||
                 path[k].iteration != last_path[k].iteration;
    last_path = path;

    // Depth 0 keeps the run's own marker: a finished top-level statement
    // moves it to the next one; a step inside a sequence leaves it on the
    // statement that applied the sequence (debug_run::current_stmt).
    int current = run.current_stmt();
    if (!path.empty() && path[0].index != current) path.clear();   // a stale stack

    ImGui::BeginChild("##program_list", {0.f, 0.f}, ImGuiChildFlags_Borders);
    // A real table keeps the tree column aligned however wide the dotted
    // numbers get.
    if (ImGui::BeginTable("##program_table", 3, ImGuiTableFlags_SizingFixedFit)) {
        ImGui::TableSetupColumn("bp",   ImGuiTableColumnFlags_WidthFixed);
        ImGui::TableSetupColumn("#",    ImGuiTableColumnFlags_WidthFixed);
        ImGui::TableSetupColumn("stmt", ImGuiTableColumnFlags_WidthStretch);
        program_tree tree{sc, path, follow, ImGui::GetStyleColorVec4(ImGuiCol_CheckMark)};
        tree.list(body, meta_body, 0, current, false, "");
        ImGui::EndTable();
    }
    ImGui::EndChild();
}

// ── shapes ────────────────────────────────────────────────────────────────────

void draw_shape(ImDrawList* dl, ImVec2 c, float r, tag_shape shape, ImU32 col) {
    float const t = std::max(1.f, r * 0.3f);   // stroke for outline shapes
    switch (shape) {
        case tag_shape::none: break;
        case tag_shape::circle:  dl->AddCircleFilled(c, r, col, 20); break;
        case tag_shape::ring:    dl->AddCircle(c, r - t * 0.5f, col, 20, t); break;
        case tag_shape::square:
            dl->AddRectFilled({ c.x - r * 0.85f, c.y - r * 0.85f },
                              { c.x + r * 0.85f, c.y + r * 0.85f }, col);
            break;
        case tag_shape::diamond: dl->AddNgonFilled(c, r * 1.1f, col, 4); break;
        case tag_shape::hexagon: dl->AddNgonFilled(c, r, col, 6); break;
        case tag_shape::triangle:
            dl->AddTriangleFilled({ c.x, c.y - r },
                                  { c.x + r * 0.95f, c.y + r * 0.75f },
                                  { c.x - r * 0.95f, c.y + r * 0.75f }, col);
            break;
        case tag_shape::cross: {
            float d = r * 0.75f;
            dl->AddLine({ c.x - d, c.y - d }, { c.x + d, c.y + d }, col, t);
            dl->AddLine({ c.x - d, c.y + d }, { c.x + d, c.y - d }, col, t);
            break;
        }
        case tag_shape::star: {
            // Triangle fan from the center: fine for a concave outline.
            constexpr float k_pi = 3.14159265f;
            ImVec2 pts[10];
            for (int i = 0; i < 10; ++i) {
                float a  = -k_pi / 2 + i * k_pi / 5;
                float rr = (i % 2 == 0) ? r * 1.1f : r * 0.45f;
                pts[i] = { c.x + std::cos(a) * rr, c.y + std::sin(a) * rr };
            }
            for (int i = 0; i < 10; ++i)
                dl->AddTriangleFilled(c, pts[i], pts[(i + 1) % 10], col);
            break;
        }
    }
}

// A small button showing the current shape; click opens a grid of all shapes.
static bool shape_picker(tag_shape& shape, ImU32 col) {
    bool changed = false;
    float const box = ImGui::GetFrameHeight();
    if (ImGui::Button("##shape", { box * 1.4f, box })) ImGui::OpenPopup("##shape_pop");
    ImVec2 lo = ImGui::GetItemRectMin(), hi = ImGui::GetItemRectMax();
    ImVec2 mid = { (lo.x + hi.x) * 0.5f, (lo.y + hi.y) * 0.5f };
    ImDrawList* dl = ImGui::GetWindowDrawList();
    if (shape == tag_shape::none)
        dl->AddLine({ mid.x - 4.f, mid.y }, { mid.x + 4.f, mid.y },
                    ImGui::GetColorU32(ImGuiCol_TextDisabled), 1.5f);
    else
        draw_shape(dl, mid, box * 0.32f, shape, col);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Shape: %s", shape_name(shape));

    if (ImGui::BeginPopup("##shape_pop")) {
        for (int i = 0; i < k_tag_shape_count; ++i) {
            ImGui::PushID(i);
            if (i % 5 != 0) ImGui::SameLine();
            bool sel = (int)shape == i;
            if (ImGui::Selectable("##s", sel, 0, { 26.f, 26.f })) {
                shape   = (tag_shape)i;
                changed = true;
                ImGui::CloseCurrentPopup();
            }
            ImVec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
            ImVec2 m = { (a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f };
            if (i == 0)
                ImGui::GetWindowDrawList()->AddLine({ m.x - 5.f, m.y }, { m.x + 5.f, m.y },
                    ImGui::GetColorU32(ImGuiCol_TextDisabled), 1.5f);
            else
                draw_shape(ImGui::GetWindowDrawList(), m, 8.f, (tag_shape)i, col);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", shape_name((tag_shape)i));
            ImGui::PopID();
        }
        ImGui::EndPopup();
    }
    return changed;
}

// Set/clear an entry in the sparse shape map (none == absent).
static void set_shape(std::unordered_map<int, tag_shape>& m, int slot, tag_shape sh) {
    if (sh == tag_shape::none) m.erase(slot); else m[slot] = sh;
}

// ── tags window ───────────────────────────────────────────────────────────────

void draw_tags_window(compiled const& meta, project_config& cfg) {
    if (meta.tag_names.empty()) {
        ImGui::TextDisabled("(no tags)");
        return;
    }

    for (int ti = 0; ti < (int)meta.tag_names.size(); ++ti) {
        auto& vmap = cfg.tag_colors[meta.tag_names[(size_t)ti]];
        auto& smap = cfg.tag_shapes[meta.tag_names[(size_t)ti]];
        // Shape row helper: preview uses the value's current color.
        auto shape_cell = [&](int slot, ImU32 col) {
            tag_shape sh = smap.count(slot) ? smap.at(slot) : tag_shape::none;
            if (shape_picker(sh, col)) set_shape(smap, slot, sh);
            ImGui::SameLine();
        };

        ImGui::PushID(ti);
        bool open = ImGui::TreeNodeEx(meta.tag_names[(size_t)ti].c_str(),
                                      ImGuiTreeNodeFlags_DefaultOpen);
        if (open) {
            auto const& vals = meta.tag_values[(size_t)ti];
            for (int vi = 0; vi < (int)vals.size(); ++vi) {
                ImGui::PushID(vi);

                ImU32 cur = vmap.count(vi) ? vmap.at(vi) : tag_color(ti, vi);
                float col[3];
                col3_from_u32(cur, col);
                if (ImGui::ColorEdit3("##vc", col,
                        ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel))
                    vmap[vi] = u32_from_col3(col);
                ImGui::SameLine();
                shape_cell(vi, cur);

                ImGui::TextUnformatted(vals[(size_t)vi].c_str());

                ImGui::PopID();
            }
            // Named unions follow the values, in the palette slots after them
            // (mask_to_slot) - `D = F | W` gets its own editable swatch.
            if (ti < (int)meta.tag_unions.size()) {
                auto const& us = meta.tag_unions[(size_t)ti];
                for (int ui = 0; ui < (int)us.size(); ++ui) {
                    int slot = (int)vals.size() + ui;
                    ImGui::PushID(slot);

                    ImU32 cur = vmap.count(slot) ? vmap.at(slot) : tag_color(ti, slot);
                    float col[3];
                    col3_from_u32(cur, col);
                    if (ImGui::ColorEdit3("##vc", col,
                            ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel))
                        vmap[slot] = u32_from_col3(col);
                    ImGui::SameLine();
                    shape_cell(slot, cur);

                    std::string members;
                    for (int vi = 0; vi < (int)vals.size() && vi < 30; ++vi)
                        if (us[(size_t)ui].second & (int64_t(1) << (vi + 1))) {
                            if (!members.empty()) members += " | ";
                            members += vals[(size_t)vi];
                        }
                    ImGui::TextUnformatted(us[(size_t)ui].first.c_str());
                    ImGui::SameLine();
                    ImGui::TextDisabled("= %s", members.c_str());

                    ImGui::PopID();
                }
            }
            ImGui::TreePop();
        }
        ImGui::PopID();
    }
}

// ── layers window ─────────────────────────────────────────────────────────────

static void set_sampler_nearest(ImDrawList const*, ImDrawCmd const*);   // composite grid
static void draw_mode_combo(layer_mode& mode, bool numeric);              // layer controls
static void draw_tileset_combo(std::string& name, std::vector<tileset_config> const& tilesets);
static void draw_corner_size_combo(corner_size& size);
static void set_sampler_linear(ImDrawList const*, ImDrawCmd const*);

layer_thumbs::~layer_thumbs() {
    for (auto* t : tex) if (t) SDL_DestroyTexture(t);
}

// Repaint every layer's texture from the snapshot: tag layers in the shared
// tag palette (unions included, mask_to_slot), number layers as a heatmap
// over the layer's own value range, empty cells in a neutral tone.
static void rebuild_thumbs(script const& sc, debug_run const& run,
                           project_config const& cfg, SDL_Renderer* renderer,
                           layer_thumbs& th) {
    level const& lv = run.snap;
    int w = lv.width(), h = lv.height(), n = lv.layer_count();
    for (int i = n; i < (int)th.tex.size(); ++i)
        if (th.tex[(size_t)i]) SDL_DestroyTexture(th.tex[(size_t)i]);
    th.tex.resize((size_t)n, nullptr);
    th.tw.resize((size_t)n, 0);
    th.th.resize((size_t)n, 0);
    if (w <= 0 || h <= 0) return;

    ImU32 empty = is_dark_theme() ? IM_COL32(48, 48, 52, 255) : IM_COL32(222, 222, 228, 255);
    std::vector<ImU32> px((size_t)w * h);
    for (int li = 0; li < n; ++li) {
        grid g = lv.layer(li);
        palette pal = layer_palette(sc.meta, cfg, sc.meta.layer_id(lv.layer_name(li)));
        int lo = INT_MAX, hi = INT_MIN;
        if (g.is_number())
            for (int y = 0; y < h; ++y)
                for (int x = 0; x < w; ++x)
                    if (!g.is_empty(x, y)) { lo = std::min(lo, g.at(x, y)); hi = std::max(hi, g.at(x, y)); }
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x) {
                ImU32& c = px[(size_t)y * w + x];
                if (g.is_empty(x, y)) { c = empty; continue; }
                int v = g.at(x, y);
                if (g.is_number())
                    c = heatmap_color(hi > lo ? (float)(v - lo) / (float)(hi - lo) : 0.5f, 1.f);
                else
                    c = tag_color(pal.tag_id, mask_to_slot(sc.meta, pal.tag_id, v), pal.colors) |
                        IM_COL32(0, 0, 0, 255);
            }
        SDL_Texture*& t = th.tex[(size_t)li];
        if (t && (th.tw[(size_t)li] != w || th.th[(size_t)li] != h)) { SDL_DestroyTexture(t); t = nullptr; }
        if (!t) {
            // ImU32 (IM_COL32) is R,G,B,A in memory - SDL's RGBA32
            t = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32,
                                  SDL_TEXTUREACCESS_STREAMING, w, h);
            if (!t) continue;
            SDL_SetTextureScaleMode(t, SDL_SCALEMODE_NEAREST);
            th.tw[(size_t)li] = w;
            th.th[(size_t)li] = h;
        }
        SDL_UpdateTexture(t, nullptr, px.data(), w * (int)sizeof(ImU32));
    }
}

// Solo `li` in the viewport (click again to restore the previous visibility).
static void toggle_solo(project_config& cfg, layer_thumbs& th, std::string const& name, int li) {
    if (th.solo == li) {
        for (size_t k = 0; k < cfg.layers.size() && k < th.saved_visible.size(); ++k)
            cfg.layers[k].visible = th.saved_visible[k] != 0;
        th.solo = -1;
        return;
    }
    if (th.solo < 0) {
        th.saved_visible.clear();
        for (auto const& lc : cfg.layers) th.saved_visible.push_back(lc.visible ? 1 : 0);
    }
    for (auto& lc : cfg.layers) lc.visible = lc.name == name;
    th.solo = li;
}

void draw_layers_window(script const& sc, debug_run const& run, project_config& cfg,
                        SDL_Renderer* renderer, layer_thumbs& th) {
    level const& lv = run.snap;
    if (lv.layer_count() == 0 || lv.width() <= 0 || lv.height() <= 0) {
        ImGui::TextDisabled(lv.layer_count() == 0 ? "(no layers)" : "(empty - nothing resized yet)");
        return;
    }
    if (th.version != run.version) {
        rebuild_thumbs(sc, run, cfg, renderer, th);
        th.version = run.version;
    }

    int w = lv.width(), h = lv.height();
    ImVec4 accent = ImGui::GetStyleColorVec4(ImGuiCol_CheckMark);

    for (int li = 0; li < lv.layer_count() && li < (int)th.tex.size(); ++li) {
        std::string name = lv.layer_name(li);
        ImGui::PushID(li);
        // Each layer is its own rounded card: header (name; then visibility toggle +
        // view options), then the thumbnail.
        ImGui::BeginChild("##layer", { 0.f, 0.f },
                          ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AlwaysUseWindowPadding);
        // A few pixels per cell: as large as the card allows, 1..6 px.
        ImDrawList* dl = ImGui::GetWindowDrawList();   // the card's own draw list
        float avail = ImGui::GetContentRegionAvail().x;
        float scale = std::clamp(std::floor(avail / (float)w), 1.f, 6.f);
        layer_config* lc = nullptr;
        for (auto& c : cfg.layers) if (c.name == name) lc = &c;
        // Header: the name on its own line, then a row with the visibility
        // toggle followed by the view options.
        if (th.solo == li) ImGui::TextColored(accent, "%s  (solo)", name.c_str());
        else               ImGui::TextUnformatted(name.c_str());
        if (lc) {
            // Icon toggle: eye when shown, dimmed eye-slash when hidden. Latch the
            // state: the click flips lc->visible, and the push/pop must stay paired.
            const bool shown = lc->visible;
            if (!shown)
                ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
            const bool toggled = ImGui::Button(shown ? phosphor::PH_EYE : phosphor::PH_EYE_SLASH,
                                               { 28.f, 0.f });
            if (!shown) ImGui::PopStyleColor();
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip(shown ? "Hide this layer in the viewport"
                                        : "Show this layer in the viewport");
            if (toggled) lc->visible = !shown;

            int lid = sc.meta.layer_id(name);
            bool numeric = lid < 0 || sc.meta.layers[(size_t)lid].tag_id < 0;
            ImGui::SameLine();
            draw_mode_combo(lc->mode, numeric);
            if (lc->mode == layer_mode::tile) {
                ImGui::SameLine();
                draw_tileset_combo(lc->tileset, cfg.tilesets);
            }
            if (lc->mode == layer_mode::corner) {
                ImGui::SameLine();
                draw_corner_size_combo(lc->corner);
            }
        }

        ImVec2 p0 = ImGui::GetCursorScreenPos();
        ImVec2 size = {scale * (float)w, scale * (float)h};
        dl->AddCallback(set_sampler_nearest, nullptr);
        ImGui::Image((ImTextureID)(intptr_t)th.tex[(size_t)li], size);
        dl->AddCallback(set_sampler_linear, nullptr);
        if (ImGui::IsItemClicked()) toggle_solo(cfg, th, name, li);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip(th.solo == li ? "Click to show all layers again"
                                            : "Click to solo this layer in the viewport");

        // the last step's written cells (observe channel)
        ImU32 hl = ImGui::GetColorU32(accent);
        for (auto const& c : run.hls) {
            if (c.layer != li || c.what != cell_highlight::kind::write) continue;
            ImVec2 a = {p0.x + c.x * scale, p0.y + c.y * scale};
            dl->AddRect(a, {a.x + scale, a.y + scale}, hl, 0.f, 0, scale >= 3.f ? 1.f : 0.5f);
        }
        ImGui::EndChild();
        ImGui::PopID();
    }
}

// ── composite grid ────────────────────────────────────────────────────────────

// tile_texture::sync() sets NEAREST on the SDL_Texture itself, but the
// SDL3 renderer backend (imgui_impl_sdlrenderer3.cpp) resets every texture
// it draws to a frame-wide sampler mode of its own (LINEAR by default) via
// ImGui_ImplSDLRenderer3_RenderState::CurrentScaleMode, stomping that
// per-texture setting for the whole frame. Bracketing the tile AddImage
// calls with these draw callbacks is the backend's documented way to
// override that mode for just those draw commands, so tilesets stay
// pixel-sharp instead of blurring back to LINEAR.
static void set_sampler_nearest(ImDrawList const*, ImDrawCmd const*) {
    if (auto* rs = ImGui_ImplSDLRenderer3_GetRenderState())
        rs->CurrentScaleMode = SDL_SCALEMODE_NEAREST;
}
static void set_sampler_linear(ImDrawList const*, ImDrawCmd const*) {
    if (auto* rs = ImGui_ImplSDLRenderer3_GetRenderState())
        rs->CurrentScaleMode = SDL_SCALEMODE_LINEAR;
}

void draw_grid_composite(script const& sc, debug_run const& run,
                         project_config const& cfg,
                         std::unordered_map<std::string, tile_texture> const& tile_textures,
                         float cell_px) {
    // The grid rendered is the public snapshot: committed statements plus the
    // current batch's applications so far (refreshed once per action).
    level const& lv = run.snap;
    int cols = lv.width(), rows = lv.height();
    if (cols == 0 || rows == 0) {
        ImGui::TextDisabled("(empty)");
        return;
    }

    // Precompute heatmap min/max per layer (only for heatmap mode).
    struct heat_range { int lo{INT_MAX}, hi{INT_MIN}; };
    std::vector<heat_range> heat(cfg.layers.size());
    for (size_t li = 0; li < cfg.layers.size(); ++li) {
        auto const& lc = cfg.layers[li];
        if (!lc.visible || lc.mode != layer_mode::heatmap) continue;
        grid g = lv[lc.name];
        for (int y = 0; y < rows; ++y)
            for (int x = 0; x < cols; ++x) {
                int v = g.at(x, y);
                if (v >= 0 || (v < -1)) {
                    heat[li].lo = std::min(heat[li].lo, v);
                    heat[li].hi = std::max(heat[li].hi, v);
                }
            }
    }

    ImDrawList* dl     = ImGui::GetWindowDrawList();
    ImVec2      origin = ImGui::GetCursorScreenPos();
    bool        show_glyph = cell_px >= 10.f;
    float       total_w    = cols * cell_px;
    float       total_h    = rows * cell_px;

    // Flat panel background, all layers stacked directly on top of it.
    dl->AddRectFilled(origin, { origin.x + total_w, origin.y + total_h },
                      cfg.grid_bg_color);

    // Corner-mode layers each get their own tile corner (TL, TR, BR, BL, then
    // wrapping), assigned by position among ALL corner layers so toggling a
    // layer's visibility never moves the others.
    std::vector<int> corner_slot(cfg.layers.size(), 0);
    for (size_t li = 0, n = 0; li < cfg.layers.size(); ++li)
        if (cfg.layers[li].mode == layer_mode::corner) corner_slot[li] = (int)(n++ % 4);

    // ── per layer: content -> highlights, stacked in composite order ─────────
    for (size_t li = 0; li < cfg.layers.size(); ++li) {
        auto const& lc = cfg.layers[li];
        if (!lc.visible || lc.opacity <= 0.f) continue;

        grid    g   = lv[lc.name];
        int     lid = sc.meta.layer_id(lc.name);
        palette pal = layer_palette(sc.meta, cfg, lid);

        // Resolve this layer's own tileset (Tile mode only; other modes
        // never touch these). Hoisted out of the inner loop so all AddImage
        // calls for the layer use the same texture and can merge into one
        // draw command.
        tileset_config const* ts  = cfg.find_tileset(lc.tileset);
        auto                   it = ts ? tile_textures.find(ts->name) : tile_textures.end();
        tile_texture const*   tex = (it != tile_textures.end()) ? &it->second : nullptr;
        ImTextureID tile_id = (tex && tex->sdl_tex)
                              ? (ImTextureID)(intptr_t)tex->sdl_tex : (ImTextureID)0;
        int   tiles_per_row = (tile_id && ts)
                              ? std::max(1, tex->width / ts->tile_w) : 1;
        float tile_uv_w     = (tile_id && ts)
                              ? (float)ts->tile_w / tex->width  : 0.f;
        float tile_uv_h     = (tile_id && ts)
                              ? (float)ts->tile_h / tex->height : 0.f;

        // Shape mode: this tag's per-value shapes (sparse; unset -> circle).
        std::unordered_map<int, tag_shape> const* shapes = nullptr;
        if (lc.mode == layer_mode::shape && pal.tag_id >= 0) {
            auto sit = cfg.tag_shapes.find(sc.meta.tag_names[(size_t)pal.tag_id]);
            if (sit != cfg.tag_shapes.end()) shapes = &sit->second;
        }

        bool tiling = (lc.mode == layer_mode::tile) && tile_id && ts;
        if (tiling) dl->AddCallback(set_sampler_nearest, nullptr);

        for (int y = 0; y < rows; ++y) {
            for (int x = 0; x < cols; ++x) {
                int v = g.at(x, y);
                if (v == -1) continue;   // empty (public API sentinel)
                // tag_color()/cell_glyph() key by 0-based value id; v is a
                // mask for a tag layer (spec section 3) but the real number as-is
                // for a numeric one (tile/heatmap modes are numeric-only or
                // numeric-semantics, so they keep using v directly).
                int vid = g.is_number() ? v : mask_to_slot(sc.meta, pal.tag_id, v);

                ImVec2 p0 = { origin.x + x * cell_px, origin.y + y * cell_px };
                ImVec2 p1 = { p0.x + cell_px,         p0.y + cell_px         };

                switch (lc.mode) {

                    case layer_mode::tile: {
                        if (!tile_id || !ts) {
                            dl->AddRectFilled(p0, p1,
                                with_alpha(IM_COL32(120, 120, 120, 255), lc.opacity));
                            break;
                        }
                        int tx = v % tiles_per_row;
                        int ty = v / tiles_per_row;
                        ImVec2 uv0 = { tx * tile_uv_w,       ty * tile_uv_h       };
                        ImVec2 uv1 = { (tx + 1) * tile_uv_w, (ty + 1) * tile_uv_h };
                        dl->AddImage(tile_id, p0, p1, uv0, uv1,
                            IM_COL32(255, 255, 255, (ImU8)(lc.opacity * 255.f + 0.5f)));
                        break;
                    }

                    case layer_mode::heatmap: {
                        auto const& hr = heat[li];
                        float t = (hr.hi > hr.lo)
                                  ? (float)(v - hr.lo) / (float)(hr.hi - hr.lo)
                                  : 0.f;
                        dl->AddRectFilled(p0, p1, heatmap_color(t, lc.opacity));
                        if (show_glyph)
                            draw_centered_text(dl, p0, cell_px,
                                IM_COL32(255, 255, 255, 180),
                                std::to_string(v).c_str());
                        break;
                    }

                    case layer_mode::shape: {
                        tag_shape sh = tag_shape::circle;
                        if (shapes) {
                            auto f = shapes->find(vid);
                            if (f != shapes->end()) sh = f->second;
                        }
                        draw_shape(dl, { (p0.x + p1.x) * 0.5f, (p0.y + p1.y) * 0.5f },
                                   cell_px * 0.36f, sh,
                                   with_alpha(tag_color(pal.tag_id, vid, pal.colors),
                                              lc.opacity));
                        break;
                    }

                    case layer_mode::corner: {
                        float  s   = cell_px * (lc.corner == corner_size::tiny  ? 0.18f
                                               : lc.corner == corner_size::small ? 0.28f
                                                                                 : 0.40f);
                        ImU32  col = with_alpha(tag_color(pal.tag_id, vid, pal.colors),
                                                lc.opacity);
                        switch (corner_slot[li]) {
                            case 0: dl->AddTriangleFilled(p0, { p0.x + s, p0.y },
                                                          { p0.x, p0.y + s }, col); break;
                            case 1: dl->AddTriangleFilled({ p1.x, p0.y }, { p1.x - s, p0.y },
                                                          { p1.x, p0.y + s }, col); break;
                            case 2: dl->AddTriangleFilled(p1, { p1.x - s, p1.y },
                                                          { p1.x, p1.y - s }, col); break;
                            default: dl->AddTriangleFilled({ p0.x, p1.y }, { p0.x + s, p1.y },
                                                           { p0.x, p1.y - s }, col); break;
                        }
                        break;
                    }

                    case layer_mode::color:
                        dl->AddRectFilled(p0, p1,
                            with_alpha(tag_color(pal.tag_id, vid, pal.colors), lc.opacity));
                        break;

                    case layer_mode::text:
                    default:
                        if (show_glyph)
                            draw_centered_text(dl, p0, cell_px,
                                with_alpha(tag_color(pal.tag_id, vid, pal.colors), lc.opacity),
                                cell_glyph(sc.meta, pal.tag_id, vid).c_str());
                        break;
                }
            }
        }
        if (tiling) dl->AddCallback(set_sampler_linear, nullptr);

        // Highlights for this layer (last application's match/write cells).
        float border = std::max(1.5f, cell_px * 0.08f);
        for (auto const& h : run.hls) {
            if (h.layer != lid) continue;
            if (h.x < 0 || h.x >= cols || h.y < 0 || h.y >= rows) continue;
            ImVec2 p0 = { origin.x + h.x * cell_px, origin.y + h.y * cell_px };
            ImVec2 p1 = { p0.x + cell_px,           p0.y + cell_px           };
            ImU32 col = (h.what == cell_highlight::kind::match)
                        ? IM_COL32(240, 210,   0, 255)
                        : IM_COL32( 60, 230,  90, 255);
            dl->AddRect(p0, p1, col, 0.f, 0, border);
        }
    }

    // Grid-line overlay, drawn last so cell boundaries stay legible over any layer.
    if (cell_px >= 3.f) {
        ImU32 grid_col = IM_COL32(255, 255, 255, 25);
        for (int y = 0; y <= rows; ++y)
            dl->AddLine({ origin.x, origin.y + y * cell_px },
                        { origin.x + total_w, origin.y + y * cell_px }, grid_col);
        for (int x = 0; x <= cols; ++x)
            dl->AddLine({ origin.x + x * cell_px, origin.y },
                        { origin.x + x * cell_px, origin.y + total_h }, grid_col);
    }

    ImGui::Dummy({ total_w, total_h });

    // Hover tooltip: cell coordinates and every layer's value (hidden layers
    // are listed too, dimmed).
    if (ImGui::IsItemHovered()) {
        ImVec2 mp = ImGui::GetMousePos();
        int hx = (int)((mp.x - origin.x) / cell_px);
        int hy = (int)((mp.y - origin.y) / cell_px);
        if (hx >= 0 && hx < cols && hy >= 0 && hy < rows) {
            ImGui::BeginTooltip();
            ImGui::TextDisabled("x %d  y %d", hx, hy);
            ImGui::Separator();
            for (auto const& lc : cfg.layers) {
                grid g = lv[lc.name];
                int  v = g.at(hx, hy);

                int lid = sc.meta.layer_id(lc.name);
                int tid = lid >= 0 ? sc.meta.layers[lid].tag_id : -1;
                std::string val_label;
                if (v == -1)               val_label = "\xc2\xb7";
                else if (g.is_number())    val_label = std::to_string(v);
                else                       val_label = mask_label(sc.meta, tid, v);

                if (v != -1) {
                    palette pal = layer_palette(sc.meta, cfg, sc.meta.layer_id(lc.name));
                    int vid = g.is_number() ? v : mask_to_slot(sc.meta, pal.tag_id, v);
                    ImVec4 colf = ImGui::ColorConvertU32ToFloat4(
                        tag_color(pal.tag_id, vid, pal.colors));
                    ImGui::ColorButton("##s", colf,
                        ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoPicker,
                        {10.f, 10.f});
                    ImGui::SameLine();
                }
                if (!lc.visible) ImGui::PushStyleColor(ImGuiCol_Text,
                                     ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
                ImGui::Text("%s: %s", lc.name.c_str(), val_label.c_str());
                if (!lc.visible) ImGui::PopStyleColor();
            }
            ImGui::EndTooltip();
        }
    }
}

// ── layer strip (a small container per layer, under the grid) ────────────────

// Viz-type combo -- mutually exclusive, so a dropdown rather than toggle
// buttons. Tile mode is only offered for numeric grids.
static void draw_mode_combo(layer_mode& mode, bool numeric) {
    struct mode_item { layer_mode mode; char const* icon; char const* name; };
    static const mode_item k_items[] = {
        { layer_mode::text,    phosphor::PH_TEXT_AA,     "Text"    },
        { layer_mode::color,   phosphor::PH_PALETTE,     "Color"   },
        { layer_mode::tile,    phosphor::PH_IMAGE,       "Tile"    },
        { layer_mode::heatmap, phosphor::PH_THERMOMETER, "Heatmap" },
        { layer_mode::shape,   phosphor::PH_SHAPES,      "Shape"   },
        { layer_mode::corner,  phosphor::PH_CORNERS_OUT, "Corner"  },
    };
    if (!numeric && mode == layer_mode::tile)  mode = layer_mode::color;
    if (numeric  && (mode == layer_mode::shape || mode == layer_mode::corner))
        mode = layer_mode::color;   // both need a tag layer

    mode_item const* current = &k_items[0];
    for (auto const& mi : k_items)
        if (mi.mode == mode) { current = &mi; break; }

    ImGui::SameLine();
    ImGui::SetNextItemWidth(44.f);
    bool open = ImGui::BeginCombo("##mode", current->icon);
    if (!open && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", current->name);
    if (open) {
        for (auto const& mi : k_items) {
            if (!numeric && mi.mode == layer_mode::tile)  continue;
            if (numeric  && (mi.mode == layer_mode::shape || mi.mode == layer_mode::corner))
                continue;
            bool selected = (mi.mode == mode);
            if (ImGui::Selectable(mi.icon, selected)) mode = mi.mode;
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", mi.name);
            if (selected) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
}

// Which tileset (by name) a Tile-mode layer draws from. Only shown once a
// layer is actually in Tile mode - other modes never read this field.
static void draw_tileset_combo(std::string& name,
                               std::vector<tileset_config> const& tilesets) {
    ImGui::SameLine();
    ImGui::SetNextItemWidth(46.f);
    // Compact preview (first 3 chars) until opened; the list shows full names.
    std::string preview = name.empty() ? "--" : name.substr(0, 3);
    ImGui::SetNextWindowSizeConstraints({ 130.f, 0.f }, { FLT_MAX, FLT_MAX });
    bool open = ImGui::BeginCombo("##tileset", preview.c_str());
    if (!open && ImGui::IsItemHovered())
        ImGui::SetTooltip("Tileset: %s", name.empty() ? "(none)" : name.c_str());
    if (open) {
        if (ImGui::Selectable("(none)", name.empty())) name.clear();
        for (auto const& ts : tilesets) {
            bool selected = (ts.name == name);
            if (ImGui::Selectable(ts.name.c_str(), selected)) name = ts.name;
            if (selected) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
}

// Corner-triangle size: a compact combo (3 letters) like the tileset one.
static void draw_corner_size_combo(corner_size& size) {
    static char const* const k_short[] = { "Nrm", "Sml", "Tny" };
    static char const* const k_full[]  = { "Normal", "Small", "Tiny" };
    ImGui::SameLine();
    ImGui::SetNextItemWidth(46.f);
    ImGui::SetNextWindowSizeConstraints({ 90.f, 0.f }, { FLT_MAX, FLT_MAX });
    bool open = ImGui::BeginCombo("##corner", k_short[(int)size]);
    if (!open && ImGui::IsItemHovered())
        ImGui::SetTooltip("Corner size: %s", k_full[(int)size]);
    if (open) {
        for (int i = 0; i < 3; ++i)
            if (ImGui::Selectable(k_full[i], (int)size == i)) size = (corner_size)i;
        ImGui::EndCombo();
    }
}

// ── settings window ───────────────────────────────────────────────────────────
// Things set once per project and rarely touched again while it's running.

void draw_settings_window(project_config& cfg,
                          std::unordered_map<std::string, tile_texture>& tile_textures,
                          SDL_Renderer* renderer, std::string const& ls_path) {
    // Fields stretch with the panel, leaving room for the longest label.
    float label_w = ImGui::CalcTextSize("Rule Cell Size").x + ImGui::GetStyle().ItemInnerSpacing.x;
    char zoom_fmt[48];
    std::snprintf(zoom_fmt, sizeof zoom_fmt, "%%.2fx  (%.0f px/cell)", cfg.cell_px());
    ImGui::SetNextItemWidth(-label_w);
    ImGui::DragFloat("Viewport Zoom", &cfg.cell_zoom, 0.05f, 0.25f, 8.f, zoom_fmt);

    ImGui::SetNextItemWidth(-label_w);
    ImGui::DragFloat("Rule Cell Size", &cfg.mini_px, 0.25f, 4.f, 48.f, "%.0f px");

    float col[3];
    col3_from_u32(cfg.grid_bg_color, col);
    if (ImGui::ColorEdit3("Background", col))
        cfg.grid_bg_color = u32_from_col3(col);

    ImGui::Separator();
    ImGui::TextUnformatted("Tilesets");

    int remove_index = -1;
    for (int i = 0; i < (int)cfg.tilesets.size(); ++i) {
        auto& ts = cfg.tilesets[(size_t)i];
        ImGui::PushID(i);
        // full panel width; the fields inside stretch with it
        ImGui::BeginChild("##ts_chip", { 0.f, 0.f },
                          ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY);

        std::string old_name = ts.name;
        char name_buf[128];
        size_t nn = ts.name.size() < sizeof(name_buf) - 1 ? ts.name.size()
                                                           : sizeof(name_buf) - 1;
        ts.name.copy(name_buf, nn);
        name_buf[nn] = '\0';
        ImGui::SetNextItemWidth(-(ImGui::GetFrameHeight() + ImGui::GetStyle().ItemSpacing.x));
        if (ImGui::InputText("##name", name_buf, sizeof(name_buf))) {
            ts.name = name_buf;
            // Layers reference a tileset by name (see project_config.hpp) --
            // keep their assignment pointing at the same tileset on rename.
            for (auto& lc : cfg.layers)
                if (lc.tileset == old_name) lc.tileset = ts.name;
        }
        ImGui::SameLine();
        if (ImGui::Button(phosphor::PH_X)) remove_index = i;

        char path_buf[512];
        size_t n = ts.path.size() < sizeof(path_buf) - 1 ? ts.path.size()
                                                         : sizeof(path_buf) - 1;
        ts.path.copy(path_buf, n);
        path_buf[n] = '\0';
        ImGui::SetNextItemWidth(-FLT_MIN);
        if (ImGui::InputText("##path", path_buf, sizeof(path_buf)))
            ts.path = path_buf;
        // step=0 disables the +/- buttons -- not used as integer UI anywhere else.
        float half = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f
                   - ImGui::CalcTextSize("W").x - ImGui::GetStyle().ItemInnerSpacing.x;
        ImGui::SetNextItemWidth(std::max(half, 30.f));
        ImGui::InputInt("W##tw", &ts.tile_w, 0, 0);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(std::max(half, 30.f));
        ImGui::InputInt("H##th", &ts.tile_h, 0, 0);

        std::string abs = resolve_path(ls_path, ts.path);
        tile_texture& tex = tile_textures[ts.name];
        bool loaded = tex.sync(renderer, abs);
        if (!ts.path.empty()) {
            if (loaded)
                ImGui::TextDisabled("Loaded %dx%d", tex.width, tex.height);
            else
                ImGui::TextColored({1.f, 0.4f, 0.4f, 1.f}, "Not found");
        }

        ImGui::EndChild();
        ImGui::PopID();
    }
    if (remove_index >= 0) {
        std::string removed = cfg.tilesets[(size_t)remove_index].name;
        cfg.tilesets.erase(cfg.tilesets.begin() + remove_index);
        tile_textures.erase(removed);
        for (auto& lc : cfg.layers)
            if (lc.tileset == removed) lc.tileset.clear();
    }

    if (ImGui::Button("Add Tileset")) {
        cfg.tilesets.push_back({
            "Tileset " + std::to_string(cfg.tilesets.size() + 1), "", 16, 16,
        });
    }
}

}  // namespace ls
