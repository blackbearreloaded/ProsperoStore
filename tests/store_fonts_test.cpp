// ProsperoStore - The baked fonts draw app names written in European languages.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include "gfx/font.hpp"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>

namespace
{

void expect(bool condition, const std::string &message)
{
    if (!condition)
    {
        std::fprintf(stderr, "FAILED: %s\n", message.c_str());
        std::exit(1);
    }
}

hui::gfx::Font load(const std::string &name)
{
    std::ifstream input(std::string(STORE_FONTS) + "/" + name + ".huifont", std::ios::binary);
    const std::string data((std::istreambuf_iterator<char>(input)),
                           std::istreambuf_iterator<char>());
    hui::gfx::Font font;
    expect(font.load(data), name + " loads");
    return font;
}

bool draws(const hui::gfx::Font &font, std::string_view text)
{
    for (std::size_t index = 0; index < text.size();)
    {
        if (!font.has_glyph(hui::gfx::next_codepoint(text, &index)))
            return false;
    }
    return true;
}

} // namespace

int main()
{
    // French, German, Spanish, Portuguese, Polish, Czech, Turkish, Nordic, Greek, Russian.
    const char *latin[] = {"R\u00e9gie5",
                           "Stra\u00dfe \u00dcbung",
                           "Ni\u00f1o \u00bfQu\u00e9?",
                           "A\u00e7\u00e3o Jo\u00e3o",
                           "Za\u017c\u00f3\u0142\u0107 g\u0119\u015bl\u0105",
                           "\u0158e\u0161en\u00ed \u017elu\u0165ou\u010dk\u00fd",
                           "\u0130stanbul \u011f\u015f\u0131",
                           "Sm\u00f8rg\u00e5s \u00c6ble",
                           "\u201cQuoted\u201d \u20ac5 \u2122"};
    const char *greek = "\u0395\u03bb\u03bb\u03b7\u03bd\u03b9\u03ba\u03ac";
    const char *russian = "\u041f\u0440\u0438\u0432\u0435\u0442 \u0401\u0436";

    // The faces every app name, author and description is set in.
    for (const char *name : {"inter-regular", "inter-semibold"})
    {
        const hui::gfx::Font font = load(name);
        for (const char *text : latin)
            expect(draws(font, text), std::string(name) + " draws " + text);
        expect(draws(font, greek), std::string(name) + " draws Greek");
        expect(draws(font, russian), std::string(name) + " draws Russian");
    }

    // The headline face: accented Latin and Cyrillic, and no Greek, which is why
    // the store sets a Greek name in the title face.
    const hui::gfx::Font display = load("montserrat-medium");
    for (const char *text : latin)
        expect(draws(display, text), std::string("montserrat-medium draws ") + text);
    expect(draws(display, russian), "montserrat-medium draws Russian");
    expect(!draws(display, greek), "montserrat-medium has no Greek");

    // An accented name is as wide as its letters, not as wide as its question marks.
    const hui::gfx::Font title = load("inter-semibold");
    expect(title.measure("R\u00e9gie5", 24) == title.measure("Regie5", 24),
           "an accent adds no width");
    expect(title.fit("R\u00e9gie5", 24, 1000.0f) == "R\u00e9gie5",
           "a name that fits is kept whole");

    for (const char *name : {"dejavu-sans-mono", "press-start-2p", "patrick-hand"})
        expect(draws(load(name), "R\u00e9gie5"), std::string(name) + " draws an accented name");
    std::puts("Font coverage checks passed.");
    return 0;
}
