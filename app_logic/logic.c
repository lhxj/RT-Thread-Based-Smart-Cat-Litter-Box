/*
 * Copyright (c) 2006-2021, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2022-08-01     ywx       the first version
 */
#include <rtthread.h>
#include <rtdevice.h>
#include <board.h>

#include "logic.h"
#include "sensor.h"
#include "drv_gpio.h"

int rt_hw_dht11_init(const char *name, struct rt_sensor_config *cfg);

rt_adc_device_t adc_dev;

rt_uint32_t cur_hum, cur_tem;
extern rt_uint32_t hum, tem;

/* mailboxes */
extern struct rt_mailbox tem_mb;
extern struct rt_mailbox hum_mb;
/* mailbox memory pools */
extern char mb_hum_pool[128];
extern char mb_tem_pool[128];

#define LED0_PIN GET_PIN(D, 3)
#define LED1_PIN GET_PIN(D, 4)
#define LED2_PIN GET_PIN(D, 5)
#define LED3_PIN GET_PIN(D, 6)
#define HONGWAI_PIN GET_PIN(E, 2)
/*define motor pin*/
#define IN1 GET_PIN(D, 8)
#define IN2 GET_PIN(D, 9)

/*define WATER_PUMP pin*/
#define BUZZER GET_PIN(D, 11)
#define O3 GET_PIN(D, 10)

#define DHT11_DATA_PIN GET_PIN(E, 10)

#define TEM_MAX 33
#define HUM_MIN 70

int box_used = 0;
extern int cur_weight;
extern int flag;
/* sensor control logic */
void Sensor_Logic_Running(void)
{
    int remote_trigger = (flag == 1);

    rt_mb_recv(&hum_mb, (rt_uint32_t *)&cur_hum, RT_WAITING_FOREVER);
    rt_mb_recv(&tem_mb, (rt_uint32_t *)&cur_tem, RT_WAITING_FOREVER);



    if(cur_weight >= 500){
            rt_pin_write(O3, PIN_HIGH);
            rt_pin_write(BUZZER, PIN_HIGH);
            rt_thread_mdelay(500);
            rt_pin_write(BUZZER, PIN_LOW);
            rt_thread_mdelay(500);
            rt_pin_write(BUZZER, PIN_HIGH);
            rt_thread_mdelay(500);
            rt_pin_write(BUZZER, PIN_LOW);
            rt_thread_mdelay(500);
            rt_pin_write(BUZZER, PIN_HIGH);
            rt_thread_mdelay(500);
            rt_pin_write(BUZZER, PIN_LOW);
            rt_thread_mdelay(500);

            rt_pin_write(BUZZER, PIN_LOW);
            rt_thread_mdelay(3000);
            rt_pin_write(O3, PIN_LOW);
        }

        if(rt_pin_read(HONGWAI_PIN) == 0 || remote_trigger){
            rt_thread_mdelay(50);
            if((rt_pin_read(HONGWAI_PIN) == 0 || remote_trigger)){
                rt_kprintf("The cat is here!\n");
                if (remote_trigger)
                {
                    flag = 0;
                }
                box_used++;
                rt_thread_mdelay(5000);
                rt_pin_write(IN1, PIN_LOW);
                rt_pin_write(IN2, PIN_HIGH);
                rt_thread_mdelay(10000);
                rt_pin_write(IN1, PIN_HIGH);
                rt_pin_write(IN2, PIN_LOW);
                rt_thread_mdelay(10000);
                rt_pin_write(IN1, PIN_LOW);
                rt_pin_write(IN2, PIN_LOW);
            }
            }



}

/* initialize sensors */
void access_Sensor(void)
{
    //DHT11
    struct rt_sensor_config cfg;
    cfg.intf.user_data = (void *)DHT11_DATA_PIN;
    rt_hw_dht11_init("dht11", &cfg);


}


