/*
 * PSX Controller Bit-Banging Simulator for Raspberry Pi Pico
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

#ifndef BUTTON_INPUT_H
#define BUTTON_INPUT_H

#include <stdint.h>

// ============================================================================
// Button Input Management
// ============================================================================

// Initialize all button GPIO pins
void button_input_init(void);

// Read all buttons and return PSX format byte 1
// bit 0 = SELECT
// bit 1 = L3 (not used in digital mode, always 1)
// bit 2 = R3 (not used in digital mode, always 1)
// bit 3 = START
// bit 4 = UP
// bit 5 = RIGHT
// bit 6 = DOWN
// bit 7 = LEFT
uint8_t button_read_byte1(void);

// Read all buttons and return PSX format byte 2
// bit 0 = L2
// bit 1 = R2
// bit 2 = L1
// bit 3 = R1
// bit 4 = Triangle
// bit 5 = Circle
// bit 6 = Cross
// bit 7 = Square
uint8_t button_read_byte2(void);

#endif // BUTTON_INPUT_H
