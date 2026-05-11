#include "menus.h"
#include "_menu_ui.h"
#include "main.h" //GPIO

#include "midi_transform.h" //calculate_incoming_midi
#include "midi_arp.h" //arp_on_tempo_tick
#include "midi_tempo.h" // mt_process_pending_tempo_out

#include "threads.h"
#include "usb_device.h" //MX_USB_DEVICE_Init
#include "utils.h" //debounce

extern TIM_HandleTypeDef htim3;
extern TIM_HandleTypeDef htim4;

// -------------------------
// Display flag helpers (local to threads.c)
// -------------------------
static inline uint32_t display_flag_for_menu(menu_list_t m)
{
    return (m < AMOUNT_OF_MENUS) ? (1 << (uint32_t)m) : 0;
}

// Wait on any menu flag bit [0..AMOUNT_OF_MENUS-1]
#define DISPLAY_FLAG_MASK ((AMOUNT_OF_MENUS >= 32) ? 0xFFFFFFFF : ((1 << (uint32_t)AMOUNT_OF_MENUS) - 1))

// -------------------------
// Internal state
// -------------------------
static osThreadId_t s_display_handle      = NULL;
static osThreadId_t s_midi_core_handle    = NULL;
static osThreadId_t s_medium_tasks_handle = NULL;
static volatile uint32_t s_pending_arp_ticks = 0;
static volatile uint32_t s_pending_tempo_ticks = 0;

static osEventFlagsId_t s_display_flags;
static const osEventFlagsAttr_t s_flags_attrs = { .name = "display_flags" };

static uint32_t threads_take_pending_ticks(volatile uint32_t *counter)
{
    return __atomic_exchange_n(counter, 0, __ATOMIC_ACQ_REL);
}

// -------------------------
// Thread attributes
// -------------------------
static const osThreadAttr_t s_display_attrs = {
  .name       = "display_update",
  .stack_size = 4096,
  .priority   = (osPriority_t)osPriorityBelowNormal,
};
static const osThreadAttr_t s_midi_core_attrs = {
  .name       = "midi_core",
  .stack_size = 128 * 4,
  .priority   = (osPriority_t)osPriorityRealtime,
};
static const osThreadAttr_t s_medium_tasks_attrs = {
  .name       = "medium_tasks",
  .stack_size = 128 * 4,
  .priority   = (osPriority_t)osPriorityAboveNormal,
};


// -------------------------
// Display thread
// -------------------------
static void DisplayUpdateThread(void *arg)
{
    (void)arg;
    for (;;)
    {
        uint32_t f = osEventFlagsWait(
            s_display_flags,
            DISPLAY_FLAG_MASK,
            osFlagsWaitAny,
            1
        );

        screen_update_menu(f);
        osDelay(30);
    }
}

// -------------------------
// MIDI core thread
// -------------------------
static void MidiCoreThread(void *argument)
{
    (void)argument;
    MX_USB_DEVICE_Init();
    for (;;)
    {
        calculate_incoming_midi();

        uint32_t flags = osThreadFlagsWait(MIDI_CORE_FLAG_MASK, osFlagsWaitAny, 1);
        (void)flags;

        uint32_t arp_ticks_to_process = threads_take_pending_ticks(&s_pending_arp_ticks);
        while (arp_ticks_to_process > 0) {
            arp_on_tempo_tick();
            --arp_ticks_to_process;
        }

        uint32_t tempo_ticks_to_process = threads_take_pending_ticks(&s_pending_tempo_ticks);
        while (tempo_ticks_to_process > 0) {
            mt_process_pending_tempo_out();
            --tempo_ticks_to_process;
        }
    }
}

// -------------------------
// Medium tasks thread
// -------------------------
static void MediumTasksThread(void *argument)
{
    (void)argument;

    // Initial menu draw trigger
    set_current_menu(CURRENT_MENU, UI_MODIFY_SET, save_get(SETTINGS_START_MENU));
    set_current_menu(OLD_MENU, UI_MODIFY_SET, AMOUNT_OF_MENUS);

    for (;;)
    {
        refresh_menu();

        panic_midi(GPIOB, Btn1_Pin, Btn2_Pin);

        osDelay(10);
    }
}

// -------------------------
// Public API
// -------------------------
void threads_start(void)
{
    s_display_flags       = osEventFlagsNew(&s_flags_attrs);
    s_display_handle      = osThreadNew(DisplayUpdateThread, NULL, &s_display_attrs);
    s_midi_core_handle    = osThreadNew(MidiCoreThread,       NULL, &s_midi_core_attrs);
    s_medium_tasks_handle = osThreadNew(MediumTasksThread,    NULL, &s_medium_tasks_attrs);
}

void threads_display_notify(uint32_t flags)
{
    if (s_display_flags) osEventFlagsSet(s_display_flags, flags);
}

void threads_midi_core_set_flags(uint32_t flags)
{
    if (s_midi_core_handle == NULL) {
        return;
    }

    if ((flags & MIDI_CORE_FLAG_ARP_TICK) != 0) {
        __atomic_fetch_add(&s_pending_arp_ticks, 1, __ATOMIC_RELAXED);
    }
    if ((flags & MIDI_CORE_FLAG_TEMPO_OUT) != 0) {
        __atomic_fetch_add(&s_pending_tempo_ticks, 1, __ATOMIC_RELAXED);
    }

    osThreadFlagsSet(s_midi_core_handle, flags);
}

void threads_midi_core_notify_tempo_tick_from_isr(void)
{
    if (s_midi_core_handle == NULL) {
        return;
    }

    __atomic_fetch_add(&s_pending_arp_ticks, 1, __ATOMIC_RELAXED);
    __atomic_fetch_add(&s_pending_tempo_ticks, 1, __ATOMIC_RELAXED);
    osThreadFlagsSet(s_midi_core_handle, MIDI_CORE_FLAG_ARP_TICK | MIDI_CORE_FLAG_TEMPO_OUT);
}




osThreadId_t threads_display_handle(void)
{
    return s_display_handle;
}
