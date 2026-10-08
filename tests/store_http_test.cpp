// ProsperoStore - Redirect, cancellation and conditional-response trust boundaries.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later
#include "net/http.hpp"
#include <cassert>
#include <vector>

using namespace store::net;
std::vector<Response> responses;
std::vector<std::string> calls;
namespace store::net
{
Response request_once(const std::string &url, std::uint64_t, const Sink &sink, Control &,
                      const std::string &)
{
    calls.push_back(url);
    assert(calls.size() <= responses.size());
    const auto response = responses[calls.size() - 1];
    if (response.status == 200)
        assert(sink("body"));
    return response;
}
} // namespace store::net
Response reply(int status, const std::string &location = {})
{
    Response response;
    response.status = status;
    response.location = location;
    return response;
}
int main()
{
    Response headers;
    assert(read_headers("HTTP/1.1 302 Found\r\nLocation: /next\r\nETag: \"abc\"\r\n\r\n", headers));
    assert(headers.location == "/next" && headers.etag == "\"abc\"");
    assert(!read_headers("Location: /a\r\nlocation: /b\r\n", headers));
    assert(!read_headers("ETag: a\r\nETAG: b\r\n", headers));
    assert(!read_headers("Location: /a\rb\n", headers));
    assert(!read_headers(std::string(65537, 'x'), headers));
    Control control;
    std::string body = "old";
    const std::string url = "https://github.com/owner/repo/releases/download/v1/app.zip";
    responses = {reply(302, "https://evil.example/payload")};
    assert(!fetch(url, Purpose::artifact, 100, body, control).ok());
    assert(calls.size() == 1 && body == "old");
    calls.clear();
    responses = {reply(302, "http://github.com/insecure")};
    assert(!fetch(url, Purpose::artifact, 100, body, control).ok());
    assert(calls.size() == 1);
    calls.clear();
    responses = {reply(302, "https://release-assets.githubusercontent.com/asset"), reply(200)};
    assert(fetch(url, Purpose::artifact, 100, body, control).ok() && body == "body");
    assert(calls.size() == 2);
    calls.clear();
    responses.assign(6, reply(302, "/loop"));
    assert(!fetch(url, Purpose::artifact, 100, body, control).ok() && calls.size() == 6);
    calls.clear();
    responses = {reply(304)};
    body = "preserved";
    assert(!fetch(url, Purpose::artifact, 100, body, control).ok() && body == "preserved");
    calls.clear();
    assert(fetch(url, Purpose::artifact, 100, body, control, "\"tag\"").ok() &&
           body == "preserved");
    calls.clear();
    assert(!fetch(url, Purpose::artifact, 100, body, control, "bad\r\nheader").ok() &&
           calls.empty());
    responses = {reply(302, "/api/v1/index.json"), reply(200)};
    assert(fetch("https://dev.example:8443/feed/", Purpose::catalog, 100, body, control).ok());
    assert(calls.back() == "https://dev.example:8443/api/v1/index.json");
    calls.clear();
    responses = {reply(302, "http://dev.example/api/v1/index.json")};
    assert(!fetch("https://dev.example/feed/", Purpose::catalog, 100, body, control).ok());
    assert(calls.size() == 1);
    calls.clear();
    control.cancelled.store(true);
    assert(!fetch(url, Purpose::artifact, 100, body, control).ok() && calls.empty());
}
