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

#include "button_input.h"
#include "config.h"
#include "hardware/gpio.h"

// ============================================================================
// Button Input Implementation
// ============================================================================

void button_input_init(void) {
    // Initialize all button pins as inputs with pull-ups
    // Buttons are active LOW (pressed = LOW)
    
    // Face buttons
    gpio_init(BTN_CIRCLE);
    gpio_set_dir(BTN_CIRCLE, GPIO_IN);
    gpio_pull_up(BTN_CIRCLE);
    
    gpio_init(BTN_CROSS);
    gpio_set_dir(BTN_CROSS, GPIO_IN);
    gpio_pull_up(BTN_CROSS);
    
    gpio_init(BTN_TRIANGLE);
    gpio_set_dir(BTN_TRIANGLE, GPIO_IN);
    gpio_pull_up(BTN_TRIANGLE);
    
    gpio_init(BTN_SQUARE);
    gpio_set_dir(BTN_SQUARE, GPIO_IN);
    gpio_pull_up(BTN_SQUARE);
    
    // Shoulder buttons
    gpio_init(BTN_L1);
    gpio_set_dir(BTN_L1, GPIO_IN);
    gpio_pull_up(BTN_L1);
    
    gpio_init(BTN_R1);
    gpio_set_dir(BTN_R1, GPIO_IN);
    gpio_pull_up(BTN_R1);
    
    gpio_init(BTN_L2);
    gpio_set_dir(BTN_L2, GPIO_IN);
    gpio_pull_up(BTN_L2);
    
    gpio_init(BTN_R2);
    gpio_set_dir(BTN_R2, GPIO_IN);
    gpio_pull_up(BTN_R2);
    
    // D-pad
    gpio_init(BTN_UP);
    gpio_set_dir(BTN_UP, GPIO_IN);
    gpio_pull_up(BTN_UP);
    
    gpio_init(BTN_DOWN);
    gpio_set_dir(BTN_DOWN, GPIO_IN);
    gpio_pull_up(BTN_DOWN);
    
    gpio_init(BTN_LEFT);
    gpio_set_dir(BTN_LEFT, GPIO_IN);
    gpio_pull_up(BTN_LEFT);
    
    gpio_init(BTN_RIGHT);
    gpio_set_dir(BTN_RIGHT, GPIO_IN);
    gpio_pull_up(BTN_RIGHT);
    
    // System buttons
    gpio_init(BTN_START);
    gpio_set_dir(BTN_START, GPIO_IN);
    gpio_pull_up(BTN_START);
    
    gpio_init(BTN_SELECT);
    gpio_set_dir(BTN_SELECT, GPIO_IN);
    gpio_pull_up(BTN_SELECT);
}

uint8_t button_read_byte1(void) {
    // PSX format byte 1:
    // bit 0 = SELECT
    // bit 1 = L3 (not used in digital mode, always 1)
    // bit 2 = R3 (not used in digital mode, always 1)
    // bit 3 = START
    // bit 4 = UP
    // bit 5 = RIGHT
    // bit 6 = DOWN
    // bit 7 = LEFT
    //
    // Button pressed = LOW (0) on GPIO
    // PSX protocol: pressed = 0, released = 1
    // So we can read GPIO directly (inverted logic matches)
    
    uint8_t byte1 = 0xFF;  // Start with all released
    
    // Read buttons and clear corresponding bits if pressed
    if (!gpio_get(BTN_SELECT)) byte1 &= ~(1 << 0);
    // L3 and R3 not implemented (bits 1 and 2 stay 1)
    if (!gpio_get(BTN_START))  byte1 &= ~(1 << 3);
    if (!gpio_get(BTN_UP))     byte1 &= ~(1 << 4);
    if (!gpio_get(BTN_RIGHT))  byte1 &= ~(1 << 5);
    if (!gpio_get(BTN_DOWN))   byte1 &= ~(1 << 6);
    if (!gpio_get(BTN_LEFT))   byte1 &= ~(1 << 7);
    
    return byte1;
}

uint8_t button_read_byte2(void) {
    // PSX format byte 2:
    // bit 0 = L2
    // bit 1 = R2
    // bit 2 = L1
    // bit 3 = R1
    // bit 4 = Triangle
    // bit 5 = Circle
    // bit 6 = Cross
    // bit 7 = Square
    
    uint8_t byte2 = 0xFF;  // Start with all released
    
    // Read buttons and clear corresponding bits if pressed
    if (!gpio_get(BTN_L2))       byte2 &= ~(1 << 0);
    if (!gpio_get(BTN_R2))       byte2 &= ~(1 << 1);
    if (!gpio_get(BTN_L1))       byte2 &= ~(1 << 2);
    if (!gpio_get(BTN_R1))       byte2 &= ~(1 << 3);
    if (!gpio_get(BTN_TRIANGLE)) byte2 &= ~(1 << 4);
    if (!gpio_get(BTN_CIRCLE))   byte2 &= ~(1 << 5);
    if (!gpio_get(BTN_CROSS))    byte2 &= ~(1 << 6);
    if (!gpio_get(BTN_SQUARE))   byte2 &= ~(1 << 7);
    
    return byte2;
}
