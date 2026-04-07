#include "movement_counter.h"

#include <stddef.h>

#define MOVEMENT_COUNTER_MINIMA_WINDOW_US (1200 * 1000LL)
#define MOVEMENT_COUNTER_MIN_CYCLE_US (1000 * 1000LL)
#define MOVEMENT_COUNTER_Z_REBOUND_THRESHOLD 4500

static bool movement_counter_select_minimum(
    movement_counter_t *counter,
    int64_t minimum_ts_us,
    int16_t minimum_z,
    movement_counter_event_t *event
) {
    if (!counter->has_selected_minimum) {
        counter->has_selected_minimum = true;
        counter->selected_minimum_ts_us = minimum_ts_us;
        counter->selected_minimum_z = minimum_z;
        counter->selected_window_peak_z = minimum_z;
        return false;
    }

    if (minimum_ts_us - counter->selected_minimum_ts_us <= MOVEMENT_COUNTER_MINIMA_WINDOW_US) {
        if (minimum_z < counter->selected_minimum_z) {
            counter->selected_minimum_ts_us = minimum_ts_us;
            counter->selected_minimum_z = minimum_z;
        }
        return false;
    }

    if (counter->selected_window_peak_z - counter->selected_minimum_z < MOVEMENT_COUNTER_Z_REBOUND_THRESHOLD) {
        counter->selected_minimum_ts_us = minimum_ts_us;
        counter->selected_minimum_z = minimum_z;
        counter->selected_window_peak_z = minimum_z;
        return false;
    }

    if (counter->has_last_counted_minimum) {
        int64_t cycle_us = counter->selected_minimum_ts_us - counter->last_counted_minimum_ts_us;
        if (cycle_us >= MOVEMENT_COUNTER_MIN_CYCLE_US) {
            counter->count += 1;
            event->count = counter->count;
            event->cycle_us = cycle_us;
            event->z_min = counter->selected_minimum_z;
        } else {
            event->count = 0;
            event->cycle_us = 0;
            event->z_min = 0;
        }
    } else {
        counter->has_last_counted_minimum = true;
        event->count = 0;
        event->cycle_us = 0;
        event->z_min = 0;
    }

    counter->last_counted_minimum_ts_us = counter->selected_minimum_ts_us;
    counter->selected_minimum_ts_us = minimum_ts_us;
    counter->selected_minimum_z = minimum_z;
    counter->selected_window_peak_z = minimum_z;

    return event->count > 0;
}

void movement_counter_init(movement_counter_t *counter) {
    if (counter == NULL) {
        return;
    }

    *counter = (movement_counter_t){0};
}

bool movement_counter_push_sample(
    movement_counter_t *counter,
    const movement_counter_sample_t *sample,
    movement_counter_event_t *event
) {
    movement_counter_sample_t middle_sample;

    if (counter == NULL || sample == NULL || event == NULL) {
        return false;
    }

    *event = (movement_counter_event_t){0};

    if (counter->has_selected_minimum && sample->accel_z > counter->selected_window_peak_z) {
        counter->selected_window_peak_z = sample->accel_z;
    }

    if (counter->sample_count < 3) {
        counter->samples[counter->sample_count] = *sample;
        counter->sample_count += 1;
        if (counter->sample_count < 3) {
            return false;
        }
    } else {
        counter->samples[0] = counter->samples[1];
        counter->samples[1] = counter->samples[2];
        counter->samples[2] = *sample;
    }

    middle_sample = counter->samples[1];
    if (middle_sample.accel_z <= counter->samples[0].accel_z &&
        middle_sample.accel_z <= counter->samples[2].accel_z) {
        return movement_counter_select_minimum(
            counter,
            middle_sample.ts_us,
            middle_sample.accel_z,
            event
        );
    }

    return false;
}
