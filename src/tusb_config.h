/*
 * PSX Controller Bit-Banging Simulator for Raspberry Pi Pico
 * TinyUSB Configuration
 * Copyright (C) 2024-2025 ntsklab
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#ifndef TUSB_CONFIG_H
#define TUSB_CONFIG_H

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// TinyUSB Host Configuration
// ============================================================================

// Defined by compiler flags from pico-sdk integration.
#ifndef CFG_TUSB_MCU
#error CFG_TUSB_MCU must be defined
#endif

#ifndef CFG_TUSB_OS
#define CFG_TUSB_OS OPT_OS_PICO
#endif

#ifndef CFG_TUSB_DEBUG
#define CFG_TUSB_DEBUG 0
#endif

// Host-only build.
#define CFG_TUH_ENABLED 1
#define CFG_TUD_ENABLED 0

// RP2040 native USB host is on RHPort 0.
#ifndef BOARD_TUH_RHPORT
#define BOARD_TUH_RHPORT 0
#endif

#ifndef BOARD_TUH_MAX_SPEED
#define BOARD_TUH_MAX_SPEED OPT_MODE_DEFAULT_SPEED
#endif

#define CFG_TUH_MAX_SPEED BOARD_TUH_MAX_SPEED

// Single controller device expected at a time.
#define CFG_TUH_DEVICE_MAX 1
#define CFG_TUH_ENUMERATION_BUFSIZE 256

// HID for joystick + keyboard, plus app custom class driver for XInput.
#define CFG_TUH_HID 4
#define CFG_TUH_HID_EPIN_BUFSIZE 64
#define CFG_TUH_VENDOR 0

#ifndef CFG_TUH_MEM_SECTION
#define CFG_TUH_MEM_SECTION
#endif

#ifndef CFG_TUH_MEM_ALIGN
#define CFG_TUH_MEM_ALIGN __attribute__((aligned(4)))
#endif

#ifdef __cplusplus
}
#endif

#endif // TUSB_CONFIG_H
