#include "ui.h"

#include "ui_menu.h"
#include "ui_prompt.h"

#include "baking_auth.h"
#include "keys.h"
#include "to_string.h"

#include <stdbool.h>
#include <string.h>

ux_state_t ux;
unsigned char G_io_seproxyhal_spi_buffer[IO_SEPROXYHAL_BUFFER_SIZE_B];

static callback_t ok_callback;
static callback_t cxl_callback;

static unsigned button_handler(unsigned button_mask, unsigned button_mask_counter);

static uint32_t ux_step, ux_step_count;

#define PROMPT_CYCLES 3
static uint32_t timeout_cycle_count;

static char idle_text[16];
char baking_auth_text[PKH_STRING_SIZE];

void require_pin(void) {
    bolos_ux_params_t params;
    memset(&params, 0, sizeof(params));
    params.ux_id = BOLOS_UX_VALIDATE_PIN;
    os_ux_blocking(&params);
}

#ifdef BAKING_APP
const bagl_element_t ui_idle_screen[] = {
    // type                               userid    x    y   w    h  str rad
    // fill      fg        bg      fid iid  txt   touchparams...       ]
    {{BAGL_RECTANGLE, 0x00, 0, 0, 128, 32, 0, 0, BAGL_FILL, 0x000000, 0xFFFFFF,
      0, 0},
     NULL,
     0,
     0,
     0,
     NULL,
     NULL,
     NULL},

    {{BAGL_ICON, 0x00, 3, 12, 7, 7, 0, 0, 0, 0xFFFFFF, 0x000000, 0,
      BAGL_GLYPH_ICON_CROSS},
     NULL,
     0,
     0,
     0,
     NULL,
     NULL,
     NULL},

    //{{BAGL_ICON                           , 0x01,  21,   9,  14,  14, 0, 0, 0
    //, 0xFFFFFF, 0x000000, 0, BAGL_GLYPH_ICON_TRANSACTION_BADGE  }, NULL, 0, 0,
    //0, NULL, NULL, NULL },
    {{BAGL_LABELINE, 0x01, 0, 12, 128, 12, 0, 0, 0, 0xFFFFFF, 0x000000,
      BAGL_FONT_OPEN_SANS_EXTRABOLD_11px | BAGL_FONT_ALIGNMENT_CENTER, 0},
     "Last Block Level",
     0,
     0,
     0,
     NULL,
     NULL,
     NULL},

    {{BAGL_LABELINE, 0x01, 0, 26, 128, 12, 0, 0, 0, 0xFFFFFF, 0x000000,
      BAGL_FONT_OPEN_SANS_EXTRABOLD_11px | BAGL_FONT_ALIGNMENT_CENTER, 0},
     idle_text,
     0,
     0,
     0,
     NULL,
     NULL,
     NULL},

    {{BAGL_LABELINE, 0x02, 0, 12, 128, 12, 0, 0, 0, 0xFFFFFF, 0x000000,
      BAGL_FONT_OPEN_SANS_EXTRABOLD_11px | BAGL_FONT_ALIGNMENT_CENTER, 0},
     "Baking Key",
     0,
     0,
     0,
     NULL,
     NULL,
     NULL},

    {{BAGL_LABELINE, 0x02, 23, 26, 82, 12, 0x80 | 10, 0, 0, 0xFFFFFF, 0x000000,
      BAGL_FONT_OPEN_SANS_EXTRABOLD_11px | BAGL_FONT_ALIGNMENT_CENTER, 26},
     baking_auth_text,
     0,
     0,
     0,
     NULL,
     NULL,
     NULL},
};

static bool do_nothing(void) {
    return false;
}
#endif

static void ui_idle(void) {
#ifdef BAKING_APP
    update_auth_text();
    ui_display(ui_idle_screen, sizeof(ui_idle_screen)/sizeof(*ui_idle_screen),
               do_nothing, exit_app, 2);
#else
    main_menu();
#endif
}

void change_idle_display(uint32_t new) {
    number_to_string(idle_text, new);
    update_auth_text();
}

void ui_initial_screen(void) {
#ifdef BAKING_APP
    change_idle_display(N_data.highest_level);
#endif
    ui_idle();
}

static bool is_idling(void) {
    return cxl_callback == exit_app;
}

static void timeout(void) {
    if (is_idling()) {
        // Idle app timeout
        update_auth_text();
        timeout_cycle_count = 0;
        UX_REDISPLAY();
    } else {
        // Prompt timeout -- simulate cancel button
        (void) button_handler(BUTTON_EVT_RELEASED | BUTTON_LEFT, 0);
    }
}

unsigned button_handler(unsigned button_mask, __attribute__((unused)) unsigned button_mask_counter) {
    callback_t callback;
    switch (button_mask) {
        case BUTTON_EVT_RELEASED | BUTTON_LEFT:
            callback = cxl_callback;
            break;
        case BUTTON_EVT_RELEASED | BUTTON_RIGHT:
            callback = ok_callback;
            break;
        default:
            return 0;
    }
    if (callback()) {
        ui_idle();
    }
    return 0;
}

const bagl_element_t *prepro(const bagl_element_t *element) {
    // Always display elements with userid 0
    if (element->component.userid == 0) return element;

    uint32_t min = 2000;
    uint32_t div = 2;

    if (is_idling()) {
        min = 4000;
    }

    if (ux_step == element->component.userid - 1 || element->component.userid == 100) {
        // timeouts are in millis
        if (ux_step_count > 1) {
            UX_CALLBACK_SET_INTERVAL(MAX(min,
                                         (1500 + bagl_label_roundtrip_duration_ms(element, 7)) / div));
        } else {
            UX_CALLBACK_SET_INTERVAL(30000 / PROMPT_CYCLES);
        }
        return element;
    } else {
        return NULL;
    }
}

void ui_display(const bagl_element_t *elems, size_t sz, callback_t ok_c, callback_t cxl_c,
                uint32_t step_count) {
    // Adapted from definition of UX_DISPLAY in header file
    timeout_cycle_count = 0;
    ux_step = 0;
    switch_screen(0);
    ux_step_count = step_count;
    ok_callback = ok_c;
    cxl_callback = cxl_c;
    ux.elements = elems;
    ux.elements_count = sz;
    ux.button_push_handler = button_handler;
    ux.elements_preprocessor = prepro;
    UX_WAKE_UP();
    UX_REDISPLAY();
}

unsigned char io_event(__attribute__((unused)) unsigned char channel) {
    // nothing done with the event, throw an error on the transport layer if
    // needed

    // can't have more than one tag in the reply, not supported yet.
    switch (G_io_seproxyhal_spi_buffer[0]) {
    case SEPROXYHAL_TAG_FINGER_EVENT:
        UX_FINGER_EVENT(G_io_seproxyhal_spi_buffer);
        break;

    case SEPROXYHAL_TAG_BUTTON_PUSH_EVENT:
        UX_BUTTON_PUSH_EVENT(G_io_seproxyhal_spi_buffer);
        break;

    case SEPROXYHAL_TAG_DISPLAY_PROCESSED_EVENT:
        UX_DISPLAYED_EVENT({});
        break;
    case SEPROXYHAL_TAG_TICKER_EVENT:
        if (ux.callback_interval_ms != 0) {
            ux.callback_interval_ms -= MIN(ux.callback_interval_ms, 100);
            if (ux.callback_interval_ms == 0) {
                // prepare next screen
                ux_step = (ux_step + 1) % ux_step_count;
                switch_screen(ux_step);

                // check if we've timed out
                if (ux_step == 0) {
                    timeout_cycle_count++;
                    if (timeout_cycle_count == PROMPT_CYCLES) {
                        timeout();
                        break; // timeout() will often display a new screen
                    }
                }

                // redisplay screen
                UX_REDISPLAY();
            }
        }
        break;
    default:
        // unknown events are acknowledged
        break;
    }

    // close the event if not done previously (by a display or whatever)
    if (!io_seproxyhal_spi_is_status_sent()) {
        io_seproxyhal_general_status();
    }
    // command has been processed, DO NOT reset the current APDU transport
    // TODO: I don't understand that comment or what this return value means
    return 1;
}

void io_seproxyhal_display(const bagl_element_t *element) {
    return io_seproxyhal_display_default((bagl_element_t *)element);
}

__attribute__((noreturn))
bool exit_app(void) {
#ifdef BAKING_APP
    require_pin();
#endif
    BEGIN_TRY_L(exit) {
        TRY_L(exit) {
            os_sched_exit(-1);
        }
        FINALLY_L(exit) {
        }
    }
    END_TRY_L(exit);

    THROW(0); // Suppress warning
}

void ui_init(void) {
    UX_INIT();
}
