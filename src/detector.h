#pragma once
void record_one();
extern int detector_state;
void process_sample(int x, int y, int z, uint64_t timestamp);
