// ps5-homebrew-ui - Polling system keyboard; never waits for user input.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later
#include "platform/ps5/ime.hpp"
#include "platform/ps5/ime_abi.hpp"
#include "platform/ps5/system.hpp"
#include "gfx/font.hpp"
#include <span>
#include <algorithm>

namespace hui::ps5
{
namespace
{
constexpr std::uint16_t module = 0x0096;
void utf16(std::string_view source, std::span<std::uint16_t> out)
{
    std::size_t written = 0, index = 0;
    while (index < source.size() && written + 1 < out.size())
    {
        auto point = gfx::next_codepoint(source, &index);
        if (point == 0)
            break;
        if (point >= 0x10000)
        {
            if (written + 2 >= out.size())
                break;
            point -= 0x10000;
            out[written++] = static_cast<std::uint16_t>(0xd800 | (point >> 10));
            out[written++] = static_cast<std::uint16_t>(0xdc00 | (point & 0x3ff));
        }
        else
            out[written++] = static_cast<std::uint16_t>(point);
    }
    out[written] = 0;
}
std::string utf8(std::span<const std::uint16_t> source)
{
    std::string out;
    for (std::size_t i = 0; i < source.size() && source[i] != 0; ++i)
    {
        std::uint32_t point = source[i];
        if (point >= 0xd800 && point <= 0xdbff && i + 1 < source.size() &&
            source[i + 1] >= 0xdc00 && source[i + 1] <= 0xdfff)
        {
            point = 0x10000 + ((point - 0xd800) << 10) + (source[++i] - 0xdc00);
        }
        else if (point >= 0xd800 && point <= 0xdfff)
            point = 0xfffd;
        const unsigned bytes = point < 0x80 ? 1 : point < 0x800 ? 2 : point < 0x10000 ? 3 : 4;
        if (bytes == 1)
            out.push_back(static_cast<char>(point));
        else
        {
            out.push_back(static_cast<char>((0xf0u << (4 - bytes)) | (point >> (6 * (bytes - 1)))));
            for (unsigned byte = bytes - 1; byte > 0; --byte)
                out.push_back(static_cast<char>(0x80 | ((point >> (6 * (byte - 1))) & 0x3f)));
        }
    }
    return out;
}
} // namespace
Ime::~Ime()
{
    close();
    if (loaded_ && !active_)
        sceSysmoduleUnloadModule(module);
}
bool Ime::open(std::string_view title, std::string_view placeholder, std::string_view value,
               std::size_t max_length)
{
    if (active_)
        return false;
    if (!loaded_)
    {
        const int initialized = sceCommonDialogInitialize();
        if (initialized < 0 && static_cast<std::uint32_t>(initialized) != 0x80b80002u)
            return false;
        if (sceSysmoduleLoadModule(module) < 0)
            return false;
        loaded_ = true;
    }
    detail::ImeParam param{};
    if (sceUserServiceGetForegroundUser(&param.user_id) < 0)
        return false;
    utf16(title, title_);
    utf16(placeholder, placeholder_);
    param.enter_label = 2;
    param.max_text_length = std::min(max_length, buffer_.size() - 1);
    utf16(value, std::span(buffer_).first(param.max_text_length + 1));
    param.input_text_buffer = buffer_.data();
    param.horizontal_alignment = param.vertical_alignment = 1;
    param.placeholder = placeholder_.data();
    param.title = title_.data();
    if (sceImeDialogInit(&param, nullptr) != 0)
        return false;
    value_ = value;
    active_ = true;
    started_ = sys::monotonic_us();
    return true;
}
Ime::State Ime::poll()
{
    if (!active_)
        return State::idle;
    const int status = sceImeDialogGetStatus();
    if (status == 1 || (status == 0 && sys::monotonic_us() - started_ < 1000000))
        return State::open;
    State state = State::failed;
    if (status == 2)
    {
        detail::ImeResult result{};
        if (sceImeDialogGetResult(&result) >= 0)
        {
            state = result.outcome == 0 ? State::accepted : State::cancelled;
            if (state == State::accepted)
                value_ = utf8(buffer_);
        }
    }
    // Do not return focus to the app while the system still owns the dialog.
    if (sceImeDialogTerm() < 0)
        return State::open;
    active_ = false;
    return state;
}
void Ime::close()
{
    if (active_)
    {
        sceImeDialogAbort();
        active_ = sceImeDialogTerm() < 0;
    }
}
} // namespace hui::ps5
