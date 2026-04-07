//
// Created by bakamake on 2026/3/27.
//
#include "driver/i2c_master.h"
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "imu_i2c.h"
#include "sdkconfig.h"

// --- I2C 主设备配置 ---

// 定义 I2C 时钟线 (SCL) 使用的 GPIO 引脚编号，具体数值由 menuconfig 中的 CONFIG_I2C_MASTER_SCL 决定
#define I2C_MASTER_SCL_IO           CONFIG_I2C_MASTER_SCL       /*!< 用于 I2C 主设备时钟的 GPIO 编号 */

// 定义 I2C 数据线 (SDA) 使用的 GPIO 引脚编号
#define I2C_MASTER_SDA_IO           CONFIG_I2C_MASTER_SDA       /*!< 用于 I2C 主设备数据的 GPIO 编号 */

// 定义使用的 I2C 端口号，这里指定为 I2C_NUM_0 (即 I2C 0号端口)
#define I2C_MASTER_NUM              I2C_NUM_0                   /*!< 主设备使用的 I2C 端口号 */

// 定义 I2C 通信的时钟频率
// 这里改成直接读取 menuconfig 配置，方便调试时在 100k / 400k 之间切换
#define I2C_MASTER_FREQ_HZ          CONFIG_I2C_MASTER_FREQUENCY /*!< I2C 主设备时钟频率 */

// 禁用发送缓冲区 (设为0表示不使用内部环形缓冲区，直接发送)
#define I2C_MASTER_TX_BUF_DISABLE   0                           /*!< I2C 主设备不需要发送缓冲 */

// 禁用接收缓冲区
#define I2C_MASTER_RX_BUF_DISABLE   0                           /*!< I2C 主设备不需要接收缓冲 */

// 定义 I2C 操作的超时时间，单位为毫秒
#define I2C_MASTER_TIMEOUT_MS       1000

// --- LSM6DS3 传感器寄存器定义 ---
// 定义 LSM6DS3 的 I2C 从机地址 (7位地址 0x6A，对应二进制 1101000)
// LSM6DS3 SAO -> ESP32-S3 的 GND 引脚（接低电平，将 I2C 设备地址设为 0x6B
//数据手册理论上是这样，这里卡了我很久，最后才发现，fuck 我用的lsm6ds3模块板子的 sao 可能带了上拉电阻，反正0x6a死活不行
//排查，使用esp-idf带的示例程序i2c_tools，编译烧录，在 idf monitor -p /dev/ttyUSB0
// i2c-tools> i2cconfig --sda 11 --scl 12
// i2c-tools> i2cdetect
//      0  1  2  3  4  5  6  7  8  9  a  b  c  d  e  f
// 00: -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- --
// 10: -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- --
// 20: -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- --
// 30: -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- --
// 40: -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- --
// 50: -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- --
// 60: -- -- -- -- -- -- -- -- -- -- -- 6b -- -- -- --
// 70: -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- --
// fuck
// 有些模块把 SA0 拉高，有些拉低，运行时探测更稳妥
#define LSM6DS3_SENSOR_ADDR_0       0x6A
#define LSM6DS3_SENSOR_ADDR_1       0x6B

// i2c 读写
esp_err_t lsm6ds3_register_read(i2c_master_dev_handle_t dev_handle,uint8_t reg_addr,uint8_t *data,size_t len) {
    return i2c_master_transmit_receive(dev_handle,&reg_addr,1,data,len,I2C_MASTER_TIMEOUT_MS);
}
esp_err_t lsm6ds3_register_write_byte(i2c_master_dev_handle_t dev_handle,uint8_t reg_addr,uint8_t data) {
    uint8_t write_buf[2] = {reg_addr,data};
    return i2c_master_transmit(dev_handle,write_buf,sizeof(write_buf),I2C_MASTER_TIMEOUT_MS);
}


// 初始化 bus
// 这里只创建总线和默认 device handle，不在这里假定模块地址一定是 0x6A 还是 0x6B
esp_err_t lsm6ds_i2c_master_init(i2c_master_bus_handle_t *bus_handle,i2c_master_dev_handle_t *dev_handle) {
    i2c_master_bus_config_t bus_config = {
        .i2c_port = I2C_MASTER_NUM,
        .sda_io_num = I2C_MASTER_SDA_IO,
        .scl_io_num = I2C_MASTER_SCL_IO,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags = {
            .enable_internal_pullup = true,
            .allow_pd = false,
        },
    };



    ESP_RETURN_ON_ERROR(i2c_new_master_bus(&bus_config,bus_handle), "LSM6DS3", "create i2c bus failed");
    i2c_device_config_t dev_config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = LSM6DS3_SENSOR_ADDR_0,
        .scl_speed_hz = I2C_MASTER_FREQ_HZ,
        .flags = {
        .disable_ack_check = false}
    };
    return i2c_master_bus_add_device(*bus_handle,&dev_config,dev_handle);
}

static esp_err_t lsm6ds3_probe_address(i2c_master_bus_handle_t bus_handle, uint16_t address) {
    esp_err_t ret = i2c_master_probe(bus_handle, address, I2C_MASTER_TIMEOUT_MS);
    if (ret == ESP_ERR_INVALID_STATE || ret == ESP_ERR_TIMEOUT) {
        // 驱动状态机或物理总线卡住时，先做一次 bus reset，再重试一次 probe
        ESP_LOGW("LSM6DS3", "I2C probe 0x%02X hit bus state error 0x%x, resetting bus", address, ret);
        ESP_RETURN_ON_ERROR(i2c_master_bus_reset(bus_handle), "LSM6DS3", "reset i2c bus failed");
        ret = i2c_master_probe(bus_handle, address, I2C_MASTER_TIMEOUT_MS);
    }
    return ret;
}

esp_err_t lsm6ds3_detect(i2c_master_bus_handle_t bus_handle, i2c_master_dev_handle_t dev_handle, uint8_t *who_am_i) {
    // 同一颗芯片在不同模块板上，SA0 可能被拉成高或低，所以这里探测两个候选地址
    const uint16_t candidates[] = {LSM6DS3_SENSOR_ADDR_0, LSM6DS3_SENSOR_ADDR_1};

    for (size_t i = 0; i < sizeof(candidates) / sizeof(candidates[0]); ++i) {
        uint16_t addr = candidates[i];
        esp_err_t ret = lsm6ds3_probe_address(bus_handle, addr);
        if (ret != ESP_OK) {
            ESP_LOGW("LSM6DS3", "probe 0x%02X failed: 0x%x", addr, ret);
            continue;
        }

        ESP_RETURN_ON_ERROR(i2c_master_device_change_address(dev_handle, addr, I2C_MASTER_TIMEOUT_MS),
                            "LSM6DS3", "switch device address failed");
        // 仅 probe 成功还不够，再读 WHO_AM_I 确认它真的是 LSM6DS3
        ESP_RETURN_ON_ERROR(lsm6ds3_register_read(dev_handle, LSM6DS3_WHO_AM_I_REG_ADDR, who_am_i, 1),
                            "LSM6DS3", "read WHO_AM_I failed");

        if (*who_am_i == 0x69) {
            ESP_LOGI("LSM6DS3", "Detected device at 0x%02X, WHO_AM_I=0x%02X", addr, *who_am_i);
            return ESP_OK;
        }

        ESP_LOGW("LSM6DS3", "Address 0x%02X responded but WHO_AM_I=0x%02X", addr, *who_am_i);
    }

    return ESP_ERR_NOT_FOUND;
}


static esp_err_t lsm6ds3_software_reset(i2c_master_dev_handle_t dev_handle) {
    uint8_t ctrl3_c = 0;

    // 写 SW_RESET 后，芯片会自清零该位；轮询这个位可以确认复位已经完成
    ESP_RETURN_ON_ERROR(lsm6ds3_register_write_byte(dev_handle, LSM6DS3_CTRL3_C_REG_ADDR, LSM6DS3_RESET_BIT),
                        "LSM6DS3", "write SW_RESET failed");

    for (int i = 0; i < 10; ++i) {
        vTaskDelay(pdMS_TO_TICKS(10));
        ESP_RETURN_ON_ERROR(lsm6ds3_register_read(dev_handle, LSM6DS3_CTRL3_C_REG_ADDR, &ctrl3_c, 1),
                            "LSM6DS3", "read CTRL3_C failed");
        if ((ctrl3_c & LSM6DS3_RESET_BIT) == 0) {
            return ESP_OK;
        }
    }
    return ESP_ERR_TIMEOUT;

}
esp_err_t lsm6ds3_init(i2c_master_dev_handle_t dev_handle) {
    ESP_RETURN_ON_ERROR(lsm6ds3_software_reset(dev_handle), "LSM6DS3", "software reset failed");
    uint8_t ctrl1_xl_status;

    ESP_RETURN_ON_ERROR(lsm6ds3_register_write_byte(dev_handle, LSM6DS3_CTRL1_XL_REG_ADDR, LSM6DS3_NORMAL_MODE),
                        "LSM6DS3", "write LSM6DS3_NORMAL_MODE_BIT failed");

    vTaskDelay(pdMS_TO_TICKS(10));
    ESP_RETURN_ON_ERROR(lsm6ds3_register_read(dev_handle, LSM6DS3_CTRL1_XL_REG_ADDR, &ctrl1_xl_status, 1),
                        "LSM6DS3", "read CTRL1_XL failed");
    if (ctrl1_xl_status == LSM6DS3_NORMAL_MODE) {
        return ESP_OK;
    }
    return ESP_ERR_TIMEOUT;
}