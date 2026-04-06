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

// ============================================================================
// TinyUSB Host Configuration
// ============================================================================

// Operating System is Linux (for host)
#define CFG_TUSB_OS OPT_OS_PICO

// Operate in USB Host mode
#define CFG_TUSB_HOST 1
#define CFG_TUSB_DEVICE 0

// Maximum device count (we only need 1 input device)
#define CFG_TUH_DEVICE_MAX 1

// Maximum HID device count
#define CFG_TUH_HID 1

// HID configuration
#define CFG_TUH_HID_EPIN_BUFSIZE 64

// Enable general debug logs
// #define CFG_TUSB_DEBUG 2

#endif // TUSB_CONFIG_H
