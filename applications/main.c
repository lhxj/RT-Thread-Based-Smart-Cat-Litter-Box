/*
 * Copyright (c) 2006-2022, RT-Thread Development Team
 * Copyright (c) 2022, Xiaohua Semiconductor Co., Ltd.
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2022-04-28     CDT          first version
 */

#include <rtthread.h>
#include <rtdevice.h>
#include <board.h>
#include <stdio.h>
#include "rtconfig.h"

#include "dev_sign_api.h"
#include "mqtt_api.h"
#include "app_logic/mqtt.h"
#include "app_logic/logic.h"
#include "app_logic/app_init.h"
#include <balance.h>
#include "ssd1306.h"

/* defined the LED_GREEN pin: PD4 */
#define LED_GREEN_PIN GET_PIN(D, 4)
#define LED0_PIN GET_PIN(D, 3)
#define LED2_PIN GET_PIN(D, 5)
#define LED3_PIN GET_PIN(D, 6)
#define HONGWAI_PIN GET_PIN(E, 2)
/*define motor pin*/
#define IN1 GET_PIN(D, 8)
#define IN2 GET_PIN(D, 9)

/*define WATER_PUMP pin*/
#define BUZZER GET_PIN(D, 11)
#define O3 GET_PIN(D, 10)

extern int cur_weight;
extern rt_uint32_t hum;
extern rt_uint32_t tem;
extern int box_used;

int main(void)
{
    char line_1[32];
    char line_2[32];
    char line_3[32];
    char line_4[32];
    char line_5[32];
    ssd1306_Init();
    if (start_rt_thread() != 0)
    {
        rt_kprintf("start_rt_thread failed\n");
    }
    balance_load();
    /* set LED_GREEN_PIN pin mode to output */
    rt_pin_mode(LED_GREEN_PIN, PIN_MODE_OUTPUT);
    rt_pin_mode(HONGWAI_PIN, PIN_MODE_INPUT);


    // configure motor control pins as output
    rt_pin_mode(IN1, PIN_MODE_OUTPUT);
    rt_pin_mode(IN2, PIN_MODE_OUTPUT);

    // configure buzzer and O3 control pins as output
    rt_pin_mode(BUZZER, PIN_MODE_OUTPUT);
    rt_pin_mode(O3, PIN_MODE_OUTPUT);

    while (1)
    {
        rt_pin_write(LED_GREEN_PIN, PIN_HIGH);
        rt_thread_mdelay(500);
        rt_pin_write(LED_GREEN_PIN, PIN_LOW);
        rt_thread_mdelay(500);

        ssd1306_Fill(Black);

        snprintf(line_1, sizeof(line_1), "W:%d U:%d", cur_weight, box_used);
        snprintf(line_2, sizeof(line_2), "ST:%s", Sensor_Logic_StateName());
        snprintf(line_3, sizeof(line_3), "FC:%s", Sensor_Logic_FaultName());
        snprintf(line_4,
                 sizeof(line_4),
                 "LK:%s B:%d P:%d",
                 (mqtt_is_link_online() == RT_TRUE) ? "ON" : "OFF",
                 Sensor_Logic_IsBinFull(),
                 Sensor_Logic_IsProtectActive());
        snprintf(line_5, sizeof(line_5), "H:%lu T:%lu", (unsigned long)hum, (unsigned long)tem);

        ssd1306_SetCursor(2, 0);
        ssd1306_WriteString(line_1, Font_7x10, White);
        ssd1306_SetCursor(2, 11);
        ssd1306_WriteString(line_2, Font_7x10, White);
        ssd1306_SetCursor(2, 22);
        ssd1306_WriteString(line_3, Font_7x10, White);
        ssd1306_SetCursor(2, 33);
        ssd1306_WriteString(line_4, Font_7x10, White);
        ssd1306_SetCursor(2, 44);
        ssd1306_WriteString(line_5, Font_7x10, White);
        ssd1306_UpdateScreen();
        rt_thread_mdelay(100);
    }
}
