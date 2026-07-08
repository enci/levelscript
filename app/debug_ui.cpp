#include "debug_ui.hpp"
#include "debug_ui_internal.hpp"
#include "phosphor_icons.hpp"
#include "platform_titlebar.hpp"
#include "parser.hpp"
#include "diagnostic.hpp"

#include <SDL3/SDL.h>
#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_sdlrenderer3.h>
#include <imgui_freetype.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <cstdio>
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
    std::ifstream ifs(sc.path);
    if (!ifs) {
        sc.status     = "Cannot open: " + sc.path;
        sc.full_error = sc.status + "\n";
        sc.ok         = false;
        return false;
    }
    std::ostringstream buf;
    buf << ifs.rdbuf();
    std::string src = buf.str();

    // Execution path: the public API compile. This generator is the only
    // thing that ever runs the script.
    generator gen = generator::compile(src, sc.path);
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
    diagnostics diags;
    auto        ast = parse(src, sc.path, diags);
    compiled    meta;
    if (!ast || !analyze(*ast, meta, diags, sc.path)) {
        // The public compile succeeded over the same source, so this is
        // effectively unreachable; degrade with a status line regardless.
        sc.full_error = diags.format_all();
        sc.status     = "Metadata analysis failed";
        sc.ok         = false;
        return false;
    }

    sc.gen    = std::move(gen);
    sc.ast    = std::move(*ast);
    sc.meta   = std::move(meta);
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

static void set_light_theme();
static void set_dark_theme();

// ── entry point ───────────────────────────────────────────────────────────────

int run_debug_ui(std::string const& path, std::optional<uint64_t> fixed_seed) {
    script sc;
    sc.path = path;
    if (!load_script(sc)) {
        std::fprintf(stderr, "%s", sc.full_error.c_str());
        return 1;
    }

    project_config cfg = project_config::load(path, sc.layer_names());

    // ── prefs (theme + window geometry + seed) -- SDL's per-user location ────
    SDL_Init(SDL_INIT_VIDEO);
    char* pref_raw = SDL_GetPrefPath("levelscript", "lsd");
    std::string pref_dir = pref_raw ? pref_raw : "";
    if (pref_raw) SDL_free(pref_raw);
    std::string ini_path   = pref_dir + "lsd.ini";
    std::string prefs_path = pref_dir + "lsd_prefs.json";

    int win_w = 1280, win_h = 800;
    std::optional<int> win_x, win_y;
    std::string theme_str        = "system";
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
                theme_str     = gets("theme", "system");
                win_w         = (int)getn("win_w", 1280);
                win_h         = (int)getn("win_h", 800);
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
    // this public-API generation; Reset constructs a fresh one via begin().
    debug_run run;
    run.restart(sc.gen, seed);

    // ── SDL3 window ───────────────────────────────────────────────────────────
    SDL_Window* window = SDL_CreateWindow(
        ("LevelScript - " + path).c_str(), win_w, win_h,
        SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
    if (win_x && win_y) SDL_SetWindowPosition(window, *win_x, *win_y);

    SDL_Renderer* renderer = SDL_CreateRenderer(window, nullptr);
    SDL_SetRenderVSync(renderer, 1);

    tile_texture tile_tex;
    if (cfg.tileset)
        tile_tex.sync(renderer, resolve_path(path, cfg.tileset->path));

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
        io.Fonts->AddFontFromFileTTF(text_font.c_str(), 14.0f, nullptr, nullptr);
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
    // Do NOT call ScaleAllSizes here -- SDL_SetRenderScale already maps logical
    // pixels to physical pixels, so ScaleAllSizes would double-scale everything.
    ImGui_ImplSDL3_InitForSDLRenderer(window, renderer);
    ImGui_ImplSDLRenderer3_Init(renderer);

    // ── theme ─────────────────────────────────────────────────────────────────
    enum class theme_mode { system, light, dark };
    theme_mode cur_theme_mode = theme_mode::system;
    if      (theme_str == "light") cur_theme_mode = theme_mode::light;
    else if (theme_str == "dark")  cur_theme_mode = theme_mode::dark;
    bool dark_theme = true;

    auto apply_theme = [&] {
        if (dark_theme) set_dark_theme(); else set_light_theme();
        // Tint the OS title bar / window border to blend with the app. lsd has
        // no menu bar, so match the dominant window background surface.
        const ImVec4& bg = ImGui::GetStyle().Colors[ImGuiCol_WindowBg];
        ls::platform::set_titlebar(window, bg.x, bg.y, bg.z, dark_theme);
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
    // Step and Next Statement are genuinely distinct on the new API:
    // one pulled application event vs. looping to the statement boundary.
    auto action_step      = [&] { run.step_once(); };
    auto action_next_stmt = [&] { run.next_statement(); };
    auto action_run       = [&] { run.run_all(); };
    auto action_reset     = [&] {
        if (!seed_locked) seed = make_seed();
        // Reload from disk so edits made in an external editor are picked up;
        // on failure the previous compile stays live and the status bar shows
        // the first diagnostic.
        if (load_script(sc)) cfg.sync_layers(sc.layer_names());
        run.restart(sc.gen, seed);
    };

    // ── main loop ─────────────────────────────────────────────────────────────
#ifdef LS_ENABLE_IMGUI_DEMO
    bool show_demo = false;
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
            switch (ev.key.scancode) {
                case SDL_SCANCODE_F10: action_step();      break;
                case SDL_SCANCODE_F11: action_next_stmt(); break;
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
        if (playing || redraw_frames > 0) {
            while (SDL_PollEvent(&ev)) { handle_event(ev); had_event = true; }
        } else if (SDL_WaitEvent(&ev)) {
            handle_event(ev);
            had_event = true;
            while (SDL_PollEvent(&ev)) handle_event(ev);
        }
        if (had_event) redraw_frames = k_cooldown_frames;
        else if (redraw_frames > 0) redraw_frames--;

        // Timed playback -- advance one application per tick at play_fps.
        if (playing) {
            if (run.done) {
                playing = false;
            } else {
                auto now = clock::now();
                float elapsed = std::chrono::duration<float>(now - last_advance).count();
                if (elapsed >= 1.f / play_fps) {
                    action_step();
                    last_advance = now;
                }
            }
        }

        // Sync tileset texture if path changed via UI.
        if (cfg.tileset)
            tile_tex.sync(renderer, resolve_path(path, cfg.tileset->path));

        ImGui_ImplSDLRenderer3_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();

        ImGuiViewport* viewport = ImGui::GetMainViewport();
        // A fixed-height, no-scrollbar window clips its content unless the
        // height also covers ImGui's own window padding (top + bottom).
        float bar_pad      = ImGui::GetStyle().WindowPadding.y * 2.f;
        float statusbar_h  = ImGui::GetFrameHeight() + bar_pad;
        float controlbar_h = ImGui::GetFrameHeight() + bar_pad;
        viewport->WorkPos.y  += controlbar_h;   // reserve a top strip for the control bar
        viewport->WorkSize.y -= controlbar_h + statusbar_h;   // + a bottom strip for the status bar
        ImGui::DockSpaceOverViewport(0, viewport);

        // ── control bar (pinned to the top, like the status bar at the bottom) ──
        {
            ImGui::SetNextWindowPos(viewport->Pos);
            ImGui::SetNextWindowSize({ viewport->Size.x, controlbar_h });
            ImGui::Begin("##ControlBar", nullptr, k_bar_flags);

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

            if (toolbtn(phosphor::PH_CARET_RIGHT, "Step (F10) - one application"))
                action_step();
            ImGui::SameLine();
            if (toolbtn(phosphor::PH_SKIP_FORWARD, "Next Statement (F11)"))
                action_next_stmt();
            ImGui::SameLine();
            if (toolbtn(phosphor::PH_FAST_FORWARD, "Run"))
                action_run();
            ImGui::SameLine();
            if (toolbtn(phosphor::PH_ARROW_COUNTER_CLOCKWISE, "Reset (R)"))
                action_reset();
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

            ImGui::Text("|");
            ImGui::SameLine();
            if (toolbtn(playing ? phosphor::PH_PAUSE : phosphor::PH_PLAY,
                        playing ? "Pause (Space)" : "Play (Space)", playing)) {
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
        ImGui::Begin("VIEWPORT");
        {
            // Leave room at the bottom for the layer strip.
            ImGui::BeginChild("##grid", {0.f, -40.f}, false,
                              ImGuiWindowFlags_HorizontalScrollbar);
            draw_grid_composite(sc, run, cfg, tile_tex, cfg.cell_px);
            ImGui::EndChild();

            ImGui::BeginChild("##layer_strip", {0.f, 0.f}, false,
                              ImGuiWindowFlags_HorizontalScrollbar);
            draw_layer_strip(cfg, sc.meta);
            ImGui::EndChild();
        }
        ImGui::End();

#ifdef LS_ENABLE_IMGUI_DEMO
        if (show_demo) ImGui::ShowDemoWindow(&show_demo);
#endif

        ImGui::Begin("TAGS");
        draw_tags_window(sc.meta, cfg);
        ImGui::End();

        ImGui::Begin("RULE");
        draw_rule_window(sc, run, cfg.mini_px, cfg);
        ImGui::End();

        ImGui::Begin("PROGRAM");
        draw_program_window(sc, run);
        ImGui::End();

        ImGui::Begin("SETTINGS");
        draw_settings_window(cfg, tile_tex, renderer, path);
        ImGui::End();

        // ── status bar ──────────────────────────────────────────────────────
        {
            ImGui::SetNextWindowPos({ viewport->Pos.x,
                                      viewport->Pos.y + viewport->Size.y - statusbar_h });
            ImGui::SetNextWindowSize({ viewport->Size.x, statusbar_h });
            ImGui::Begin("##StatusBar", nullptr, k_bar_flags);

            std::string status = std::string(phosphor::PH_STEPS) + " stmt "
                + std::to_string(run.current_stmt()) + "/"
                + std::to_string(run.stmt_count);
            if (run.started && run.apps_in_stmt > 0)
                status += "   apps=" + std::to_string(run.apps_in_stmt);
            ImGui::TextDisabled("%s", status.c_str());
            if (run.done) { ImGui::SameLine(); ImGui::TextDisabled("[done]"); }

            ImGui::SameLine(0.f, 24.f);
            ImGui::TextDisabled("%s", phosphor::PH_DICE_FIVE);
            ImGui::SameLine();
            char seed_hex[24];
            std::snprintf(seed_hex, sizeof(seed_hex), "%08llX",
                          (unsigned long long)seed);
            ImGui::TextDisabled("%s", seed_hex);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Click to copy");
            if (ImGui::IsItemClicked()) ImGui::SetClipboardText(seed_hex);

            if (!sc.status.empty()) {
                ImGui::SameLine(0.f, 24.f);
                if (sc.ok) {
                    ImGui::TextDisabled("%s", phosphor::PH_CHECK_CIRCLE);
                    ImGui::SameLine();
                    ImGui::TextDisabled("%s", sc.status.c_str());
                } else {
                    ImGui::TextColored({1.f, 0.4f, 0.4f, 1.f}, "%s",
                                       phosphor::PH_WARNING_CIRCLE);
                    ImGui::SameLine();
                    ImGui::TextColored({1.f, 0.4f, 0.4f, 1.f}, "%s",
                                       sc.status.c_str());
                }
            }
            ImGui::End();
        }

        // ── render ────────────────────────────────────────────────────────────
        ImGui::Render();
        SDL_SetRenderScale(renderer, io.DisplayFramebufferScale.x,
                           io.DisplayFramebufferScale.y);
        const auto& bg = ImGui::GetStyle().Colors[ImGuiCol_WindowBg];
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

// ── theme palettes ────────────────────────────────────────────────────────────

static void set_light_theme() {
    auto& style = ImGui::GetStyle();
    style.FrameRounding = 5.0f;
    style.ChildRounding = 5.0f;
    style.GrabRounding  = 5.0f;
    ImVec4* c = style.Colors;
    c[ImGuiCol_Text]                 = ImVec4(0.15f, 0.15f, 0.15f, 1.00f);
    c[ImGuiCol_TextDisabled]         = ImVec4(0.50f, 0.50f, 0.50f, 1.00f);
    c[ImGuiCol_WindowBg]             = ImVec4(0.90f, 0.90f, 0.91f, 1.00f);
    c[ImGuiCol_ChildBg]              = ImVec4(0.00f, 0.00f, 0.00f, 0.05f);
    c[ImGuiCol_PopupBg]              = ImVec4(0.95f, 0.95f, 0.96f, 0.98f);
    c[ImGuiCol_Border]               = ImVec4(0.70f, 0.70f, 0.72f, 0.40f);
    c[ImGuiCol_BorderShadow]         = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    c[ImGuiCol_FrameBg]              = ImVec4(0.82f, 0.82f, 0.83f, 1.00f);
    c[ImGuiCol_FrameBgHovered]       = ImVec4(0.75f, 0.75f, 0.77f, 1.00f);
    c[ImGuiCol_FrameBgActive]        = ImVec4(0.68f, 0.68f, 0.70f, 1.00f);
    c[ImGuiCol_TitleBg]              = ImVec4(0.82f, 0.82f, 0.83f, 1.00f);
    c[ImGuiCol_TitleBgActive]        = ImVec4(0.75f, 0.75f, 0.77f, 1.00f);
    c[ImGuiCol_TitleBgCollapsed]     = ImVec4(0.90f, 0.90f, 0.91f, 0.75f);
    c[ImGuiCol_MenuBarBg]            = ImVec4(0.86f, 0.86f, 0.87f, 1.00f);
    c[ImGuiCol_ScrollbarBg]          = ImVec4(0.86f, 0.86f, 0.87f, 1.00f);
    c[ImGuiCol_ScrollbarGrab]        = ImVec4(0.70f, 0.70f, 0.72f, 1.00f);
    c[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.60f, 0.60f, 0.62f, 1.00f);
    c[ImGuiCol_ScrollbarGrabActive]  = ImVec4(0.50f, 0.50f, 0.52f, 1.00f);
    c[ImGuiCol_CheckMark]            = ImVec4(0.26f, 0.59f, 0.98f, 1.00f);
    c[ImGuiCol_SliderGrab]           = ImVec4(0.55f, 0.55f, 0.57f, 1.00f);
    c[ImGuiCol_SliderGrabActive]     = ImVec4(0.45f, 0.45f, 0.47f, 1.00f);
    c[ImGuiCol_Button]               = ImVec4(0.78f, 0.78f, 0.80f, 1.00f);
    c[ImGuiCol_ButtonHovered]        = ImVec4(0.70f, 0.70f, 0.72f, 1.00f);
    c[ImGuiCol_ButtonActive]         = ImVec4(0.62f, 0.62f, 0.64f, 1.00f);
    c[ImGuiCol_Header]               = ImVec4(0.26f, 0.59f, 0.98f, 0.25f);
    c[ImGuiCol_HeaderHovered]        = ImVec4(0.26f, 0.59f, 0.98f, 0.50f);
    c[ImGuiCol_HeaderActive]         = ImVec4(0.26f, 0.59f, 0.98f, 0.70f);
    c[ImGuiCol_Separator]            = ImVec4(0.70f, 0.70f, 0.72f, 0.50f);
    c[ImGuiCol_SeparatorHovered]     = ImVec4(0.26f, 0.59f, 0.98f, 0.60f);
    c[ImGuiCol_SeparatorActive]      = ImVec4(0.26f, 0.59f, 0.98f, 0.80f);
    c[ImGuiCol_ResizeGrip]           = ImVec4(0.26f, 0.59f, 0.98f, 0.20f);
    c[ImGuiCol_ResizeGripHovered]    = ImVec4(0.26f, 0.59f, 0.98f, 0.50f);
    c[ImGuiCol_ResizeGripActive]     = ImVec4(0.26f, 0.59f, 0.98f, 0.80f);
    c[ImGuiCol_TabHovered]           = ImVec4(0.26f, 0.59f, 0.98f, 0.60f);
    c[ImGuiCol_Tab]                  = ImVec4(0.78f, 0.78f, 0.80f, 0.86f);
    c[ImGuiCol_TabSelected]          = ImVec4(0.70f, 0.70f, 0.72f, 1.00f);
    c[ImGuiCol_TabSelectedOverline]  = ImVec4(0.26f, 0.59f, 0.98f, 1.00f);
    c[ImGuiCol_TabDimmed]            = ImVec4(0.86f, 0.86f, 0.87f, 0.97f);
    c[ImGuiCol_TabDimmedSelected]    = ImVec4(0.78f, 0.78f, 0.80f, 1.00f);
    c[ImGuiCol_PlotLines]            = ImVec4(0.39f, 0.39f, 0.39f, 1.00f);
    c[ImGuiCol_PlotLinesHovered]     = ImVec4(1.00f, 0.43f, 0.35f, 1.00f);
    c[ImGuiCol_PlotHistogram]        = ImVec4(0.90f, 0.70f, 0.00f, 1.00f);
    c[ImGuiCol_PlotHistogramHovered] = ImVec4(1.00f, 0.60f, 0.00f, 1.00f);
    c[ImGuiCol_TableHeaderBg]        = ImVec4(0.78f, 0.78f, 0.80f, 1.00f);
    c[ImGuiCol_TableBorderStrong]    = ImVec4(0.70f, 0.70f, 0.72f, 1.00f);
    c[ImGuiCol_TableBorderLight]     = ImVec4(0.78f, 0.78f, 0.80f, 1.00f);
    c[ImGuiCol_TableRowBg]           = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    c[ImGuiCol_TableRowBgAlt]        = ImVec4(0.00f, 0.00f, 0.00f, 0.04f);
    c[ImGuiCol_TextSelectedBg]       = ImVec4(0.26f, 0.59f, 0.98f, 0.25f);
    c[ImGuiCol_DragDropTarget]       = ImVec4(1.00f, 1.00f, 0.00f, 0.90f);
    c[ImGuiCol_NavCursor]            = ImVec4(0.26f, 0.59f, 0.98f, 1.00f);
    c[ImGuiCol_NavWindowingHighlight]= ImVec4(1.00f, 1.00f, 1.00f, 0.70f);
    c[ImGuiCol_NavWindowingDimBg]    = ImVec4(0.80f, 0.80f, 0.80f, 0.20f);
    c[ImGuiCol_ModalWindowDimBg]     = ImVec4(0.20f, 0.20f, 0.20f, 0.35f);
}

static void set_dark_theme() {
    auto& style = ImGui::GetStyle();
    style.FrameRounding = 5.0f;
    style.ChildRounding = 5.0f;
    style.GrabRounding  = 5.0f;
    ImVec4* c = style.Colors;
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
