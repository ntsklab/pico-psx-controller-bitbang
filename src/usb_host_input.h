/*
 * PSX Controller Bit-Banging Simulator for Raspberry Pi Pico
 * USB Host Input Handler
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

#ifndef USB_HOST_INPUT_H
#define USB_HOST_INPUT_H

#include <stdint.h>
#include <stdbool.h>

// ============================================================================
// USB Host Input Management
// ============================================================================

// Initialize USB Host for input device detection
void usb_host_input_init(void);

// USB Host task - call regularly from main loop to handle USB events
void usb_host_input_task(void);

// Get current button state in PSX format
// Returns 2 bytes of PSX controller state:
// Byte 1: SELECT(0), L3(1), R3(2), START(3), UP(4), RIGHT(5), DOWN(6), LEFT(7)
// Byte 2: L2(0), R2(1), L1(2), R1(3), TRIANGLE(4), CIRCLE(5), CROSS(6), SQUARE(7)
void usb_host_get_button_state(uint8_t *byte1, uint8_t *byte2);

// Get P2 button state (DDR P2 panel mapping)
void usb_host_get_p2_button_state(uint8_t *byte1, uint8_t *byte2);

// Get analog stick positions (if supported by device)
// Returns true if analog data is available
bool usb_host_get_analog_state(uint8_t *lx, uint8_t *ly, uint8_t *rx, uint8_t *ry);

// Check if a USB device is connected
bool usb_host_is_device_connected(void);

// Get connected device type string for debugging
const char* usb_host_get_device_type(void);

#endif // USB_HOST_INPUT_H
