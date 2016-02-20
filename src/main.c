#include <pebble.h>
#include "detector.h"

static int *nullPtr;
static int assert_handler(const char *cond, const char *fn, int line) {
    APP_LOG(APP_LOG_LEVEL_ERROR, "Assertion failed: %s:%d: %s is false!", fn, line, cond);
    return 1;
}

#define assert(x) ((void)(!(x) && assert_handler(#x, __FILE__, __LINE__) && (*nullPtr = 42, 1)))

static Window *window;
static TextLayer *text_layer;
static TextLayer *number_layer;
static TextLayer *time_layer;
static ActionBarLayer *action_bar;


int count;

static int recordingStatus;
static int paused;
static uint64_t startTime;
static int now, prostrationLength, firstTime;

#define PKTSIZE 14
#define MAXQ 400

#define PKT_FLAG_POST 1

typedef struct DataPacket {
    struct DataPacket *next;
    uint16_t byteSize;
    uint16_t flags;
    int16_t payload[PKTSIZE*3];
} *Pkt;

static int numRetries, sendState;
static Pkt qHead, qTail;
static int qLength;

#define KEY_ACC_DATA 1
#define KEY_APP_READY 2
#define KEY_POST_DATA 3
#define KEY_DATA_POSTED 4

static void sendFirst() {
    //APP_LOG(APP_LOG_LEVEL_DEBUG, "sending first, rs=%d", recordingStatus);
    
    sendState = 1;
    DictionaryIterator *iterator;
    app_message_outbox_begin(&iterator);   
    if (qHead->flags & PKT_FLAG_POST) {
        dict_write_int8(iterator, KEY_POST_DATA, 1);
        recordingStatus = 4;
    }
    dict_write_data(iterator, KEY_ACC_DATA, (const uint8_t*)&qHead->payload, qHead->byteSize);
    app_message_outbox_send();
}

static void pokeSend() {
    if (sendState != 2) return;
    if (recordingStatus == 0 || recordingStatus == 4) return;
    if (qHead)
        sendFirst();
}

static void inbox_received_callback(DictionaryIterator *iterator, void *context) {
    APP_LOG(APP_LOG_LEVEL_DEBUG, "Message received!");
    Tuple *data = dict_find(iterator, KEY_APP_READY);
    if (data && sendState == 0) {
        APP_LOG(APP_LOG_LEVEL_INFO, "Got ready msg fom phone; will stream data now");
        sendState = 2;
        pokeSend();
    }
    
    static char errCode[30];
    
    data = dict_find(iterator, KEY_DATA_POSTED);
    if (data) {
        APP_LOG(APP_LOG_LEVEL_DEBUG, "Data posted: %d", (int)data->value->int32);
        if (data->value->int32 == 200)
            text_layer_set_text(text_layer, "Saved!");
        else {
            snprintf(errCode, sizeof errCode, "Error (%d) saving", (int)data->value->int32);
            text_layer_set_text(text_layer, errCode);
        }
        recordingStatus = 0;
    }
}

static int displayUpdateStatus = 0;
static void showCount() {
    displayUpdateStatus = 0;
    static char s_buffer[10];
    static char s_time[10];
    snprintf(s_buffer, sizeof(s_buffer), "%d", count);
    text_layer_set_text(number_layer, s_buffer);
    if (qLength)
        snprintf(s_time, sizeof(s_time), "%d to go", qLength);
    else
        snprintf(s_time, sizeof(s_time), "%02d:%02d", prostrationLength/60, prostrationLength % 60);
    text_layer_set_text(time_layer, s_time);
}

static void queueDisplayUpdate() {
    if (displayUpdateStatus == 0) {
        displayUpdateStatus = 1;
        app_timer_register(1000, showCount, NULL);
    }
}

static void inbox_dropped_callback(AppMessageResult reason, void *context) {
  APP_LOG(APP_LOG_LEVEL_ERROR, "Message dropped!");
}


static void outbox_failed_callback(DictionaryIterator *iterator, AppMessageResult reason, void *context) {
    // APP_LOG(APP_LOG_LEVEL_ERROR, "Outbox send failed!");
    queueDisplayUpdate();
    if (numRetries++ < 1000) {
        app_timer_register(1000, sendFirst, NULL);
    }
}

static void popQEntry() {
    Pkt p = qHead;
    qHead = p->next;
    qLength--;
    free(p);

    if (!qHead) {
        qTail = NULL;
        assert(qLength == 0);
    }
}

static void outbox_sent_callback(DictionaryIterator *iterator, void *context) {
    //APP_LOG(APP_LOG_LEVEL_INFO, "Outbox send success!");
    numRetries = 0;
    popQEntry();
    queueDisplayUpdate();
    if (qHead) {
        sendFirst();
    } else {
        sendState = 2;
    }
}

void record_one() {
    count++;
    if (firstTime == 0)
        // assume prostration started 3 seconds ago
        firstTime = now - 3;
    prostrationLength = now - firstTime;
    showCount();
}

//#define DBG(args...) APP_LOG(APP_LOG_LEVEL_DEBUG, args)
#define DBG(args...)

static void data_handler(AccelData *data, uint32_t num_samples) {
    assert(num_samples <= PKTSIZE);
    
    if (paused) return;
    
    Pkt pkt = (Pkt)malloc(sizeof(struct DataPacket));
    int ptr = 0;
    
    for (unsigned i = 0; i < num_samples; ++i) {
        if (data[i].did_vibrate)
            continue;
        
        if (startTime == 0)
            startTime = data[i].timestamp;
        
        now = (data[i].timestamp - startTime) / 1000 + 100;
        process_sample(data[i].x, data[i].y, data[i].z, data[i].timestamp);
        
        #define R(f,k) pkt->payload[ptr++] = ((data[i].f) & 0x1fff) | (((detector_state >> k) & 0x7) << 13)
        R(x,0);
        R(y,3);
        R(z,6);
    }
    
    if (recordingStatus == 0) {
        free(pkt);
        return;
    }
    
    pkt->next = NULL;
    pkt->byteSize = ptr * 2;
    pkt->flags = 0;
    
    if (recordingStatus == 2) {
        pkt->flags |= PKT_FLAG_POST;
        recordingStatus = 3;
    }
    
    if (qHead == NULL) {
        qHead = qTail = pkt;
        qLength = 1;
        pokeSend();
    } else {
        if (qLength > MAXQ) {
            popQEntry();
        }

        if (qHead) {
            qTail->next = pkt;
        } else {
            assert(qLength == 0);
            qHead = pkt;
        }

        qTail = pkt;
        qLength++;
    }
}

static void select_click_handler(ClickRecognizerRef recognizer, void *context) {
    count = 0;
    showCount();
}

static void startUp() {
    text_layer_set_text(text_layer, "Swim!");
    APP_LOG(APP_LOG_LEVEL_INFO, "Mem free: %d", (int)heap_bytes_free());    
    uint32_t num_samples = PKTSIZE;
    accel_data_service_subscribe(num_samples, data_handler);
    accel_service_set_sampling_rate(ACCEL_SAMPLING_50HZ);
    showCount();
}

static GBitmap *iconTrash, *iconCloudup, *iconResume, *iconPause;

static void up_click_handler(ClickRecognizerRef recognizer, void *context) {
    if (paused) {
        paused = 0;
        startUp();
        action_bar_layer_set_icon_animated(action_bar, BUTTON_ID_UP, iconPause, true);
    } else {
        text_layer_set_text(text_layer, "Paused.");
        paused = 1;
        accel_data_service_unsubscribe();
        action_bar_layer_set_icon_animated(action_bar, BUTTON_ID_UP, iconResume, true);
    }
}

static void down_click_handler(ClickRecognizerRef recognizer, void *context) {    
    if (sendState == 0) return;
    
    if (recordingStatus == 0) {
        text_layer_set_text(text_layer, "Recording...");    
        recordingStatus = 1;
        pokeSend();
    } else if (recordingStatus == 1) {
        text_layer_set_text(text_layer, "Saving...");    
        recordingStatus = 2;
        pokeSend();
    } else {
    }
}

static void click_config_provider(void *context) {
  window_single_click_subscribe(BUTTON_ID_SELECT, select_click_handler);
  window_single_click_subscribe(BUTTON_ID_UP, up_click_handler);
  window_single_click_subscribe(BUTTON_ID_DOWN, down_click_handler);
}

static void window_load(Window *window) {
    Layer *window_layer = window_get_root_layer(window);
    GRect bounds = layer_get_bounds(window_layer);
    
    int x = 5;
    int w = bounds.size.w - 30 - x;

    text_layer = text_layer_create(GRect(x, 0, w, 60));
    text_layer_set_text_alignment(text_layer, GTextAlignmentCenter);
    text_layer_set_overflow_mode(text_layer, GTextOverflowModeWordWrap);
    text_layer_set_font(text_layer, fonts_get_system_font(FONT_KEY_GOTHIC_28));

    number_layer = text_layer_create(GRect(x, 60, w, bounds.size.h));
    text_layer_set_text(number_layer, "");
    text_layer_set_text_alignment(number_layer, GTextAlignmentCenter);
    text_layer_set_font(number_layer, fonts_get_system_font(FONT_KEY_BITHAM_42_BOLD));

    time_layer = text_layer_create(GRect(x, bounds.size.h - 35, w, 35));
    text_layer_set_text(time_layer, "00:00");
    text_layer_set_text_alignment(time_layer, GTextAlignmentCenter);
    text_layer_set_font(time_layer, fonts_get_system_font(FONT_KEY_GOTHIC_28));

    layer_add_child(window_layer, text_layer_get_layer(text_layer));
    layer_add_child(window_layer, text_layer_get_layer(number_layer));
    layer_add_child(window_layer, text_layer_get_layer(time_layer));
    
    
    action_bar = action_bar_layer_create();
    action_bar_layer_add_to_window(action_bar, window);
    action_bar_layer_set_click_config_provider(action_bar, click_config_provider);

    // Set the icons:
    iconTrash = gbitmap_create_with_resource(RESOURCE_ID_IMG_TRASH);
    iconCloudup = gbitmap_create_with_resource(RESOURCE_ID_IMG_CLOUDUP);
    iconResume = gbitmap_create_with_resource(RESOURCE_ID_IMG_RESUME);
    iconPause = gbitmap_create_with_resource(RESOURCE_ID_IMG_BREAK);
    
    action_bar_layer_set_icon_animated(action_bar, BUTTON_ID_UP, iconPause, true);
    action_bar_layer_set_icon_animated(action_bar, BUTTON_ID_SELECT, iconTrash, true);
    action_bar_layer_set_icon_animated(action_bar, BUTTON_ID_DOWN, iconCloudup, true);
    
    startUp();
}

static void window_unload(Window *window) {
    text_layer_destroy(text_layer);
    gbitmap_destroy(iconTrash);
    gbitmap_destroy(iconCloudup);
    gbitmap_destroy(iconResume);
    gbitmap_destroy(iconPause);
}

static void init(void) {
    // Register callbacks
    app_message_register_inbox_received(inbox_received_callback);
    app_message_register_inbox_dropped(inbox_dropped_callback);
    app_message_register_outbox_failed(outbox_failed_callback);
    app_message_register_outbox_sent(outbox_sent_callback);

    // Open AppMessage with sensible buffer sizes
    app_message_open(126, 126);

    window = window_create();
    window_set_click_config_provider(window, click_config_provider);
    window_set_window_handlers(window, (WindowHandlers) {
        .load = window_load,
        .unload = window_unload,
    });
    
    const bool animated = true;
    window_stack_push(window, animated);
}

static void deinit(void) {
    window_destroy(window);
    accel_data_service_unsubscribe();
    app_message_deregister_callbacks();
    while (qHead) {
        popQEntry();
    }
}

int main(void) {
    init();
    app_event_loop();
    deinit();
}

// vim: sw=4
