#include "render_png.hpp"
#include "project_config.hpp"
#include <stb_image.h>
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <vector>

namespace ls {

namespace {

struct rgba { uint8_t r, g, b, a; };

// The debugger's dark-theme cycle (debug_ui_views.cpp), indexed by value id.
constexpr rgba k_palette[12] = {
    {133,  50,  50, 255}, {133,  91,  50, 255}, {133, 133,  50, 255}, { 67, 133,  50, 255},
    { 50, 133,  91, 255}, { 50, 133, 133, 255}, { 50,  91, 133, 255}, { 50,  50, 133, 255},
    { 91,  50, 133, 255}, {133,  50, 133, 255}, {133,  50,  91, 255}, {102,  77,  59, 255},
};

rgba from_abgr(uint32_t c) {
    return { (uint8_t)(c & 0xFF), (uint8_t)((c >> 8) & 0xFF),
             (uint8_t)((c >> 16) & 0xFF), (uint8_t)((c >> 24) & 0xFF) };
}

// tag_color()'s number-layer fallback: the same hash and HSV mapping.
rgba number_color(int v) {
    uint32_t h = (uint32_t)(v + 1) * 2246822519u;   // tag_id -1 zeroes the other term
    h ^= h >> 16;
    float hue = (float)(h & 0xFFFFu) / 65536.f;
    float sat = 0.5f + 0.2f * ((float)((h >> 16) & 0xFFu) / 255.f);
    float val = 0.65f + 0.2f * ((float)((h >> 24) & 0xFFu) / 255.f);
    float f = hue * 6.f;
    int   i = (int)f % 6;
    float t = f - std::floor(f);
    float p = val * (1 - sat), q = val * (1 - sat * t), u = val * (1 - sat * (1 - t));
    float r, g, b;
    switch (i) {
        case 0:  r = val; g = u;   b = p;   break;
        case 1:  r = q;   g = val; b = p;   break;
        case 2:  r = p;   g = val; b = u;   break;
        case 3:  r = p;   g = q;   b = val; break;
        case 4:  r = u;   g = p;   b = val; break;
        default: r = val; g = p;   b = q;   break;
    }
    return { (uint8_t)(r * 255.f + 0.5f), (uint8_t)(g * 255.f + 0.5f),
             (uint8_t)(b * 255.f + 0.5f), 255 };
}

// A tag cell's lowest value bit as its 0-based value id. Named masks take
// their own palette slot in the debugger; the public API can't see them, so a
// multi-value cell takes its first member's color here.
int mask_to_vid(int mask) {
    for (int b = 1; b <= 30; ++b)
        if (mask & (1 << b)) return b - 1;
    return -1;
}

struct sheet {
    int w{0}, h{0};
    std::unique_ptr<uint8_t, void (*)(void*)> px{nullptr, stbi_image_free};
};

}  // namespace

std::string render_png(level const& lv, std::string const& ls_path,
                       std::string const& out_path, int scale) {
    std::vector<std::string> names;
    for (int i = 0; i < lv.layer_count(); ++i) names.push_back(lv.layer_name(i));

    bool has_sidecar = std::filesystem::exists(project_config::json_path(ls_path));
    project_config cfg = project_config::load(ls_path, names);
    if (!has_sidecar)
        for (auto& lc : cfg.layers)
            if (!lv[lc.name].is_number()) lc.mode = layer_mode::color;

    // Tilesets the visible tile layers use, loaded once, relative to the script
    auto dir = std::filesystem::path(ls_path).parent_path();
    std::map<std::string, sheet> sheets;
    int tile_px = 0;
    for (auto const& lc : cfg.layers) {
        if (!lc.visible || lc.mode != layer_mode::tile) continue;
        tileset_config const* ts = cfg.find_tileset(lc.tileset);
        if (!ts) return "layer '" + lc.name + "' has no tileset '" + lc.tileset + "'";
        if (sheets.count(ts->name)) continue;
        std::string p = (dir / ts->path).string();
        sheet s;
        int n = 0;
        s.px.reset(stbi_load(p.c_str(), &s.w, &s.h, &n, 4));
        if (!s.px) return "cannot load tileset '" + p + "': " + stbi_failure_reason();
        sheets.emplace(ts->name, std::move(s));
        tile_px = std::max({ tile_px, ts->tile_w, ts->tile_h });
    }
    if (tile_px == 0) tile_px = 8;

    int cell = tile_px * std::max(1, scale);
    int W = lv.width() * cell, H = lv.height() * cell;
    if (W == 0 || H == 0) return "the level is empty";
    std::vector<rgba> img((size_t)W * H, from_abgr(cfg.grid_bg_color));

    auto blend = [&](int x, int y, rgba c, float opacity) {
        rgba& d = img[(size_t)y * W + x];
        float a = c.a / 255.f * opacity;
        d.r = (uint8_t)(c.r * a + d.r * (1 - a) + 0.5f);
        d.g = (uint8_t)(c.g * a + d.g * (1 - a) + 0.5f);
        d.b = (uint8_t)(c.b * a + d.b * (1 - a) + 0.5f);
    };

    for (auto const& lc : cfg.layers) {
        if (!lc.visible || lc.opacity <= 0.f) continue;
        if (lc.mode != layer_mode::tile && lc.mode != layer_mode::color) continue;
        grid g = lv[lc.name];
        tileset_config const* ts = cfg.find_tileset(lc.tileset);
        sheet const* sh = lc.mode == layer_mode::tile ? &sheets.at(ts->name) : nullptr;
        int per_row = sh ? std::max(1, sh->w / ts->tile_w) : 1;

        for (int cy = 0; cy < lv.height(); ++cy)
            for (int cx = 0; cx < lv.width(); ++cx) {
                if (g.is_empty(cx, cy)) continue;
                int v = g.at(cx, cy);
                int x0 = cx * cell, y0 = cy * cell;
                if (sh) {
                    if (v < 0) continue;
                    int tx = (v % per_row) * ts->tile_w, ty = (v / per_row) * ts->tile_h;
                    if (ty + ts->tile_h > sh->h) continue;   // past the sheet
                    for (int y = 0; y < cell; ++y)
                        for (int x = 0; x < cell; ++x) {
                            int sx = tx + x * ts->tile_w / cell, sy = ty + y * ts->tile_h / cell;
                            uint8_t const* p = sh->px.get() + ((size_t)sy * sh->w + sx) * 4;
                            blend(x0 + x, y0 + y, { p[0], p[1], p[2], p[3] }, lc.opacity);
                        }
                } else {
                    rgba c;
                    if (g.is_number()) {
                        c = number_color(v);
                    } else {
                        int vid = mask_to_vid(v);
                        if (vid < 0) continue;
                        c = k_palette[vid % 12];
                    }
                    for (int y = 0; y < cell; ++y)
                        for (int x = 0; x < cell; ++x) blend(x0 + x, y0 + y, c, lc.opacity);
                }
            }
    }

    if (!stbi_write_png(out_path.c_str(), W, H, 4, img.data(), W * 4))
        return "cannot write '" + out_path + "'";
    return "";
}

}  // namespace ls
