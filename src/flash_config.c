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

#include "flash_config.h"
#include "hardware/flash.h"
#include "hardware/sync.h"
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include <string.h>
#include <stdio.h>

// ============================================================================
// Flash Configuration Constants
// ============================================================================

// Magic number to identify valid configuration
#define CONFIG_MAGIC 0x50535843  // "PSXC" in ASCII

// Flash offset for configuration storage
// Use last sector of 2MB flash (offset 0x1FF000 = 2MB - 4KB)
// NOTE: Adjust this if your Pico has different flash size
#define FLASH_CONFIG_OFFSET (PICO_FLASH_SIZE_BYTES - FLASH_SECTOR_SIZE)

// Get pointer to config in flash (XIP mapped address)
#define FLASH_CONFIG_ADDR (XIP_BASE + FLASH_CONFIG_OFFSET)

// ============================================================================
// Internal Functions
// ============================================================================

// Calculate simple checksum
static uint32_t calculate_checksum(const flash_config_t *config)
{
    uint32_t sum = 0;
    sum += config->magic;
    sum += config->debug_mode;
    sum += config->latching_mode;
    // Don't include checksum field itself
    return sum;
}

// ============================================================================
// Public Functions
// ============================================================================

void flash_config_init(void)
{
    // Nothing to do - flash is memory mapped and ready to read
}

bool flash_config_load(bool *debug_mode, bool *latching_mode)
{
    // Read config from flash (memory mapped, no special read needed)
    const flash_config_t *stored_config = (const flash_config_t *)FLASH_CONFIG_ADDR;
    
    // Validate magic number
    if (stored_config->magic != CONFIG_MAGIC) {
        return false;  // No valid config found
    }
    
    // Validate checksum
    uint32_t expected_checksum = calculate_checksum(stored_config);
    if (stored_config->checksum != expected_checksum) {
        return false;  // Corrupted config
    }
    
    // Load values
    *debug_mode = stored_config->debug_mode ? true : false;
    *latching_mode = stored_config->latching_mode ? true : false;
    
    return true;
}

// External Core1 entry point
extern void core1_entry(void);

void flash_config_save(bool debug_mode, bool latching_mode)
{
    // Prepare config structure in RAM (not on stack during flash ops)
    static flash_config_t config;
    config.magic = CONFIG_MAGIC;
    config.debug_mode = debug_mode ? 1 : 0;
    config.latching_mode = latching_mode ? 1 : 0;
    config.reserved[0] = 0;
    config.reserved[1] = 0;
    config.checksum = calculate_checksum(&config);
    
    printf("Saving to flash (this will take ~400ms)...\n");
    
    // CRITICAL: Stop Core1 before flash operations
    // Flash erase/write freezes the XIP bus, breaking code execution from flash
    multicore_reset_core1();
    
    // Give Core1 time to stop
    sleep_ms(10);
    
    // Flash operations require interrupts to be disabled
    uint32_t ints = save_and_disable_interrupts();
    
    // Erase the sector (4KB) - takes ~400ms
    flash_range_erase(FLASH_CONFIG_OFFSET, FLASH_SECTOR_SIZE);
    
    // Write the config - align to 256 bytes as required
    uint8_t buffer[FLASH_PAGE_SIZE] = {0};  // 256 bytes
    memcpy(buffer, &config, sizeof(flash_config_t));
    flash_range_program(FLASH_CONFIG_OFFSET, buffer, FLASH_PAGE_SIZE);
    
    // Re-enable interrupts
    restore_interrupts(ints);
    
    printf("Flash write complete. Restarting Core1...\n");
    
    // Restart Core1
    multicore_launch_core1(core1_entry);
    
    printf("Settings saved successfully\n");
}
