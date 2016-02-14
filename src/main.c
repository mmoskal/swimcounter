#include <pebble.h>

static Window *window;
static TextLayer *text_layer;

static void select_click_handler(ClickRecognizerRef recognizer, void *context) {
    text_layer_set_text(text_layer, "Select");
}

static int aboveZeroStart = -1, phaseMax, phaseMaxAt, tryPhaseMaxAt, lastCount, lastHigh;
static int count, sampleNo;

static void showCount() {
  // Long lived buffer
  static char s_buffer[20];

  // Compose string of all data for 3 samples
  snprintf(s_buffer, sizeof(s_buffer), 
    "%d", count
  );
    
    text_layer_set_text(text_layer, s_buffer);
}

static uint64_t startTime;
static int ind = 0;



static void data_handler(AccelData *data, uint32_t num_samples) {
    // we need a peek 150ms wide and at least 1300 high
    // followed by a value under -1600 less than 1500ms after the peak
    for (unsigned i = 0; i < num_samples; ++i) {
        if (data[i].did_vibrate)
            continue;
        
        if (startTime == 0)
            startTime = data[i].timestamp - 10000;
        
        int now = (int)(data[i].timestamp - startTime);
        
        int x = -data[i].x;  // - for left hand
        
        if (x > -500)
            lastHigh = now;

        if (x > 0) {
            if (aboveZeroStart < 0) {
                aboveZeroStart = now;
                phaseMax = 0;            
                tryPhaseMaxAt = 0;
                ind = 500;
            }
            if (x > 1300 && x > phaseMax) {
                phaseMax = x;
                tryPhaseMaxAt = now;
                ind = 1000;
            }
            if (tryPhaseMaxAt && now - aboveZeroStart > 150) {
                APP_LOG(APP_LOG_LEVEL_DEBUG, "Real phase max, %d", (int)(now - aboveZeroStart));
                phaseMaxAt = tryPhaseMaxAt;
                tryPhaseMaxAt = 0;
                ind = 1500;
            }
        }
        else {
            aboveZeroStart = -1;
            ind = 0;
            if (x < -1600) {
                APP_LOG(APP_LOG_LEVEL_DEBUG, "d0=%d d1=%d", now - lastHigh, now - phaseMaxAt);
                if (now - phaseMaxAt < 1500) {
                    // do not count too often
                    if (now - lastCount > 1500) {
                        ind = 2000;
                        lastCount = now;
                        count = count + 1;
                        phaseMax = 0;
                        vibes_short_pulse();
                        showCount();
                    }
                }
            }
        }
        
        APP_LOG(APP_LOG_LEVEL_DEBUG, "ACCEL: %d,%d,%d,%d", ++sampleNo, now, x, ind);
    }
}

static void up_click_handler(ClickRecognizerRef recognizer, void *context) {
    text_layer_set_text(text_layer, "Counting");
    
    uint32_t num_samples = 25;
    accel_data_service_subscribe(num_samples, data_handler);
    accel_service_set_sampling_rate(ACCEL_SAMPLING_50HZ);
}

static void down_click_handler(ClickRecognizerRef recognizer, void *context) {
    text_layer_set_text(text_layer, "Down");
    count = 0;
    showCount();
}

static void click_config_provider(void *context) {
  window_single_click_subscribe(BUTTON_ID_SELECT, select_click_handler);
  window_single_click_subscribe(BUTTON_ID_UP, up_click_handler);
  window_single_click_subscribe(BUTTON_ID_DOWN, down_click_handler);
}

static void window_load(Window *window) {
  Layer *window_layer = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(window_layer);

  text_layer = text_layer_create(GRect(5, 0, bounds.size.w - 10, bounds.size.h));
  text_layer_set_text(text_layer, "Press UP");
  text_layer_set_overflow_mode(text_layer, GTextOverflowModeWordWrap);
  text_layer_set_font(text_layer, fonts_get_system_font(FONT_KEY_GOTHIC_24));
    
  layer_add_child(window_layer, text_layer_get_layer(text_layer));
}

static void window_unload(Window *window) {
  text_layer_destroy(text_layer);
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
}

int main(void) {
    init();
    app_event_loop();
    deinit();
}

// vim: sw=4
