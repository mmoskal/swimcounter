#include <pebble.h>
#include "detector.h"

static int lowStart = -1, phaseMin, phaseMinAt, tryPhaseMinAt, lastCount, phase1, phase2;
static uint64_t startTime;

int detector_state;

//#define DBG(args...) APP_LOG(APP_LOG_LEVEL_DEBUG, args)
#define DBG(args...)

#if 1
#define R(x) ((9*x)/10)
#define T(x) ((10*x)/8)
#else
#define R(x) (x)
#define T(x) (x)
#endif

#define INBOUNDS(low, v, high) ((R(low) <= (v)) && ((v) < T(high)))

void process_sample(int x, int y, int z, uint64_t timestamp)
{
    if (startTime == 0)
        startTime = timestamp - 10000;

    int now = (int)(timestamp - startTime);

    if (x < 100) {
        if (lowStart < 0) {
            lowStart = now;
            detector_state = 1;
        }
        if (x < R(-1200) && x < phaseMin) {
            phaseMin = x;
            tryPhaseMinAt = now;
            detector_state = 2;
        }
        if (tryPhaseMinAt && INBOUNDS(100, now - lowStart, 2000)) {
            DBG("Real phase max, %d", (int)(now - lowStart));
            phaseMinAt = tryPhaseMinAt;
            tryPhaseMinAt = 0;
            detector_state = 3;
        }
    }
    else {
        if (lowStart > 0) {
            lowStart = -1;
            phaseMin = 0;
            tryPhaseMinAt = 0;
        }

        detector_state = 4;
        if (x > R(1000)) {
            detector_state = 5;
            if (INBOUNDS(600, now - phaseMinAt, 2500)) {
                detector_state = 6;
                phase1 = now;
                phaseMinAt = 0;
            }
        }
    }

    if (z < R(-1000) && INBOUNDS(400, now - phase1, 1600)) {
        detector_state = 7;
        phase2 = now;
        phase1 = 0;
    }

    if (y < R(-800) && INBOUNDS(400, now - phase2, 3800)) {
        detector_state = 8;
        // do not count too often; 4s would be the real non-testing limit
        if (now - lastCount > 2500) {
            detector_state = 20;
            lastCount = now;
            phase1 = 0;
            phase2 = 0;
            record_one();
        }
    }
}
