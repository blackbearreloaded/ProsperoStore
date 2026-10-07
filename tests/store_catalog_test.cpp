// ProsperoStore - Trust, parser, identity and URL regression checks.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include "catalog/catalog.hpp"
#include "third_party/monocypher/monocypher-ed25519.h"

#include <cassert>
#include <iostream>

using namespace store::catalog;

int main()
{
    assert(title_id("PPSA99000") && !title_id("PPSA99/00") && !title_id("../PPSA99"));
    assert(update_available("01.000.009", "01.000.010"));
    assert(!update_available("01.000.010", "01.000.010"));
    assert(!update_available("02.000.000", "01.999.999"));
    assert(!update_available("unknown", "01.000.000"));
    assert(!update_available("1.000.000", "01.000.000"));
    assert(sha256("abc") == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    assert(api_url("https://homebrew.page/api/v1/index.json"));
    std::string base = "unchanged";
    assert(normalize_api(" https://dev.example:8443/api/v1 ", base));
    assert(base == "https://dev.example:8443/api/v1/");
    assert(normalize_api("", base) && base == kDefaultApi);
    for (const auto *bad :
         {"http://dev.example/api/v1/", "file:///tmp/feed/", "https://user@dev.example/api/",
          "https://dev.example:0/api/", "https://dev.example:65536/api/",
          "https://dev.example:/api/", "https://dev.example/api/?q=x", "https://dev.example/api/#x",
          "https://dev.example/api/\nverify_signatures=0"})
    {
        assert(!normalize_api(bad, base));
        assert(base == kDefaultApi);
    }
    assert(!normalize_api("https://dev.example/" + std::string(512, 'a'), base));
    assert(artifact_url("https://github.com/owner/repo/releases/download/v1/a.zip"));
    assert(!artifact_url("https://release-assets.githubusercontent.com/a"));
    assert(artifact_url("https://release-assets.githubusercontent.com/a", true));
    for (const char *url :
         {"http://github.com/a", "https://github.com.evil/a", "https://github.com@evil/a",
          "https://evil/github.com/a", "https://github.com:443/a", "https://github.com/\r\na"})
        assert(!artifact_url(url, true));
    const std::string index =
        R"({"schema":3,"apps":[{"titleid":"PPSA99000","name":"ProsperoStore","kind":"app","status":"coming_soon","content_version":null,"unknown":{"future":true}}]})";
    std::vector<Entry> entries;
    std::string error;
    assert(parse_index(index, entries, error) && entries.size() == 1);
    for (const char *bad :
         {"{}", R"({"schema":3,"schema":3,"apps":[]})", R"({"schema":4,"apps":[]})",
          R"({"schema":3,"apps":[{"titleid":"../../etc"}]})"})
        assert(!parse_index(bad, entries, error));
    assert(entries.size() == 1); // Failed refresh never replaces the last good catalog.
    Entry detail;
    const std::string full_icon =
        R"({"schema":3,"titleid":"PPSA99000","name":"Store","kind":"app","status":"coming_soon","icon":"https://homebrew.page/icons/PPSA99000.png"})";
    assert(parse_detail(full_icon, "PPSA99000", detail, error));
    assert(detail.large_icon == "https://homebrew.page/icons/PPSA99000.png");
    auto bad_icon = full_icon;
    bad_icon.replace(bad_icon.find("https://homebrew.page"), 21, "https://untrusted.example");
    assert(parse_detail(bad_icon, "PPSA99000", detail, error)); // Custom HTTPS icon host.
    bad_icon.replace(bad_icon.find("https://"), 8, "http://");
    assert(!parse_detail(bad_icon, "PPSA99000", detail, error));
    std::map<std::string, std::string> versions;
    assert(parse_versions(R"({"schema":3,"apps":{"PPSA99000":{"content_version":null}}})", versions,
                          error));
    assert(versions.at("PPSA99000").empty());
    assert(!parse_versions(R"({"schema":3,"apps":{"PPSA99000":{"content_version":"latest"}}})",
                           versions, error));

    PublicKey key{};
    std::array<std::uint8_t, 32> seed{};
    std::array<std::uint8_t, 64> secret{};
    crypto_ed25519_key_pair(secret.data(), key.data(), seed.data());
    const std::string manifest =
        "{\"schema\":3,\"sequence\":8,\"commit\":\"fixture\",\"files\":{\"index.json\":\"" +
        sha256(index) + "\",\"versions.json\":\"" + sha256("versions") + "\"}}";
    std::string signature(64, '\0');
    crypto_ed25519_sign(reinterpret_cast<std::uint8_t *>(signature.data()), secret.data(),
                        reinterpret_cast<const std::uint8_t *>(manifest.data()), manifest.size());
    Manifest verified;
    const std::array<PublicKey, 1> keys{key};
    assert(verify_manifest(manifest, signature, 8, keys, verified, error));
    assert(verified.verifies("index.json", index));
    assert(!verified.verifies("index.json", index + " "));
    assert(!verified.verifies("../index.json", index));
    assert(!verify_manifest(manifest, signature, 9, keys, verified, error));
    assert(!verify_manifest(manifest + " ", signature, 0, keys, verified, error));
    assert(!verify_manifest(manifest, signature, 0, public_keys(), verified, error));
    signature[0] ^= 1;
    assert(!verify_manifest(manifest, signature, 0, keys, verified, error));
    assert(verified.sequence == 8);
    std::cout << "Catalog trust and parser checks passed\n";
}
