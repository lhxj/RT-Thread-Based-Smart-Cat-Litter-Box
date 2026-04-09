/*
 * Copyright (c) 2006-2021, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2022-08-01     ywx       the first version
 */
/*
 * Copyright (C) 2015-2018 Alibaba Group Holding Limited
 *
 * Again edit by rt-thread group
 * Change Logs:
 * Date          Author          Notes
 * 2019-07-21    MurphyZhao      first edit
 */

#include "rtthread.h"
#include "dev_sign_api.h"
#include "mqtt_api.h"
#include "mqtt.h"
#include <rtdevice.h>
#include <board.h>
#include "rtconfig.h"
#include <string.h>

int flag = 0;

extern rt_uint32_t hum, tem;
extern int cur_weight;
extern int box_used;
#define IN1 GET_PIN(D, 8)
#define IN2 GET_PIN(D, 9)
#define BUZZER GET_PIN(D, 11)
char DEMO_PRODUCT_KEY[IOTX_PRODUCT_KEY_LEN + 1] = {0};
char DEMO_DEVICE_NAME[IOTX_DEVICE_NAME_LEN + 1] = {0};
char DEMO_DEVICE_SECRET[IOTX_DEVICE_SECRET_LEN + 1] = {0};

static int payload_contains(const char *payload, int payload_len, const char *token)
{
    int token_len = strlen(token);
    int i;

    if (payload == RT_NULL || token == RT_NULL || payload_len <= 0 || token_len <= 0 || payload_len < token_len)
    {
        return 0;
    }

    for (i = 0; i <= payload_len - token_len; i++)
    {
        if (memcmp(payload + i, token, token_len) == 0)
        {
            return 1;
        }
    }

    return 0;
}

static void handle_remote_control(const iotx_mqtt_topic_info_t *topic_info)
{
    const char *payload = (const char *)topic_info->payload;
    int payload_len = topic_info->payload_len;

    /* Accept a few simple command styles for compatibility:
     * {"CleanNow":1} / {"clean_now":1} / {"flag":1}
     */
    if (payload_contains(payload, payload_len, "\"CleanNow\":1") ||
        payload_contains(payload, payload_len, "\"clean_now\":1") ||
        payload_contains(payload, payload_len, "\"flag\":1"))
    {
        flag = 1;
        EXAMPLE_TRACE("remote clean command accepted");
    }
    else if (payload_contains(payload, payload_len, "\"CleanNow\":0") ||
             payload_contains(payload, payload_len, "\"clean_now\":0") ||
             payload_contains(payload, payload_len, "\"flag\":0"))
    {
        flag = 0;
        rt_pin_write(IN1, PIN_LOW);
        rt_pin_write(IN2, PIN_LOW);
        EXAMPLE_TRACE("remote clean command cleared");
    }
}

void example_message_arrive(void *pcontext, void *pclient, iotx_mqtt_event_msg_pt msg)
{
    iotx_mqtt_topic_info_t     *topic_info = (iotx_mqtt_topic_info_pt) msg->msg;

    switch (msg->event_type) {
        case IOTX_MQTT_EVENT_PUBLISH_RECEIVED:
            /* print topic name and topic message */
            EXAMPLE_TRACE("Message Arrived:");
            EXAMPLE_TRACE("Topic  : %.*s", topic_info->topic_len, topic_info->ptopic);
            EXAMPLE_TRACE("Payload: %.*s", topic_info->payload_len, topic_info->payload);
            EXAMPLE_TRACE("\n");

            if (topic_info->ptopic && strstr(topic_info->ptopic, "thing/service/property/set") != RT_NULL)
            {
                handle_remote_control(topic_info);
            }
            break;
        default:
            break;
    }
}

int example_subscribe(void *handle)
{
    int res = 0;
    const char *fmt = "/sys/%s/%s/thing/service/property/set";
    char *topic = NULL;
    int topic_len = 0;

    topic_len = strlen(fmt) + strlen(DEMO_PRODUCT_KEY) + strlen(DEMO_DEVICE_NAME) + 1;
    topic = HAL_Malloc(topic_len);
    if (topic == NULL) {
        EXAMPLE_TRACE("memory not enough");
        return -1;
    }
    memset(topic, 0, topic_len);
    HAL_Snprintf(topic, topic_len, fmt, DEMO_PRODUCT_KEY, DEMO_DEVICE_NAME);

    res = IOT_MQTT_Subscribe(handle, topic, IOTX_MQTT_QOS0, example_message_arrive, NULL);
    if (res < 0) {
        EXAMPLE_TRACE("subscribe failed");
        HAL_Free(topic);
        return -1;
    }

    HAL_Free(topic);
    return 0;
}

int my_publish(void *handle)
{
    int             res = 0;
    const char     *fmt = "/sys/%s/%s/thing/event/property/post";
    const char     *fmt_payload = "{\"params\" : { \"temperature\":%d,\"humidity\":%d, \"Weight\":%d, \"RunTimes\":%d} }";
    char           *topic = NULL;
    int             topic_len = 0;
    int             payload_len = 0;
    char           *payload = NULL;
    payload_len = 160;
    payload = HAL_Malloc(payload_len);
        if (payload == NULL) {
            EXAMPLE_TRACE("memory not enough");
            return -1;
        }
        memset(payload, 0, payload_len);
    HAL_Snprintf(payload,payload_len,fmt_payload,tem,hum,cur_weight,box_used);

    topic_len = strlen(fmt) + strlen(DEMO_PRODUCT_KEY) + strlen(DEMO_DEVICE_NAME) + 1;
    topic = HAL_Malloc(topic_len);
    if (topic == NULL) {
        EXAMPLE_TRACE("memory not enough");
        return -1;
    }
    memset(topic, 0, topic_len);
    HAL_Snprintf(topic, topic_len, fmt, DEMO_PRODUCT_KEY, DEMO_DEVICE_NAME);


    res = IOT_MQTT_Publish_Simple(0, topic, IOTX_MQTT_QOS0, payload, strlen(payload));
    if (res < 0) {
        EXAMPLE_TRACE("publish failed, res = %d", res);
        HAL_Free(topic);
        HAL_Free(payload);
        return -1;
    }

    HAL_Free(topic);
    HAL_Free(payload);
    return 0;
}

void example_event_handle(void *pcontext, void *pclient, iotx_mqtt_event_msg_pt msg)
{
    EXAMPLE_TRACE("msg->event_type : %d", msg->event_type);

    if (msg->event_type == IOTX_MQTT_EVENT_DISCONNECT)
    {
        flag = 0;
        rt_pin_write(IN1, PIN_LOW);
        rt_pin_write(IN2, PIN_LOW);
    }
}

