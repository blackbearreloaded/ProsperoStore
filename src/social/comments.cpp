// ProsperoStore - Likes and comments: bounded parsing of files nobody signed.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include "social/comments.hpp"
#include "third_party/yyjson/yyjson.h"
#include <cmath>
#include <memory>

namespace store::social
{
namespace
{
struct Close
{
    void operator()(yyjson_doc *document) const
    {
        yyjson_doc_free(document);
    }
};
using Document = std::unique_ptr<yyjson_doc, Close>;

// The root object of a schema 1 file, or null.
yyjson_val *root(std::string_view body, std::size_t limit, Document &document)
{
    if (body.empty() || body.size() > limit)
        return nullptr;
    document.reset(yyjson_read(body.data(), body.size(), 0));
    auto *value = document ? yyjson_doc_get_root(document.get()) : nullptr;
    if (!value || !yyjson_is_obj(value))
        return nullptr;
    auto *schema = yyjson_obj_get(value, "schema");
    return schema && yyjson_is_uint(schema) && yyjson_get_uint(schema) == 1 ? value : nullptr;
}

// A whole number of likes or comments: anything else, or less than none, is none.
std::uint32_t count(yyjson_val *object, const char *key)
{
    auto *value = yyjson_obj_get(object, key);
    if (!value)
        return 0;
    if (yyjson_is_uint(value))
        return static_cast<std::uint32_t>(std::min<std::uint64_t>(yyjson_get_uint(value), kMost));
    if (yyjson_is_real(value))
    {
        const double number = yyjson_get_real(value);
        if (std::isfinite(number) && number >= 1.0)
            return number >= static_cast<double>(kMost) ? kMost
                                                        : static_cast<std::uint32_t>(number);
    }
    return 0;
}

std::string_view words(yyjson_val *object, const char *key)
{
    auto *value = yyjson_obj_get(object, key);
    if (!value || !yyjson_is_str(value))
        return {};
    return {yyjson_get_str(value), yyjson_get_len(value)};
}

Counts counts(yyjson_val *object)
{
    Counts result;
    result.likes = count(object, "likes");
    result.comments = count(object, "comments");
    if (const auto url = words(object, "url"); discussion_url(url))
        result.url = url;
    return result;
}

// Characters that draw nothing but change how text around them is laid out or read.
bool formatting(std::uint32_t c)
{
    return c == 0xad || c == 0x34f || c == 0x61c || (c >= 0x115f && c <= 0x1160) ||
           (c >= 0x17b4 && c <= 0x17b5) || (c >= 0x180b && c <= 0x180f) ||
           (c >= 0x200b && c <= 0x200f) || (c >= 0x202a && c <= 0x202e) ||
           (c >= 0x2060 && c <= 0x206f) || c == 0x3164 || (c >= 0xfe00 && c <= 0xfe0f) ||
           c == 0xfeff || c == 0xffa0 || (c >= 0xfff0 && c <= 0xfffb) ||
           (c >= 0x1bca0 && c <= 0x1bca3) || (c >= 0x1d173 && c <= 0x1d17a) ||
           (c >= 0xe0000 && c <= 0xe0fff);
}
bool blank(std::uint32_t c)
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == 0x0b || c == 0x0c || c == 0x85 ||
           c == 0xa0 || c == 0x1680 || (c >= 0x2000 && c <= 0x200a) || c == 0x2028 || c == 0x2029 ||
           c == 0x202f || c == 0x205f || c == 0x3000;
}
} // namespace

std::string plain(std::string_view text, std::size_t limit)
{
    std::string out;
    std::size_t characters = 0;
    bool gap = false;
    for (std::size_t at = 0; at < text.size() && characters < limit;)
    {
        // One well-formed UTF-8 character, shortest form only; any other byte is skipped.
        const auto lead = static_cast<unsigned char>(text[at]);
        const std::size_t length = lead < 0x80       ? 1
                                   : lead >> 5 == 6  ? 2
                                   : lead >> 4 == 14 ? 3
                                   : lead >> 3 == 30 ? 4
                                                     : 0;
        if (length == 0 || at + length > text.size())
        {
            ++at;
            continue;
        }
        std::uint32_t c = length == 1 ? lead : lead & (0xffu >> (length + 1));
        bool valid = true;
        for (std::size_t k = 1; k < length; ++k)
        {
            const auto next = static_cast<unsigned char>(text[at + k]);
            valid = valid && next >> 6 == 2;
            c = (c << 6) | (next & 0x3f);
        }
        constexpr std::uint32_t least[] = {0, 0, 0x80, 0x800, 0x10000};
        if (!valid || c < least[length] || c > 0x10ffff || (c >= 0xd800 && c <= 0xdfff))
        {
            ++at;
            continue;
        }
        const auto bytes = text.substr(at, length);
        at += length;
        if (blank(c))
        {
            gap = !out.empty();
            continue;
        }
        if (c < 0x20 || (c >= 0x7f && c < 0xa0) || formatting(c))
            continue;
        if (gap)
        {
            if (++characters >= limit)
                break;
            out += ' ';
            gap = false;
        }
        out.append(bytes);
        ++characters;
    }
    return out;
}

bool parse_index(std::string_view body, Index &out)
{
    Document document;
    auto *value = root(body, kIndexLimit, document);
    auto *apps = value ? yyjson_obj_get(value, "apps") : nullptr;
    if (!apps || !yyjson_is_obj(apps))
        return false;
    Index index;
    std::size_t at, last;
    yyjson_val *key, *app;
    yyjson_obj_foreach(apps, at, last, key, app)
    {
        const std::string_view id(yyjson_get_str(key), yyjson_get_len(key));
        if (index.size() >= kApps)
            break;
        if (title_id(id) && yyjson_is_obj(app))
            index.insert_or_assign(std::string(id), counts(app));
    }
    out = std::move(index);
    return true;
}

bool parse_thread(std::string_view body, std::string_view id, Thread &out)
{
    Document document;
    auto *value = root(body, kThreadLimit, document);
    if (!value || !title_id(id) || words(value, "titleid") != id)
        return false;
    Thread thread;
    thread.counts = counts(value);
    if (auto *latest = yyjson_obj_get(value, "latest"); latest && yyjson_is_arr(latest))
    {
        std::size_t at, last;
        yyjson_val *item;
        yyjson_arr_foreach(latest, at, last, item)
        {
            if (thread.latest.size() >= kComments)
                break;
            if (!yyjson_is_obj(item))
                continue;
            Comment comment;
            comment.text = plain(words(item, "text"), kTextLimit);
            if (comment.text.empty())
                continue;
            comment.author = plain(words(item, "author"), kAuthorLimit);
            if (comment.author.empty())
                comment.author = "someone";
            // The day alone, and only when it is written as one.
            const auto date = words(item, "date");
            const auto digits = [&](std::size_t from, std::size_t length)
            {
                return std::all_of(date.begin() + static_cast<std::ptrdiff_t>(from),
                                   date.begin() + static_cast<std::ptrdiff_t>(from + length),
                                   [](char c) { return c >= '0' && c <= '9'; });
            };
            if (date.size() >= 10 && digits(0, 4) && date[4] == '-' && digits(5, 2) &&
                date[7] == '-' && digits(8, 2))
                comment.date = date.substr(0, 10);
            thread.latest.push_back(std::move(comment));
        }
    }
    out = std::move(thread);
    return true;
}

std::vector<std::string> most_liked(std::vector<Ranked> apps, std::size_t limit)
{
    std::erase_if(apps, [](const Ranked &app) { return app.likes == 0; });
    std::stable_sort(apps.begin(), apps.end(), [](const Ranked &a, const Ranked &b)
                     { return more_liked(a.likes, a.name, b.likes, b.name); });
    if (apps.size() > limit)
        apps.resize(limit);
    std::vector<std::string> ids;
    for (auto &app : apps)
        ids.push_back(std::move(app.id));
    return ids;
}
} // namespace store::social
