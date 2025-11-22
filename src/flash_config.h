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

#ifndef FLASH_CONFIG_H
#define FLASH_CONFIG_H

#include <stdint.h>
#include <stdbool.h>

// ============================================================================
// Flash Configuration Storage
// ============================================================================

// Configuration structure (stored in Flash)
typedef struct {
    uint32_t magic;           // Magic number to validate config (0x50535843 = "PSXC")
    uint8_t debug_mode;       // Debug mode: 0=OFF, 1=ON
    uint8_t latching_mode;    // Latching mode: 0=OFF, 1=ON
    uint8_t reserved[2];      // Reserved for future use
    uint32_t checksum;        // Simple checksum for validation
} flash_config_t;

// Initialize flash configuration system
void flash_config_init(void);

// Load configuration from flash
// Returns true if valid config found, false otherwise
bool flash_config_load(bool *debug_mode, bool *latching_mode);

// Save current configuration to flash
void flash_config_save(bool debug_mode, bool latching_mode);

#endif // FLASH_CONFIG_H
