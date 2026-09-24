#include "debug_ui_internal.hpp"
#include "phosphor_icons.hpp"

#include <SDL3/SDL.h>
#include <imgui_impl_sdlrenderer3.h>
#include <stb_image.h>

#include <algorithm>
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

// lsd has no theme flag threaded through the view code; read it back off
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
    if (v >= (int)vals.size()) return "?";
    return vals[v].substr(0, 1);
}

// grid::at() returns a value MASK for a tag layer (bit 1..30 - a single bit
// for a normal cell, spec §3), but tag_color()/cell_glyph() and the tag-color
// config's vmap all still key by the 0-based value id. Converts a mask back
// to that id (the lowest set value bit); -1 (the public API's empty/invalid
// sentinel) passes through unchanged.
static int mask_to_vid(int mask) {
    if (mask < 0) return mask;
    for (int b = 1; b <= 30; ++b)
        if (mask & (1 << b)) return b - 1;
    return -1;
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
    std::string p;
    switch (s.strat) {
        case strategy::one: p = "one "; break;
        case strategy::all: p = "all "; break;
        case strategy::some:
            p = s.is_percent ? "some(percent=" + std::to_string(s.percent) + ") "
                             : "some(max=" + std::to_string(s.max_count) + ") ";
            break;
    }
    p += s.rule_name;
    if (s.pol == exec_policy::incremental) p += "  policy=incremental";
    if (s.pol == exec_policy::stabilize)   p += "  policy=stabilize";
    if (s.guard)                           p += "  when (...)";
    return p;
}

char const* stmt_icon(program_stmt const& s) {
    if (s.what == program_stmt::kind::apply) return phosphor::PH_LIGHTNING;
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
        case compiled_cell::kind::value:
            break;
    }
    if (pat.is_number || pat.is_where) {
        if (cc.val == num_empty) return { IM_COL32(80, 80, 80, 255), "." };
        return { tag_color(-1, (int)cc.val, nullptr), std::to_string(cc.val) };
    }
    // Tag mask: empty-only reads as '.', otherwise show the lowest value bit
    // (a union mask previews as its first member).
    int vid = mask_to_vid((int)cc.val);
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

static void draw_lhs_group(compiled const& meta, compiled_pair const& pair,
                           float mini_px, project_config const& cfg) {
    for (size_t gi = 0; gi < pair.lhs.size(); ++gi) {
        if (gi > 0) ImGui::SameLine(0.f, 8.f);
        draw_pattern_group(meta, pair.lhs[gi], mini_px, cfg);
    }
}

static void draw_rhs_group(compiled const& meta, compiled_pair const& pair,
                           float mini_px, project_config const& cfg) {
    // The write side is a (possibly nested) tree; preview every leaf pattern
    // it may write, flattened (all { any } branches shown side by side).
    std::vector<compiled_pattern const*> leaves;
    collect_write_leaves(pair.rhs, leaves);
    for (size_t i = 0; i < leaves.size(); ++i) {
        if (i > 0) {
            ImGui::SameLine(0.f, 10.f);
            ImGui::BeginGroup();
            ImGui::Dummy({0.f, 0.f});
            ImGui::TextDisabled(phosphor::PH_ARROWS_LEFT_RIGHT);
            ImGui::EndGroup();
            ImGui::SameLine(0.f, 10.f);
        }
        draw_pattern_group(meta, *leaves[i], mini_px, cfg);
    }
}

// One LHS-group -> icon-arrow -> RHS-group row, vertically centered on the arrow.
static void draw_rule_variant(compiled const& meta, compiled_pair const& pair,
                              float mini_px, project_config const& cfg) {
    std::vector<compiled_pattern const*> rhs_leaves;
    collect_write_leaves(pair.rhs, rhs_leaves);
    if (pair.lhs.empty() && rhs_leaves.empty()) return;

    float lhs_h = 0.f;
    for (auto const& lpat : pair.lhs)
        lhs_h = std::max(lhs_h, pattern_group_height(lpat, mini_px));
    float rhs_h = 0.f;
    for (auto const* rpat : rhs_leaves)
        rhs_h = std::max(rhs_h, pattern_group_height(*rpat, mini_px));
    float row_h = std::max(lhs_h, rhs_h);

    ImGui::BeginGroup();

    ImGui::BeginGroup();
    if (row_h > lhs_h) ImGui::Dummy({0.f, (row_h - lhs_h) * 0.5f});
    draw_lhs_group(meta, pair, mini_px, cfg);
    ImGui::EndGroup();

    ImGui::SameLine(0.f, 20.f);
    ImGui::BeginGroup();
    float icon_h = ImGui::GetTextLineHeight();
    if (row_h > icon_h) ImGui::Dummy({0.f, (row_h - icon_h) * 0.5f});
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.40f, 0.68f, 0.98f, 1.f));
    ImGui::TextUnformatted(phosphor::PH_ARROW_RIGHT);
    ImGui::PopStyleColor();
    ImGui::EndGroup();

    ImGui::SameLine(0.f, 20.f);
    ImGui::BeginGroup();
    if (row_h > rhs_h) ImGui::Dummy({0.f, (row_h - rhs_h) * 0.5f});
    draw_rhs_group(meta, pair, mini_px, cfg);
    ImGui::EndGroup();

    ImGui::EndGroup();
}

void draw_rule_window(script const& sc, debug_run const& run, float mini_px,
                      project_config const& cfg) {
    auto const& stmts = sc.ast.program.stmts;
    int show = run.shown_stmt();
    if (show < 0 || show >= (int)stmts.size()) {
        ImGui::TextDisabled("(none yet)");
        return;
    }
    auto const& stmt = stmts[(size_t)show];
    if (stmt.what != program_stmt::kind::apply) {
        ImGui::TextDisabled("%s", stmt_desc(stmt).c_str());
        return;
    }
    if (show >= (int)sc.meta.stmts.size()) return;
    int rid = sc.meta.stmts[(size_t)show].rule_id;
    if (rid < 0 || rid >= (int)sc.meta.rules.size()) {
        ImGui::TextDisabled("?");
        return;
    }

    auto const& cr = sc.meta.rules[(size_t)rid];
    ImGui::TextUnformatted(stmt.rule_name.c_str());
    for (auto const& rd : sc.ast.rules) {
        if (rd.name != stmt.rule_name) continue;
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
        break;
    }
    ImGui::Separator();
    if (cr.pairs.empty()) return;

    int n = (int)cr.pairs.size();
    for (int pi = 0; pi < n; ++pi) {
        ImGui::PushID(pi);
        if (n > 1)
            ImGui::TextDisabled("%s %d / %d", phosphor::PH_STACK, pi + 1, n);
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

void draw_program_window(script const& sc, debug_run const& run) {
    auto const& stmts = sc.ast.program.stmts;
    if (stmts.empty()) {
        ImGui::TextDisabled("(empty program)");
        return;
    }

    int current = run.current_stmt();
    ImVec4 accent = ImGui::GetStyleColorVec4(ImGuiCol_CheckMark);

    ImGui::BeginChild("##program_list", {0.f, 0.f}, ImGuiChildFlags_Borders);
    // A real table keeps the index/icon/description columns aligned once
    // line numbers stop being all the same width (single vs. multi-digit).
    if (ImGui::BeginTable("##program_table", 3, ImGuiTableFlags_SizingFixedFit)) {
        ImGui::TableSetupColumn("#",    ImGuiTableColumnFlags_WidthFixed);
        ImGui::TableSetupColumn("icon", ImGuiTableColumnFlags_WidthFixed);
        ImGui::TableSetupColumn("desc", ImGuiTableColumnFlags_WidthStretch);

        // Right-align the index within a stable 3-digit gutter.
        float num_gutter = ImGui::CalcTextSize("000").x;

        for (int i = 0; i < (int)stmts.size(); ++i) {
            auto const& stmt = stmts[(size_t)i];
            bool is_current = (i == current);
            bool is_past    = (i < current);

            ImGui::TableNextRow();
            if (is_current)
                ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0,
                                       ImGui::GetColorU32(ImGuiCol_Header));

            ImGui::PushID(i);
            if (is_current)   ImGui::PushStyleColor(ImGuiCol_Text, accent);
            else if (is_past) ImGui::PushStyleColor(ImGuiCol_Text,
                                  ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));

            ImGui::TableNextColumn();
            std::string num = std::to_string(i);
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + num_gutter
                                 - ImGui::CalcTextSize(num.c_str()).x);
            ImGui::TextUnformatted(num.c_str());

            ImGui::TableNextColumn();
            ImGui::TextUnformatted(stmt_icon(stmt));

            ImGui::TableNextColumn();
            ImGui::TextUnformatted(stmt_desc(stmt).c_str());
            if (is_current && ImGui::IsWindowAppearing())
                ImGui::SetScrollHereY(0.25f);

            if (is_current || is_past) ImGui::PopStyleColor();
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    ImGui::EndChild();
}

// ── tags window ───────────────────────────────────────────────────────────────

void draw_tags_window(compiled const& meta, project_config& cfg) {
    if (meta.tag_names.empty()) {
        ImGui::TextDisabled("(no tags)");
        return;
    }

    for (int ti = 0; ti < (int)meta.tag_names.size(); ++ti) {
        auto& vmap = cfg.tag_colors[meta.tag_names[(size_t)ti]];

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

                ImGui::TextUnformatted(vals[(size_t)vi].c_str());

                ImGui::PopID();
            }
            ImGui::TreePop();
        }
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

        bool tiling = (lc.mode == layer_mode::tile) && tile_id && ts;
        if (tiling) dl->AddCallback(set_sampler_nearest, nullptr);

        for (int y = 0; y < rows; ++y) {
            for (int x = 0; x < cols; ++x) {
                int v = g.at(x, y);
                if (v == -1) continue;   // empty (public API sentinel)
                // tag_color()/cell_glyph() key by 0-based value id; v is a
                // mask for a tag layer (spec §3) but the real number as-is
                // for a numeric one (tile/heatmap modes are numeric-only or
                // numeric-semantics, so they keep using v directly).
                int vid = g.is_number() ? v : mask_to_vid(v);

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

    // Hover tooltip: cell coordinates and every visible layer's value.
    if (ImGui::IsItemHovered()) {
        ImVec2 mp = ImGui::GetMousePos();
        int hx = (int)((mp.x - origin.x) / cell_px);
        int hy = (int)((mp.y - origin.y) / cell_px);
        if (hx >= 0 && hx < cols && hy >= 0 && hy < rows) {
            ImGui::BeginTooltip();
            ImGui::TextDisabled("x %d  y %d", hx, hy);
            ImGui::Separator();
            for (auto const& lc : cfg.layers) {
                if (!lc.visible) continue;
                grid g = lv[lc.name];
                int  v = g.at(hx, hy);

                std::string val_label;
                if (v == -1)               val_label = "\xc2\xb7";
                else if (g.is_number())    val_label = std::to_string(v);
                else                       val_label = g.valueName(v);

                if (v != -1) {
                    palette pal = layer_palette(sc.meta, cfg, sc.meta.layer_id(lc.name));
                    int vid = g.is_number() ? v : mask_to_vid(v);
                    ImVec4 colf = ImGui::ColorConvertU32ToFloat4(
                        tag_color(pal.tag_id, vid, pal.colors));
                    ImGui::ColorButton("##s", colf,
                        ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoPicker,
                        {10.f, 10.f});
                    ImGui::SameLine();
                }
                ImGui::Text("%s: %s", lc.name.c_str(), val_label.c_str());
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
    };
    if (!numeric && mode == layer_mode::tile) mode = layer_mode::color;

    mode_item const* current = &k_items[0];
    for (auto const& mi : k_items)
        if (mi.mode == mode) { current = &mi; break; }

    ImGui::SameLine();
    ImGui::SetNextItemWidth(44.f);
    bool open = ImGui::BeginCombo("##mode", current->icon);
    if (!open && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", current->name);
    if (open) {
        for (auto const& mi : k_items) {
            if (!numeric && mi.mode == layer_mode::tile) continue;
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
    ImGui::SetNextItemWidth(110.f);
    char const* preview = name.empty() ? "(none)" : name.c_str();
    if (ImGui::BeginCombo("##tileset", preview)) {
        if (ImGui::Selectable("(none)", name.empty())) name.clear();
        for (auto const& ts : tilesets) {
            bool selected = (ts.name == name);
            if (ImGui::Selectable(ts.name.c_str(), selected)) name = ts.name;
            if (selected) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
}

void draw_layer_strip(project_config& cfg, compiled const& meta) {
    for (int i = 0; i < (int)cfg.layers.size(); ++i) {
        auto& lc = cfg.layers[(size_t)i];
        ImGui::PushID(i);
        if (i > 0) ImGui::SameLine();

        ImGui::BeginChild("##layer_chip", { 0.f, 0.f },
                          ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeX
                          | ImGuiChildFlags_AutoResizeY);

        ImGui::Checkbox("##vis", &lc.visible);
        ImGui::SameLine();
        ImGui::TextUnformatted(lc.name.c_str());
        int lid = meta.layer_id(lc.name);
        bool numeric = (lid < 0) || (meta.layers[(size_t)lid].tag_id < 0);
        draw_mode_combo(lc.mode, numeric);
        if (lc.mode == layer_mode::tile)
            draw_tileset_combo(lc.tileset, cfg.tilesets);

        ImGui::EndChild();
        ImGui::PopID();
    }
}

// ── settings window ───────────────────────────────────────────────────────────
// Things set once per project and rarely touched again while it's running.

void draw_settings_window(project_config& cfg,
                          std::unordered_map<std::string, tile_texture>& tile_textures,
                          SDL_Renderer* renderer, std::string const& ls_path) {
    ImGui::SetNextItemWidth(160.f);
    ImGui::DragFloat("Viewport Zoom", &cfg.cell_zoom, 0.05f, 0.25f, 8.f, "%.2fx");
    ImGui::SameLine();
    ImGui::TextDisabled("(%.0f px/cell)", cfg.cell_px());

    ImGui::SetNextItemWidth(160.f);
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
        ImGui::BeginChild("##ts_chip", { 0.f, 0.f },
                          ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeX
                          | ImGuiChildFlags_AutoResizeY);

        std::string old_name = ts.name;
        char name_buf[128];
        size_t nn = ts.name.size() < sizeof(name_buf) - 1 ? ts.name.size()
                                                           : sizeof(name_buf) - 1;
        ts.name.copy(name_buf, nn);
        name_buf[nn] = '\0';
        ImGui::SetNextItemWidth(120.f);
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
        ImGui::SetNextItemWidth(260.f);
        if (ImGui::InputText("##path", path_buf, sizeof(path_buf)))
            ts.path = path_buf;
        // step=0 disables the +/- buttons -- not used as integer UI anywhere else.
        ImGui::SetNextItemWidth(70.f);
        ImGui::InputInt("W##tw", &ts.tile_w, 0, 0);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(70.f);
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
