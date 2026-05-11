/*
 * utils.c
 *
 *  Created on: Jun 21, 2025
 *      Author: Romain Dereu
 */
#include <string.h>
#include "cmsis_os2.h" //osDelay
#include "memory_main.h"
#include "stm32f4xx_hal.h"   // HAL types (TIM, GPIO)
#include "text.h"
#include "utils.h"

#include "midi_arp.h"
#include "midi_usb.h"


extern TIM_HandleTypeDef htim3;
extern TIM_HandleTypeDef htim4;


void list_of_UART_to_send_to(uint8_t send_channels,
                           	 UART_HandleTypeDef **UART_list){
	extern UART_HandleTypeDef huart1;
	extern UART_HandleTypeDef huart2;

	if (send_channels == MIDI_OUT_1){
		UART_list[0] = &huart1;
		UART_list[1] = NULL;
	}

	else if (send_channels == MIDI_OUT_2){
		UART_list[0] = &huart2;
		UART_list[1] = NULL;
	}

	else if (send_channels == MIDI_OUT_1_2 || send_channels == MIDI_OUT_SPLIT){
		UART_list[0] = &huart1;
		UART_list[1] = &huart2;
	}

	else{
		UART_list[0] = NULL;
		UART_list[1] = NULL;
	}

}


/* ---------------------------
 * Panic / MIDI helpers
 * --------------------------- */

// Panic on a single UART
void all_notes_off(UART_HandleTypeDef *huart) {
    uint8_t all_notes_off_msg[3];
    uint8_t reset_ctrl_msg[3];

    for (uint8_t channel = 0; channel < 16; channel++) {
        uint8_t status = 0xB0 | channel;

        // All Notes Off
        all_notes_off_msg[0] = status;
        all_notes_off_msg[1] = 123;  // Controller number for All Notes Off
        all_notes_off_msg[2] = 0;

        // Reset All Controllers
        reset_ctrl_msg[0] = status;
        reset_ctrl_msg[1] = 121;     // Controller number for Reset All Controllers
        reset_ctrl_msg[2] = 0;

        HAL_UART_Transmit(huart, all_notes_off_msg, 3, 1000);
        HAL_UART_Transmit(huart, reset_ctrl_msg, 3, 1000);
    }
}

// Panic on both UART1 and UART2
static void all_notes_off_usb(void)
{
    if (midi_usb_mode_allows_out((uint8_t)save_get(SETTINGS_SEND_USB)) == 0) {
        return;
    }

    uint8_t all_notes_off_msg[3];
    uint8_t reset_ctrl_msg[3];

    for (uint8_t channel = 0; channel < 16; channel++) {
        uint8_t status = 0xB0 | channel;

        all_notes_off_msg[0] = status;
        all_notes_off_msg[1] = 123;
        all_notes_off_msg[2] = 0;

        reset_ctrl_msg[0] = status;
        reset_ctrl_msg[1] = 121;
        reset_ctrl_msg[2] = 0;

        send_usb_midi_message(all_notes_off_msg, 3);
        send_usb_midi_message(reset_ctrl_msg, 3);
    }
}

void panic_midi(GPIO_TypeDef *port,
				uint16_t pin1,
				uint16_t pin2) {
	extern UART_HandleTypeDef huart1;
	extern UART_HandleTypeDef huart2;

	if (HAL_GPIO_ReadPin(port, pin1) == GPIO_PIN_RESET &&
	    HAL_GPIO_ReadPin(port, pin2) == GPIO_PIN_RESET) {

    osDelay(300); // Debounce delay

    if (HAL_GPIO_ReadPin(port, pin1) == GPIO_PIN_RESET &&
        HAL_GPIO_ReadPin(port, pin2) == GPIO_PIN_RESET) {

    	all_notes_off(&huart1);
    	all_notes_off(&huart2);
        all_notes_off_usb();
        arp_panic_clear();
    }
}

}

uint8_t debounce_button(GPIO_TypeDef *port,
		                uint16_t      pin,
		                uint8_t     *prev_state,
		                uint32_t      db_ms)
{
    uint8_t cur = HAL_GPIO_ReadPin(port, pin);

    // If we have a prev_state pointer, only fire on high→low (prev==1 && cur==0).
    // Otherwise, fire on cur==0 immediately.
    if (cur == 0 && (!prev_state || *prev_state == 1)) {
        osDelay(db_ms);
        cur = HAL_GPIO_ReadPin(port, pin);
        if (cur == 0) {
            if (prev_state) *prev_state = 0;
            return 1;
        }
    }

    // remember state if tracking
    if (prev_state) *prev_state = cur;
    return 0;
}
