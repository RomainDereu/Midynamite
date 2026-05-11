/*
 * midi_usb.c
 *
 *  Created on: Jul 18, 2025
 *      Author: Romain Dereu
 */


#include "usbd_midi.h"
#include "midi_usb.h"
#include "midi_transform.h"
#include "memory_main.h"

extern USBD_HandleTypeDef hUsbDeviceFS;

void send_usb_midi_message(uint8_t *midi_message, uint8_t length) {
    if (USBD_MIDI_GetState(&hUsbDeviceFS) != MIDI_IDLE) return;

    uint8_t cin;
    uint8_t status = midi_message[0];

    switch (status) {
        case 0xF8: // Timing Clock
        case 0xFA: // Start
        case 0xFB: // Continue
        case 0xFC: // Stop
        case 0xFE: // Active Sensing
        case 0xFF: // Reset
            cin = 0x0F; // Single-byte message
            break;

        default:
            switch (status & 0xF0) {
                case 0x80: case 0x90: case 0xA0:
                case 0xB0: case 0xE0:
                    cin = 0x08; break;  // 3-byte messages
                case 0xC0: case 0xD0:
                    cin = 0x0C; break;  // 2-byte messages
                case 0xF0:
                    cin = 0x05; break;  // Start of SysEx
                default:
                    cin = 0x0F; break;  // Default to single-byte
            }
            break;
    }

    static uint8_t packetsBuffer[4];
    packetsBuffer[0] = (0x00 << 4) | cin;
    packetsBuffer[1] = midi_message[0];
    packetsBuffer[2] = (length > 1) ? midi_message[1] : 0;
    packetsBuffer[3] = (length > 2) ? midi_message[2] : 0;

    USBD_MIDI_SendPackets(&hUsbDeviceFS, packetsBuffer, 4);
}

static uint8_t usb_midi_cin_length(uint8_t cin)
{
    switch (cin & 0x0F) {
        case 0x02:
        case 0x0C:
        case 0x0D:
        case 0x06:
            return 2;
        case 0x03:
        case 0x04:
        case 0x07:
        case 0x08:
        case 0x09:
        case 0x0A:
        case 0x0B:
        case 0x0E:
            return 3;
        case 0x05:
        case 0x0F:
            return 1;
        default:
            return 0;
    }
}

void USBD_MIDI_OnPacketsReceived(uint8_t *data, uint8_t len)
{
    if (data == NULL || len < 4) {
        return;
    }

    if (midi_usb_mode_allows_thru((uint8_t)save_get(SETTINGS_SEND_USB)) == 0) {
        return;
    }

    const uint8_t packet_count = (uint8_t)(len / 4);
    for (uint8_t packet_index = 0; packet_index < packet_count; ++packet_index) {
        const uint8_t offset = (uint8_t)(packet_index * 4);
        const uint8_t status = data[(uint8_t)(offset + 1)];
        if (status < 0x80) {
            continue;
        }

        uint8_t msg_len = usb_midi_cin_length(data[offset]);
        if (msg_len == 0) {
            msg_len = midi_message_length(status);
        }

        for (uint8_t byte_index = 0; byte_index < msg_len; ++byte_index) {
            midi_buffer_push(data[(uint8_t)(offset + 1 + byte_index)]);
        }
    }
}
