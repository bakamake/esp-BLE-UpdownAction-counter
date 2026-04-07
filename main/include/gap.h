//
// Created by bakamake on 2026/3/27.
//

#ifndef ESP_BLE_TOUCH_HID_GAP_H
#define ESP_BLE_TOUCH_HID_GAP_H


/* Includes */
/* NimBLE GAP APIs */
#include "services/gap/ble_svc_gap.h"

/* Defines */
#define BLE_GAP_APPEARANCE_GENERIC_TAG 0x0200
#define BLE_GAP_URI_PREFIX_HTTPS 0x17
#define BLE_GAP_LE_ROLE_PERIPHERAL 0x00

/* Public function declarations */
void adv_init(void);
int gap_init(void);


#endif //ESP_BLE_TOUCH_HID_GAP_H