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

#ifndef SHARED_STATE_H
#define SHARED_STATE_H

#include <stdint.h>

// ============================================================================
// Shared State Structure for Inter-Core Communication
// ============================================================================

// Controller button state in PSX protocol format
typedef struct {
    uint8_t buttons1;       // Byte 3: SELECT, L3, R3, START, UP, RIGHT, DOWN, LEFT
    uint8_t buttons2;       // Byte 4: L2, R2, L1, R1, Triangle, Circle, Cross, Square
} controller_state_t;

// Double-buffered shared state for lock-free access
typedef struct {
    controller_state_t buffer[2];       // Double buffer
    volatile uint32_t write_index;      // Index being written by Core 0
    volatile uint32_t read_index;       // Index being read by Core 1
} shared_controller_state_t;

// ============================================================================
// Global Shared State
// ============================================================================

extern shared_controller_state_t g_shared_state;

// ============================================================================
// Function Prototypes
// ============================================================================

// Initialize shared state
void shared_state_init(void);

// Core 0: Write new button state
void shared_state_write(uint8_t btn1, uint8_t btn2);

// Core 1: Read stable button state
void shared_state_read(uint8_t *btn1, uint8_t *btn2);

#endif // SHARED_STATE_H
