/*
 * midi_modify.h
 *
 *  Created on: Feb 27, 2025
 *      Author: Astaa
 */

#ifndef INC_MIDI_MODIFY_H_
#define INC_MIDI_MODIFY_H_

#include "main.h"
#include "screen_driver.h"

void display_incoming_midi(UART_HandleTypeDef huart_ptr, uint8_t (* midi_rx_buff_ptr)[10], const screen_driver_Font_t * font);


#endif /* INC_MIDI_MODIFY_H_ */
