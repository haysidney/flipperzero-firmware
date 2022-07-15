#include <furi.h>
#include <gui/gui.h>
#include <input/input.h>
#include <stdlib.h>
//#include "gpio/usb_uart_bridge.h"
#include "furi_hal.h"

typedef enum {
    EventTypeTick,
    EventTypeKey,
} EventType;

typedef struct {
    EventType type;
    InputEvent input;
} PluginEvent;

typedef struct {
    int x;
    int y;
} PluginState;

// 0: 13,14
// 1: 15,16
const int uart_ch = 0;

static void state_init(PluginState* const plugin_state) {
    plugin_state->x = 100;
    plugin_state->y = 30;
}

static void input_callback(InputEvent* input_event, osMessageQueueId_t event_queue) {
    furi_assert(event_queue);

    PluginEvent event = {.type = EventTypeKey, .input = *input_event};
    osMessageQueuePut(event_queue, &event, 0, osWaitForever);
}

static void render_callback(Canvas* const canvas, void* ctx) {
    const PluginState* plugin_state = acquire_mutex((ValueMutex*)ctx, 25);
    if(plugin_state == NULL) {
        return;
    }
    // border around the edge of the screen
    canvas_draw_frame(canvas, 0, 0, 128, 64);

    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str_aligned(
        canvas, plugin_state->x, plugin_state->y, AlignRight, AlignBottom, "Hello World");

    release_mutex((ValueMutex*)ctx, plugin_state);
}

static void usb_uart_serial_deinit() {
    FURI_LOG_D("ESP8266", "Deinitializing Serial RX");

    furi_hal_uart_set_irq_cb(uart_ch, NULL, NULL);
    if(uart_ch == FuriHalUartIdUSART1)
        furi_hal_console_enable();
    else if(uart_ch == FuriHalUartIdLPUART1)
        furi_hal_uart_deinit(uart_ch);
}

// Callback for when data is received from the ESP8266.
static void usb_uart_on_irq_cb(UartIrqEvent ev, uint8_t data, void* context) {
    UNUSED(data);
    UNUSED(context);
    // UsbUartBridge* usb_uart = (UsbUartBridge*)context;
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;

    FURI_LOG_D("ESP8266", "Called Back.");

    if(ev == UartIrqEventRXNE) {
        FURI_LOG_D("ESP8266", "Received Data.");
        // xStreamBufferSendFromISR(usb_uart->rx_stream, &data, 1, &xHigherPriorityTaskWoken);
        // furi_thread_flags_set(furi_thread_get_id(usb_uart->thread), WorkerEvtRxDone);
        portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
    }

    usb_uart_serial_deinit();
}

static void usb_uart_serial_init() {
    FURI_LOG_D("ESP8266", "Initializing Serial RX");

    if(uart_ch == FuriHalUartIdUSART1) {
        furi_hal_console_disable();
    } else if(uart_ch == FuriHalUartIdLPUART1) {
        furi_hal_uart_init(uart_ch, 115200);
    }
    furi_hal_uart_set_irq_cb(uart_ch, usb_uart_on_irq_cb, NULL);
}

int32_t esp8266_app(void* p) {
    UNUSED(p);
    osMessageQueueId_t event_queue = osMessageQueueNew(8, sizeof(PluginEvent), NULL);

    PluginState* plugin_state = malloc(sizeof(PluginState));
    state_init(plugin_state);

    ValueMutex state_mutex;
    if(!init_mutex(&state_mutex, plugin_state, sizeof(PluginState))) {
        FURI_LOG_E("ESP8266", "cannot create mutex\r\n");
        free(plugin_state);
        return 255;
    }

    // Set system callbacks
    ViewPort* view_port = view_port_alloc();
    view_port_draw_callback_set(view_port, render_callback, &state_mutex);
    view_port_input_callback_set(view_port, input_callback, event_queue);

    // Open GUI and register view_port
    Gui* gui = furi_record_open("gui");
    gui_add_view_port(gui, view_port, GuiLayerFullscreen);

    PluginEvent event;
    for(bool processing = true; processing;) {
        osStatus_t event_status = osMessageQueueGet(event_queue, &event, NULL, 100);

        PluginState* plugin_state = (PluginState*)acquire_mutex_block(&state_mutex);

        if(event_status == osOK) {
            // press events
            if(event.type == EventTypeKey) {
                if(event.input.type == InputTypePress) {
                    switch(event.input.key) {
                    case InputKeyUp:
                        plugin_state->y--;
                        break;
                    case InputKeyDown:
                        plugin_state->y++;
                        break;
                    case InputKeyRight:
                        plugin_state->x++;
                        break;
                    case InputKeyLeft:
                        plugin_state->x--;
                        break;
                    case InputKeyOk:
                        usb_uart_serial_init();
                        break;
                    case InputKeyBack:
                        // Exit the plugin
                        processing = false;
                        break;
                    }
                }
            }
        } else {
            // FURI_LOG_D("ESP8266", "osMessageQueue: event timeout");
            // event timeout
        }

        view_port_update(view_port);
        release_mutex(&state_mutex, plugin_state);
    }

    usb_uart_serial_deinit();
    view_port_enabled_set(view_port, false);
    gui_remove_view_port(gui, view_port);
    furi_record_close("gui");
    view_port_free(view_port);
    osMessageQueueDelete(event_queue);
    delete_mutex(&state_mutex);
    free(plugin_state);
    return 0;
}