// ProsperoStore - Keys of a USB keyboard as the controller buttons they stand for.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "core/input.hpp"
#include <cstdint>
#include <span>

namespace store
{
// USB HID usage codes, as the console's keyboard library reports them.
namespace hid
{
constexpr std::uint16_t kEnter = 0x28, kEscape = 0x29, kBackspace = 0x2a, kTab = 0x2b,
                        kSpace = 0x2c, kSlash = 0x38, kF1 = 0x3a, kF2 = 0x3b, kF3 = 0x3c,
                        kF5 = 0x3e, kF6 = 0x3f, kF7 = 0x40, kF10 = 0x43, kPageUp = 0x4b,
                        kDelete = 0x4c, kPageDown = 0x4e, kRight = 0x4f, kLeft = 0x50, kDown = 0x51,
                        kUp = 0x52, kKeypadEnter = 0x58, kApplication = 0x65;
constexpr std::uint32_t kShift = 0x02 | 0x20; // left and right Shift in the modifier byte
} // namespace hid

// The buttons a set of held keys presses. Arrows move, Enter or Space is Cross,
// Escape or Backspace is Circle, Tab (Shift+Tab) and Page Down (Page Up) switch
// sections like R1 (L1), / or F3 searches (Triangle), Delete or F2 is Square
// (Downloads, uninstall), F10 or the Menu key is Options, F5 sorts (R3), F6 is the Touchpad
// (like or comment), F7 is R2 (to an app's comments and back).
inline std::uint32_t keyboard_buttons(std::span<const std::uint16_t> keys, std::uint32_t modifiers)
{
    using namespace hui::pad_bits;
    std::uint32_t buttons = 0;
    for (const auto key : keys)
        switch (key)
        {
        case hid::kUp:
            buttons |= kUp;
            break;
        case hid::kDown:
            buttons |= kDown;
            break;
        case hid::kLeft:
            buttons |= kLeft;
            break;
        case hid::kRight:
            buttons |= kRight;
            break;
        case hid::kEnter:
        case hid::kKeypadEnter:
        case hid::kSpace:
            buttons |= kCross;
            break;
        case hid::kEscape:
        case hid::kBackspace:
            buttons |= kCircle;
            break;
        case hid::kTab:
            buttons |= (modifiers & hid::kShift) ? kL1 : kR1;
            break;
        case hid::kPageDown:
            buttons |= kR1;
            break;
        case hid::kPageUp:
            buttons |= kL1;
            break;
        case hid::kSlash:
        case hid::kF3:
            buttons |= kTriangle;
            break;
        case hid::kDelete:
        case hid::kF2:
            buttons |= kSquare;
            break;
        case hid::kF10:
        case hid::kApplication:
            buttons |= kOptions;
            break;
        case hid::kF5:
            buttons |= kR3;
            break;
        case hid::kF6:
            buttons |= kTouchpad;
            break;
        case hid::kF7:
            buttons |= kR2;
            break;
        default:
            break;
        }
    return buttons;
}
} // namespace store
