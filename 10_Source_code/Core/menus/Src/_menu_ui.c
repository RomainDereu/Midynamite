/*
 * _menu_ui.c
 *
 *  Created on: Aug 23, 2025
 *      Author: Romain Dereu
 */

#include <stddef.h>
#include "cmsis_os2.h" //For osDelay
#include "main.h" // Timer
#include "_menu_ui.h" // menu change functions
#include "_menu_controller.h" // For menu_field_row_span
#include "midi_tempo.h" //mt_start_stop
#include "screen_driver.h" //Font
#include "utils.h" // Debounce


typedef struct {
    uint8_t current_menu;
    uint8_t old_menu;
} ui_state_t;

// --- function-scoped static state (internal) ---
static inline ui_state_t* get_menus_state(void) {
    static ui_state_t menus_state = { .current_menu = 0, .old_menu = 0 };
    return &menus_state;
}

// Small helpers (internal)
static inline uint8_t menu_wrap(uint8_t v) { return (uint8_t)(v % AMOUNT_OF_MENUS); }
static inline uint8_t ui_menu_current(void) { return get_menus_state()->current_menu; }
static inline uint8_t ui_menu_old(void)     { return get_menus_state()->old_menu; }

static inline void ui_menu_set(uint8_t v) {
    ui_state_t *st = get_menus_state();
    v = menu_wrap(v);
    if (v != st->current_menu) {
        st->old_menu     = st->current_menu;
        st->current_menu = v;
    }
}
static inline void ui_menu_next(void) { ui_menu_set((uint8_t)(get_menus_state()->current_menu + 1)); }

// Public API kept stable
uint8_t get_current_menu(menu_list_t field) {
    switch (field) {
        case CURRENT_MENU: return ui_menu_current();
        case OLD_MENU:     return ui_menu_old();
        default:           return 0;
    }
}

uint8_t set_current_menu(menu_list_t field, ui_modify_op_t op, uint8_t value_if_set) {
    if (field != CURRENT_MENU) return 1;      // Only CURRENT_MENU is mutable here
    if (op == UI_MODIFY_INCREMENT) { ui_menu_next();            return 1; }
    if (op == UI_MODIFY_SET)       { ui_menu_set(value_if_set); return 1; }
    return 0;
}

// ------------------------------------------------------

void initialize_screen(void){
  screen_driver_Init();
  screen_driver_UpdateContrast();
}

void draw_line(uint8_t x1, uint8_t y1, uint8_t x2, uint8_t y2){
    screen_driver_Line(x1, y1, x2, y2, White );
}

static void draw_text(const char *s, int16_t x, int16_t y, ui_font_t font) {
    if (!s) return;
    switch (font) {
        case UI_6x8:   write_68(s, x, y); break;
        case UI_6x8_2: break;
        case UI_11x18: write_1118(s, x, y); break;
        case UI_16x24: break;
    }
}

static ui_font_t elem_font(const ui_element *e)
{
    switch (e->type) {
        case ELEM_TEXT:
        case ELEM_ITEM:
        case ELEM_SWINGPCT:
            return UI_6x8;

        case ELEM_TEXT_1118:
        case ELEM_ITEM_1118:
            return UI_11x18;

        case ELEM_TEXT_1624:
        case ELEM_ITEM_1624:
            return UI_16x24;

        case ELEM_16CH:
        case ELEM_8STEPS:
            return UI_6x8_2;
    }

    return UI_6x8;
}

static void draw_text_ul(const char *s, int16_t x, int16_t y, ui_font_t font, uint8_t ul) {
    if (!s) return;
    switch (font) {
        case UI_6x8:   write_underline_68(s, x, y, ul);   break;
        case UI_6x8_2: write_underline_68_2(s, x, y, ul); break;
        case UI_11x18: write_underline_1118(s, x, y, ul); break;
        case UI_16x24: write_underline_1624(s, x, y, ul); break;
    }
}

static inline void draw_item_row(const ui_element *e)
{
    const save_field_t  f   = (save_field_t)e->save_item;
    const save_limits_t lim = save_limits[f];

    const int32_t v = save_get(f);

    // Compute table index
    uint32_t idx = (lim.min < 0) ? (uint32_t)(v - lim.min) : (uint32_t)v;

    // Clamp to table range implied by limits
    const uint32_t last = (lim.min < 0)
                        ? (uint32_t)(lim.max - lim.min)
                        : (uint32_t) lim.max;
    if (idx > last) idx = last;

    const char *const *table = (const char *const *)e->text;
    draw_text_ul(table[idx], e->x, e->y, elem_font(e), ui_is_field_selected(f) ? 1 : 0);
}

static inline uint8_t elem_is_visible(const ui_element *e, menu_group_mask_t active_groups_mask)
{
    if (e->ctrl_group_id == 0) return 1;
    uint8_t id = e->ctrl_group_id;
    if (id < 1 || id > CTRL_GROUP_SLOT_MAX) return 0;
    menu_group_mask_t bit = (((menu_group_mask_t)1) << (id - 1));
    return (active_groups_mask & bit) ? 1 : 0;
}

// -------------------------
// 8-step (one-line) drawing
// -------------------------
static void menu_ui_draw_8_steps(const ui_element *e)
{
    const save_field_t f    = (save_field_t)e->save_item;
    const uint32_t     mask = (uint32_t)save_get(f);
    const int8_t       selb = ui_selected_bit(f);

    uint8_t len = (uint8_t)save_get(ARPEGGIATOR_LENGTH);
    if (len < 1) len = 1;
    if (len > 8) len = 8;

    const int16_t base_x = (int16_t)e->x;
    const int16_t y      = (int16_t)e->y;

    // draw ONLY visible steps
    for (uint8_t i = 0; i < len; ++i) {
        const char *label = (mask & (1 << i)) ? "X" : "0";
        const uint8_t ul  = (selb == (int8_t)i) ? 1 : 0;

        draw_text_ul(label, (int16_t)(base_x + 10 * i), y, elem_font(e), ul);
    }
}

// -------------------------
// Swing percent drawing (3 chars: " 5%".."99%")
// -------------------------

// Convert swing ticks (0..ticks-1) into percent (rounded).
// Note: your controller clamps swing to >= 1, so UI never sees 0 unless old data exists.
static inline uint8_t arp_swing_percent(uint8_t div_idx, uint8_t swing_ticks)
{
    const uint16_t ticks = arp_step_ticks(div_idx);
    if (ticks == 0) return 0;

    if (ticks > 100) {
        if (swing_ticks > 99) swing_ticks = 99;
        return swing_ticks;
    }

    const uint8_t maxv = (uint8_t)(ticks - 1);
    if (swing_ticks > maxv) swing_ticks = maxv;

    // rounded: (swing_ticks * 100) / ticks
    const uint16_t p = (uint16_t)swing_ticks * 100 + (ticks / 2);
    return (uint8_t)(p / ticks);
}


static void menu_ui_draw_swing_percent(const ui_element *e)
{
    const save_field_t swing_f = (save_field_t)e->save_item;

    const uint8_t div_idx = (uint8_t)save_get(ARPEGGIATOR_DIVISION);
    const uint16_t ticks  = arp_step_ticks(div_idx);
    const uint8_t maxv     = (ticks > 100) ? 99 : ((ticks > 0) ? (uint8_t)(ticks - 1) : 0);

    uint8_t swing = (uint8_t)save_get(swing_f);
    if (swing < 1) swing = 1;         // enforce “no 0” even if old save exists
    if (swing > maxv) swing = maxv;

    uint8_t pct = arp_swing_percent(div_idx, swing);
    if (pct > 99) pct = 99; // keep it 2 digits max for fixed 3-char UI

    char buf[4];
    buf[0] = (pct >= 10) ? (char)('0' + (pct / 10)) : ' ';   // leading space for 0..9
    buf[1] = (char)('0' + (pct % 10));
    buf[2] = '%';
    buf[3] = 0;

    draw_text_ul(buf, e->x, e->y, elem_font(e), ui_is_field_selected(swing_f) ? 1 : 0);
}


// -------------------------
// 16ch drawing
// -------------------------
static void menu_ui_draw_16ch(const ui_element *e) {
    const save_field_t f    = (save_field_t)e->save_item;
    const uint32_t     mask = (uint32_t)save_get(f);
    const int8_t       selb = ui_selected_bit(f);   // -1 if not selected

    // Use the element's text as the "on" glyph, defaulting to "O"
    const char *on_label = e->text ? e->text : "O";

    const int16_t base_x = (int16_t)e->x;
    const int16_t y1     = (int16_t)e->y;          // first row at LINE_*
    const int16_t y2     = (int16_t)(y1 + 10);     // second row exactly +10 px

    for (uint8_t i = 0; i < 16; ++i) {
        const char  *label = (mask & (1 << i)) ? on_label
                                                : message->one_to_sixteen_one_char[i];
        const int16_t x    = (int16_t)(base_x + 10 * (i % 8));
        const int16_t y    = (i < 8) ? y1 : y2;
        const uint8_t ul   = (selb == (int8_t)i) ? 1 : 0;

        draw_text_ul(label, x, y, elem_font(e), ul);
    }
}

typedef void (*ui_fn0_t)(void);

typedef struct {
    ui_fn0_t ui_update;
    ui_fn0_t ui_code;
} MenuVTable;

static inline menu_list_t current_menu(void) {
    uint8_t m = get_current_menu(CURRENT_MENU);
    return (m < AMOUNT_OF_MENUS) ? (menu_list_t)m : MENU_TEMPO;
}

static const MenuVTable kMenuVT[AMOUNT_OF_MENUS] = {
    [MENU_TEMPO]       = { ui_update_tempo,       ui_code_tempo },
    [MENU_SPLIT]       = { ui_update_split,       ui_code_split },
    [MENU_MODIFY]      = { ui_update_modify,      ui_code_modify },
    [MENU_TRANSPOSE]   = { ui_update_transpose,   ui_code_transpose },
    [MENU_ARPEGGIATOR] = { ui_update_arpeggiator, ui_code_arpeggiator },
    [MENU_DISPATCH]    = { ui_update_dispatch,    ui_code_dispatch },
    [MENU_SETTINGS]    = { ui_update_settings,    ui_code_settings },
};


static void ui_code_menu(void){
    const menu_list_t m = current_menu();
    kMenuVT[m].ui_code();
}

// One small tick per page
void menu_ui_render(menu_list_t menu, const ui_element *elems, size_t count) {
    (void)menu;

    const menu_group_mask_t active = ui_active_groups();

    screen_driver_Fill(Black);

    for (size_t i = 0; i < count; ++i) {
        const ui_element *e = &elems[i];

        if (!elem_is_visible(e, active)) continue;

        switch (e->type) {
            case ELEM_TEXT:
            case ELEM_TEXT_1118:
            case ELEM_TEXT_1624:
                draw_text(e->text, e->x, e->y, elem_font(e));
                break;
            case ELEM_ITEM:
            case ELEM_ITEM_1118:
            case ELEM_ITEM_1624:
                draw_item_row(e);
                break;
            case ELEM_16CH:     menu_ui_draw_16ch(e);                    break;
            case ELEM_8STEPS:   menu_ui_draw_8_steps(e);                 break;
            case ELEM_SWINGPCT: menu_ui_draw_swing_percent(e);           break;
            default: break;
        }
    }

    ui_code_menu();

    draw_line(0, 10, 127, 10);
    screen_driver_UpdateScreen();
}


void screen_update_menu(uint32_t flag){
    if (menu_select_blocks_render()) {
        return;
    }

    const menu_list_t m = current_menu();
    if (flag & flag_for_menu(m)) kMenuVT[m].ui_update();
}

static void notify_menu_refresh(void);

static uint32_t s_settings_saved_until = 0;

uint8_t settings_recently_saved(void)
{
    return ((int32_t)(HAL_GetTick() - s_settings_saved_until) < 0) ? 1 : 0;
}


static void settings_save_pressed(void)
{
    store_settings();
    s_settings_saved_until = HAL_GetTick() + 1000;
    notify_menu_refresh();
}


// ==============================
// Selector DATA & computes
// ==============================
// save-based computes
static uint8_t sel_mod_vel_type()     { return (save_get(MODIFY_VELOCITY_TYPE)    == MIDI_MODIFY_FIXED_VEL) ? 1 : 0; }
static uint8_t sel_transpose_type()   { return (save_get(TRANSPOSE_TRANSPOSE_TYPE)== MIDI_TRANSPOSE_SCALED) ? 1 : 0; }
static uint8_t sel_fixed0()           { return 0; }

static uint8_t split_section_idx(void)
{
    const uint8_t sel_row = menu_nav_get_select(MENU_SPLIT);

    CtrlActiveList union_rows = {0};
    if (!build_union_for_position_page(MENU_SPLIT, &union_rows)) {
        return 0;
    }

    uint32_t gid = 0;
    if (!menu_row_hit(&union_rows, sel_row, NULL, NULL, &gid)) {
        return 0;
    }

    if (gid == CTRL_SPLIT_PAGE_2) {
        return 1;
    }

    return 0;
}

static uint8_t sel_split_type_page1(void)
{
    if (split_section_idx() != 0) {
        return 3;
    }

    const uint8_t split_type = (uint8_t)save_get(SPLIT_TYPE);
    return (split_type < 3) ? split_type : 0;
}


// -------------------------
// Group-id lists (avoid compound literals in static tables)
// -------------------------
static const ctrl_group_id_t GR_TEMPO_ALL[]        = { CTRL_TEMPO_ALL, CTRL_SHARED_TEMPO, 0};

static const ctrl_group_id_t GR_SPLIT_SECTIONS[]   = { CTRL_SPLIT_PAGE_1, CTRL_SPLIT_PAGE_2 };
static const ctrl_group_id_t GR_SPLIT_TYPES[]      = { CTRL_SPLIT_TYPE_NOTE, CTRL_SPLIT_TYPE_CH, CTRL_SPLIT_TYPE_VELOCITY, 0 };

static const ctrl_group_id_t GR_MODIFY_ALL[]       = { CTRL_MODIFY_ALL };
static const ctrl_group_id_t GR_MODIFY_CHANGE[]    = { CTRL_MODIFY_CHANGE };
static const ctrl_group_id_t GR_MODIFY_VEL_TYPE[]  = { CTRL_MODIFY_VEL_CHANGED, CTRL_MODIFY_VEL_FIXED };

static const ctrl_group_id_t GR_TRANSPOSE_ALL[]    = { CTRL_TRANSPOSE_ALL };
static const ctrl_group_id_t GR_TRANSPOSE_TYPE[]   = { CTRL_TRANSPOSE_SHIFT, CTRL_TRANSPOSE_SCALED };

static const ctrl_group_id_t GR_ARP_SECTIONS[] = {CTRL_ARPEGGIATOR_PAGE_1, CTRL_ARPEGGIATOR_PAGE_2,};
static const ctrl_group_id_t GR_ARP_TEMPO_GATE[] = {CTRL_SHARED_TEMPO, 0};

static const ctrl_group_id_t GR_DISPATCH_ALL[]        = { CTRL_DISPATCH_ALL };


static const ctrl_group_id_t GR_SETTINGS_ALWAYS[]  = { CTRL_SETTINGS_ALWAYS };
static const ctrl_group_id_t GR_SETTINGS_SECTIONS[] = {
    CTRL_SETTINGS_GLOBAL1,
    CTRL_SETTINGS_GLOBAL2,
    CTRL_SETTINGS_FILTER,
    CTRL_SETTINGS_ABOUT
};



// Selector table (DATA only, page-driven)
const page_group_rule_t kPageGroupRules[] = {
    { GROUP_STATE_BASED, 1, GR_TEMPO_ALL,         SAVE_FIELD_INVALID,          sel_fixed0,           0, MENU_TEMPO },

    { CURRENT_POSITION_BASED, 2, GR_SPLIT_SECTIONS, SAVE_FIELD_INVALID,       NULL,                 0, MENU_SPLIT },
    { GROUP_STATE_BASED, 4, GR_SPLIT_TYPES,      SAVE_FIELD_INVALID,          sel_split_type_page1, 0, MENU_SPLIT },

    { GROUP_STATE_BASED, 1, GR_MODIFY_ALL,        SAVE_FIELD_INVALID,          sel_fixed0,           0, MENU_MODIFY },
    { GROUP_STATE_BASED, 1, GR_MODIFY_CHANGE,     SAVE_FIELD_INVALID,          sel_fixed0,           0, MENU_MODIFY },
    { GROUP_STATE_BASED, 2, GR_MODIFY_VEL_TYPE,   MODIFY_VELOCITY_TYPE,        sel_mod_vel_type,     1, MENU_MODIFY },

    { GROUP_STATE_BASED, 1, GR_TRANSPOSE_ALL,     SAVE_FIELD_INVALID,          sel_fixed0,           0, MENU_TRANSPOSE },
    { GROUP_STATE_BASED, 2, GR_TRANSPOSE_TYPE,    TRANSPOSE_TRANSPOSE_TYPE,    sel_transpose_type,   1, MENU_TRANSPOSE },

	{ CURRENT_POSITION_BASED, 2, GR_ARP_SECTIONS,    SAVE_FIELD_INVALID, NULL, 0, MENU_ARPEGGIATOR },
	{ CURRENT_POSITION_BASED, 2, GR_ARP_TEMPO_GATE,  SAVE_FIELD_INVALID, NULL, 0, MENU_ARPEGGIATOR },

    { GROUP_STATE_BASED, 1, GR_DISPATCH_ALL,         SAVE_FIELD_INVALID,          sel_fixed0,           0, MENU_DISPATCH },


    { GROUP_STATE_BASED, 1, GR_SETTINGS_ALWAYS,   SAVE_FIELD_INVALID,          sel_fixed0,           0, MENU_SETTINGS },
    { CURRENT_POSITION_BASED, 4, GR_SETTINGS_SECTIONS, SAVE_FIELD_INVALID,     NULL,                0, MENU_SETTINGS },


};

const size_t kPageGroupRulesCount = (sizeof(kPageGroupRules) / sizeof(kPageGroupRules[0]));

// -------------------------
// Active lists cache per page
// -------------------------
typedef struct {
    CtrlActiveList list[AMOUNT_OF_MENUS];
} MenuActiveLists;

static MenuActiveLists s_menu_lists;

CtrlActiveList* list_for_page(menu_list_t page)
{
    if (page >= AMOUNT_OF_MENUS) page = MENU_TEMPO;
    return &s_menu_lists.list[page];
}



void build_union_for_groups_local(const ctrl_group_id_t *groups, uint8_t n_groups, CtrlActiveList *out)
{
    uint8_t count = 0;

    for (uint16_t f = 0; f < SAVE_FIELD_COUNT && count < MENU_ACTIVE_LIST_CAP; ++f) {
        const menu_controls_t mt = menu_controls[f];

        // must be one of requested groups
        uint8_t ok = 0;
        for (uint8_t g = 0; g < n_groups; ++g) {
        	 if (groups[g] == 0) break;
        	 if (mt.groups == groups[g]) { ok = 1; break; }
        }
        if (!ok) continue;

        // only include selectable fields
        if (mt.handler == NULL) continue;

        // insert sorted by ui_order (same ordering rule as controller uses)
        uint8_t pos = count;
        while (pos > 0) {
            const save_field_t prev_f = (save_field_t)out->fields_idx[pos - 1];
            if (menu_controls[prev_f].ui_order <= mt.ui_order) break;
            out->fields_idx[pos] = out->fields_idx[pos - 1];
            --pos;
        }
        out->fields_idx[pos] = f;
        ++count;
    }

    out->count = count;
}


uint8_t idx_from_save(save_field_t f, uint8_t case_count) {
    int32_t v = (f != SAVE_FIELD_INVALID) ? save_get(f) : 0;
    if (v < 0) v = 0;
    uint8_t idx = (uint8_t)v;
    return (idx < case_count) ? idx : 0;
}


uint8_t group_list_len(const ctrl_group_id_t *groups)
{
    uint8_t n = 0;
    while (n < 31 && groups[n] != 0) n++;   // 0 terminator
    return n;
}

uint8_t idx_from_position_selector(const page_group_rule_t *sel)
{
    CtrlActiveList u = {0};
    if (!build_union_for_position_page(sel->page, &u)) return 0;

    const uint8_t sel_row = menu_nav_get_select(sel->page);
    uint8_t cursor = 0;

    for (uint8_t i = 0; i < u.count; ++i) {
        const save_field_t f = (save_field_t)u.fields_idx[i];
        const uint8_t span = menu_field_row_span(f);

        if (sel_row < (uint8_t)(cursor + span)) {
            const ctrl_group_id_t gid = (ctrl_group_id_t)menu_controls[f].groups;

            // Map gid -> index within THIS selector's groups[]
            const uint8_t n_groups =
                (sel->case_count == 1) ? group_list_len(sel->groups) : sel->case_count;

            for (uint8_t k = 0; k < n_groups; ++k) {
                if (sel->groups[k] == 0) break;
                if (sel->groups[k] == gid) return k;
            }
            return 0;
        }
        cursor = (uint8_t)(cursor + span);
    }
    return 0;
}


uint8_t build_union_for_position_page(menu_list_t page, CtrlActiveList *out)
{
    ctrl_group_id_t tmp[32] = {0};
    uint8_t tmp_count = 0;

    for (size_t i = 0; i < kPageGroupRulesCount; ++i) {
        const page_group_rule_t *sel = &kPageGroupRules[i];
        if (sel->page != page) continue;
        if (sel->kind != CURRENT_POSITION_BASED) continue;

        const uint8_t n =
            (sel->case_count == 1) ? group_list_len(sel->groups) : sel->case_count;

        for (uint8_t k = 0; k < n; ++k) {
            const ctrl_group_id_t gid = sel->groups[k];
            if (gid == 0) continue;

            uint8_t exists = 0;
            for (uint8_t j = 0; j < tmp_count; ++j) {
                if (tmp[j] == gid) { exists = 1; break; }
            }
            if (!exists && tmp_count < (uint8_t)(sizeof(tmp) / sizeof(tmp[0]) - 1)) {
                tmp[tmp_count++] = gid;
                tmp[tmp_count] = 0;
            }
        }
    }

    if (page == MENU_SPLIT) {
        const uint8_t split_type = (uint8_t)save_get(SPLIT_TYPE);
        ctrl_group_id_t split_gid = CTRL_SPLIT_TYPE_NOTE;

        if (split_type == 1) {
            split_gid = CTRL_SPLIT_TYPE_CH;
        } else if (split_type == 2) {
            split_gid = CTRL_SPLIT_TYPE_VELOCITY;
        }

        uint8_t exists = 0;
        for (uint8_t j = 0; j < tmp_count; ++j) {
            if (tmp[j] == split_gid) { exists = 1; break; }
        }
        if (!exists && tmp_count < (uint8_t)(sizeof(tmp) / sizeof(tmp[0]) - 1)) {
            tmp[tmp_count++] = split_gid;
            tmp[tmp_count] = 0;
        }
    }


    if (tmp_count == 0) return 0;

    build_union_for_groups_local(tmp, tmp_count, out);
    return 1;
}


// ==============================
// Save helper functions / small UI IO
// ==============================

static uint8_t menu_change_check(){
    return menu_select_refresh();

}

static void notify_menu_refresh(){
    threads_display_notify(flag_for_menu(current_menu()));
}

static uint8_t handle_menu_toggle(GPIO_TypeDef *port, uint16_t pin1, uint16_t pin2)
{
    static uint8_t prev_s1 = 1;

    const uint8_t s1 = HAL_GPIO_ReadPin(port, pin1);
    const uint8_t s2 = HAL_GPIO_ReadPin(port, pin2);

    // Rising → falling on s1 while s2 is high
    if (s1 == 0 && prev_s1 == 1 && s2 == 1) {
        osDelay(100);
        // Re-read after debounce
        if (HAL_GPIO_ReadPin(port, pin1) == 0 && HAL_GPIO_ReadPin(port, pin2) == 1) {
            prev_s1 = 0;
            notify_menu_refresh();
            return 1;
        }
    }

    prev_s1 = s1;
    return 0;
}

// Unified “subpage toggle” used by MODIFY and TRANSPOSE
void toggle_subpage(menu_list_t field) {
    if (handle_menu_toggle(GPIOB, Btn1_Pin, Btn2_Pin)) {
        select_press_menu_change(field);  // resets select + rebuilds list
    }
}


static void start_stop_pressed() {
    menu_list_t menu = (menu_list_t)get_current_menu(CURRENT_MENU);
    save_field_t field = sending_field_for_menu(menu);
    if (field != SAVE_FIELD_INVALID) {
        save_modify_u8(field, SAVE_MODIFY_INCREMENT, 0);
        if (menu == MENU_TEMPO) { mt_start_stop(); }
        notify_menu_refresh();
    }
}


void refresh_menu(void)
{
    const uint8_t selecting_menu = menu_change_check();

    uint8_t old_menu = get_current_menu(OLD_MENU);
    uint8_t current = get_current_menu(CURRENT_MENU);

    if (!selecting_menu && old_menu != current) {
        notify_menu_refresh();
    }

    if (!selecting_menu) {
        update_menu();

	const menu_list_t m = current_menu();
	 if (m == MENU_SPLIT || m == MENU_MODIFY || m == MENU_TRANSPOSE || m == MENU_ARPEGGIATOR) {
		 toggle_subpage(m);
	 }

	 static uint8_t old_btn1_state = 1;
	 if (m == MENU_SETTINGS && HAL_GPIO_ReadPin(GPIOB, Btn2_Pin) == GPIO_PIN_SET &&
		 debounce_button(GPIOB, Btn1_Pin, &old_btn1_state, 50)) {
		 settings_save_pressed();
	 	 }
    }

    current = get_current_menu(CURRENT_MENU);
    set_current_menu(OLD_MENU, UI_MODIFY_SET, current);

    static uint8_t old_btn3_state = 1;
    if (!selecting_menu && debounce_button(GPIOB, Btn3_Pin, &old_btn3_state, 50)) {
        start_stop_pressed();
    }
}


void midi_display_on_off(uint8_t on_or_off, uint8_t bottom_line){
    draw_line(ON_OFF_VERT_LINE, 10, ON_OFF_VERT_LINE, bottom_line);
    uint8_t text_position = bottom_line/2-2;
    const char *text_print = message->off_on[on_or_off];
    write_1118(text_print, 95, text_position);
}
