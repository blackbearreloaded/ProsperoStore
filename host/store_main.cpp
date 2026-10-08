// ProsperoStore - Host renderer for inspecting the same screens as the console.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include "app/ambient.hpp"
#include "app/keyboard_map.hpp"
#include "app/store.hpp"
#include "catalog/client.hpp"
#include "catalog/icons.hpp"
#include "core/image.hpp"
#include "core/qr.hpp"
#include "core/save_file.hpp"
#include "gfx/gl_program.hpp"

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>
#include <sstream>
#include <cassert>
#include <set>
#include <vector>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wmissing-field-initializers"
#include "../third_party/stb/stb_image_write.h"
#pragma clang diagnostic pop

static void check_artwork_requests()
{
    store::Screen screen;
    assert(screen.artwork().empty());
    std::vector<store::App> apps;
    for (unsigned i = 0; i < 100; ++i)
    {
        store::App app;
        app.title_id = "PPSA" + std::to_string(99000 + i);
        char name[16];
        std::snprintf(name, sizeof(name), "App %03u", i);
        app.name = name;
        app.kind = "app";
        apps.push_back(app);
    }
    screen.set_catalog(std::move(apps), "test");
    hui::ui::Feedback feedback;
    // Discover: down from "New and updated" to the Apps shelf, then along it.
    hui::InputFrame down, right;
    down.nav = hui::Direction::down;
    right.nav = hui::Direction::right;
    screen.update(down, 1.0f / 60.0f, feedback);
    for (unsigned i = 0; i < 20; ++i)
    {
        const auto wanted = screen.artwork();
        assert(!wanted.empty() && wanted.size() <= 16);
        assert(std::set<std::string>(wanted.begin(), wanted.end()).size() == wanted.size());
        screen.set_icon(wanted.front(), 1);
        screen.set_ambient(wanted.front(), 2);
        assert(screen.artwork() == wanted); // Uploading artwork must never reset focus.
        screen.update(right, 1.0f / 60.0f, feedback);
    }
    const auto focused = screen.artwork().front();
    assert(focused == "PPSA99020");
    hui::InputFrame confirm;
    confirm.pressed = hui::action_bit(hui::Action::confirm);
    screen.update(confirm, 1.0f / 60.0f, feedback);
    assert(screen.artwork() == std::vector<std::string>{focused});
    screen.set_catalog({}, "empty");
    assert(screen.artwork().empty());
}

static void check_search_and_sort()
{
    store::Screen screen;
    store::App a, b, c;
    a.title_id = "PPSA99001";
    a.name = "Zulu";
    a.author = "Bear Studio";
    a.released = "2026-01-01";
    a.updated = "2026-10-01";
    b.title_id = "PPSA99002";
    b.name = "alpha";
    b.author = "Another Developer";
    b.released = "2026-09-01";
    b.updated = "2026-09-01";
    c.title_id = "PPSA99003";
    c.name = "Coming Soon";
    a.kind = b.kind = c.kind = "app";
    screen.set_catalog({a, b, c}, "test");
    {
        // Sorting and search work on a section's grid: go to Apps.
        hui::InputFrame next;
        next.pressed = hui::action_bit(hui::Action::page_next);
        hui::ui::Feedback quiet;
        screen.update(next, 1.0f / 60.0f, quiet);
    }
    assert(screen.artwork().front() == b.title_id);
    screen.set_query("STUDIO");
    assert(screen.artwork() == std::vector<std::string>{a.title_id});
    screen.set_query("ALpHa");
    assert(screen.artwork() == std::vector<std::string>{b.title_id});
    screen.set_query("no such app");
    assert(screen.artwork().empty());
    screen.set_query("");
    hui::InputFrame sort;
    sort.pressed = hui::action_bit(hui::Action::r3);
    hui::ui::Feedback feedback;
    screen.update(sort, 1.0f / 60.0f, feedback);
    assert(screen.artwork().front() == b.title_id); // Newest release.
    screen.update(sort, 1.0f / 60.0f, feedback);
    assert(screen.artwork().front() == a.title_id); // Recently updated.
    screen.set_catalog({c, b, a}, "refreshed");
    assert(screen.artwork().front() == a.title_id);
    screen.update(sort, 1.0f / 60.0f, feedback);
    assert(screen.artwork().front() == b.title_id);
    hui::InputFrame search;
    search.pressed = hui::action_bit(hui::Action::north);
    screen.update(search, 1.0f / 60.0f, feedback);
    assert(screen.pending_search);
    screen.pending_search = false;
    hui::InputFrame open;
    open.pressed = hui::action_bit(hui::Action::confirm);
    screen.update(open, 1.0f / 60.0f, feedback);
    assert(screen.pending_detail == b.title_id);
    screen.set_detail_error(b.title_id, "offline");
    screen.pending_detail.clear();
    screen.update(search, 1.0f / 60.0f, feedback);
    assert(screen.pending_detail == b.title_id && !screen.pending_search);
    store::catalog::Entry detail;
    detail.id = b.title_id;
    detail.description = "Verified description";
    screen.set_detail(detail);
    screen.set_catalog({}, "removed");
    assert(screen.artwork().empty());
    screen.update(search, 1.0f / 60.0f, feedback);
    assert(screen.pending_search);
}

static void check_installed_sections()
{
    store::Screen screen;
    store::App app;
    app.title_id = "PPSA99010";
    app.name = "Example";
    app.available_version = "02.000.000";
    screen.set_catalog({app}, "verified", true);
    store::system::Inventory inventory;
    store::system::InstalledApp installed;
    installed.id = app.title_id;
    installed.name = app.name;
    installed.version = "01.000.000";
    installed.path = "/data/homebrew/" + app.title_id;
    installed.managed = true;
    inventory.apps.push_back(installed);
    screen.set_inventory(inventory);
    hui::InputFrame section;
    section.pressed = hui::action_bit(hui::Action::page_next);
    hui::ui::Feedback feedback;
    for (int index = 0; index < 5; ++index)
        screen.update(section, 1.0f / 60.0f, feedback);
    assert(screen.artwork() == std::vector<std::string>{app.title_id});
    screen.update(section, 1.0f / 60.0f, feedback);
    assert(screen.artwork() == std::vector<std::string>{app.title_id});
    // Update all: Square asks for the details, then orders the update.
    screen.set_installer(true, true, "", "/data/homebrew");
    screen.set_running({}, true);
    hui::InputFrame square;
    square.pressed = hui::action_bit(hui::Action::west);
    screen.update(square, 1.0f / 60.0f, feedback);
    screen.update({}, 1.0f / 60.0f, feedback);
    assert(screen.pending_detail == app.title_id);
    assert(screen.pending_order.kind == store::Order::Kind::none);
    store::catalog::Entry listed;
    listed.id = app.title_id;
    listed.format = "zip";
    listed.digest = std::string(64, 'a');
    screen.set_detail(listed);
    screen.update({}, 1.0f / 60.0f, feedback);
    assert(screen.pending_order.kind == store::Order::Kind::install);
    assert(screen.pending_order.entry.digest == listed.digest);
    assert(screen.pending_order.location == "/data/homebrew");
    screen.pending_order = {};
    screen.pending_detail.clear();
    // A running app is passed over.
    screen.set_running({app.title_id}, true);
    screen.update(square, 1.0f / 60.0f, feedback);
    screen.update({}, 1.0f / 60.0f, feedback);
    assert(screen.pending_order.kind == store::Order::Kind::none);
    screen.set_running({}, true);
    inventory.apps.front().managed = false;
    screen.set_inventory(inventory);
    assert(screen.artwork().empty());
}

static void check_keyboard_keys()
{
    using namespace hui::pad_bits;
    const auto press = [](std::initializer_list<std::uint16_t> keys, std::uint32_t modifiers = 0)
    {
        const std::vector<std::uint16_t> held(keys);
        return store::keyboard_buttons(held, modifiers);
    };
    assert(press({store::hid::kUp}) == kUp && press({store::hid::kLeft}) == kLeft);
    assert(press({store::hid::kEnter}) == kCross && press({store::hid::kSpace}) == kCross);
    assert(press({store::hid::kEscape}) == kCircle && press({store::hid::kBackspace}) == kCircle);
    assert(press({store::hid::kTab}) == kR1 && press({store::hid::kTab}, 0x02) == kL1);
    assert(press({store::hid::kPageDown}) == kR1 && press({store::hid::kPageUp}) == kL1);
    assert(press({store::hid::kSlash}) == kTriangle && press({store::hid::kDelete}) == kSquare);
    assert(press({store::hid::kF10}) == kOptions && press({store::hid::kF5}) == kR3);
    assert(press({store::hid::kDown, store::hid::kEnter}) == (kDown | kCross));
    assert(press({0x04 /* A */}) == 0 && press({}) == 0);
}

static void check_hold_to_uninstall()
{
    store::Screen screen;
    store::App app;
    app.title_id = "PPSA99010";
    app.name = "Example";
    app.kind = "app";
    app.available_version = "01.000.000";
    screen.set_catalog({app}, "verified", true);
    store::system::Inventory inventory;
    store::system::InstalledApp installed;
    installed.id = app.title_id;
    installed.name = app.name;
    installed.version = "01.000.000";
    installed.path = "/data/homebrew/" + app.title_id;
    installed.managed = true;
    inventory.apps.push_back(installed);
    screen.set_inventory(inventory);
    screen.set_installer(true, true, "", "/data/homebrew");
    screen.set_running({}, true);
    assert(screen.open_app(app.title_id));
    store::catalog::Entry listed;
    listed.id = app.title_id;
    listed.version = "01.000.000";
    listed.content_version = "01.000.000";
    listed.format = "zip";
    listed.digest = std::string(64, 'a');
    screen.set_detail(listed);
    hui::ui::Feedback feedback;
    for (int frame = 0; frame < 30; ++frame)
        screen.update({}, 1.0f / 60.0f, feedback);
    // A tap does nothing but say how.
    hui::InputFrame tap;
    tap.pressed = hui::action_bit(hui::Action::confirm);
    tap.held = tap.pressed;
    screen.update(tap, 1.0f / 60.0f, feedback);
    hui::InputFrame up;
    up.released = hui::action_bit(hui::Action::confirm);
    screen.update(up, 1.0f / 60.0f, feedback);
    assert(screen.pending_order.kind == store::Order::Kind::none);
    // Letting go halfway does nothing either.
    hui::InputFrame hold;
    hold.held = hui::action_bit(hui::Action::confirm);
    screen.update(tap, 1.0f / 60.0f, feedback);
    for (int frame = 0; frame < 30; ++frame)
        screen.update(hold, 1.0f / 60.0f, feedback);
    screen.update(up, 1.0f / 60.0f, feedback);
    assert(screen.pending_order.kind == store::Order::Kind::none);
    for (int frame = 0; frame < 120; ++frame)
        screen.update({}, 1.0f / 60.0f, feedback);
    // A full hold orders the uninstall.
    screen.update(tap, 1.0f / 60.0f, feedback);
    for (int frame = 0; frame < 90 && screen.pending_order.kind == store::Order::Kind::none; ++frame)
        screen.update(hold, 1.0f / 60.0f, feedback);
    assert(screen.pending_order.kind == store::Order::Kind::uninstall);

    store::Settings settings;
    settings.reduce_motion = true;
    assert(store::parse_settings(store::format_settings(settings)).reduce_motion);
    assert(!store::parse_settings("location=/data/homebrew\n").reduce_motion);
}

static void check_catalog_settings()
{
    using namespace store;
    assert(parse_settings("").verify_signatures);
    Settings settings;
    settings.catalog_url = "https://dev.example/api/v1/";
    settings.verify_signatures = false;
    const auto saved = parse_settings(format_settings(settings));
    assert(saved.catalog_url == settings.catalog_url && !saved.verify_signatures);
    assert(parse_settings("catalog_url=http://bad/\nverify_signatures=oops\n").verify_signatures);
    assert(parse_settings("catalog_url=http://bad/\n").catalog_url == catalog::kDefaultApi);
    Screen screen;
    screen.open_panel(1);
    hui::ui::Feedback feedback;
    hui::InputFrame down, up, confirm, back, right;
    down.nav = hui::Direction::down;
    up.nav = hui::Direction::up;
    right.nav = hui::Direction::right;
    confirm.pressed = hui::action_bit(hui::Action::confirm);
    back.pressed = hui::action_bit(hui::Action::back);
    for (int i = 0; i < 6; ++i)
        screen.update(down, 0.016f, feedback);
    screen.update(confirm, 0.016f, feedback); // Development options: Official catalog.
    // The official catalog can't go while no custom one is in use.
    screen.update(confirm, 0.016f, feedback);
    assert(screen.settings().use_official && !screen.settings().use_custom);
    // Custom catalog on: with no address yet, the store asks for one.
    screen.update(down, 0.016f, feedback);
    screen.update(confirm, 0.016f, feedback);
    assert(screen.settings().use_custom && screen.pending_catalog_url);
    assert(screen.settings().use_official); // not active yet: the official one stays
    screen.pending_catalog_url = false;
    screen.set_catalog_url("https://dev.example/api/v1");
    assert(screen.settings().catalog_url == settings.catalog_url && screen.settings_changed);
    screen.set_catalog_url("http://bad/");
    assert(screen.settings().catalog_url == settings.catalog_url);
    assert(screen.settings().custom_active() && screen.settings().use_official); // both
    // Now the official catalog can be switched off (custom only), and on again.
    screen.update(up, 0.016f, feedback);
    screen.update(confirm, 0.016f, feedback);
    assert(!screen.settings().use_official);
    screen.update(confirm, 0.016f, feedback);
    assert(screen.settings().use_official);
    // The address row asks again.
    screen.update(down, 0.016f, feedback);
    screen.update(down, 0.016f, feedback);
    screen.update(confirm, 0.016f, feedback);
    assert(screen.pending_catalog_url);
    screen.pending_catalog_url = false;
    // Signature checks: asking is not consent.
    screen.update(down, 0.016f, feedback);
    screen.update(confirm, 0.016f, feedback);
    assert(screen.settings().verify_signatures);
    screen.update(back, 0.016f, feedback);
    assert(screen.settings().verify_signatures);
    screen.update(confirm, 0.016f, feedback);
    screen.update(right, 0.016f, feedback);
    screen.update(confirm, 0.016f, feedback);
    assert(!screen.settings().verify_signatures);
    // Restore: the official catalog alone, checked.
    screen.update(down, 0.016f, feedback);
    screen.update(confirm, 0.016f, feedback);
    assert(screen.settings().verify_signatures && screen.settings().catalog_url == catalog::kDefaultApi);
    assert(screen.settings().use_official && !screen.settings().use_custom);
    // The two switches survive the settings file, and a file can't switch both off.
    Settings both;
    both.catalog_url = "https://dev.example/api/v1/";
    both.use_custom = true;
    both.use_official = false;
    const auto kept = parse_settings(format_settings(both));
    assert(kept.use_custom && !kept.use_official && kept.custom_active());
    assert(parse_settings("use_official=0\nuse_custom=0\n").use_official);
    assert(parse_settings("use_official=0\nuse_custom=1\n").use_official); // no address: not active
}

int main(int argc, char **argv)
{
    check_artwork_requests();
    check_search_and_sort();
    check_installed_sections();
    check_hold_to_uninstall();
    check_keyboard_keys();
    check_catalog_settings();
    if (argc < 3 || argc > 5)
        return 2;
    const auto get_display = reinterpret_cast<PFNEGLGETPLATFORMDISPLAYEXTPROC>(
        eglGetProcAddress("eglGetPlatformDisplayEXT"));
    EGLDisplay display =
        get_display ? get_display(EGL_PLATFORM_SURFACELESS_MESA, EGL_DEFAULT_DISPLAY, nullptr)
                    : eglGetDisplay(EGL_DEFAULT_DISPLAY);
    EGLint major = 0;
    EGLint minor = 0;
    if (!eglInitialize(display, &major, &minor) || !eglBindAPI(EGL_OPENGL_API))
        return 3;
    const EGLint attributes[] = {EGL_CONTEXT_MAJOR_VERSION,
                                 4,
                                 EGL_CONTEXT_MINOR_VERSION,
                                 5,
                                 EGL_CONTEXT_OPENGL_PROFILE_MASK,
                                 EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT,
                                 EGL_NONE};
    EGLContext context = eglCreateContext(display, EGL_NO_CONFIG_KHR, EGL_NO_CONTEXT, attributes);
    if (context == EGL_NO_CONTEXT ||
        !eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, context))
        return 4;
    {
        hui::gfx::set_glsl_prefix("#version 450 core\n");
        hui::gfx::Renderer renderer;
        store::Fonts fonts;
        hui::gfx::Canvas target;
        if (!renderer.init() || !fonts.load(renderer, argv[1]) || !target.create(1920, 1080, 1))
            return 5;
        store::Screen screen;
        // Settings previews do not need a live catalog or cached downloads.
        if (argc == 4 && (std::string(argv[3]) == "settings" || std::string(argv[3]) == "catalog-settings"))
        {
            screen.open_panel(1);
            hui::ui::Feedback quiet;
            hui::InputFrame down;
            down.nav = hui::Direction::down;
            if (std::string(argv[3]) == "catalog-settings")
            {
                for (int i = 0; i < 6; ++i)
                    screen.update(down, 0.016f, quiet);
                hui::InputFrame confirm;
                confirm.pressed = hui::action_bit(hui::Action::confirm);
                screen.update(confirm, 0.016f, quiet);
            }
        }
        std::vector<GLuint> textures;
        {
            std::string encoded;
            hui::Image image;
            if (hui::save::read_file(std::string(argv[1]) + "/images/coming-soon.png", &encoded) &&
                hui::decode_png(encoded, image))
            {
                textures.push_back(
                    renderer.batch().create_texture(image.width, image.height, image.rgba.data()));
                screen.set_coming_soon_art(textures.back());
            }
        }
        if (argc >= 4 && std::string(argv[3]) != "settings" && std::string(argv[3]) != "catalog-settings")
        {
            store::catalog::Client catalog(argv[3]);
            store::catalog::Snapshot snapshot;
            std::string error;
            if (!catalog.cached(snapshot, error))
            {
                std::fprintf(stderr, "%s\n", error.c_str());
                return 8;
            }
            std::vector<store::App> apps;
            for (const auto &entry : snapshot.entries)
            {
                apps.push_back({entry.id, entry.name, entry.author, entry.description, entry.kind,
                                entry.version, entry.status == "coming_soon" ? "Coming soon" : "",
                                0, entry.released, entry.updated});
                apps.back().available_version = entry.content_version;
                apps.back().size = entry.size;
            }
            screen.set_catalog(std::move(apps), "Verified catalog");
            store::catalog::Icons icons(std::string(argv[3]) + "/icons");
            store::net::Control control;
            for (const auto &entry : snapshot.entries)
            {
                hui::Image image;
                if (!icons.cached(entry, image))
                {
                    std::string encoded;
                    const auto response = store::net::fetch(
                        entry.icon, store::net::Purpose::catalog, 2u << 20, encoded, control);
                    if (!response.ok() || !icons.store(entry, encoded, image))
                        continue;
                }
                const auto texture =
                    renderer.batch().create_texture(image.width, image.height, image.rgba.data());
                textures.push_back(texture);
                screen.set_icon(entry.id, texture);
                // Its own picture, made from the icon, and the colour the screen leans toward.
                std::uint32_t vivid = 0;
                const auto ambient = store::make_ambient(image, vivid);
                if (!ambient.rgba.empty())
                {
                    const auto field = renderer.batch().create_texture(ambient.width, ambient.height,
                                                                       ambient.rgba.data());
                    textures.push_back(field);
                    screen.set_ambient(entry.id, field);
                }
                screen.set_accent(entry.id, hui::gfx::Color::rgb(vivid ? vivid : store::average_colour(image)));
            }
            // The verified details the stage would ask for, from the cache.
            for (const auto &entry : snapshot.entries)
            {
                store::catalog::Entry detail;
                std::string why;
                if (catalog.detail(snapshot, entry.id, detail, control, why))
                    screen.set_detail(detail);
            }
        }
        hui::ui::Feedback feedback;
        if (argc == 5)
        {
            const std::string mode = argv[4];
            hui::InputFrame down, right, nav_up;
            down.nav = hui::Direction::down;
            right.nav = hui::Direction::right;
            nav_up.nav = hui::Direction::up;
            const auto settle = [&](int frames)
            {
                for (int frame = 0; frame < frames; ++frame)
                    screen.update({}, 1.0f / 60.0f, feedback);
            };
            if (mode == "stage")
            {
                // The featured app on the stage, the focus on its button.
                screen.update(nav_up, 1.0f / 60.0f, feedback);
                settle(30);
            }
            else if (mode == "shelf" || mode == "shelf-2" || mode == "shelf-3")
            {
                const int downs = mode == "shelf" ? 0 : mode == "shelf-2" ? 1 : 2;
                for (int i = 0; i < downs; ++i)
                {
                    screen.update(down, 1.0f / 60.0f, feedback);
                    settle(20);
                }
                for (int i = 0; i < 2; ++i)
                {
                    screen.update(right, 1.0f / 60.0f, feedback);
                    settle(20);
                }
            }
            else if (mode == "games" || mode == "apps")
            {
                hui::InputFrame section;
                section.pressed = hui::action_bit(hui::Action::page_next);
                for (int index = 0; index < (mode == "apps" ? 1 : 2); ++index)
                    screen.update(section, 1.0f / 60.0f, feedback);
                settle(30);
                screen.update(right, 1.0f / 60.0f, feedback);
                settle(20);
            }
            else if (mode == "page-light" || mode == "page-installing" || mode == "page-unpacking" ||
                     mode == "page-installed" || mode == "page-eden")
            {
                screen.set_installer(true, true, "", "/data/homebrew");
                screen.set_running({}, true);
                screen.set_locations({{"/data/homebrew", 548ull << 30}});
                const std::string id = mode == "page-eden"        ? "PPSA99008"
                                       : mode == "page-installed" ? "PPSA99006"
                                       : mode == "page-light"     ? "PPSA99002"
                                                                  : "PPSA99169";
                if (mode == "page-installed")
                {
                    store::system::Inventory inventory;
                    inventory.apps = {{id, "ProsperoPuzzles", "01.000.010", "/data/homebrew/" + id,
                                       "", false, true, false}};
                    screen.set_inventory(std::move(inventory));
                }
                assert(screen.open_app(id));
                screen.pending_detail.clear();
                hui::Image qr;
                assert(hui::encode_qr("https://homebrew.page/app/" + id + "/", qr));
                const auto texture = renderer.batch().create_texture(qr.width, qr.height, qr.rgba.data());
                textures.push_back(texture);
                screen.set_qr(id, texture, qr.width);
                if (mode == "page-installing" || mode == "page-unpacking")
                {
                    const std::uint64_t total = mode == "page-installing" ? 197867360ull : 412000000ull;
                    const int phase = mode == "page-installing" ? 1 : 3;
                    screen.set_activity({id, phase, total / 5, total, {"PPSA99420"}});
                    settle(60);
                    screen.set_activity({id, phase, total * 43 / 100, total, {"PPSA99420"}});
                }
            }
            else if (mode == "reel-install")
            {
                screen.set_installer(true, true, "", "/data/homebrew");
                screen.set_running({}, true);
                screen.set_locations({{"/data/homebrew", 548ull << 30}});
                assert(screen.open_app("PPSA99169"));
                screen.pending_detail.clear();
                hui::Image qr;
                assert(hui::encode_qr("https://homebrew.page/app/PPSA99169/", qr));
                const auto texture = renderer.batch().create_texture(qr.width, qr.height, qr.rgba.data());
                textures.push_back(texture);
                screen.set_qr("PPSA99169", texture, qr.width);
            }
            else if (mode == "reel-browse")
            {
            }
            else if (mode == "reel-intro")
                screen.play_intro();
            else if (mode == "busy-home")
            {
                screen.set_installer(true, true, "", "/data/homebrew");
                screen.set_activity({"PPSA99169", 1, 197867360ull / 5, 197867360ull, {"PPSA99420"}});
                settle(60);
                screen.set_activity({"PPSA99169", 1, 197867360ull * 43 / 100, 197867360ull, {"PPSA99420"}});
                for (int i = 0; i < 1; ++i)
                {
                    screen.update(right, 1.0f / 60.0f, feedback);
                    settle(20);
                }
            }
            else if (mode == "search")
                screen.set_query("radio");
            else if (mode == "busy")
            {
                // An install in progress, seen from the grid and the top bar.
                screen.set_installer(true, true, "", "/data/homebrew");
                screen.set_activity({"PPSA99007", 1, 30u << 20, 58u << 20, {"PPSA99420"}});
            }
            else if (mode == "queue" || mode == "downloads" || mode == "settings" || mode == "about")
            {
                screen.set_installer(true, true, "", "/data/homebrew");
                screen.set_self("PPSA99000", "01.000.000");
                screen.set_locations({{"/data/homebrew", 548ull << 30},
                                      {"/mnt/ext1/homebrew", 912ull << 30}});
                if (mode == "queue" || mode == "downloads")
                {
                    screen.set_activity({"PPSA99007", 3, 61u << 20, 94u << 20,
                                         {"PPSA99420", "PPSA99169"}});
                    screen.finish_job(true, false, "ProsperoRadio installed",
                                      "ShadowMountPlus will add it to your home screen in a moment.");
                    screen.finish_job(false, false, "PS5SX2: not changed", "Close the app first");
                }
                screen.open_panel(mode == "queue" || mode == "downloads" ? 0 : mode == "settings" ? 1 : 2);
                if (mode == "downloads")
                    settle(700);
                // Settings: down to the location, right to the next one, saved once.
                if (mode == "settings")
                {
                    hui::InputFrame right;
                    right.nav = hui::Direction::right;
                    screen.update(right, 1.0f / 60.0f, feedback);
                    assert(screen.settings().location == "/mnt/ext1/homebrew");
                    assert(screen.settings_changed);
                    assert(store::parse_settings(store::format_settings(screen.settings())).location ==
                           "/mnt/ext1/homebrew");
                    screen.update(right, 1.0f / 60.0f, feedback);
                    assert(screen.settings().location == "/data/homebrew");
                    assert(!store::parse_settings("location=../x\nsounds=0\n").sounds);
                    assert(store::parse_settings("location=../x\n").location == "/data/homebrew");
                }
                if (mode == "queue")
                {
                    // Cross on the first row cancels the running job.
                    hui::InputFrame cross;
                    cross.pressed = hui::action_bit(hui::Action::confirm);
                    screen.update(cross, 1.0f / 60.0f, feedback);
                    assert(screen.pending_order.kind == store::Order::Kind::cancel);
                    assert(screen.pending_order.id == "PPSA99007");
                    screen.pending_order = {};
                }
            }
            else if (mode == "notice")
                screen.notify("Update available: 01.000.010",
                              "A newer ProsperoStore is listed on homebrew.page.");
            else if (mode == "scrolled" || mode == "coming-soon")
            {
                hui::InputFrame move;
                move.nav = hui::Direction::down;
                move.pressed = mode == "scrolled" ? 0 : hui::action_bit(hui::Action::page_next);
                for (int index = 0; index < (mode == "scrolled" ? 2 : 4); ++index)
                    screen.update(move.pressed ? hui::InputFrame{.pressed = move.pressed} : move,
                                  1.0f / 60.0f, feedback);
            }
            else if (mode == "empty")
                screen.set_query("no matching application");
            else if (mode == "updated")
            {
                hui::InputFrame sort;
                sort.pressed = hui::action_bit(hui::Action::r3);
                screen.update(sort, 1.0f / 60.0f, feedback);
                screen.update(sort, 1.0f / 60.0f, feedback);
            }
            else if (mode == "installed" || mode == "updates" || mode == "local-detail")
            {
                store::system::Inventory inventory;
                inventory.apps = {
                    {"PPSA99001", "ProsperoRadio", "01.000.000", "/data/homebrew/PPSA99001", "",
                     false, true, false},
                    {"PPSA99002", "ProsperoLight", "01.000.000", "/data/homebrew/PPSA99002",
                     "Installed outside ProsperoStore. Not managed by this app.", false, false,
                     false},
                    {"PPSA99980", "Example local app", "01.000.001", "/mnt/ext1/homebrew/PPSA99980",
                     "Installed outside ProsperoStore. Not managed by this app.", false, false,
                     false},
                    {"", "Example.ffpkg", "", "/mnt/ext1/homebrew/Example.ffpkg",
                     "Image installed outside ProsperoStore. Not managed by this app.", true, false,
                     false}};
                screen.set_inventory(std::move(inventory));
                screen.set_status("Installed library preview");
                hui::InputFrame section;
                section.pressed = hui::action_bit(hui::Action::page_next);
                for (int index = 0; index < (mode == "updates" ? 6 : 5); ++index)
                    screen.update(section, 1.0f / 60.0f, feedback);
                if (mode == "local-detail")
                {
                    screen.set_query("Example local");
                    hui::InputFrame open;
                    open.pressed = hui::action_bit(hui::Action::confirm);
                    screen.update(open, 1.0f / 60.0f, feedback);
                    assert(screen.pending_detail.empty());
                }
            }
            else if (mode == "detail" || mode == "detail-error" || mode == "detail-end" ||
                     mode == "detail-armed" || mode == "detail-progress" || mode == "confirm" ||
                     mode == "adopt")
            {
                if (mode == "detail-armed" || mode == "detail-progress" || mode == "confirm" ||
                    mode == "adopt")
                    screen.set_installer(true, true, "", "/data/homebrew");
                if (mode == "adopt")
                {
                    // Installed by hand, listed in the catalog: it can be handed over.
                    store::system::Inventory inventory;
                    inventory.apps = {{"PPSA99001", "ProsperoRadio", "01.000.005",
                                       "/data/homebrew/PPSA99001",
                                       "Installed outside ProsperoStore. Not managed by this app.",
                                       false, false, false}};
                    screen.set_inventory(std::move(inventory));
                }
                if (mode == "confirm")
                {
                    store::system::Inventory inventory;
                    inventory.apps = {{"PPSA99001", "ProsperoRadio", "01.000.000",
                                       "/data/homebrew/PPSA99001", "", false, true, false}};
                    screen.set_inventory(std::move(inventory));
                }
                screen.set_query("radio");
                hui::InputFrame open;
                open.pressed = hui::action_bit(hui::Action::confirm);
                screen.update(open, 1.0f / 60.0f, feedback);
                assert(!screen.pending_detail.empty());
                if (mode == "detail-error")
                    screen.set_detail_error(screen.pending_detail, "The network is unavailable.");
                else
                {
                    store::catalog::Client client(argv[3]);
                    store::catalog::Snapshot snapshot;
                    store::catalog::Entry entry;
                    store::net::Control control;
                    std::string error;
                    assert(client.cached(snapshot, error));
                    assert(client.detail(snapshot, screen.pending_detail, entry, control, error));
                    screen.set_detail(entry);
                    hui::Image qr;
                    assert(hui::encode_qr("https://homebrew.page/app/" + entry.id + "/", qr));
                    const auto texture =
                        renderer.batch().create_texture(qr.width, qr.height, qr.rgba.data());
                    assert(texture);
                    textures.push_back(texture);
                    screen.set_qr(entry.id, texture, qr.width);
                    if (mode == "detail-progress")
                    {
                        // Two samples a second apart give the page a speed, and so a time left.
                        screen.set_activity({entry.id, 1, entry.size / 5, entry.size, {}});
                        for (int frame = 0; frame < 60; ++frame)
                            screen.update({}, 1.0f / 60.0f, feedback);
                        screen.set_activity({entry.id, 1, entry.size * 2 / 5, entry.size, {}});
                    }
                    if (mode == "detail-armed")
                    {
                        // Cross asks for the install of exactly this verified record.
                        screen.update(open, 1.0f / 60.0f, feedback);
                        assert(screen.pending_order.kind == store::Order::Kind::install);
                        assert(screen.pending_order.entry.digest == entry.digest);
                        assert(screen.pending_order.location == "/data/homebrew");
                        screen.pending_order = {};
                    }
                    if (mode == "adopt")
                    {
                        // Cross asks first; the answer orders the hand-over, nothing else.
                        screen.update(open, 1.0f / 60.0f, feedback);
                        assert(screen.pending_order.kind == store::Order::Kind::none);
                    }
                    if (mode == "confirm")
                    {
                        // Square asks first; nothing is ordered until the answer.
                        hui::InputFrame square;
                        square.pressed = hui::action_bit(hui::Action::west);
                        screen.update(square, 1.0f / 60.0f, feedback);
                        assert(screen.pending_order.kind == store::Order::Kind::none);
                    }
                }
            }
            else
                return 9;
        }
        for (int frame = 0; frame < 120; ++frame)
            screen.update({}, 1.0f / 60.0f, feedback);
        if (const char *reel = std::getenv("STORE_REEL"); reel && argc == 5)
        {
            // A clip: presses at given frames, every second frame written as a
            // 960x540 JPEG; the installer's progress is simulated for "reel-install".
            const std::string scene = argv[4];
            std::vector<std::pair<int, std::string>> steps;
            int frames = 0;
            if (scene == "reel-browse")
            {
                steps = {{20, "up"},     {150, "right"}, {230, "down"},  {300, "right"},
                         {360, "right"}, {420, "right"}, {490, "down"},  {550, "right"},
                         {610, "right"}, {680, "confirm"}, {860, "back"}, {960, "r1"},
                         {1030, "r1"},  {1100, "right"}};
                frames = 1190;
            }
            else if (scene == "reel-install")
            {
                steps = {{40, "confirm"}};
                frames = 760;
            }
            else if (scene == "reel-intro")
            {
                frames = 300;
                screen.play_intro(); // after the warm-up frames, so the clip starts at zero
            }
            const auto press = [](const std::string &name)
            {
                hui::InputFrame input;
                if (name == "up" || name == "down" || name == "left" || name == "right")
                    input.nav = name == "up"     ? hui::Direction::up
                                : name == "down" ? hui::Direction::down
                                : name == "left" ? hui::Direction::left
                                                 : hui::Direction::right;
                else if (name == "confirm")
                    input.pressed = hui::action_bit(hui::Action::confirm);
                else if (name == "back")
                    input.pressed = hui::action_bit(hui::Action::back);
                else if (name == "r1")
                    input.pressed = hui::action_bit(hui::Action::page_next);
                return input;
            };
            std::vector<unsigned char> pixels(1920 * 1080 * 4), half(960 * 540 * 3);
            stbi_flip_vertically_on_write(1);
            const std::string id = "PPSA99169";
            const std::uint64_t total = 197867360ull, unpacked = 412000000ull;
            for (int frame = 0; frame < frames; ++frame)
            {
                hui::InputFrame input;
                for (const auto &[at, name] : steps)
                    if (at == frame)
                        input = press(name);
                if (scene == "reel-install")
                {
                    // Download 4 s, verify 0.5 s, unpack 2.5 s, finish 0.4 s, then installed.
                    const int start = 60;
                    const int f = frame - start;
                    if (f >= 0 && f < 240)
                        screen.set_activity({id, 1, total * static_cast<std::uint64_t>(f) / 240, total, {}});
                    else if (f >= 240 && f < 270)
                        screen.set_activity({id, 2, 0, 0, {}});
                    else if (f >= 270 && f < 420)
                        screen.set_activity({id, 3, unpacked * static_cast<std::uint64_t>(f - 270) / 150,
                                             unpacked, {}});
                    else if (f >= 420 && f < 444)
                        screen.set_activity({id, 4, 0, 0, {}});
                    else if (f == 444)
                    {
                        screen.set_activity({});
                        store::system::Inventory inventory;
                        inventory.apps = {{id, "RetroArch", "01.000.000", "/data/homebrew/" + id, "",
                                           false, true, false}};
                        screen.set_inventory(std::move(inventory));
                        screen.finish_job(true, false, "RetroArch installed",
                                          "ShadowMountPlus adds it to your home screen in a moment.");
                    }
                }
                screen.update(input, 1.0f / 60.0f, feedback);
                if (frame % 2)
                    continue;
                screen.draw(renderer, fonts.refs);
                renderer.present(target.framebuffer(), 1920, 1080);
                glBindFramebuffer(GL_FRAMEBUFFER, target.framebuffer());
                glReadPixels(0, 0, 1920, 1080, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
                for (int y = 0; y < 540; ++y)
                    for (int x = 0; x < 960; ++x)
                        for (int c = 0; c < 3; ++c)
                        {
                            const auto at = [&](int dx, int dy)
                            { return pixels[((2 * y + dy) * 1920 + (2 * x + dx)) * 4 + c]; };
                            half[(y * 960 + x) * 3 + c] = static_cast<unsigned char>(
                                (at(0, 0) + at(1, 0) + at(0, 1) + at(1, 1) + 2) / 4);
                        }
                char name[512];
                std::snprintf(name, sizeof(name), "%s/%05d.jpg", reel, frame / 2);
                if (!stbi_write_jpg(name, 960, 540, 3, half.data(), 90))
                    return 6;
            }
            std::printf("Reel: %d frames in %s\n", frames / 2, reel);
            glDeleteTextures(static_cast<GLsizei>(textures.size()), textures.data());
            eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
            eglDestroyContext(display, context);
            eglTerminate(display);
            return 0;
        }
        screen.draw(renderer, fonts.refs);
        if (argc == 5 && std::string(argv[4]) == "detail-end")
        {
            hui::InputFrame scroll;
            scroll.nav = hui::Direction::down;
            for (int frame = 0; frame < 120; ++frame)
                screen.update(scroll, 1.0f / 60.0f, feedback);
            screen.draw(renderer, fonts.refs);
        }
        renderer.present(target.framebuffer(), 1920, 1080);
        glBindFramebuffer(GL_FRAMEBUFFER, target.framebuffer());
        std::vector<unsigned char> pixels(1920 * 1080 * 4);
        glReadPixels(0, 0, 1920, 1080, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
        stbi_flip_vertically_on_write(1);
        if (!stbi_write_png(argv[2], 1920, 1080, 4, pixels.data(), 1920 * 4))
            return 6;
        const auto error = glGetError();
        std::printf("Snapshot: %s GL=0x%x draws=%zu\n", argv[2], error, renderer.last_draw_calls());
        if (error != GL_NO_ERROR)
            return 7;
        glDeleteTextures(static_cast<GLsizei>(textures.size()), textures.data());
    }
    eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    eglDestroyContext(display, context);
    eglTerminate(display);
    return 0;
}
