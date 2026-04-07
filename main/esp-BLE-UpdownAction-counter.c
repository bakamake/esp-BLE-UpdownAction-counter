/*
 * SPDX-FileCopyrightText: 2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 */
/* Includes */
#include "common.h"
#include "gap.h"
#include "imu_i2c.h"
#include "movement_counter.h"
#include "sdkconfig.h"
#include "esp_timer.h"

#define IMU_READ_PERIOD_MS 20
/* Library function declarations */
void ble_store_config_init(void);

/* Private function declarations */
static void on_stack_reset(int reason);
static void on_stack_sync(void);
static void nimble_host_config_init(void);
static void nimble_host_task(void *param);

/* Private functions */
/*
 *  Stack event callback functions
 *      - on_stack_reset is called when host resets BLE stack due to errors
 *      - on_stack_sync is called when host has synced with controller
 */
static void on_stack_reset(int reason) {
    /* On reset, print reset reason to console */
    ESP_LOGI(TAG, "nimble stack reset, reset reason: %d", reason);
}

static void on_stack_sync(void) {
    /* On stack sync, do advertising initialization */
    adv_init();
}

static void nimble_host_config_init(void) {
    /* Set host callbacks */
    ble_hs_cfg.reset_cb = on_stack_reset;
    ble_hs_cfg.sync_cb = on_stack_sync;
    ble_hs_cfg.store_status_cb = ble_store_util_status_rr;

    /* Store host configuration */
    ble_store_config_init();
}

static void nimble_host_task(void *param) {
    /* Task entry log */
    ESP_LOGI(TAG, "nimble host task has been started!");

    /* This function won't return until nimble_port_stop() is executed */
    nimble_port_run();

    /* Clean up at exit */
    vTaskDelete(NULL);
}

static void lsm6ds3_read_task(void *dev_handle) {
    uint8_t lsm6ds3_who_I_am;
    movement_counter_t movement_counter;

    movement_counter_init(&movement_counter);
    {
        // 简单测试：读取 WHO_AM_I 寄存器 (0x0F) 验证设备通信
        esp_err_t ret = lsm6ds3_register_read(dev_handle, LSM6DS3_WHO_AM_I_REG_ADDR, &lsm6ds3_who_I_am, 1);
        if (ret == ESP_OK) {
            ESP_LOGI("I2C", "✓ I2C OK | WHO_AM_I: 0x%02X (expected: 0x69)", lsm6ds3_who_I_am);

            if (lsm6ds3_who_I_am == 0x69) {
                ESP_LOGI("I2C", "  → LSM6DS3 device detected!");
            } else {
                ESP_LOGW("I2C", "  → Unknown device ID!");
            }
        } else {
            ESP_LOGE("I2C", "✗ I2C FAILED | error: 0x%02X", ret);
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    while (1) {
        uint8_t accele_data[6];
        movement_counter_sample_t sample;
        movement_counter_event_t event;
        /*
        uint8_t gyro_data[6];  // 陀螺仪X、Y、Z各2字节
        // 读取 0x22 开始的6字节 (GYRO X、Y、Z)
        ESP_ERROR_CHECK(lsm6ds3_register_read(dev_handle, 0x22, gyro_data, 6));
        int16_t gyro_x = (int16_t)(gyro_data[1] << 8 | gyro_data[0]);
        int16_t gyro_y = (int16_t)(gyro_data[3] << 8 | gyro_data[2]);
        int16_t gyro_z = (int16_t)(gyro_data[5] << 8 | gyro_data[4]);
        ESP_LOGE("lsm6ds3", "Gyro X: %d, Y: %d, Z: %d", gyro_x, gyro_y, gyro_z);
        */
        esp_err_t ret = lsm6ds3_register_read(dev_handle, LSM6DS3_OUTX_L_X_ADDR, accele_data, 6);

        if (ret != ESP_OK) {
            ESP_LOGE("LSM6DS3", "read accel failed: 0x%x", ret);
            vTaskDelay(pdMS_TO_TICKS(IMU_READ_PERIOD_MS));
            continue;
        }

        // 解析数据 (LSM6DS3 是小端序)
        //显式强转 int16_t xxx =(int16_t)(xx)
        //这里读小端序，操作是在切割int8_t,交换顺序重新组合成int16_t自然数学大端序
        int16_t accele_x = (int16_t)(accele_data[1] << 8 | accele_data[0]);
        //这里读小端序，操作是在切割int8_t,交换顺序重新组合成int16_t自然数学大端序
        int16_t accele_y = (int16_t)(accele_data[3] << 8 | accele_data[2]);
        //这里读小端序，操作是在切割int8_t,交换顺序重新组合成int16_t自然数学大端序
        int16_t accele_z = (int16_t)(accele_data[5] << 8 | accele_data[4]);

        sample.ts_us = esp_timer_get_time();
        sample.accel_x = accele_x;
        sample.accel_y = accele_y;
        sample.accel_z = accele_z;
        
        ESP_LOGI("ACCEL", "ts_ms=%" PRIu32 ",x=%d,y=%d,z=%d", esp_log_timestamp(), accele_x, accele_y, accele_z);

        if (movement_counter_push_sample(&movement_counter, &sample, &event)) {
            ESP_LOGI(
                "SQUAT",
                "count=%" PRIu32 " cycle_ms=%" PRIi64 " z_min=%d",
                event.count,
                event.cycle_us / 1000,
                event.z_min
            );
        }

        vTaskDelay(pdMS_TO_TICKS(IMU_READ_PERIOD_MS));
    }
}

void app_main(void) {
    /* Local variables */
    int rc = 0;
    esp_err_t ret = ESP_OK;

    /* NVS flash initialization */
    ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
        ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "failed to initialize nvs flash, error code: %d ", ret);
        return;
    }

    /* NimBLE host stack initialization */
    ret = nimble_port_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "failed to initialize nimble stack, error code: %d ",
                 ret);
        return;
    }

#if CONFIG_BT_NIMBLE_GAP_SERVICE
    /* GAP service initialization */
    rc = gap_init();
    if (rc != 0) {
        ESP_LOGE(TAG, "failed to initialize GAP service, error code: %d", rc);
        return;
    }
#endif

    /* NimBLE host configuration initialization */
    nimble_host_config_init();




/*
*    i2c部分,
*    lsm6ds_i2c_master_init i2c 初始化 ，让idf 框架构造 启动i2c总线，给一个总线句柄和设备句柄，
*    lsm6ds3_detect 检查  who_am_i
*
*/
    uint8_t who_am_i ;
    i2c_master_bus_handle_t bus_handle;
    i2c_master_dev_handle_t dev_handle;
    ret = lsm6ds_i2c_master_init(&bus_handle,&dev_handle);
    if (ret != ESP_OK) {
        ESP_LOGE("LSM6DS3", "ESP_I2C init failed: 0x%x", ret);
        return;
    }
    ESP_LOGI("LSM6DS3","ESP_I2C initialized successfully");

    ret = lsm6ds3_detect(bus_handle, dev_handle, &who_am_i);
    if (ret != ESP_OK) {
        ESP_LOGE("LSM6DS3", "device detect failed: 0x%x", ret);
        return;
    }

    // 复位后等待 SW_RESET 自动清零，确认芯片回到稳定状态
    ret = lsm6ds3_init(dev_handle);
    if (ret != ESP_OK) {
        ESP_LOGE("LSM6DS3", "software reset failed: 0x%x", ret);
        return;
    }

    /* Start NimBLE host task thread and return */
    xTaskCreate(nimble_host_task, "NimBLE Host", 4*1024, NULL, 5, NULL);

    xTaskCreate(
    lsm6ds3_read_task,   // 任务函数的指针 [2]
    "imu_read_task",     // 任务的描述性名称（主要用于调试） [2]
    4096,                // 任务栈大小（4096 字节对于 I2C 读取绝对够用） [2]
    dev_handle,                // 传递给任务的参数（这里不需要，填 NULL） [2]
    5,                   // 任务优先级（数字越大优先级越高） [2]
    NULL                 // 任务句柄（如果不需要在外部控制这个任务，填 NULL） [2]
);
}
