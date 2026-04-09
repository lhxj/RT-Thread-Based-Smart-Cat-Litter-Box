/*
 * Copyright (c) 2006-2021, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2022-08-01     ywx       the first version
 */
#ifndef APPLICATIONS_APP_LOGIC_LOGIC_H_
#define APPLICATIONS_APP_LOGIC_LOGIC_H_

#define AHT10_I2C_BUS "i2c2"

#define ADC_DEV_NAME        "adc1"      /* ADC device name */
#define ADC_DEV_CHANNEL     12           /* ADC channel */
#define REFER_VOLTAGE       330          /* 3.3V reference voltage x100 */
#define CONVERT_BITS        (1 << 12)    /* 12-bit conversion range */

void access_Sensor(void);
void Sensor_Logic_Running(void);

#endif /* APPLICATIONS_APP_LOGIC_LOGIC_H_ */
