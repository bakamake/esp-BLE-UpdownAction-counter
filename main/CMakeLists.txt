idf_component_register(
    SRCS
        "esp-BLE-UpdownAction-counter.c"
        "src/gap.c"
        "src/imu_i2c.c"
        "src/movement_counter.c"
    INCLUDE_DIRS "./include"
    REQUIRES bt nvs_flash esp_driver_i2c esp_timer
)
