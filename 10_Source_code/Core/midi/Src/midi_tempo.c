/*
 * midi_tempo.c
 *
 *  Created on: Sep 5, 2025
 *      Author: Romain Dereu
 */
#include "midi_tempo.h"
#include "utils.h" //For enums
#include "midi_usb.h"
#include "threads.h"

#define MT_UART_TX_TIMEOUT_MS 1

static uint8_t s_mt_send_to_midi_out = 0;
static uint8_t s_mt_divider_phase = 0;


void set_tempo_bpm(uint32_t bpm)
{
    if (bpm == 0) {
        return;
    }

    const uint32_t tempo_click_rate = 6000000 / (bpm * 48);
    const uint32_t current_counter = TIM2->CNT;

    TIM2->ARR = tempo_click_rate;
    if (current_counter >= tempo_click_rate) {
        TIM2->CNT = 0;
    }
}




void mt_set_send_to_midi_out(uint8_t send_to_midi_out)
{
    s_mt_send_to_midi_out = send_to_midi_out;
}

void tempo_sync_from_save(void)
{
    set_tempo_bpm(save_get(TEMPO_CURRENT_TEMPO));
    mt_set_send_to_midi_out((uint8_t)save_get(TEMPO_SEND_TO_MIDI_OUT));
    s_mt_divider_phase = 0;
}


static inline void select_outs_and_send(uint8_t clock_byte)
{
    s_mt_send_to_midi_out = (uint8_t)save_get(TEMPO_SEND_TO_MIDI_OUT);
    UART_HandleTypeDef *uart_list_tempo[2];
    list_of_UART_to_send_to(s_mt_send_to_midi_out, uart_list_tempo);

    const uint8_t usb_mode = (uint8_t)save_get(SETTINGS_SEND_USB);
    if ((usb_mode == MIDI_USB_OUT) || (usb_mode == MIDI_USB_THRU_OUT)) {
        send_usb_midi_message(&clock_byte, 1);
    }

    for (int i = 0; i < 2; i++) {
        if (uart_list_tempo[i] != NULL) {
            HAL_UART_Transmit(uart_list_tempo[i], &clock_byte, 1, MT_UART_TX_TIMEOUT_MS);
        }
    }
}

void mt_process_pending_tempo_out(void)
{
    if (save_get(TEMPO_CURRENTLY_SENDING) == 0) {
        s_mt_divider_phase = 0;
        return;
    }

    s_mt_divider_phase ^= 1;
    if (s_mt_divider_phase != 0) {
        return;
    }

    select_outs_and_send(0xF8);
}

void mt_start_stop(void)
{
    const uint8_t clock_sending = save_get(TEMPO_CURRENTLY_SENDING);

    // Stop clock
    if (clock_sending == 0) {
        s_mt_divider_phase = 0;
        select_outs_and_send(0xFC);
    }
    // Start clock
    else if (clock_sending == 1) {
        s_mt_divider_phase = 0;
        select_outs_and_send(0xFA);
    }
}
