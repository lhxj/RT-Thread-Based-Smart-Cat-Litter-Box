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
#include <string.h>
#include "rtconfig.h"

#include "dev_sign_api.h"
#include "mqtt_api.h"
#include "mqtt.h"
#include "logic.h"
#include "sensor.h"
#include "drv_gpio.h"

/* mailboxes */
struct rt_mailbox tem_mb;
struct rt_mailbox hum_mb;
/* mailbox memory pools */
char mb_hum_pool[128];
char mb_tem_pool[128];


/* dynamic semaphore handle */
static rt_sem_t dynamic_sem = RT_NULL;

/* dynamic mutex handle */
static rt_mutex_t dynamic_mutex = RT_NULL;

#define TASK_TIMESLICE     5
#define CONTROL_LOOP_PERIOD_MS 100

extern char DEMO_PRODUCT_KEY[IOTX_PRODUCT_KEY_LEN + 1];
extern char DEMO_DEVICE_NAME[IOTX_DEVICE_NAME_LEN + 1];
extern char DEMO_DEVICE_SECRET[IOTX_DEVICE_SECRET_LEN + 1];

// 1) sensor task
/*************** sensor task *************/
#define SENSOR_PRIORITY                         12
#define SENSOR_TASK_SIZE                        4096
rt_sem_t sensor_data_sem = RT_NULL;
static rt_thread_t sensor_task_thread = RT_NULL;
rt_uint32_t hum;
rt_uint32_t tem;
/* sensor task entry */
static void ReadSensor_Task(void *parameter);
/*************** sensor task *************/


// 2) control task
/*************** control task *************/
#define CONTROL_PRIORITY                        14
#define CONTROL_TASK_SIZE                       4096
static rt_thread_t control_task_thread = RT_NULL;
/* control task entry */
static void StartControl_Task(void *parameter);
/*************** control task *************/

// 3) MQTT task
/*************** MQTT task *************/
#define MQTT_PRIORITY                        16
#define MQTT_TASK_SIZE                       4096
static rt_thread_t MQTT_task_thread = RT_NULL;
/* MQTT task entry */
static void Mqtt_Task(void *parameter);
/*************** MQTT task *************/

/* create and start runtime threads */
int start_rt_thread(void)
{
    rt_err_t result;

    /* init humidity mailbox */
    result = rt_mb_init(&hum_mb,
                        "hum_mbt",                      // mailbox name
                        &mb_hum_pool[0],                // mailbox memory pool
                        sizeof(mb_hum_pool) / 4,        // max message count (4 bytes each)
                        RT_IPC_FLAG_FIFO);              // FIFO waiting policy
    if (result != RT_EOK)
    {
        rt_kprintf("init mailbox failed.\n");
    }

    /* init temperature mailbox */
    result = rt_mb_init(&tem_mb,
                        "tem_mbt",                      // mailbox name
                        &mb_tem_pool[0],                 // mailbox memory pool
                        sizeof(mb_tem_pool) / 4,         // max message count (4 bytes each)
                        RT_IPC_FLAG_FIFO);               // FIFO waiting policy
    if (result != RT_EOK)
    {
        rt_kprintf("init mailbox failed.\n");
    }


        /* create dynamic semaphore with initial value 0 */

    dynamic_sem = rt_sem_create("dsem", 0, RT_IPC_FLAG_PRIO);
    if (dynamic_sem == RT_NULL)
    {
        rt_kprintf("create dynamic semaphore failed.\n");
        return -1;
    }
    else
    {
        //rt_kprintf("create done. dynamic semaphore value = 0.\n");
    }


    /* create dynamic mutex */
    dynamic_mutex = rt_mutex_create("dmutex", RT_IPC_FLAG_PRIO);
    if (dynamic_mutex == RT_NULL)
    {
        rt_kprintf("create dynamic mutex failed.\n");
        return -1;
    }

    Sensor_Logic_Init();

    /* 1) create sensor task */
    sensor_task_thread = rt_thread_create("sensor_th",
                                          ReadSensor_Task, RT_NULL,
                                          SENSOR_TASK_SIZE,
                                          SENSOR_PRIORITY, TASK_TIMESLICE);

        /* start sensor task */
    if (sensor_task_thread != RT_NULL)
        rt_thread_startup(sensor_task_thread);

    /* 2) create control task */
    control_task_thread = rt_thread_create("con_th",
                                           StartControl_Task, RT_NULL,
                                           CONTROL_TASK_SIZE,
                                           CONTROL_PRIORITY, TASK_TIMESLICE);

        /* start control task */
    if (control_task_thread != RT_NULL)
        rt_thread_startup(control_task_thread);


    /* 3) create MQTT task */
    MQTT_task_thread = rt_thread_create("mqtt_th",
                                        Mqtt_Task, RT_NULL,
                                        MQTT_TASK_SIZE,
                                        MQTT_PRIORITY, TASK_TIMESLICE);
    if (MQTT_task_thread != RT_NULL)
        rt_thread_startup(MQTT_task_thread);

    rt_thread_mdelay(2000);
    return 0;
}

/* sensor task entry */
static void ReadSensor_Task(void *parameter)
{
    access_Sensor();
    rt_device_t dev = RT_NULL;
    struct rt_sensor_data sensor_data;
    rt_size_t res;
    rt_uint8_t get_data_freq = 1;  //1Hz
    dev = rt_device_find("temp_dht11");
    rt_device_open(dev, RT_DEVICE_FLAG_RDWR);
    rt_device_control(dev, RT_SENSOR_CTRL_SET_ODR, (void *)(&get_data_freq));

    while(1)
    {
        res = rt_device_read(dev, 0, &sensor_data, 1);
        if (sensor_data.data.temp >= 0)
        {
            uint8_t temp = (sensor_data.data.temp & 0xffff) >> 0;      // get temp
            tem = (rt_uint32_t)temp;
            uint8_t humi = (sensor_data.data.temp & 0xffff0000) >> 16; // get humi
            hum = (rt_uint32_t)humi;
        }
        //rt_kprintf("enter the ReadSensor_Task\n");


        //rt_kprintf("the hum = %d%, the tem = %d\r\n", hum, tem);
        //rt_kprintf("tem:%d\n" ,tem);

        // send temperature/humidity values via mailbox
        rt_mutex_take(dynamic_mutex, RT_WAITING_FOREVER);
        rt_mb_send(&tem_mb, tem);
        rt_thread_mdelay(10);
        rt_mb_send(&hum_mb, hum);
        //rt_kprintf("send the mailbox\n");
        rt_mutex_release(dynamic_mutex);

        // notify control task
        rt_sem_release(dynamic_sem);
        rt_thread_mdelay(1000);
    }
}
/* control task entry */
static void StartControl_Task(void *parameter)
{
    rt_err_t result;

    RT_UNUSED(parameter);

    while(1)
    {
        result = rt_sem_take(dynamic_sem, rt_tick_from_millisecond(CONTROL_LOOP_PERIOD_MS));
        if (result == RT_EOK)
        {
            Sensor_Logic_UpdateInputs();
        }
        else if (result != -RT_ETIMEOUT)
        {
            rt_kprintf("control sem take failed: %d\r\n", result);
        }

        Sensor_Logic_Running();
    }
}

/* MQTT task entry */
static void Mqtt_Task(void *parameter)
{
    void                   *pclient = NULL;
    int                     res = 0;
    int                     loop_cnt = 0;
    iotx_mqtt_param_t       mqtt_params;

    RT_UNUSED(parameter);

    HAL_GetProductKey(DEMO_PRODUCT_KEY);
    HAL_GetDeviceName(DEMO_DEVICE_NAME);
    HAL_GetDeviceSecret(DEMO_DEVICE_SECRET);

    EXAMPLE_TRACE("mqtt example");

    memset(&mqtt_params, 0x0, sizeof(mqtt_params));

    mqtt_params.handle_event.h_fp = example_event_handle;

    while (1)
    {
        pclient = IOT_MQTT_Construct(&mqtt_params);
        if (NULL == pclient)
        {
            mqtt_set_link_state(RT_FALSE);
            EXAMPLE_TRACE("MQTT construct failed, retry later");
            rt_thread_mdelay(2000);
            continue;
        }

        res = example_subscribe(pclient);
        if (res < 0)
        {
            mqtt_set_link_state(RT_FALSE);
            EXAMPLE_TRACE("MQTT subscribe failed, reconnect");
            IOT_MQTT_Destroy(&pclient);
            rt_thread_mdelay(2000);
            continue;
        }

        loop_cnt = 0;
        while (1)
        {
            if (0 == loop_cnt % 20)
            {
                my_publish(pclient);
            }

            res = IOT_MQTT_Yield(pclient, 200);
            if (res < 0)
            {
                mqtt_set_link_state(RT_FALSE);
                EXAMPLE_TRACE("MQTT yield failed, reconnect");
                IOT_MQTT_Destroy(&pclient);
                break;
            }

            loop_cnt += 1;
        }

        rt_thread_mdelay(2000);
    }
}

