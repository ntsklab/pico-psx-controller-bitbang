/*
 * PSX Controller Bit-Banging Simulator for Raspberry Pi Pico
 * XInput-compatible USB Host Driver (TinyUSB app class driver)
 * Copyright (C) 2024-2025 ntsklab
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#include "pico/time.h"
#include "tusb.h"
#include "host/usbh.h"
#include "host/usbh_pvt.h"

// USB class/subclass/protocol for Xbox 360 style XInput gamepad interface.
#define XINPUT_INTERFACE_CLASS      0xFF
#define XINPUT_INTERFACE_SUBCLASS   0x5D
#define XINPUT_INTERFACE_PROTOCOL   0x01
#define XINPUT_FIRST_XFER_DELAY_US  100000u

typedef struct {
    uint8_t daddr;
    uint8_t itf_num;
    uint8_t ep_in;
    uint8_t ep_out;
    uint16_t epin_size;
    uint16_t epout_size;
    bool mounted;
    bool pending_first_transfer;
    uint32_t pending_first_transfer_time;
    uint8_t report_buffer[64];
} xinput_interface_t;

static xinput_interface_t g_xinput_itf;

static void xinput_reset_state(void)
{
    memset(&g_xinput_itf, 0, sizeof(g_xinput_itf));
}

bool xinput_init(void)
{
    xinput_reset_state();
    return true;
}

bool xinput_open(uint8_t rhport, uint8_t daddr, tusb_desc_interface_t const* desc_itf, uint16_t max_len)
{
    (void) rhport;

    uint8_t const* p_desc = (uint8_t const*) desc_itf;
    uint8_t const* desc_end = p_desc + max_len;

    while (p_desc < desc_end) {
        if (tu_desc_type(p_desc) != TUSB_DESC_INTERFACE) {
            p_desc = tu_desc_next(p_desc);
            continue;
        }

        tusb_desc_interface_t const* itf = (tusb_desc_interface_t const*) p_desc;

        if (itf->bInterfaceClass == XINPUT_INTERFACE_CLASS &&
            itf->bInterfaceSubClass == XINPUT_INTERFACE_SUBCLASS &&
            itf->bInterfaceProtocol == XINPUT_INTERFACE_PROTOCOL) {
            g_xinput_itf.daddr = daddr;
            g_xinput_itf.itf_num = itf->bInterfaceNumber;
            g_xinput_itf.mounted = true;

            uint8_t const* p_ep = tu_desc_next(p_desc);

            // Xbox interface often has a class-specific descriptor between IF and EP.
            if (p_ep < desc_end && tu_desc_type(p_ep) != TUSB_DESC_ENDPOINT) {
                p_ep = tu_desc_next(p_ep);
            }

            for (uint8_t i = 0; i < itf->bNumEndpoints && p_ep < desc_end; i++) {
                if (tu_desc_type(p_ep) == TUSB_DESC_ENDPOINT) {
                    tusb_desc_endpoint_t const* ep = (tusb_desc_endpoint_t const*) p_ep;

                    if (tu_edpt_dir(ep->bEndpointAddress) == TUSB_DIR_IN) {
                        g_xinput_itf.ep_in = ep->bEndpointAddress;
                        g_xinput_itf.epin_size = tu_edpt_packet_size(ep);
                    } else {
                        g_xinput_itf.ep_out = ep->bEndpointAddress;
                        g_xinput_itf.epout_size = tu_edpt_packet_size(ep);
                    }

                    TU_ASSERT(tuh_edpt_open(daddr, ep));
                }
                p_ep = tu_desc_next(p_ep);
            }

            // Reuse HID app callback path in usb_host_input.c.
            tuh_hid_mount_cb(daddr, 0, NULL, 0);
            return true;
        }

        p_desc = tu_desc_next(p_desc);
    }

    return false;
}

bool xinput_set_config(uint8_t daddr, uint8_t itf_num)
{
    if (!g_xinput_itf.mounted || g_xinput_itf.daddr != daddr || g_xinput_itf.itf_num != itf_num) {
        return false;
    }

    g_xinput_itf.pending_first_transfer = true;
    g_xinput_itf.pending_first_transfer_time = time_us_32();

    // Notify USBH that this interface has finished class setup.
    usbh_driver_set_config_complete(daddr, itf_num);
    return true;
}

bool xinput_xfer_cb(uint8_t daddr, uint8_t ep_addr, xfer_result_t result, uint32_t xferred_bytes)
{
    if (!g_xinput_itf.mounted || daddr != g_xinput_itf.daddr || ep_addr != g_xinput_itf.ep_in) {
        return false;
    }

    if (result == XFER_RESULT_SUCCESS && xferred_bytes > 0) {
        tuh_hid_report_received_cb(daddr, 0, g_xinput_itf.report_buffer, (uint16_t) xferred_bytes);
    }

    if (g_xinput_itf.mounted && g_xinput_itf.ep_in) {
        usbh_edpt_xfer(daddr, g_xinput_itf.ep_in, g_xinput_itf.report_buffer, g_xinput_itf.epin_size);
    }

    return true;
}

void xinput_close(uint8_t daddr)
{
    if (g_xinput_itf.mounted && g_xinput_itf.daddr == daddr) {
        tuh_hid_umount_cb(daddr, 0);
    }

    xinput_reset_state();
}

bool xinput_receive_report(void)
{
    if (!g_xinput_itf.mounted || g_xinput_itf.ep_in == 0 || !g_xinput_itf.pending_first_transfer) {
        return false;
    }

    uint32_t const now = time_us_32();
    if ((now - g_xinput_itf.pending_first_transfer_time) < XINPUT_FIRST_XFER_DELAY_US) {
        return false;
    }

    g_xinput_itf.pending_first_transfer = false;
    return usbh_edpt_xfer(g_xinput_itf.daddr, g_xinput_itf.ep_in, g_xinput_itf.report_buffer, g_xinput_itf.epin_size);
}

static usbh_class_driver_t const g_xinput_driver = {
#if CFG_TUSB_DEBUG >= 2
    .name = "XINPUT",
#endif
    .init = xinput_init,
    .deinit = NULL,
    .open = xinput_open,
    .set_config = xinput_set_config,
    .xfer_cb = xinput_xfer_cb,
    .close = xinput_close,
};

usbh_class_driver_t const* usbh_app_driver_get_cb(uint8_t* driver_count)
{
    *driver_count = 1;
    return &g_xinput_driver;
}
