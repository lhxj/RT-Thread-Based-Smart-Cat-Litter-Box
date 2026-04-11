/*
 * Copyright (c) 2006-2021, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2022-08-01     ywx          the first version
 */
#include <rtthread.h>
#include <rtdevice.h>
#include <board.h>

#include "logic.h"
#include "fsm.h"
#include "mqtt.h"
#include "sensor.h"
#include "drv_gpio.h"

#ifdef RT_USING_FINSH
#include <finsh.h>
#endif

int rt_hw_dht11_init(const char *name, struct rt_sensor_config *cfg);

/* mailboxes */
extern struct rt_mailbox tem_mb;
extern struct rt_mailbox hum_mb;

#define HONGWAI_PIN GET_PIN(E, 2)
#define IN1 GET_PIN(D, 8)
#define IN2 GET_PIN(D, 9)
#define BUZZER GET_PIN(D, 11)
#define O3 GET_PIN(D, 10)
#define DHT11_DATA_PIN GET_PIN(E, 10)

#define BIN_FULL_WEIGHT_THRESHOLD 500

static rt_uint32_t cur_hum;
static rt_uint32_t cur_tem;
static rt_bool_t g_logic_ready = RT_FALSE;
static rt_bool_t g_prev_occupied = RT_FALSE;
static rt_bool_t g_prev_bin_full = RT_FALSE;
static rt_bool_t g_remote_clean_request = RT_FALSE;
static rt_bool_t g_reset_request = RT_FALSE;
static litter_fsm_ctx_t g_fsm_ctx;

extern int cur_weight;

int box_used = 0;

static rt_bool_t logic_is_occupied(void)
{
    return (rt_pin_read(HONGWAI_PIN) == PIN_LOW) ? RT_TRUE : RT_FALSE;
}

static rt_bool_t logic_is_bin_full(void)
{
    return (cur_weight >= BIN_FULL_WEIGHT_THRESHOLD) ? RT_TRUE : RT_FALSE;
}

static rt_bool_t logic_is_protect_active(rt_bool_t occupied_now, litter_fsm_state_t current_state)
{
    if ((occupied_now == RT_TRUE) &&
        ((current_state == FSM_STATE_CLEANING) || (current_state == FSM_STATE_SAFE_STOP)))
    {
        return RT_TRUE;
    }

    return RT_FALSE;
}

static void logic_motor_forward_start(void)
{
    rt_pin_write(IN1, PIN_LOW);
    rt_pin_write(IN2, PIN_HIGH);
    rt_kprintf("[ACT] motor forward\r\n");
}

static void logic_motor_reverse_start(void)
{
    rt_pin_write(IN1, PIN_HIGH);
    rt_pin_write(IN2, PIN_LOW);
    rt_kprintf("[ACT] motor reverse\r\n");
}

static void logic_motor_stop(void)
{
    rt_pin_write(IN1, PIN_LOW);
    rt_pin_write(IN2, PIN_LOW);
    rt_pin_write(BUZZER, PIN_LOW);
    rt_pin_write(O3, PIN_LOW);
    rt_kprintf("[ACT] motor stop\r\n");
}

static void logic_fault_handler(int fault_code)
{
    rt_pin_write(BUZZER, PIN_LOW);
    rt_pin_write(O3, PIN_LOW);
    rt_kprintf("[LOGIC] fault latched=%d\r\n", fault_code);
}

static const litter_fsm_ops_t g_fsm_ops =
{
    .motor_forward_start = logic_motor_forward_start,
    .motor_reverse_start = logic_motor_reverse_start,
    .motor_stop = logic_motor_stop,
    .on_fault = logic_fault_handler,
};

/* initialize sensors */
void access_Sensor(void)
{
    struct rt_sensor_config cfg;

    cfg.intf.user_data = (void *)DHT11_DATA_PIN;
    rt_hw_dht11_init("dht11", &cfg);
}

void Sensor_Logic_Init(void)
{
    rt_bool_t occupied_now;
    rt_bool_t bin_full_now;

    if (g_logic_ready == RT_TRUE)
    {
        return;
    }

    rt_pin_mode(HONGWAI_PIN, PIN_MODE_INPUT);
    rt_pin_mode(IN1, PIN_MODE_OUTPUT);
    rt_pin_mode(IN2, PIN_MODE_OUTPUT);
    rt_pin_mode(BUZZER, PIN_MODE_OUTPUT);
    rt_pin_mode(O3, PIN_MODE_OUTPUT);

    occupied_now = logic_is_occupied();
    bin_full_now = logic_is_bin_full();

    litter_fsm_init(&g_fsm_ctx, &g_fsm_ops);

    litter_fsm_sync_inputs(&g_fsm_ctx, occupied_now, bin_full_now, RT_FALSE);
    g_prev_occupied = occupied_now;
    g_prev_bin_full = bin_full_now;

    if (occupied_now == RT_TRUE)
    {
        litter_fsm_dispatch(&g_fsm_ctx, EVT_OCCUPIED_ON);
    }

    if (bin_full_now == RT_TRUE)
    {
        litter_fsm_dispatch(&g_fsm_ctx, EVT_BIN_FULL);
    }

    g_logic_ready = RT_TRUE;
}

void Sensor_Logic_UpdateInputs(void)
{
    rt_ubase_t mb_value;

    if (g_logic_ready == RT_FALSE)
    {
        Sensor_Logic_Init();
    }

    if (rt_mb_recv(&tem_mb, &mb_value, RT_WAITING_NO) == RT_EOK)
    {
        cur_tem = (rt_uint32_t)mb_value;
    }

    if (rt_mb_recv(&hum_mb, &mb_value, RT_WAITING_NO) == RT_EOK)
    {
        cur_hum = (rt_uint32_t)mb_value;
    }

    RT_UNUSED(cur_tem);
    RT_UNUSED(cur_hum);
}

void Sensor_Logic_RequestClean(void)
{
    g_remote_clean_request = RT_TRUE;
    rt_kprintf("[LOGIC] clean request queued\r\n");
}

void Sensor_Logic_RequestReset(void)
{
    g_reset_request = RT_TRUE;
    rt_kprintf("[LOGIC] reset request queued\r\n");
}

litter_fsm_state_t Sensor_Logic_GetState(void)
{
    return litter_fsm_get_state(&g_fsm_ctx);
}

int Sensor_Logic_GetFaultCode(void)
{
    return litter_fsm_get_fault_code(&g_fsm_ctx);
}

const char *Sensor_Logic_StateName(void)
{
    return litter_fsm_state_name(Sensor_Logic_GetState());
}

const char *Sensor_Logic_FaultName(void)
{
    return litter_fsm_fault_name(Sensor_Logic_GetFaultCode());
}

rt_bool_t Sensor_Logic_IsBinFull(void)
{
    return litter_fsm_is_bin_full(&g_fsm_ctx);
}

rt_bool_t Sensor_Logic_IsProtectActive(void)
{
    return litter_fsm_is_protect_active(&g_fsm_ctx);
}

#ifdef RT_USING_FINSH
static void litter_status(void)
{
    rt_kprintf("state=%s fault=%s(%d) bin_full=%d protect=%d mqtt_link=%d\r\n",
               Sensor_Logic_StateName(),
               Sensor_Logic_FaultName(),
               Sensor_Logic_GetFaultCode(),
               Sensor_Logic_IsBinFull(),
               Sensor_Logic_IsProtectActive(),
               mqtt_is_link_online());
}
MSH_CMD_EXPORT(litter_status, show litter box state/fault/link status);

static void litter_reset(void)
{
    Sensor_Logic_RequestReset();
}
MSH_CMD_EXPORT(litter_reset, request local litter fault recover);
#endif

/* sensor control logic */
void Sensor_Logic_Running(void)
{
    litter_fsm_state_t current_state;
    rt_bool_t occupied_now;
    rt_bool_t bin_full_now;
    rt_bool_t protect_now;

    if (g_logic_ready == RT_FALSE)
    {
        Sensor_Logic_Init();
    }

    occupied_now = logic_is_occupied();
    bin_full_now = logic_is_bin_full();
    current_state = litter_fsm_get_state(&g_fsm_ctx);
    protect_now = logic_is_protect_active(occupied_now, current_state);

    litter_fsm_sync_inputs(&g_fsm_ctx, occupied_now, bin_full_now, protect_now);

    if ((bin_full_now == RT_TRUE) && (g_prev_bin_full == RT_FALSE))
    {
        litter_fsm_dispatch(&g_fsm_ctx, EVT_BIN_FULL);
    }

    current_state = litter_fsm_get_state(&g_fsm_ctx);
    if (current_state != FSM_STATE_FAULT)
    {
        if (occupied_now != g_prev_occupied)
        {
            if (occupied_now == RT_TRUE)
            {
                if (current_state == FSM_STATE_IDLE)
                {
                    box_used++;
                }

                if (current_state == FSM_STATE_CLEANING)
                {
                    litter_fsm_dispatch(&g_fsm_ctx, EVT_PROTECT_TRIGGER);
                }
                else
                {
                    litter_fsm_dispatch(&g_fsm_ctx, EVT_OCCUPIED_ON);
                }
            }
            else
            {
                litter_fsm_dispatch(&g_fsm_ctx, EVT_OCCUPIED_OFF);
            }
        }

        if (g_remote_clean_request == RT_TRUE)
        {
            g_remote_clean_request = RT_FALSE;
            litter_fsm_dispatch(&g_fsm_ctx, EVT_CLEAN_START);
        }
    }

    if (g_reset_request == RT_TRUE)
    {
        g_reset_request = RT_FALSE;
        litter_fsm_dispatch(&g_fsm_ctx, EVT_RESET);
    }

    litter_fsm_tick(&g_fsm_ctx);

    g_prev_occupied = occupied_now;
    g_prev_bin_full = bin_full_now;
}
