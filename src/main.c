#include <pebble.h>

static Window *window;
static TextLayer *text_layer;
static TextLayer *number_layer;
static TextLayer *time_layer;
static ActionBarLayer *action_bar;

static int phaseStop, phaseLen, aboveZeroStart = -1, phaseMax, phaseMaxAt, tryPhaseMaxAt, lastCount;
static int count, sampleNo, lastTime;

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
    char hexbufA[num_samples * 4 + 25];
    char hexbufB[num_samples * 4 + 25];
    
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
        
        unsigned int mid = num_samples / 2;
        char *ptr =  i >= mid ? &hexbufB[(i - mid) * 8] : &hexbufA[i * 8];
        #define R(v,s) (((v >> 4) & 0x3ff) << s)
        snprintf(ptr, 9, "%08x", R(data[i].x, 0) |  R(data[i].y, 10) | R(data[i].z, 20) | (ind << 30));
        
        DBG("ACCEL: %d,%d,%d,%d", sampleNo, now, x, ind);
    }
    
    APP_LOG(APP_LOG_LEVEL_DEBUG, "HXTC: %s", hexbufA);
    APP_LOG(APP_LOG_LEVEL_DEBUG, "HXTC: %s", hexbufB);
}

static void select_click_handler(ClickRecognizerRef recognizer, void *context) {
    count = 0;
    showCount();
}

static void up_click_handler(ClickRecognizerRef recognizer, void *context) {
    text_layer_set_text(text_layer, "Go ahead!");
    
    uint32_t num_samples = 20;
    accel_data_service_subscribe(num_samples, data_handler);
    accel_service_set_sampling_rate(ACCEL_SAMPLING_50HZ);
}

static void down_click_handler(ClickRecognizerRef recognizer, void *context) {    
    text_layer_set_text(text_layer, "Cheating...");
    count++;
    showCount();
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
}

int main(void) {
    init();
    app_event_loop();
    deinit();
}

// vim: sw=4
