#include <pebble.h>
#include "detector.h"

static int lastCount, prevCount, lapCounted;
static uint64_t startTime;

int detector_state;

int poolSize = 20;
int strokeCount, laps;


//#define DBG(args...) APP_LOG(APP_LOG_LEVEL_DEBUG, args)
#define DBG(args...)

#if 0
#define R(x) ((9*x)/10)
#define T(x) ((10*x)/8)
#else
#define R(x) (x)
#define T(x) (x)
#endif

#define INBOUNDS(low, v, high) ((R(low) <= (v)) && ((v) < T(high)))

static int xMax, xMaxAt, xZeroAt;
static int yMax, yMaxAt, yZeroAt;

void resetDet() {
    startTime = 0;
    xMaxAt = 0;
    xMax = 0;
    yMaxAt = 0;
    yMax = 0;
    yZeroAt = 0;
    xZeroAt = 0;
    strokeCount = 0;
    laps = 0;
}

void process_sample(int x, int y, int z, uint64_t timestamp)
{
    if (startTime == 0)
        startTime = timestamp - 10000;

    (void)z;

    int now = (int)(timestamp - startTime);

    if (now - xMaxAt > 1000) {
        detector_state = 1;
        xMaxAt = 0;
        xMax = 0;
    }

    if (now - yMaxAt > 1200) {
        detector_state = 2;
        yMaxAt = 0;
        yMax = 0;
    }

    if (x > 1200 && x > xMax) {
        detector_state = 3;
        xMax = x;
        xMaxAt = now;
    }

    if (y > 1200 && y > yMax) {
        detector_state = 4;
        yMax = y;
        yMaxAt = now;
    }

    if (x < 0 && INBOUNDS(60, now - xMaxAt, 800)) {
        detector_state = 9;
        xZeroAt = now;
        xMax = 0;
        xMaxAt = 0;
    }

    int minStroke = (poolSize >> 1) - 1;
    int maxStroke = poolSize - 2;

    if (y < 0 && INBOUNDS(60, now - yMaxAt, 1200) && INBOUNDS(60, now - xZeroAt, 1600)) {
        detector_state = 10;
        // do not count too often
        if (yMaxAt - lastCount > 1000) {
            detector_state = 20;
            prevCount = lastCount;
            lastCount = yMaxAt;
            yZeroAt = now;
            yMax = 0;
            yMaxAt = 0;

            if (!lapCounted && strokeCount >= minStroke) {
                laps++;
                lapCounted = 1;
            }

            if (lastCount - prevCount > 4000 || 
                (lastCount - prevCount > 2000 && strokeCount >= minStroke) ||
                strokeCount >= maxStroke) {
                strokeCount = 0;
                lapCounted = 0;
            }

            strokeCount++;

            update_view();
        }

    }
}
