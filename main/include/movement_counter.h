#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    int64_t ts_us;
    int16_t accel_x;
    int16_t accel_y;
    int16_t accel_z;
} movement_counter_sample_t;

typedef struct {
    uint32_t count;
    int64_t cycle_us;
    int16_t z_min;
} movement_counter_event_t;

typedef struct {
    movement_counter_sample_t samples[3];
    uint8_t sample_count;
    bool has_selected_minimum;
    int64_t selected_minimum_ts_us;
    int16_t selected_minimum_z;
    int16_t selected_window_peak_z;
    bool has_last_counted_minimum;
    int64_t last_counted_minimum_ts_us;
    uint32_t count;
} movement_counter_t;

void movement_counter_init(movement_counter_t *counter);

bool movement_counter_push_sample(
    movement_counter_t *counter,
    const movement_counter_sample_t *sample,
    movement_counter_event_t *event
);
