#include "psx_bitbang.h"
#include "config.h"
#include "hardware/gpio.h"
#include "hardware/structs/sio.h"
#include "hardware/sync.h"
#include "pico/time.h"
#include <stdio.h>

// Direct SIO register access for reliable open-drain control
// Using pico SDK structures for safer access
inline void gpio_out_low(uint gpio) {
    // Ensure output register is LOW before enabling output
    gpio_put(gpio, 0);
    __dmb();  // Memory barrier to ensure write completes
    // Now enable output (this drives the pin LOW via open-drain)
    gpio_set_dir(gpio, GPIO_OUT);
    __dmb();  // Memory barrier
}

inline void gpio_hi_z(uint gpio) {
    // Disable output (release to external pull-up)
    gpio_set_dir(gpio, GPIO_IN);
    __dmb();  // Memory barrier
}

// ============================================================================
// PSX Bit-Banging Implementation
// ============================================================================

void psx_bitbang_init(void) {
    // CRITICAL: Ensure GPIO function is set to SIO (GPIO mode) BEFORE using gpio_init
    // This prevents conflicts with UART or other peripherals
    gpio_set_function(PIN_DAT, GPIO_FUNC_SIO);
    gpio_set_function(PIN_ACK, GPIO_FUNC_SIO);
    gpio_set_function(PIN_CMD, GPIO_FUNC_SIO);
    gpio_set_function(PIN_CLK, GPIO_FUNC_SIO);
    gpio_set_function(PIN_SEL, GPIO_FUNC_SIO);
    
    // Initialize DAT pin (open-drain, bidirectional)
    gpio_init(PIN_DAT);
    gpio_put(PIN_DAT, 0);           // Set output register to LOW FIRST
    gpio_disable_pulls(PIN_DAT);    // NO internal pull-up - rely on external pull-up
    gpio_set_dir(PIN_DAT, GPIO_IN); // Start in Hi-Z state
    
    // Initialize ACK pin (open-drain output)
    // CRITICAL: ACK must NOT have internal pull-up!
    // The PSX has external pull-up on the bus.
    // Internal pull-up may prevent ACK from going LOW.
    gpio_init(PIN_ACK);
    gpio_put(PIN_ACK, 0);           // Set output register to LOW FIRST
    gpio_disable_pulls(PIN_ACK);    // NO internal pull-up - PSX has external pull-up
    gpio_set_dir(PIN_ACK, GPIO_IN); // Start in Hi-Z state
    
    // Initialize CMD pin (input from PSX)
    gpio_init(PIN_CMD);
    gpio_disable_pulls(PIN_CMD);    // No pull - PSX has external pull-up
    gpio_set_dir(PIN_CMD, GPIO_IN);
    
    // Initialize CLK pin (input from PSX)
    gpio_init(PIN_CLK);
    gpio_disable_pulls(PIN_CLK);    // No pull - PSX drives this line
    gpio_set_dir(PIN_CLK, GPIO_IN);
    
    // Initialize SEL pin (input from PSX, active LOW)
    gpio_init(PIN_SEL);
    gpio_disable_pulls(PIN_SEL);    // No pull - PSX drives this line
    gpio_set_dir(PIN_SEL, GPIO_IN);
}

// ============================================================================
// Open-Drain Control Functions
// ============================================================================

inline void psx_dat_hiz(void) {
    gpio_set_dir(PIN_DAT, GPIO_IN);  // Hi-Z (pulled HIGH externally)
}

inline void psx_dat_low(void) {
    gpio_set_dir(PIN_DAT, GPIO_OUT); // Drive LOW
}

inline void psx_ack_hiz(void) {
    gpio_set_dir(PIN_ACK, GPIO_IN);  // Hi-Z (pulled HIGH externally)
}

inline void psx_ack_low(void) {
    gpio_set_dir(PIN_ACK, GPIO_OUT); // Drive LOW
}

// ============================================================================
// Bus Line Reading Functions
// ============================================================================

inline bool psx_read_sel(void) {
    return gpio_get(PIN_SEL);
}

inline bool psx_read_clk(void) {
    return gpio_get(PIN_CLK);
}

inline bool psx_read_cmd(void) {
    return gpio_get(PIN_CMD);
}

// ============================================================================
// Clock Edge Detection with Timeout
// ============================================================================

bool __time_critical_func(psx_wait_clk_rising)(uint32_t timeout_us) {
    uint32_t start = time_us_32();
    
    // Wait for CLK to go HIGH
    while (!gpio_get(PIN_CLK)) {
        // Check for timeout
        if ((time_us_32() - start) > timeout_us) {
            return false;
        }
        // Check if SELECT went HIGH (transaction aborted)
        if (gpio_get(PIN_SEL)) {
            return false;
        }
    }
    
    return true;
}

bool __time_critical_func(psx_wait_clk_falling)(uint32_t timeout_us) {
    uint32_t start = time_us_32();
    
    // Wait for CLK to go LOW
    while (gpio_get(PIN_CLK)) {
        // Check for timeout
        if ((time_us_32() - start) > timeout_us) {
            return false;
        }
        // Check if SELECT went HIGH (transaction aborted)
        if (gpio_get(PIN_SEL)) {
            return false;
        }
    }
    
    return true;
}

// ============================================================================
// Byte-Level Communication
// ============================================================================

uint8_t __time_critical_func(psx_receive_byte)(void) {
    uint8_t data = 0;
    
    // Receive 8 bits, LSB first
    for (int bit = 0; bit < 8; bit++) {
        // Wait for CLK falling edge (PSX outputs data on falling edge)
        if (!psx_wait_clk_falling(PSX_CLK_TIMEOUT_US)) {
            return 0xFF;  // Timeout or abort
        }
        
        // Wait for CLK rising edge (sample point)
        if (!psx_wait_clk_rising(PSX_CLK_TIMEOUT_US)) {
            return 0xFF;  // Timeout or abort
        }
        
        // Sample CMD line on rising edge
        if (gpio_get(PIN_CMD)) {
            data |= (1 << bit);
        }
    }
    
    return data;
}

bool __time_critical_func(psx_send_byte)(uint8_t data) {
    // Send 8 bits, LSB first
    for (int bit = 0; bit < 8; bit++) {
        // Wait for CLK falling edge (output data)
        if (!psx_wait_clk_falling(PSX_CLK_TIMEOUT_US)) {
            // Ensure DAT is Hi-Z before returning
            gpio_set_dir(PIN_DAT, GPIO_IN);
            return false;  // Timeout or abort
        }
        
        // Set DAT line according to current bit immediately after falling edge
        if (data & (1 << bit)) {
            gpio_set_dir(PIN_DAT, GPIO_IN);   // Hi-Z = 1
        } else {
            gpio_set_dir(PIN_DAT, GPIO_OUT);  // LOW = 0
        }
        
        // Wait for CLK rising edge (PSX samples data)
        if (!psx_wait_clk_rising(PSX_CLK_TIMEOUT_US)) {
            // Ensure DAT is Hi-Z before returning
            gpio_set_dir(PIN_DAT, GPIO_IN);
            return false;  // Timeout or abort
        }
    }
    
    // After byte is sent, ensure DAT returns to Hi-Z (idle state)
    gpio_set_dir(PIN_DAT, GPIO_IN);
    
    return true;
}

// Simultaneous send and receive (full duplex)
uint8_t __time_critical_func(psx_transfer_byte)(uint8_t data_out) {
    uint8_t data_in = 0;
    
    // Transfer 8 bits, LSB first
    for (int bit = 0; bit < 8; bit++) {
        // Wait for CLK falling edge
        if (!psx_wait_clk_falling(PSX_CLK_TIMEOUT_US)) {
            return 0xFF;  // Timeout or abort
        }
        
        // Sample input data on CMD line immediately after falling edge
        bool cmd_bit = gpio_get(PIN_CMD);
        
        // Output data on DAT line
        if (data_out & (1 << bit)) {
            gpio_set_dir(PIN_DAT, GPIO_IN);   // Hi-Z = 1
        } else {
            gpio_set_dir(PIN_DAT, GPIO_OUT);  // LOW = 0
        }
        
        // Wait for CLK rising edge
        if (!psx_wait_clk_rising(PSX_CLK_TIMEOUT_US)) {
            // Ensure DAT is Hi-Z before returning
            gpio_set_dir(PIN_DAT, GPIO_IN);
            return 0xFF;  // Timeout or abort
        }
        
        // Use the sampled bit
        if (cmd_bit) {
            data_in |= (1 << bit);
        }
    }
    
    // After byte is transferred, ensure DAT returns to Hi-Z (idle state)
    gpio_set_dir(PIN_DAT, GPIO_IN);
    
    return data_in;
}

void __time_critical_func(psx_send_ack)(void) {
    // Wait a bit after the last bit before asserting ACK
    busy_wait_us_32(ACK_DELAY_US);
    
    // Assert ACK (drive LOW) - no debug output during pulse, timing critical!
    gpio_out_low(PIN_ACK);
    
    // Hold ACK for specified duration
    busy_wait_us_32(ACK_PULSE_WIDTH_US);
    
    // Release ACK (Hi-Z)
    gpio_hi_z(PIN_ACK);
}

// ============================================================================
// Bus Release
// ============================================================================

inline void psx_release_bus(void) {
    // Release both DAT and ACK to Hi-Z
    gpio_set_dir(PIN_DAT, GPIO_IN);
    gpio_set_dir(PIN_ACK, GPIO_IN);
}
