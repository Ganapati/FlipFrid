#include <furi.h>
#include <gui/gui.h>
#include <input/input.h>
#include <stdlib.h>

#include "../lfrfid/helpers/rfid_timer_emulator.h"
#include "flipfrid.h"

#define MAX_REPEAT 3
#define TAG "FLIPFRID"

uint8_t em_id_list[12][5] = {
    {0x00, 0x00, 0x00, 0x00, 0x00}, // Default uid
    {0xFF, 0xFF, 0xFF, 0xFF, 0xFF}, // Only FF
    {0x11, 0x11, 0x11, 0x11, 0x11}, // Only 11
    {0x22, 0x22, 0x22, 0x22, 0x22}, // Only 22
    {0x33, 0x33, 0x33, 0x33, 0x33}, // Only 33
    {0x44, 0x44, 0x44, 0x44, 0x44}, // Only 44
    {0x55, 0x55, 0x55, 0x55, 0x55}, // Only 55
    {0x66, 0x66, 0x66, 0x66, 0x66}, // Only 66
    {0x77, 0x77, 0x77, 0x77, 0x77}, // Only 77
    {0x88, 0x88, 0x88, 0x88, 0x88}, // Only 88
    {0x99, 0x99, 0x99, 0x99, 0x99}, // Only 99
    {0x12, 0x34, 0x56, 0x78, 0x9A}, // Incremental UID
};

typedef enum {
    EventTypeTick,
    EventTypeKey,
} EventType;

// STRUCTS
typedef struct {
    uint8_t current_uid;
    uint8_t current_uid_repeat;
    RfidTimerEmulator emulator;
} FlipFridState;

typedef struct {
    EventType type;
    InputEvent input;
} Event;

void emit(uint8_t uid[], RfidTimerEmulator emulator) {
    FURI_LOG_I(TAG, "Emit func");
    emulator.stop();
    emulator.start(
        LfrfidKeyType::KeyEM4100, uid, lfrfid_key_get_type_data_count(LfrfidKeyType::KeyEM4100));
}

static void flipfrid_draw_callback(Canvas* const canvas, void* ctx) {
    const FlipFridState* flipfrid_state = (FlipFridState*)acquire_mutex((ValueMutex*)ctx, 25);
    UNUSED(ctx);

    if(flipfrid_state == NULL) {
        return;
    }

    canvas_clear(canvas);
    canvas_set_color(canvas, ColorBlack);

    // Frame
    canvas_draw_frame(canvas, 0, 0, 128, 64);

    // Title
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str_aligned(canvas, 64, 8, AlignCenter, AlignTop, "Flip/Frid");

    // UID
    canvas_set_font(canvas, FontSecondary);
    char uid[15];
    snprintf(
        uid,
        sizeof(uid),
        "%X:%X:%X:%X:%X",
        em_id_list[flipfrid_state->current_uid][0],
        em_id_list[flipfrid_state->current_uid][1],
        em_id_list[flipfrid_state->current_uid][2],
        em_id_list[flipfrid_state->current_uid][3],
        em_id_list[flipfrid_state->current_uid][4]);
    canvas_draw_str_aligned(canvas, 64, 32, AlignCenter, AlignCenter, uid);

    // Progress bar
    char progress[MAX_REPEAT + 2];
    strcat(progress, "[");
    for(int i = 0; i < flipfrid_state->current_uid_repeat; i++) {
        strcat(progress, "=");
    }
    for(int i = 0; i < (MAX_REPEAT - flipfrid_state->current_uid_repeat); i++) {
        strcat(progress, "-");
    }
    strcat(progress, "]");
    canvas_draw_str_aligned(canvas, 64, 52, AlignCenter, AlignBottom, progress);

    emit(em_id_list[flipfrid_state->current_uid], flipfrid_state->emulator);

    release_mutex((ValueMutex*)ctx, flipfrid_state);
}

void flipfrid_input_callback(InputEvent* input_event, FuriMessageQueue* event_queue) {
    furi_assert(event_queue);
    Event event = {.type = EventTypeKey, .input = *input_event};
    furi_message_queue_put(event_queue, &event, FuriWaitForever);
}

static void flipfrid_timer_callback(FuriMessageQueue* event_queue) {
    furi_assert(event_queue);
    InputEvent ie;
    ie.sequence = 0;
    ie.type = InputTypePress;
    ie.key = InputKeyOk;
    Event event = {.type = EventTypeTick, .input = ie};
    furi_message_queue_put(event_queue, &event, 0);
}

FlipFridApp::FlipFridApp() {
}

// ENTRYPOINT
void FlipFridApp::run() {
    // Input
    FURI_LOG_I(TAG, "Initializing input");
    FuriMessageQueue* event_queue = furi_message_queue_alloc(8, sizeof(InputEvent));
    FlipFridState* flipfrid_state = (FlipFridState*)malloc(sizeof(FlipFridState));
    ValueMutex flipfrid_state_mutex;

    // Mutex
    FURI_LOG_I(TAG, "Initializing flipfrid mutex");
    if(!init_mutex(&flipfrid_state_mutex, flipfrid_state, sizeof(FlipFridState))) {
        FURI_LOG_E(TAG, "cannot create mutex\r\n");
        furi_message_queue_free(event_queue);
        free(flipfrid_state);
    }

    // Configure view port
    FURI_LOG_I(TAG, "Initializing viewport");
    ViewPort* view_port = view_port_alloc();
    view_port_draw_callback_set(view_port, flipfrid_draw_callback, &flipfrid_state_mutex);
    view_port_input_callback_set(view_port, flipfrid_input_callback, event_queue);

    // Configure timer
    FURI_LOG_I(TAG, "Initializing timer");
    FuriTimer* timer =
        furi_timer_alloc(flipfrid_timer_callback, FuriTimerTypePeriodic, event_queue);
    furi_timer_start(timer, furi_kernel_get_tick_frequency() / 6); // configTICK_RATE_HZ_RAW 1000

    // Register view port in GUI
    FURI_LOG_I(TAG, "Initializing gui");
    Gui* gui = (Gui*)furi_record_open(RECORD_GUI);
    gui_add_view_port(gui, view_port, GuiLayerFullscreen);

    Event event;
    flipfrid_state->current_uid = 0;
    flipfrid_state->current_uid_repeat = 0;

    bool running = true;
    while(running) {
        // Get next event
        FuriStatus event_status = furi_message_queue_get(event_queue, &event, 100);

        if(event_status == FuriStatusOk) {
            if(event.type == EventTypeKey) {
                FURI_LOG_D(TAG, "EventTypeKey");
                // TODO: Find why event.input.key is always 10
                FURI_LOG_D(TAG, "PRESS");
                running = false;
            } else if(event.type == EventTypeTick) {
                FURI_LOG_D(TAG, "EventTypeTick");
                // Loop 3 times each uid
                flipfrid_state->current_uid_repeat++;
                if(flipfrid_state->current_uid_repeat > MAX_REPEAT) {
                    flipfrid_state->current_uid_repeat = 0;
                    flipfrid_state->current_uid++;
                    if(flipfrid_state->current_uid >= sizeof(em_id_list) / 5) {
                        flipfrid_state->current_uid = 0;
                    }
                }
            }
            // Update
            view_port_update(view_port);
        }
    }

    // Cleanup
    FURI_LOG_I(TAG, "Cleaning up");
    flipfrid_state->emulator.stop();
    free(flipfrid_state);
    gui_remove_view_port(gui, view_port);
    view_port_free(view_port);
    furi_message_queue_free(event_queue);
    furi_record_close(RECORD_GUI);
}