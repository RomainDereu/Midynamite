/* _menu_controller.h
 *
 *  Created on: Sep 8, 2025
 *      Author: Romain Dereu
 */

#ifndef MIDI_INC_MENU_CONTROLLER_H_
#define MIDI_INC_MENU_CONTROLLER_H_

#include <stdint.h>
#include "memory_main.h"   // for save_field_t, SAVE_FIELD_COUNT, etc.
#include "menus.h" // for menu_list_t, CtrlActiveList, list_for_page



// ---------------------
// UI submenu id
// ---------------------
typedef enum {
    CTRL_TEMPO_ALL = 1,
	CTRL_SHARED_TEMPO,

    CTRL_SPLIT_ALL,
    CTRL_SPLIT_PAGE_1,
    CTRL_SPLIT_PAGE_2,
    CTRL_SPLIT_TYPE_NOTE,
    CTRL_SPLIT_TYPE_CH,
    CTRL_SPLIT_TYPE_VELOCITY,

    CTRL_MODIFY_CHANGE,
    CTRL_MODIFY_ALL,
    CTRL_MODIFY_VEL_CHANGED,
    CTRL_MODIFY_VEL_FIXED,

    CTRL_TRANSPOSE_SHIFT,
    CTRL_TRANSPOSE_SCALED,
    CTRL_TRANSPOSE_ALL,

	CTRL_ARPEGGIATOR_PAGE_1,
	CTRL_ARPEGGIATOR_PAGE_2,

	CTRL_DISPATCH_ALL,

    CTRL_SETTINGS_GLOBAL1,
    CTRL_SETTINGS_GLOBAL2,
    CTRL_SETTINGS_FILTER,
    CTRL_SETTINGS_ALL,
    CTRL_SETTINGS_ABOUT,
    CTRL_SETTINGS_ALWAYS,

} ctrl_group_id_t;


#define CTRL_GROUP_SLOT_MAX 64

#if defined(__STDC_VERSION__) && (__STDC_VERSION__ >= 201112L)
_Static_assert((unsigned)CTRL_SETTINGS_ALWAYS <= CTRL_GROUP_SLOT_MAX,
               "Controller uses more than 64 group slots");
#endif


// ---------------------
// Field change bits
// ---------------------
#define CHANGE_BITS_WORDS (((SAVE_FIELD_COUNT) + 31) / 32)
extern uint32_t s_field_change_bits[CHANGE_BITS_WORDS];

// ---------------------
// Wrapping options
// ---------------------
#define WRAP        0
#define NO_WRAP     1

// ---------------------
// Menu controls
// ---------------------
typedef void (*save_handler_t)(save_field_t field);

typedef struct {
    uint8_t wrap;
    save_handler_t handler;
    uint8_t groups;
    uint16_t ui_order;
} menu_controls_t;

extern const menu_controls_t menu_controls[SAVE_FIELD_COUNT];


void ctrl_build_active_fields(menu_group_mask_t active_groups, CtrlActiveList *out);
menu_group_mask_t ctrl_active_mask_for_page(menu_list_t page);

// =====================
// Display flag helpers
// =====================

uint8_t menu_row_hit(const CtrlActiveList *list, uint8_t row, save_field_t *out_field, uint8_t *out_bit, uint32_t *out_gid);

// Forward declaration used elsewhere
void threads_display_notify(uint32_t flags);

// Return a single-bit mask for a menu.
static inline uint32_t flag_for_menu(menu_list_t m) {
    return (m < AMOUNT_OF_MENUS) ? (1 << (uint32_t)m) : (1 << (uint32_t)MENU_TEMPO);
}

// Menu → "sending" save_field_t lookup
static inline save_field_t sending_field_for_menu(menu_list_t m) {
    switch (m) {
        case MENU_TEMPO:       return TEMPO_CURRENTLY_SENDING;
        case MENU_SPLIT:       return SPLIT_CURRENTLY_SENDING;
        case MENU_MODIFY:      return MODIFY_CURRENTLY_SENDING;
        case MENU_ARPEGGIATOR: return ARPEGGIATOR_CURRENTLY_SENDING;
        case MENU_DISPATCH:    return DISPATCH_CURRENTLY_SENDING;
        case MENU_TRANSPOSE:   return TRANSPOSE_CURRENTLY_SENDING;
        case MENU_SETTINGS:    return SAVE_FIELD_INVALID;
        default:               return SAVE_FIELD_INVALID;
    }
}

// ---------------------
// Row_span helper
// ---------------------
static inline uint8_t menu_field_row_span(save_field_t f)
{
    switch (f) {

        case SETTINGS_FILTERED_CH:
            return 16;

        case ARPEGGIATOR_NOTES: {
            uint8_t len = (uint8_t)save_get(ARPEGGIATOR_LENGTH);
            if (len < 1) len = 1;
            if (len > 8) len = 8;
            return len;
        }

        default:
            return 1;
    }
}

// ---------------------
// UI API
// ---------------------
void     select_press_menu_change(menu_list_t sel_field);

int8_t   ui_selected_bit(save_field_t f);
uint8_t  ui_is_field_selected(save_field_t f);
menu_group_mask_t ui_active_groups(void);

void     menu_nav_begin_and_update(menu_list_t field);
void     save_mark_all_changed(void);

uint8_t  menu_nav_get_select(menu_list_t field);

void     update_menu();

//update_dispatch_from_ch needs to be updated every cycle


// ---------------------
// Shared arpeggiator timing helpers
// ---------------------
// 48 PPQ grid. Divisions: 1/1 1/2 1/3 1/4 1/6 1/8 1/12 1/16 1/24 1/32
// step ticks:            192 96  64  48  32  24   16    12    8     6
static inline uint16_t arp_step_ticks(uint8_t div_idx)
{
    static const uint16_t ticks_map[10] = { 192, 96, 64, 48, 32, 24, 16, 12, 8, 6 };
    if (div_idx > 9) div_idx = 9;
    return ticks_map[div_idx];
}



#ifdef UNIT_TEST
void update_value_inc1(save_field_t);
void update_value_inc10(save_field_t);
void update_value_inc12(save_field_t);

void update_tempo_bpm(save_field_t);
void update_tempo_send_to_out(save_field_t);

void update_dispatch_from_ch(save_field_t field);

void shadow_select(save_field_t field);
void update_contrast(save_field_t f);

void update_arp_swing(save_field_t field);
void update_arp_division(save_field_t field);

void update_bits_field(save_field_t field, uint8_t bit_index, uint8_t bits_count);
void update_bits_16_fields(save_field_t field);
void update_bits_8_steps(save_field_t field);

#endif

#endif /* MIDI_INC_MENU_CONTROLLER_H_ */
