#ifndef PSX_BITBANG_H
#define PSX_BITBANG_H

#include <stdint.h>
#include <stdbool.h>

// ============================================================================
// PSX Bit-Banging Low-Level Functions
// ============================================================================

// Initialize PSX bus GPIO pins
void psx_bitbang_init(void);

// Open-drain control functions for DAT and ACK lines
// Hi-Z state: set pin to input mode (pulled HIGH by external resistor)
// LOW state: set pin to output mode with LOW value
void psx_dat_hiz(void);     // Release DAT line (Hi-Z)
void psx_dat_low(void);     // Assert DAT line LOW
void psx_ack_hiz(void);     // Release ACK line (Hi-Z)
void psx_ack_low(void);     // Assert ACK line LOW

// Read current state of bus lines
bool psx_read_sel(void);    // Read SELECT line (true = HIGH/inactive)
bool psx_read_clk(void);    // Read CLOCK line
bool psx_read_cmd(void);    // Read COMMAND line

// Wait for clock edges with timeout
// Returns true if edge detected, false if timeout
bool psx_wait_clk_rising(uint32_t timeout_us);
bool psx_wait_clk_falling(uint32_t timeout_us);

// Receive one byte from PSX (read CMD line on CLK rising edges)
// Returns received byte, or 0xFF on timeout
uint8_t psx_receive_byte(void);

// Send one byte to PSX (set DAT line on CLK falling edges)
// Returns true if successful, false if timeout
bool psx_send_byte(uint8_t data);

// Send ACK pulse after byte transmission
// Asserts ACK LOW for ACK_PULSE_WIDTH_US after ACK_DELAY_US
void psx_send_ack(void);

// Simultaneous send and receive (full duplex)
// Sends data_out while receiving and returning data_in
uint8_t psx_transfer_byte(uint8_t data_out);

// Release bus completely (both DAT and ACK to Hi-Z)
void psx_release_bus(void);

#endif // PSX_BITBANG_H
