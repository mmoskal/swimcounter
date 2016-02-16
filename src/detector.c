#include <pebble.h>
#include "detector.h"

static int phaseStop, phaseLen, lowStart = -1, phaseMin, phaseMinAt, tryPhaseMinAt, lastCount;
static int lastTime;
static uint64_t startTime;

int detector_state;

//#define DBG(args...) APP_LOG(APP_LOG_LEVEL_DEBUG, args)
#define DBG(args...)

void process_sample(int x, int y, int z, uint64_t timestamp)
{
    if (startTime == 0)
        startTime = timestamp - 10000;

    (void)y;
    (void)z;
    
    int now = (int)(timestamp - startTime);
    
    if (x < 100) {
        if (lowStart < 0) {
            // check if it was just a short break
            if (now - phaseStop < 200 && now - phaseStop < phaseLen) {
                lowStart = phaseStop - phaseLen;
                detector_state = 3;
            } else {
                lowStart = now;
                detector_state = 1;
            }
        }
        if (x < -1000 && x < phaseMin) {
            phaseMin = x;
            tryPhaseMinAt = now;
            detector_state = 2;
        }
        if (tryPhaseMinAt && now - lowStart > 100) {
            DBG("Real phase max, %d", (int)(now - lowStart));
            phaseMinAt = tryPhaseMinAt;
            tryPhaseMinAt = 0;
            detector_state = 3;
        }
    }
    else {
        if (lowStart > 0) {
            if (phaseMinAt >= lowStart) {
                phaseStop = now;
                phaseLen = now - lowStart;
            }
            lowStart = -1;
            phaseMin = 0;
            tryPhaseMinAt = 0;
        }
        detector_state = 0;
        if (phaseLen > 250) {
            detector_state = 4;
            if (x > 1000) {
                detector_state = 5;
                //DBG("d0=%d d1=%d", now - lastHigh, now - phaseMinAt);
                if (now - phaseMinAt < 4000 && now - phaseStop < phaseLen / 4 + 600) {
                    detector_state = 6;
                    // do not count too often; 4-5s would be the real non-testing limit
                    if (now - lastCount > 2500) {
                        detector_state = 20;
                        lastCount = now;
                        phaseMinAt = 0;
                        lastTime = (now - 10000) / 1000;
                        record_one();
                    }
                }
            }
        }
    }
}
