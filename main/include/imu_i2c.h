#pragma once

#include <stdint.h>
#include "driver/i2c_master.h"
#include "esp_err.h"

// LSM6DS3 寄存器定义，放在头文件里供主流程和任务代码共用
// 0x0F = WHO_AM_I 寄存器地址
// 0x69 = 读取 WHO_AM_I 时 LSM6DS3 应返回的设备 ID
#define LSM6DS3_WHO_AM_I_REG_ADDR   0x0F

// CTRL3_C 包含软件复位等控制位
#define LSM6DS3_CTRL3_C_REG_ADDR    0x12

// CTRL1_XL 是加速度计输出数据率 / 量程 / 滤波相关控制寄存器
#define LSM6DS3_CTRL1_XL_REG_ADDR   0x10

// CTRL3_C 中的 RESET 位，写 1 后芯片执行软件复位，随后会自动清零
#define LSM6DS3_RESET_BIT           0x01

// CTRL1_XL 中的 NORMAL_MODE ( 104Hz )
#define LSM6DS3_NORMAL_MODE     0x40

// 加速度 X 轴低字节输出寄存器，连续读取 6 字节可得到 XYZ 三轴数据
#define LSM6DS3_OUTX_L_X_ADDR       0x28

// 读取寄存器
esp_err_t lsm6ds3_register_read(i2c_master_dev_handle_t dev_handle, uint8_t reg_addr, uint8_t *data, size_t len);

// 向单个寄存器写 1 字节
esp_err_t lsm6ds3_register_write_byte(i2c_master_dev_handle_t dev_handle, uint8_t reg_addr, uint8_t data);

// 初始化 I2C master bus 和 LSM6DS3 对应的 device handle
esp_err_t lsm6ds_i2c_master_init(i2c_master_bus_handle_t *bus_handle, i2c_master_dev_handle_t *dev_handle);

// 运行时探测 0x6A / 0x6B，并读取 WHO_AM_I 确认设备身份
esp_err_t lsm6ds3_detect(i2c_master_bus_handle_t bus_handle, i2c_master_dev_handle_t dev_handle, uint8_t *who_am_i);

// 触发软件复位，并轮询等待 SW_RESET 位自动清零
esp_err_t lsm6ds3_init(i2c_master_dev_handle_t dev_handle);
