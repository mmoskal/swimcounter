#pragma once
void update_view();
extern int detector_state;
extern int poolSize, strokeCount, laps;
void process_sample(int x, int y, int z, uint64_t timestamp);
void resetDet();
