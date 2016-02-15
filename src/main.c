#include <pebble.h>


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

static int phaseStop, phaseLen, aboveZeroStart = -1, phaseMax, phaseMaxAt, tryPhaseMaxAt, lastCount;
static int count, sampleNo, lastTime;
static int recordingStatus;

#define PKTSIZE 14

typedef struct DataPacket {
    struct DataPacket *next;
    int byteSize;
    int16_t payload[PKTSIZE*3];
} *Pkt;

static int numRetries, sendState;
static Pkt queue;

#define KEY_ACC_DATA 1
#define KEY_APP_READY 2
#define KEY_POST_DATA 3
#define KEY_DATA_POSTED 4

static void sendFirst() {
    //APP_LOG(APP_LOG_LEVEL_DEBUG, "sending first, rs=%d", recordingStatus);
    
    sendState = 1;
    DictionaryIterator *iterator;
    app_message_outbox_begin(&iterator);   
    if (recordingStatus == 2) {
        dict_write_int8(iterator, KEY_POST_DATA, 1);
        recordingStatus = 3;
    }
    dict_write_data(iterator, KEY_ACC_DATA, (const uint8_t*)&queue->payload, queue->byteSize);
    app_message_outbox_send();
}

static void pokeSend() {
    if (sendState != 2) return;
    if (recordingStatus == 0 || recordingStatus == 3) return;
    if (queue)
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
    
    data = dict_find(iterator, KEY_DATA_POSTED);
    if (data) {
        APP_LOG(APP_LOG_LEVEL_DEBUG, "Data posted: %d", (int)data->value->int32);
        if (data->value->int32 == 200)
            text_layer_set_text(text_layer, "Saved!");
        else
            text_layer_set_text(text_layer, "Error saving.");
        recordingStatus = 0;
    }
}

static void inbox_dropped_callback(AppMessageResult reason, void *context) {
  APP_LOG(APP_LOG_LEVEL_ERROR, "Message dropped!");
}

static void outbox_failed_callback(DictionaryIterator *iterator, AppMessageResult reason, void *context) {
    APP_LOG(APP_LOG_LEVEL_ERROR, "Outbox send failed!");
    if (numRetries++ < 10) {
        sendFirst();
    }
}

static void outbox_sent_callback(DictionaryIterator *iterator, void *context) {
    //APP_LOG(APP_LOG_LEVEL_INFO, "Outbox send success!");
    numRetries = 0;
    Pkt p = queue;
    queue = p->next;
    free(p);
    if (queue) {
        sendFirst();
    } else {
        sendState = 2;
    }
}

static void showCount() {
    static char s_buffer[10];
    static char s_time[10];
    snprintf(s_buffer, sizeof(s_buffer), "%d", count);
    text_layer_set_text(number_layer, s_buffer);
    snprintf(s_time, sizeof(s_time), "%02d:%02d", lastTime/60, lastTime % 60);
    text_layer_set_text(time_layer, s_time);
}

static uint64_t startTime;
static int ind = 0;

//#define DBG(args...) APP_LOG(APP_LOG_LEVEL_DEBUG, args)
#define DBG(args...)

static void data_handler(AccelData *data, uint32_t num_samples) {
    assert(num_samples <= PKTSIZE);
    
    Pkt pkt = (Pkt)malloc(sizeof(struct DataPacket));
    int ptr = 0;
    
    // we need a peek 150ms wide and at least 1300 high
    // followed by a value under -1600 less than 1500ms after the peak
    for (unsigned i = 0; i < num_samples; ++i) {
        if (data[i].did_vibrate)
            continue;
        
        if (startTime == 0)
            startTime = data[i].timestamp - 10000;
        
        int now = (int)(data[i].timestamp - startTime);
        
        int x = -data[i].x;  // - for left hand
        
        if (x > -500) {
            if (aboveZeroStart < 0) {
                aboveZeroStart = now;
                ind = 1;
            }
            if (x > 1000 && x > phaseMax) {
                phaseMax = x;
                tryPhaseMaxAt = now;
                ind = 2;
            }
            if (tryPhaseMaxAt && now - aboveZeroStart > 200) {
                DBG("Real phase max, %d", (int)(now - aboveZeroStart));
                phaseMaxAt = tryPhaseMaxAt;
                tryPhaseMaxAt = 0;
                ind = 3;
            }
        }
        else {
            if (aboveZeroStart > 0) {
                if (phaseMaxAt >= aboveZeroStart) {
                    phaseStop = now;
                    phaseLen = now - aboveZeroStart;
                }
                aboveZeroStart = -1;
                phaseMax = 0;
                tryPhaseMaxAt = 0;
            }
            ind = 0;
            if (x < -1000) {
                DBG("d0=%d d1=%d", now - lastHigh, now - phaseMaxAt);
                if (now - phaseMaxAt < 4000 && now - phaseStop < phaseLen / 4 + 600) {
                    // do not count too often; 4-5s would be the real non-testing limit
                    if (now - lastCount > 2500) {
                        ind = 4;
                        lastCount = now;
                        count = count + 1;
                        phaseMaxAt = 0;
                        vibes_short_pulse();
                        lastTime = (now - 10000) / 1000;
                        showCount();
                    }
                }
            }
        }
        
        sampleNo++;
        
        #define R(f,k) pkt->payload[ptr++] = ((data[i].f) & 0x1fff) | (((ind >> k) & 0x7) << 13)
        
        R(x,0);
        R(y,3);
        R(z,6);
        
        DBG("ACCEL: %d,%d,%d,%d", sampleNo, now, x, ind);
    }
    
    pkt->next = NULL;
    pkt->byteSize = ptr * 2;
    
    if (queue == NULL) {
        queue = pkt;
        pokeSend();
    } else {
        Pkt last = queue;
        int depth = 0;
        while (last->next) {
            last = last->next;
            depth++;
        }        
        last->next = pkt;
        if (depth > 200) {
            pkt = queue;
            queue = queue->next;
            free(pkt);
        }
    }
    
    //APP_LOG(APP_LOG_LEVEL_DEBUG, "HXTC: %s", hexbufA);
    //APP_LOG(APP_LOG_LEVEL_DEBUG, "HXTC: %s", hexbufB);
}

static void select_click_handler(ClickRecognizerRef recognizer, void *context) {
    count = 0;
    showCount();
}

static void up_click_handler(ClickRecognizerRef recognizer, void *context) {
    text_layer_set_text(text_layer, "Go ahead!");
    
    
    APP_LOG(APP_LOG_LEVEL_INFO, "Mem free: %d", (int)heap_bytes_free());
    
    
    uint32_t num_samples = PKTSIZE;
    accel_data_service_subscribe(num_samples, data_handler);
    accel_service_set_sampling_rate(ACCEL_SAMPLING_50HZ);
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

static GBitmap *icon0, *icon1, *icon2;

static void window_load(Window *window) {
    Layer *window_layer = window_get_root_layer(window);
    GRect bounds = layer_get_bounds(window_layer);
    
    int x = 5;
    int w = bounds.size.w - 30 - x;

    text_layer = text_layer_create(GRect(x, 0, w, 60));
    text_layer_set_text(text_layer, "UP to start");
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
    // The loading of the icons is omitted for brevity... See gbitmap_create_with_resource()
    icon0 = gbitmap_create_with_resource(RESOURCE_ID_IMG_TRASH);
    icon1 = gbitmap_create_with_resource(RESOURCE_ID_IMG_SETTINGS);
    icon2 = gbitmap_create_with_resource(RESOURCE_ID_IMG_PLAY);
    
    action_bar_layer_set_icon_animated(action_bar, BUTTON_ID_UP, icon2, true);
    action_bar_layer_set_icon_animated(action_bar, BUTTON_ID_SELECT, icon0, true);
    action_bar_layer_set_icon_animated(action_bar, BUTTON_ID_DOWN, icon1, true);
}

static void window_unload(Window *window) {
    text_layer_destroy(text_layer);
    gbitmap_destroy(icon0);
    gbitmap_destroy(icon1);
    gbitmap_destroy(icon2);
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
    while (queue) {
        Pkt tmp = queue;
        queue = queue->next;
        free(tmp);
    }
}

int main(void) {
    init();
    app_event_loop();
    deinit();
}

// vim: sw=4
