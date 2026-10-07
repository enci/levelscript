#include "debug_ui.hpp"
#include "debug_ui_internal.hpp"
#include "fs_resolver.hpp"
#include "modules.hpp"
#include "phosphor_icons.hpp"
#include "platform_titlebar.hpp"
#include "parser.hpp"
#include "version.hpp"
#include "diagnostic.hpp"

#include <SDL3/SDL.h>
#include <imgui.h>
#include <imgui_internal.h>   // DockBuilder (default layout)
#include <imgui_impl_sdl3.h>
#include <imgui_impl_sdlrenderer3.h>
#include <imgui_freetype.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

namespace ls {

// One shared flag set for the two pinned bars (control bar top, status bar
// bottom) -- they are chrome, not dockable windows.
static constexpr ImGuiWindowFlags k_bar_flags =
    ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoDocking
    | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar
    | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus;

// ── script loading ────────────────────────────────────────────────────────────

bool load_script(script& sc) {
    std::string name = canonical_module_name(sc.path);
    std::string src;
    if (!read_text_file(name, src)) {
        sc.status     = "Cannot open: " + sc.path;
        sc.full_error = sc.status + "\n";
        sc.ok         = false;
        return false;
    }

    // Execution path: the public API compile. This generator is the only
    // thing that ever runs the script.
    generator gen = generator::compile(src, name, fs_resolver());
    if (!gen) {
        sc.full_error = gen.error();
        auto nl       = sc.full_error.find('\n');
        sc.status     = nl == std::string::npos ? sc.full_error
                                                : sc.full_error.substr(0, nl);
        sc.ok = false;
        return false;
    }

    // Display metadata only: a second parse+analyze over the same source
    // (statement labels, rule pattern previews, tag/layer tables). Read-only;
    // it never executes anything.
    diagnostics    diags;
    module_closure mods = load_closure(src, name, fs_resolver(), diags);
    compiled       meta;
    if (diags.has_errors() || !analyze(mods, meta, diags)) {
        // The public compile succeeded over the same source, so this is
        // effectively unreachable; degrade with a status line regardless.
        sc.full_error = diags.format_all();
        sc.status     = "Metadata analysis failed";
        sc.ok         = false;
        return false;
    }

    sc.gen    = std::move(gen);
    sc.ast    = std::move(mods.merged);
    sc.meta   = std::move(meta);
    sc.entry  = sc.gen.sequence(sc.entry_name);
    if (sc.entry < 0 && sc.gen.sequence_count() > 0) {
        sc.entry      = 0;
        sc.entry_name = sc.gen.sequence_name(0);
    }
    sc.status = "Loaded OK";
    sc.ok     = true;
    return true;
}

// ── resources ─────────────────────────────────────────────────────────────────

// Look next to the current directory first (repo layout), then next to the
// executable. "" when not found -- callers fall back gracefully.
static std::string find_resource(char const* rel) {
    if (std::ifstream(rel).good()) return rel;
    char const* base = SDL_GetBasePath();   // SDL3: owned by SDL, do not free
    if (base) {
        std::string p = std::string(base) + rel;
        if (std::ifstream(p).good()) return p;
    }
    return {};
}

// ── themes (defined below run_debug_ui) ──────────────────────────────────────

// Color of the gaps between docked panes (the dock host's background).
static ImVec4 g_dock_gap;
static void set_style_metrics();
static void set_light_theme();
static void set_dark_theme();

// ── entry point ───────────────────────────────────────────────────────────────

int run_debug_ui(std::string const& path, std::optional<uint64_t> fixed_seed,
                 std::string const& entry) {
    script sc;
    sc.path = path;
    sc.entry_name = entry;
    if (!load_script(sc)) {
        std::fprintf(stderr, "%s", sc.full_error.c_str());
        return 1;
    }

    project_config cfg = project_config::load(path, sc.layer_names());

    // ── prefs (theme + window geometry + seed) -- SDL's per-user location ────
    SDL_Init(SDL_INIT_VIDEO);
    char* pref_raw = SDL_GetPrefPath("levelscript", "debugger");
    std::string pref_dir = pref_raw ? pref_raw : "";
    if (pref_raw) SDL_free(pref_raw);
    std::string ini_path   = pref_dir + "layout.ini";
    std::string prefs_path = pref_dir + "prefs.json";
    // The tool was called `lsd`: carry its layout and prefs over once.
    if (!pref_dir.empty()) {
        namespace fs = std::filesystem;
        std::error_code ec;
        fs::path old_dir = fs::path(pref_dir).parent_path().parent_path() / "lsd";
        auto carry = [&](char const* from, std::string const& to) {
            if (!fs::exists(to, ec) && fs::exists(old_dir / from, ec))
                fs::copy_file(old_dir / from, to, ec);
        };
        carry("lsd.ini", ini_path);
        carry("lsd_prefs.json", prefs_path);
    }

    int win_w = 1600, win_h = 960;
    std::optional<int> win_x, win_y;
    std::string theme_str        = "light";
    float       pref_play_fps    = 4.f;
    uint64_t    pref_seed        = 0;
    bool        pref_seed_locked = false;
    {
        std::ifstream f(prefs_path);
        if (f) {
            auto j = nlohmann::json::parse(f, nullptr, /*allow_exceptions=*/false);
            if (j.is_object()) {
                auto gets = [&](char const* k, std::string def) {
                    auto it = j.find(k);
                    return (it != j.end() && it->is_string()) ? (std::string)*it : def;
                };
                auto getn = [&](char const* k, double def) {
                    auto it = j.find(k);
                    return (it != j.end() && it->is_number()) ? (double)*it : def;
                };
                theme_str     = gets("theme", "light");
                win_w         = (int)getn("win_w", 1600);
                win_h         = (int)getn("win_h", 960);
                pref_play_fps = (float)getn("play_fps", 4.0);
                pref_seed     = (uint64_t)getn("seed", 0.0);
                if (auto it = j.find("seed_locked"); it != j.end() && it->is_boolean())
                    pref_seed_locked = *it;
                if (j.contains("win_x") && j.contains("win_y")) {
                    win_x = (int)getn("win_x", 0.0);
                    win_y = (int)getn("win_y", 0.0);
                }
            }
        }
    }

    auto make_seed = [&]() -> uint64_t {
        if (fixed_seed) return *fixed_seed;
        // 32 bits is already plenty of variation for regenerating a level,
        // and keeps the seed field a tidy 8 hex digits in the UI.
        return (uint64_t)(uint32_t)
            std::chrono::high_resolution_clock::now().time_since_epoch().count();
    };
    // An explicit --seed always wins; otherwise restore a locked seed from
    // the last session, or roll a fresh one.
    uint64_t seed        = (!fixed_seed && pref_seed_locked) ? pref_seed : make_seed();
    bool     seed_locked = pref_seed_locked;

    // The one in-flight run. Every step/snapshot/highlight below goes through
    // this public-API run; Reset constructs a fresh one via generator::run().
    debug_run run;
    run.restart(sc.gen, sc.entry, seed);

    // ── SDL3 window ───────────────────────────────────────────────────────────
    SDL_Window* window = SDL_CreateWindow(
        ("LevelScript - " + path).c_str(), win_w, win_h,
        SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
    if (win_x && win_y) SDL_SetWindowPosition(window, *win_x, *win_y);

    SDL_Renderer* renderer = SDL_CreateRenderer(window, nullptr);
    SDL_SetRenderVSync(renderer, 1);

    std::unordered_map<std::string, tile_texture> tile_textures;
    sync_tilesets(renderer, path, cfg, tile_textures);

    // ── ImGui ─────────────────────────────────────────────────────────────────
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard
                    | ImGuiConfigFlags_DockingEnable
                    | ImGuiConfigFlags_DpiEnableScaleFonts;  // rasterize fonts at display DPI
    io.ConfigDpiScaleFonts = true;                           // auto-scale fonts per viewport DPI
    // IniFilename is a raw pointer -- ini_path must outlive the ImGui context.
    io.IniFilename = ini_path.c_str();
    // FreeType backend -- crisper rendering, especially at HiDPI.
    io.Fonts->SetFontLoader(ImGuiFreeType::GetFontLoader());
    // UI text is Latin only (.ls scripts are ASCII), so ImGui's default glyph
    // range suffices. UI icons come from the merged Phosphor font. Missing
    // font files degrade to ImGui's built-in font rather than failing.
    std::string text_font = find_resource("resources/Roboto-Regular.ttf");
    std::string icon_font = find_resource("resources/Phosphor-Bold.woff");
    if (!text_font.empty())
        io.Fonts->AddFontFromFileTTF(text_font.c_str(), 15.0f, nullptr, nullptr);
    else
        io.Fonts->AddFontDefault();
    if (!icon_font.empty()) {
        ImFontConfig icon_cfg;
        icon_cfg.MergeMode     = true;
        icon_cfg.GlyphOffset.y = 2.0f;
        static const ImWchar phosphor_ranges[] = {
            (ImWchar)phosphor::PH_RANGE_BEGIN, (ImWchar)phosphor::PH_RANGE_END, 0
        };
        io.Fonts->AddFontFromFileTTF(icon_font.c_str(), 14.0f, &icon_cfg,
                                     phosphor_ranges);
    }
    // Docked tab labels use their own face/size (nullptr -> fall back to the UI font).
    // Must come AFTER the icon merge above: MergeMode merges into the last font added.
    constexpr float k_tab_font_size = 16.0f;
    std::string tab_font_path = find_resource("resources/Roboto-Bold.ttf");
    ImFont* tab_font = tab_font_path.empty() ? nullptr
        : io.Fonts->AddFontFromFileTTF(tab_font_path.c_str(), k_tab_font_size, nullptr, nullptr);
    // Do NOT call ScaleAllSizes here -- SDL_SetRenderScale already maps logical
    // pixels to physical pixels, so ScaleAllSizes would double-scale everything.
    ImGui_ImplSDL3_InitForSDLRenderer(window, renderer);
    ImGui_ImplSDLRenderer3_Init(renderer);

    // ── theme ─────────────────────────────────────────────────────────────────
    enum class theme_mode { system, light, dark };
    theme_mode cur_theme_mode = theme_mode::light;
    if      (theme_str == "light") cur_theme_mode = theme_mode::light;
    else if (theme_str == "dark")  cur_theme_mode = theme_mode::dark;
    else if (theme_str == "system") cur_theme_mode = theme_mode::system;
    bool dark_theme = false;

    auto apply_theme = [&] {
        set_style_metrics();
        if (dark_theme) set_dark_theme(); else set_light_theme();
        // Tint the OS title bar / window border to blend with the app. The debugger has
        // no menu bar, so match the dominant window background surface.
        ls::platform::set_titlebar(window, g_dock_gap.x, g_dock_gap.y, g_dock_gap.z, dark_theme);
    };

    auto resolve_theme = [&] {
        switch (cur_theme_mode) {
            case theme_mode::light: dark_theme = false; break;
            case theme_mode::dark:  dark_theme = true;  break;
            case theme_mode::system: {
                SDL_SystemTheme t = SDL_GetSystemTheme();
                if (t != SDL_SYSTEM_THEME_UNKNOWN)
                    dark_theme = (t == SDL_SYSTEM_THEME_DARK);
                break;
            }
        }
    };

    float play_fps = pref_play_fps;
    layer_thumbs thumbs;   // the Layers panel's textures

    // Captures live window geometry at save time, so it's current whether
    // called right after a theme toggle or at shutdown.
    auto save_prefs = [&] {
        nlohmann::json j;
        char const* mode = "system";
        if      (cur_theme_mode == theme_mode::light) mode = "light";
        else if (cur_theme_mode == theme_mode::dark)  mode = "dark";
        j["theme"]       = mode;
        j["play_fps"]    = play_fps;
        j["seed"]        = seed;
        j["seed_locked"] = seed_locked;
        int w, h, x, y;
        SDL_GetWindowSize(window, &w, &h);
        SDL_GetWindowPosition(window, &x, &y);
        j["win_w"] = w; j["win_h"] = h;
        j["win_x"] = x; j["win_y"] = y;
        std::ofstream f(prefs_path);
        if (f) f << j.dump(2);
    };

    resolve_theme();
    apply_theme();

    // ── actions ───────────────────────────────────────────────────────────────
    // VS Code's debug toolbar: Continue / Pause, Step Over, Step Into, Step
    // Out, Restart. Multi-event commands run time-sliced in the main loop
    // (debug_run::tick) and stop at the next breakpoint.
    auto action_continue  = [&] { run.begin(debug_run::command::cont); };
    auto action_pause     = [&] { run.pause(); };
    auto action_step_over = [&] { run.begin(debug_run::command::step_over); };
    auto action_step_into = [&] { run.step_into(); };
    auto action_step_out  = [&] { run.begin(debug_run::command::step_out); };
    auto action_reset     = [&] {
        if (!seed_locked) seed = make_seed();
        // Reload from disk so edits made in an external editor are picked up;
        // on failure the previous compile stays live and the status bar shows
        // the first diagnostic.
        if (load_script(sc)) cfg.sync_layers(sc.layer_names());
        run.restart(sc.gen, sc.entry, seed);
    };

    // ── main loop ─────────────────────────────────────────────────────────────
#ifdef LS_ENABLE_IMGUI_DEMO
    bool show_demo = false;
    bool show_style = false;
#endif
    bool running = true;
    bool playing = false;
    using clock = std::chrono::steady_clock;
    auto last_advance = clock::now();

    // Event-driven redraw: block in SDL_WaitEvent when idle so an untouched
    // window consumes ~0 CPU/GPU. Keep pumping frames while playback runs or
    // during a short cooldown after the last event (ImGui needs a few frames
    // to settle hovers/animations). Seed the cooldown so the first frames paint.
    constexpr int k_cooldown_frames = 3;
    int redraw_frames = k_cooldown_frames;

    auto handle_event = [&](const SDL_Event& ev) {
        ImGui_ImplSDL3_ProcessEvent(&ev);
        if (ev.type == SDL_EVENT_QUIT) running = false;
        if (ev.type == SDL_EVENT_SYSTEM_THEME_CHANGED &&
                cur_theme_mode == theme_mode::system) {
            SDL_SystemTheme t = SDL_GetSystemTheme();
            if (t != SDL_SYSTEM_THEME_UNKNOWN) {
                dark_theme = (t == SDL_SYSTEM_THEME_DARK);
                apply_theme();
            }
        }
        if (ev.type == SDL_EVENT_KEY_DOWN && !io.WantCaptureKeyboard) {
            bool shift = (ev.key.mod & SDL_KMOD_SHIFT) != 0;
            bool cmd   = (ev.key.mod & (SDL_KMOD_CTRL | SDL_KMOD_GUI)) != 0;
            switch (ev.key.scancode) {   // VS Code's debug keys
                case SDL_SCANCODE_F5:
                    if (cmd && shift)      action_reset();
                    else if (!run.busy())  action_continue();
                    break;
                case SDL_SCANCODE_F6:  action_pause();     break;
                case SDL_SCANCODE_F10: action_step_over(); break;
                case SDL_SCANCODE_F11:
                    if (shift) action_step_out(); else action_step_into();
                    break;
                case SDL_SCANCODE_R:   action_reset();     break;
                case SDL_SCANCODE_Q:   running = false;    break;
                case SDL_SCANCODE_SPACE:
                    playing = !playing;
                    last_advance = clock::now();
                    break;
                default: break;
            }
        }
    };

    while (running) {
        // While playback runs or we're mid-cooldown, poll without blocking so
        // the loop keeps ticking (vsync caps it at the display rate).
        // Otherwise block until the next event.
        bool had_event = false;
        SDL_Event ev;
        if (playing || run.busy() || redraw_frames > 0) {
            while (SDL_PollEvent(&ev)) { handle_event(ev); had_event = true; }
        } else if (SDL_WaitEvent(&ev)) {
            handle_event(ev);
            had_event = true;
            while (SDL_PollEvent(&ev)) handle_event(ev);
        }
        if (had_event) redraw_frames = k_cooldown_frames;
        else if (redraw_frames > 0) redraw_frames--;

        // A running debug command: a few ms of events per frame, so the
        // viewport updates live and Pause (F6) can interrupt.
        if (run.busy()) {
            run.tick(sc.breakpoints, 8.0);
            redraw_frames = k_cooldown_frames;
        }

        // Timed playback -- advance one application per tick at play_fps;
        // a breakpoint stops it like any other command.
        if (playing && run.done) playing = false;
        if (playing && !run.busy()) {
            {
                auto now = clock::now();
                float elapsed = std::chrono::duration<float>(now - last_advance).count();
                if (elapsed >= 1.f / play_fps) {
                    run.advance_cmd(sc.breakpoints);
                    run.refresh();
                    if (!run.paused_at.empty()) playing = false;
                    last_advance = now;
                }
            }
        }

        // Sync tileset textures if a path changed via UI.
        sync_tilesets(renderer, path, cfg, tile_textures);

        ImGui_ImplSDLRenderer3_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();

        ImGuiViewport* viewport = ImGui::GetMainViewport();
        // A fixed-height, no-scrollbar window clips its content unless the
        // height also covers ImGui's own window padding (top + bottom).
        float bar_pad      = ImGui::GetStyle().WindowPadding.y * 2.f;
        const float status_pad_y = 5.f;   // slimmer than the control bar
        float statusbar_h  = ImGui::GetTextLineHeight() + status_pad_y * 2.f;
        float controlbar_h = ImGui::GetFrameHeight() + bar_pad;
        viewport->WorkPos.y  += controlbar_h;   // reserve a top strip for the control bar
        viewport->WorkSize.y -= controlbar_h + statusbar_h;   // + a bottom strip for the status bar
        {
            // Inset the dock area by one gap on every side so the separator also
            // frames the outer edges of the panes (the cleared backdrop shows through).
            const float gap = ImGui::GetStyle().DockingSeparatorSize;
            viewport->WorkPos.x  += gap;
            viewport->WorkPos.y  += gap;
            viewport->WorkSize.x -= gap * 2.f;
            viewport->WorkSize.y -= gap * 2.f;
        }
        {
            // Same as ImGui::DockSpaceOverViewport (same window label and ids, so saved
            // layouts still load) but with NoBackground, so the gaps between panes and
            // the inset frame show the g_dock_gap clear color. (The Passthru dock flag
            // is no good here: it paints a WindowBg fill over the whole dockspace.)
            char label[32];
            std::snprintf(label, sizeof label, "WindowOverViewport_%08X", viewport->ID);
            ImGui::SetNextWindowPos(viewport->WorkPos);
            ImGui::SetNextWindowSize(viewport->WorkSize);
            ImGui::SetNextWindowViewport(viewport->ID);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.f);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.f);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, { 0.f, 0.f });
            ImGui::Begin(label, nullptr,
                ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse
                | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove
                | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoBringToFrontOnFocus
                | ImGuiWindowFlags_NoNavFocus | ImGuiWindowFlags_NoBackground);
            ImGui::PopStyleVar(3);
            // Fonts are a global stack, so this applies to the tab bars the dock
            // host draws here, while pane contents (separate windows) keep the UI font.
            if (tab_font) ImGui::PushFont(tab_font, k_tab_font_size);
            const ImGuiID dock_id = ImGui::GetID("DockSpace");
            // No saved layout (first run, or layout.ini deleted): dock every pane
            // in a sensible default instead of letting them stack on top of each other.
            if (!ImGui::DockBuilderGetNode(dock_id)) {
                ImGui::DockBuilderRemoveNode(dock_id);
                ImGui::DockBuilderAddNode(dock_id, ImGuiDockNodeFlags_DockSpace);
                ImGui::DockBuilderSetNodeSize(dock_id, viewport->WorkSize);
                ImGuiID center = dock_id, left, right, left_bottom, right_bottom;
                left  = ImGui::DockBuilderSplitNode(center, ImGuiDir_Left,  0.22f, nullptr, &center);
                right = ImGui::DockBuilderSplitNode(center, ImGuiDir_Right, 0.30f, nullptr, &center);
                ImGui::DockBuilderSplitNode(left,  ImGuiDir_Down, 0.40f, &left_bottom,  &left);
                ImGui::DockBuilderSplitNode(right, ImGuiDir_Down, 0.45f, &right_bottom, &right);
                ImGui::DockBuilderDockWindow("Sequence",   left);
                ImGui::DockBuilderDockWindow("Layers",     left_bottom);
                ImGui::DockBuilderDockWindow("Viewport", center);
                ImGui::DockBuilderDockWindow("Rule",         right);
                ImGui::DockBuilderDockWindow("Tags",         right_bottom);
                ImGui::DockBuilderDockWindow("Settings", right_bottom);
                ImGui::DockBuilderFinish(dock_id);
            }
            ImGui::DockSpace(dock_id, { 0.f, 0.f });
            if (tab_font) ImGui::PopFont();
            ImGui::End();
        }

        // ── control bar (pinned to the top, like the status bar at the bottom) ──
        {
            ImGui::SetNextWindowPos(viewport->Pos);
            ImGui::SetNextWindowSize({ viewport->Size.x, controlbar_h });
            ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.f);   // pinned chrome stays square
            ImGui::PushStyleColor(ImGuiCol_WindowBg, g_dock_gap);     // top band = frame color
            ImGui::Begin("##ControlBar", nullptr, k_bar_flags);
            ImGui::PopStyleColor();
            ImGui::PopStyleVar();

            // Icon button with a hover tooltip carrying the full label and
            // shortcut. When `active` is true it renders held-down (a "stays
            // pressed" toggle) by borrowing the button's active color.
            auto toolbtn = [&](char const* icon, char const* tip, bool active = false) {
                if (active) {
                    const ImVec4 held = ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive);
                    ImGui::PushStyleColor(ImGuiCol_Button, held);
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, held);
                }
                bool clicked = ImGui::Button(icon, { 28.f, 0.f });
                if (active) ImGui::PopStyleColor(2);
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tip);
                return clicked;
            };

            // VS Code's debug toolbar: same order, names, keys and colors -
            // blue stepping icons, a green restart.
            ImVec4 dbg_blue  = dark_theme ? ImVec4(0.459f, 0.745f, 1.000f, 1.f)    // #75BEFF
                                          : ImVec4(0.000f, 0.478f, 0.800f, 1.f);   // #007ACC
            ImVec4 dbg_green = dark_theme ? ImVec4(0.537f, 0.820f, 0.522f, 1.f)    // #89D185
                                          : ImVec4(0.220f, 0.541f, 0.204f, 1.f);   // #388A34
            auto dbgbtn = [&](char const* icon, char const* tip, ImVec4 col, bool enabled) {
                ImGui::BeginDisabled(!enabled);
                ImGui::PushStyleColor(ImGuiCol_Text, col);
                bool clicked = toolbtn(icon, tip);
                ImGui::PopStyleColor();
                ImGui::EndDisabled();
                ImGui::SameLine();
                return clicked;
            };
            bool can_step = !run.done && !run.busy();
            if (run.busy()) {
                if (dbgbtn(phosphor::PH_PAUSE, "Pause (F6)", dbg_blue, true)) action_pause();
            } else {
                if (dbgbtn(phosphor::PH_PLAY,
                           run.started ? "Continue (F5) - run to the next breakpoint"
                                       : "Run (F5) - run to the first breakpoint",
                           dbg_blue, !run.done))
                    action_continue();
            }
            if (dbgbtn(phosphor::PH_ARROW_ARC_RIGHT,
                       "Step Over (F10) - finish the statement, sequences included",
                       dbg_blue, can_step))
                action_step_over();
            if (dbgbtn(phosphor::PH_ARROW_LINE_DOWN,
                       "Step Into (F11) - one rule application", dbg_blue, can_step))
                action_step_into();
            if (dbgbtn(phosphor::PH_ARROW_LINE_UP,
                       "Step Out (Shift+F11) - finish the current sequence",
                       dbg_blue, can_step))
                action_step_out();
            if (dbgbtn(phosphor::PH_ARROW_CLOCKWISE, "Restart (Ctrl+Shift+F5, R)",
                       dbg_green, true))
                action_reset();
            ImGui::Text("|");
            ImGui::SameLine();

            if (toolbtn(seed_locked ? phosphor::PH_LOCK : phosphor::PH_LOCK_OPEN,
                        seed_locked ? "Seed locked - Reset reuses it"
                                    : "Seed unlocked - Reset picks a new one",
                        seed_locked)) {
                seed_locked = !seed_locked;
            }
            ImGui::SameLine();
            ImGui::SetNextItemWidth(140.f);
            ImGui::InputScalar("##seed", ImGuiDataType_U64, &seed, nullptr, nullptr,
                               "%08llX", ImGuiInputTextFlags_CharsHexadecimal
                                       | ImGuiInputTextFlags_CharsUppercase);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Seed, hex (used on the next Reset)");
            ImGui::SameLine();

            // Entry (section 6): any sequence; choosing one restarts the run.
            ImGui::SetNextItemWidth(160.f);
            if (ImGui::BeginCombo("##entry", sc.entry_name.c_str())) {
                for (int i = 0; i < sc.gen.sequence_count(); ++i) {
                    bool sel = i == sc.entry;
                    if (ImGui::Selectable(sc.gen.sequence_name(i).c_str(), sel) && !sel) {
                        sc.entry      = i;
                        sc.entry_name = sc.gen.sequence_name(i);
                        run.restart(sc.gen, sc.entry, seed);
                    }
                    if (sel) ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Entry - the sequence a run applies");
            ImGui::SameLine();

            ImGui::Text("|");
            ImGui::SameLine();
            if (toolbtn(playing ? phosphor::PH_PAUSE_CIRCLE : phosphor::PH_PLAY_CIRCLE,
                        playing ? "Stop animating (Space)"
                                : "Animate (Space) - one application per tick", playing)) {
                playing = !playing;
                last_advance = clock::now();
            }
            ImGui::SameLine();
            ImGui::SetNextItemWidth(100.f);
            ImGui::SliderFloat("##fps", &play_fps, 0.5f, 30.f, "%.1f fps");

#ifdef LS_ENABLE_IMGUI_DEMO
            ImGui::SameLine();
            ImGui::Text("|");
            ImGui::SameLine();
            ImGui::Checkbox("Demo", &show_demo);
            ImGui::SameLine();
            ImGui::Checkbox("Style", &show_style);
#endif
            {
                // Order matches enum theme_mode { system, light, dark }.
                static char const* k_theme_icons[] =
                    { phosphor::PH_MONITOR, phosphor::PH_SUN, phosphor::PH_MOON };
                static char const* k_theme_names[] = { "System", "Light", "Dark" };
                int idx = (int)cur_theme_mode;
                std::string preview = std::string(k_theme_icons[idx]) + " " + k_theme_names[idx];

                const float combo_w = 120.f;
                ImGui::SameLine(ImGui::GetWindowWidth() - combo_w
                                - ImGui::GetStyle().WindowPadding.x);
                ImGui::SetNextItemWidth(combo_w);
                if (ImGui::BeginCombo("##theme", preview.c_str())) {
                    for (int i = 0; i < 3; ++i) {
                        std::string label = std::string(k_theme_icons[i]) + " " + k_theme_names[i];
                        bool selected = (i == idx);
                        if (ImGui::Selectable(label.c_str(), selected)) {
                            cur_theme_mode = (theme_mode)i;
                            resolve_theme();
                            apply_theme();
                            save_prefs();
                        }
                        if (selected) ImGui::SetItemDefaultFocus();
                    }
                    ImGui::EndCombo();
                }
            }
            ImGui::End();
        }

        // ── grid (layer strip + canvas) ─────────────────────────────────────
        // Title case; "###ID" keeps each window's id - and its saved dock
        // position in layout.ini - independent of the title.
        ImGui::Begin("Viewport###VIEWPORT");
        ImGui::BeginChild("##grid", {0.f, 0.f}, false, ImGuiWindowFlags_HorizontalScrollbar);
        draw_grid_composite(sc, run, cfg, tile_textures, cfg.cell_px());
        ImGui::EndChild();
        ImGui::End();

#ifdef LS_ENABLE_IMGUI_DEMO
        if (show_demo) ImGui::ShowDemoWindow(&show_demo);
        if (show_style) {
            if (ImGui::Begin("Style Editor", &show_style)) {
                ImGui::ColorEdit4("Dock gap", &g_dock_gap.x);
                ImGui::ShowStyleEditor();
            }
            ImGui::End();
        }
#endif

        ImGui::Begin("Tags###TAGS");
        draw_tags_window(sc.meta, cfg);
        ImGui::End();

        ImGui::Begin("Rule###RULE", nullptr, ImGuiWindowFlags_HorizontalScrollbar);
        draw_rule_window(sc, run, cfg.mini_px, cfg);
        ImGui::End();

        ImGui::Begin("Program###PROGRAM");
        draw_program_window(sc, run);
        ImGui::End();

        ImGui::SetNextWindowSize({260.f, 420.f}, ImGuiCond_FirstUseEver);
        ImGui::Begin("Layers###LAYERS");
        draw_layers_window(sc, run, cfg, renderer, thumbs);
        ImGui::End();

        ImGui::Begin("Settings###SETTINGS");
        draw_settings_window(cfg, tile_textures, renderer, path);
        ImGui::End();

        // ── status bar ──────────────────────────────────────────────────────
        {
            ImGui::SetNextWindowPos({ viewport->Pos.x,
                                      viewport->Pos.y + viewport->Size.y - statusbar_h });
            ImGui::SetNextWindowSize({ viewport->Size.x, statusbar_h });

            // VS Code (Dark+/Light+) style: a saturated bar with white text --
            // blue while the script is healthy, red when the last (re)load
            // failed. The right-hand chip is a darker block carrying the load
            // status so a failure is hard to miss.
            const ImVec4 bar_col  = sc.ok ? ImVec4(0.000f, 0.478f, 0.800f, 1.f)   // #007ACC
                                          : ImVec4(0.780f, 0.180f, 0.180f, 1.f);
            const ImVec4 chip_col = sc.ok ? ImVec4(0.000f, 0.360f, 0.600f, 1.f)
                                          : ImVec4(0.520f, 0.070f, 0.070f, 1.f);
            const ImVec4 text_col = { 1.f, 1.f, 1.f, 1.f };
            const ImVec4 dim_col  = { 1.f, 1.f, 1.f, 0.78f };
            ImGui::PushStyleColor(ImGuiCol_WindowBg, bar_col);
            ImGui::PushStyleColor(ImGuiCol_Text, dim_col);
            ImGui::PushStyleColor(ImGuiCol_TextDisabled, dim_col);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,
                                { ImGui::GetStyle().WindowPadding.x, status_pad_y });
            // Tooltips are deferred until the bar's style overrides are popped,
            // so they keep the normal popup colors instead of white-on-white.
            std::string bar_tip;
            ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.f);
            ImGui::Begin("##StatusBar", nullptr, k_bar_flags);
            ImGui::PopStyleVar();

            std::string status = std::string(phosphor::PH_STEPS) + " stmt "
                + std::to_string(run.current_stmt()) + "/"
                + std::to_string(run.stmt_count);
            if (run.started && run.apps_in_stmt > 0)
                status += "   apps=" + std::to_string(run.apps_in_stmt);
            ImGui::TextUnformatted(status.c_str());
            if (run.done) { ImGui::SameLine(); ImGui::TextUnformatted("[done]"); }
            if (!run.paused_at.empty()) {
                ImGui::SameLine(0.f, 24.f);
                ImGui::Text("%s Paused on %s", phosphor::PH_CIRCLE, run.paused_at.c_str());
            }

            ImGui::SameLine(0.f, 24.f);
            ImGui::TextUnformatted(phosphor::PH_DICE_FIVE);
            ImGui::SameLine();
            char seed_hex[24];
            std::snprintf(seed_hex, sizeof(seed_hex), "%08llX",
                          (unsigned long long)seed);
            ImGui::TextUnformatted(seed_hex);
            if (ImGui::IsItemHovered()) bar_tip = "Click to copy";
            if (ImGui::IsItemClicked()) ImGui::SetClipboardText(seed_hex);

            ImGui::SameLine(0.f, 24.f);
            ImGui::Text("%s %d x %d", phosphor::PH_GRID_FOUR,
                        run.snap.width(), run.snap.height());
            if (ImGui::IsItemHovered()) bar_tip = "Grid size (columns x rows)";

            ImGui::SameLine(0.f, 24.f);
            ImGui::TextDisabled("LevelScript " LS_VERSION_STRING);
            if (ImGui::IsItemHovered()) bar_tip = "LevelScript version";

            // Load-status chip, flush against the right edge.
            if (!sc.status.empty()) {
                std::string msg = std::string(sc.ok ? phosphor::PH_CHECK_CIRCLE
                                                    : phosphor::PH_WARNING_CIRCLE)
                                + "  " + sc.status;
                const float chip_pad = 12.f;
                ImVec2 wpos  = ImGui::GetWindowPos();
                ImVec2 wsize = ImGui::GetWindowSize();
                float  chip_w = std::min(ImGui::CalcTextSize(msg.c_str()).x + chip_pad * 2.f,
                                         wsize.x * 0.6f);
                ImVec2 c0 = { wpos.x + wsize.x - chip_w, wpos.y };
                ImVec2 c1 = { wpos.x + wsize.x,          wpos.y + wsize.y };
                ImDrawList* dl = ImGui::GetWindowDrawList();
                dl->AddRectFilled(c0, c1, ImGui::GetColorU32(chip_col));
                dl->PushClipRect(c0, c1, true);
                dl->AddText({ c0.x + chip_pad, wpos.y + status_pad_y },
                            ImGui::GetColorU32(text_col), msg.c_str());
                dl->PopClipRect();
                if (!sc.ok && ImGui::IsMouseHoveringRect(c0, c1))
                    bar_tip = sc.full_error;
            }
            ImGui::End();
            ImGui::PopStyleVar();
            ImGui::PopStyleColor(3);
            if (!bar_tip.empty()) ImGui::SetTooltip("%s", bar_tip.c_str());
        }

        // ── render ────────────────────────────────────────────────────────────
        ImGui::Render();
        SDL_SetRenderScale(renderer, io.DisplayFramebufferScale.x,
                           io.DisplayFramebufferScale.y);
        const auto& bg = g_dock_gap;   // backdrop = gap color, so it frames the dock area
        SDL_SetRenderDrawColor(renderer,
            (Uint8)(bg.x * 255), (Uint8)(bg.y * 255), (Uint8)(bg.z * 255), 255);
        SDL_RenderClear(renderer);
        ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), renderer);
        SDL_RenderPresent(renderer);
    }

    // ── save config + cleanup ─────────────────────────────────────────────────
    cfg.save(path);
    save_prefs();

    ImGui_ImplSDLRenderer3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}

// ── style metrics (shared by every theme) ─────────────────────────────────────

static void set_style_metrics() {
    auto& style = ImGui::GetStyle();
    style.FrameRounding = 5.0f;
    style.ChildRounding = 5.0f;
    style.PopupRounding = 5.0f;
    style.WindowRounding = 5.0f;   // tooltips use this, not PopupRounding
    style.GrabRounding  = 5.0f;
    style.DockingSeparatorSize = 6.0f;   // visible gap between docked panes

    // No borders: panes are separated by the dock gap instead.
    style.WindowBorderSize = 0.0f;
    style.ChildBorderSize  = 0.0f;
    style.PopupBorderSize  = 0.0f;
    style.FrameBorderSize  = 0.0f;
    style.TabBorderSize    = 0.0f;
    style.WindowMenuButtonPosition = ImGuiDir_None;   // no dropdown button on docked tab bars
    style.TabRounding      = 0.0f;
    style.TabBarOverlineSize = 0.0f;
}

// ── theme palettes (colors only) ──────────────────────────────────────────────

static void set_light_theme() {
    g_dock_gap = ImVec4(0xE5 / 255.f, 0xE5 / 255.f, 0xE5 / 255.f, 1.00f);   // #E5E5E5

    ImVec4* colors = ImGui::GetStyle().Colors;
    colors[ImGuiCol_Text]                   = ImVec4(0.15f, 0.15f, 0.15f, 1.00f);
    colors[ImGuiCol_TextDisabled]           = ImVec4(0.50f, 0.50f, 0.50f, 1.00f);
    colors[ImGuiCol_WindowBg]               = ImVec4(0.96f, 0.96f, 0.96f, 1.00f);
    colors[ImGuiCol_ChildBg]                = ImVec4(0.00f, 0.00f, 0.00f, 0.03f);
    colors[ImGuiCol_PopupBg]                = ImVec4(0.95f, 0.95f, 0.96f, 0.98f);
    colors[ImGuiCol_Border]                 = ImVec4(0.90f, 0.90f, 0.90f, 1.00f);
    colors[ImGuiCol_BorderShadow]           = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    colors[ImGuiCol_FrameBg]                = ImVec4(0.87f, 0.87f, 0.88f, 1.00f);
    colors[ImGuiCol_FrameBgHovered]         = ImVec4(0.75f, 0.75f, 0.77f, 1.00f);
    colors[ImGuiCol_FrameBgActive]          = ImVec4(0.68f, 0.68f, 0.70f, 1.00f);
    colors[ImGuiCol_TitleBg]                = ImVec4(0.96f, 0.96f, 0.96f, 1.00f);
    colors[ImGuiCol_TitleBgActive]          = ImVec4(0.96f, 0.96f, 0.96f, 1.00f);
    colors[ImGuiCol_TitleBgCollapsed]       = ImVec4(0.95f, 0.95f, 0.96f, 0.75f);
    colors[ImGuiCol_MenuBarBg]              = ImVec4(0.91f, 0.91f, 0.92f, 1.00f);
    colors[ImGuiCol_ScrollbarBg]            = ImVec4(0.91f, 0.91f, 0.92f, 0.00f);
    colors[ImGuiCol_ScrollbarGrab]          = ImVec4(0.70f, 0.70f, 0.72f, 1.00f);
    colors[ImGuiCol_ScrollbarGrabHovered]   = ImVec4(0.60f, 0.60f, 0.62f, 1.00f);
    colors[ImGuiCol_ScrollbarGrabActive]    = ImVec4(0.50f, 0.50f, 0.52f, 1.00f);
    colors[ImGuiCol_CheckMark]              = ImVec4(0.50f, 0.50f, 0.52f, 1.00f);
    colors[ImGuiCol_CheckboxSelectedBg]     = ImVec4(0.83f, 0.83f, 0.85f, 0.42f);
    colors[ImGuiCol_SliderGrab]             = ImVec4(0.55f, 0.55f, 0.57f, 1.00f);
    colors[ImGuiCol_SliderGrabActive]       = ImVec4(0.45f, 0.45f, 0.47f, 1.00f);
    colors[ImGuiCol_Button]                 = ImVec4(0.83f, 0.83f, 0.85f, 1.00f);
    colors[ImGuiCol_ButtonHovered]          = ImVec4(0.70f, 0.70f, 0.72f, 1.00f);
    colors[ImGuiCol_ButtonActive]           = ImVec4(0.62f, 0.62f, 0.64f, 1.00f);
    colors[ImGuiCol_Header]                 = ImVec4(0.53f, 0.53f, 0.53f, 0.25f);
    colors[ImGuiCol_HeaderHovered]          = ImVec4(0.53f, 0.53f, 0.53f, 0.25f);
    colors[ImGuiCol_HeaderActive]           = ImVec4(0.53f, 0.53f, 0.53f, 0.38f);
    colors[ImGuiCol_Separator]              = ImVec4(0.70f, 0.70f, 0.72f, 0.50f);
    colors[ImGuiCol_SeparatorHovered]       = ImVec4(0.26f, 0.59f, 0.98f, 0.60f);
    colors[ImGuiCol_SeparatorActive]        = ImVec4(0.26f, 0.59f, 0.98f, 0.80f);
    colors[ImGuiCol_ResizeGrip]             = ImVec4(0.26f, 0.59f, 0.98f, 0.20f);
    colors[ImGuiCol_ResizeGripHovered]      = ImVec4(0.26f, 0.59f, 0.98f, 0.50f);
    colors[ImGuiCol_ResizeGripActive]       = ImVec4(0.26f, 0.59f, 0.98f, 0.80f);
    colors[ImGuiCol_InputTextCursor]        = ImVec4(1.00f, 1.00f, 1.00f, 1.00f);
    colors[ImGuiCol_TabHovered]             = ImVec4(0.53f, 0.53f, 0.53f, 0.32f);
    colors[ImGuiCol_Tab]                    = ImVec4(0.78f, 0.78f, 0.80f, 0.00f);
    colors[ImGuiCol_TabSelected]            = ImVec4(0.70f, 0.70f, 0.72f, 0.00f);
    colors[ImGuiCol_TabSelectedOverline]    = ImVec4(0.26f, 0.59f, 0.98f, 0.00f);
    colors[ImGuiCol_TabDimmed]              = ImVec4(0.78f, 0.78f, 0.80f, 0.00f);
    colors[ImGuiCol_TabDimmedSelected]      = ImVec4(0.78f, 0.78f, 0.80f, 0.00f);
    colors[ImGuiCol_TabDimmedSelectedOverline]  = ImVec4(0.50f, 0.50f, 0.50f, 0.00f);
    colors[ImGuiCol_DockingPreview]         = ImVec4(0.38f, 0.38f, 0.38f, 0.70f);
    colors[ImGuiCol_DockingEmptyBg]         = ImVec4(0.20f, 0.20f, 0.20f, 1.00f);
    colors[ImGuiCol_PlotLines]              = ImVec4(0.39f, 0.39f, 0.39f, 1.00f);
    colors[ImGuiCol_PlotLinesHovered]       = ImVec4(1.00f, 0.43f, 0.35f, 1.00f);
    colors[ImGuiCol_PlotHistogram]          = ImVec4(0.90f, 0.70f, 0.00f, 1.00f);
    colors[ImGuiCol_PlotHistogramHovered]   = ImVec4(1.00f, 0.60f, 0.00f, 1.00f);
    colors[ImGuiCol_TableHeaderBg]          = ImVec4(0.78f, 0.78f, 0.80f, 1.00f);
    colors[ImGuiCol_TableBorderStrong]      = ImVec4(0.70f, 0.70f, 0.72f, 1.00f);
    colors[ImGuiCol_TableBorderLight]       = ImVec4(0.78f, 0.78f, 0.80f, 1.00f);
    colors[ImGuiCol_TableRowBg]             = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    colors[ImGuiCol_TableRowBgAlt]          = ImVec4(0.00f, 0.00f, 0.00f, 0.04f);
    colors[ImGuiCol_TextLink]               = ImVec4(0.26f, 0.59f, 0.98f, 1.00f);
    colors[ImGuiCol_TextSelectedBg]         = ImVec4(0.26f, 0.59f, 0.98f, 0.25f);
    colors[ImGuiCol_TreeLines]              = ImVec4(0.43f, 0.43f, 0.50f, 0.50f);
    colors[ImGuiCol_DragDropTarget]         = ImVec4(1.00f, 1.00f, 0.00f, 0.90f);
    colors[ImGuiCol_DragDropTargetBg]       = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    colors[ImGuiCol_UnsavedMarker]          = ImVec4(1.00f, 1.00f, 1.00f, 1.00f);
    colors[ImGuiCol_NavCursor]              = ImVec4(0.26f, 0.59f, 0.98f, 1.00f);
    colors[ImGuiCol_NavWindowingHighlight]  = ImVec4(1.00f, 1.00f, 1.00f, 0.70f);
    colors[ImGuiCol_NavWindowingDimBg]      = ImVec4(0.80f, 0.80f, 0.80f, 0.20f);
    colors[ImGuiCol_ModalWindowDimBg]       = ImVec4(0.20f, 0.20f, 0.20f, 0.35f);
}

static void set_dark_theme() {
    g_dock_gap = ImVec4(0.10f, 0.10f, 0.11f, 1.00f);
    ImVec4* c = ImGui::GetStyle().Colors;
    c[ImGuiCol_Text]                 = ImVec4(0.85f, 0.85f, 0.85f, 1.00f);
    c[ImGuiCol_TextDisabled]         = ImVec4(0.50f, 0.50f, 0.50f, 1.00f);
    c[ImGuiCol_WindowBg]             = ImVec4(0.16f, 0.16f, 0.17f, 1.00f);
    c[ImGuiCol_ChildBg]              = ImVec4(0.00f, 0.00f, 0.00f, 0.15f);
    c[ImGuiCol_PopupBg]              = ImVec4(0.12f, 0.12f, 0.13f, 0.96f);
    c[ImGuiCol_Border]               = ImVec4(0.35f, 0.35f, 0.38f, 0.40f);
    c[ImGuiCol_BorderShadow]         = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    c[ImGuiCol_FrameBg]              = ImVec4(0.22f, 0.22f, 0.23f, 1.00f);
    c[ImGuiCol_FrameBgHovered]       = ImVec4(0.28f, 0.28f, 0.30f, 1.00f);
    c[ImGuiCol_FrameBgActive]        = ImVec4(0.34f, 0.34f, 0.36f, 1.00f);
    c[ImGuiCol_TitleBg]              = ImVec4(0.10f, 0.10f, 0.11f, 1.00f);
    c[ImGuiCol_TitleBgActive]        = ImVec4(0.16f, 0.29f, 0.48f, 1.00f);
    c[ImGuiCol_TitleBgCollapsed]     = ImVec4(0.00f, 0.00f, 0.00f, 0.51f);
    c[ImGuiCol_MenuBarBg]            = ImVec4(0.14f, 0.14f, 0.15f, 1.00f);
    c[ImGuiCol_ScrollbarBg]          = ImVec4(0.14f, 0.14f, 0.15f, 1.00f);
    c[ImGuiCol_ScrollbarGrab]        = ImVec4(0.28f, 0.28f, 0.30f, 1.00f);
    c[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.34f, 0.34f, 0.36f, 1.00f);
    c[ImGuiCol_ScrollbarGrabActive]  = ImVec4(0.40f, 0.40f, 0.42f, 1.00f);
    c[ImGuiCol_CheckMark]            = ImVec4(0.40f, 0.68f, 0.98f, 1.00f);
    c[ImGuiCol_SliderGrab]           = ImVec4(0.38f, 0.39f, 0.40f, 1.00f);
    c[ImGuiCol_SliderGrabActive]     = ImVec4(0.48f, 0.49f, 0.50f, 1.00f);
    c[ImGuiCol_Button]               = ImVec4(0.22f, 0.22f, 0.23f, 1.00f);
    c[ImGuiCol_ButtonHovered]        = ImVec4(0.28f, 0.28f, 0.30f, 1.00f);
    c[ImGuiCol_ButtonActive]         = ImVec4(0.34f, 0.34f, 0.36f, 1.00f);
    c[ImGuiCol_Header]               = ImVec4(0.26f, 0.59f, 0.98f, 0.25f);
    c[ImGuiCol_HeaderHovered]        = ImVec4(0.26f, 0.59f, 0.98f, 0.60f);
    c[ImGuiCol_HeaderActive]         = ImVec4(0.26f, 0.59f, 0.98f, 0.80f);
    c[ImGuiCol_Separator]            = ImVec4(0.35f, 0.35f, 0.38f, 0.50f);
    c[ImGuiCol_SeparatorHovered]     = ImVec4(0.10f, 0.40f, 0.75f, 0.78f);
    c[ImGuiCol_SeparatorActive]      = ImVec4(0.10f, 0.40f, 0.75f, 1.00f);
    c[ImGuiCol_ResizeGrip]           = ImVec4(0.26f, 0.59f, 0.98f, 0.20f);
    c[ImGuiCol_ResizeGripHovered]    = ImVec4(0.26f, 0.59f, 0.98f, 0.50f);
    c[ImGuiCol_ResizeGripActive]     = ImVec4(0.26f, 0.59f, 0.98f, 0.80f);
    c[ImGuiCol_TabHovered]           = ImVec4(0.26f, 0.59f, 0.98f, 0.60f);
    c[ImGuiCol_Tab]                  = ImVec4(0.18f, 0.35f, 0.58f, 0.86f);
    c[ImGuiCol_TabSelected]          = ImVec4(0.20f, 0.41f, 0.68f, 1.00f);
    c[ImGuiCol_TabSelectedOverline]  = ImVec4(0.26f, 0.59f, 0.98f, 1.00f);
    c[ImGuiCol_TabDimmed]            = ImVec4(0.07f, 0.10f, 0.15f, 0.97f);
    c[ImGuiCol_TabDimmedSelected]    = ImVec4(0.14f, 0.26f, 0.42f, 1.00f);
    c[ImGuiCol_PlotLines]            = ImVec4(0.61f, 0.61f, 0.61f, 1.00f);
    c[ImGuiCol_PlotLinesHovered]     = ImVec4(1.00f, 0.43f, 0.35f, 1.00f);
    c[ImGuiCol_PlotHistogram]        = ImVec4(0.90f, 0.70f, 0.00f, 1.00f);
    c[ImGuiCol_PlotHistogramHovered] = ImVec4(1.00f, 0.60f, 0.00f, 1.00f);
    c[ImGuiCol_TableHeaderBg]        = ImVec4(0.19f, 0.19f, 0.20f, 1.00f);
    c[ImGuiCol_TableBorderStrong]    = ImVec4(0.31f, 0.31f, 0.35f, 1.00f);
    c[ImGuiCol_TableBorderLight]     = ImVec4(0.23f, 0.23f, 0.25f, 1.00f);
    c[ImGuiCol_TableRowBg]           = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    c[ImGuiCol_TableRowBgAlt]        = ImVec4(1.00f, 1.00f, 1.00f, 0.04f);
    c[ImGuiCol_TextSelectedBg]       = ImVec4(0.26f, 0.59f, 0.98f, 0.25f);
    c[ImGuiCol_DragDropTarget]       = ImVec4(1.00f, 1.00f, 0.00f, 0.90f);
    c[ImGuiCol_NavCursor]            = ImVec4(0.26f, 0.59f, 0.98f, 1.00f);
    c[ImGuiCol_NavWindowingHighlight]= ImVec4(1.00f, 1.00f, 1.00f, 0.70f);
    c[ImGuiCol_NavWindowingDimBg]    = ImVec4(0.80f, 0.80f, 0.80f, 0.20f);
    c[ImGuiCol_ModalWindowDimBg]     = ImVec4(0.80f, 0.80f, 0.80f, 0.35f);
}

}  // namespace ls
