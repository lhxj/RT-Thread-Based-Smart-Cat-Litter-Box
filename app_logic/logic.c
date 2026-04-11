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
#define HALL_POS1_HOME_PIN GET_PIN(A, 4)
#define HALL_POS2_DUMP_PIN GET_PIN(A, 5)
#define HALL_POS3_SAND_RETURN_PIN GET_PIN(A, 6)

#define BIN_FULL_WEIGHT_THRESHOLD 500

typedef enum
{
    HALL_POSITION_UNKNOWN = 0,
    HALL_POSITION_TRANSITION,
    HALL_POSITION_POS1_HOME,
    HALL_POSITION_POS2_DUMP,
    HALL_POSITION_POS3_SAND_RETURN,
    HALL_POSITION_INVALID,
} litter_hall_position_t;

typedef struct
{
    rt_base_t pos1_pin;
    rt_base_t pos2_pin;
    rt_base_t pos3_pin;
    rt_uint8_t active_level;
    rt_uint32_t sample_period_ms;
    rt_uint32_t debounce_ms;
} litter_hall_config_t;

typedef struct
{
    rt_tick_t last_sample_tick;
    rt_tick_t candidate_started_tick;
    litter_hall_position_t raw_position;
    litter_hall_position_t candidate_position;
    litter_hall_position_t stable_position;
    litter_clean_phase_t last_reported_phase;
    litter_hall_position_t last_reported_position;
    rt_uint8_t raw_active_bits;
} litter_hall_state_t;

static rt_uint32_t cur_hum;
static rt_uint32_t cur_tem;
static rt_bool_t g_logic_ready = RT_FALSE;
static rt_bool_t g_prev_occupied = RT_FALSE;
static rt_bool_t g_prev_bin_full = RT_FALSE;
static rt_bool_t g_remote_clean_request = RT_FALSE;
static rt_bool_t g_reset_request = RT_FALSE;
static litter_fsm_ctx_t g_fsm_ctx;
static litter_hall_state_t g_hall_state;

static const litter_hall_config_t g_hall_cfg =
{
    /* Frozen development assumptions for hall GPIO, active level and sampling. */
    .pos1_pin = HALL_POS1_HOME_PIN,
    .pos2_pin = HALL_POS2_DUMP_PIN,
    .pos3_pin = HALL_POS3_SAND_RETURN_PIN,
    .active_level = PIN_LOW,
    .sample_period_ms = 10,
    .debounce_ms = 20,
};

extern int cur_weight;

int box_used = 0;

static void logic_hall_init(void)
{
    rt_memset(&g_hall_state, 0, sizeof(g_hall_state));
    g_hall_state.raw_position = HALL_POSITION_UNKNOWN;
    g_hall_state.candidate_position = HALL_POSITION_UNKNOWN;
    g_hall_state.stable_position = HALL_POSITION_UNKNOWN;
    g_hall_state.last_reported_phase = FSM_CLEAN_PHASE_NONE;
    g_hall_state.last_reported_position = HALL_POSITION_UNKNOWN;
}

static const char *logic_hall_position_name(litter_hall_position_t position)
{
    switch (position)
    {
    case HALL_POSITION_UNKNOWN:
        return "UNKNOWN";
    case HALL_POSITION_TRANSITION:
        return "TRANSITION";
    case HALL_POSITION_POS1_HOME:
        return "POS1_HOME";
    case HALL_POSITION_POS2_DUMP:
        return "POS2_DUMP";
    case HALL_POSITION_POS3_SAND_RETURN:
        return "POS3_SAND_RETURN";
    case HALL_POSITION_INVALID:
        return "INVALID";
    default:
        return "UNKNOWN";
    }
}

static rt_uint8_t logic_hall_read_active_bits(void)
{
    rt_uint8_t active_bits = 0;

    if (rt_pin_read(g_hall_cfg.pos1_pin) == g_hall_cfg.active_level)
    {
        active_bits |= 0x01;
    }

    if (rt_pin_read(g_hall_cfg.pos2_pin) == g_hall_cfg.active_level)
    {
        active_bits |= 0x02;
    }

    if (rt_pin_read(g_hall_cfg.pos3_pin) == g_hall_cfg.active_level)
    {
        active_bits |= 0x04;
    }

    return active_bits;
}

static litter_hall_position_t logic_hall_decode_position(rt_uint8_t active_bits)
{
    /* Truth table: one active-low hit means a stable position, all-high means transition. */
    switch (active_bits)
    {
    case 0x00:
        return HALL_POSITION_TRANSITION;
    case 0x01:
        return HALL_POSITION_POS1_HOME;
    case 0x02:
        return HALL_POSITION_POS2_DUMP;
    case 0x04:
        return HALL_POSITION_POS3_SAND_RETURN;
    default:
        return HALL_POSITION_INVALID;
    }
}

static litter_fsm_event_t logic_hall_event_from_position(litter_hall_position_t position)
{
    switch (position)
    {
    case HALL_POSITION_POS1_HOME:
        return EVT_POS1_REACHED;
    case HALL_POSITION_POS2_DUMP:
        return EVT_POS2_REACHED;
    case HALL_POSITION_POS3_SAND_RETURN:
        return EVT_POS3_REACHED;
    default:
        return EVT_NONE;
    }
}

static litter_hall_position_t logic_hall_target_position_from_phase(litter_clean_phase_t phase)
{
    switch (phase)
    {
    case FSM_CLEAN_PHASE_TO_POS2:
        return HALL_POSITION_POS2_DUMP;

    case FSM_CLEAN_PHASE_TO_POS3:
        return HALL_POSITION_POS3_SAND_RETURN;

    case FSM_CLEAN_PHASE_TO_POS1:
        return HALL_POSITION_POS1_HOME;

    case FSM_CLEAN_PHASE_NONE:
    default:
        return HALL_POSITION_UNKNOWN;
    }
}

static rt_bool_t logic_hall_poll(litter_hall_position_t *stable_position)
{
    rt_tick_t now;
    rt_tick_t sample_ticks;
    rt_tick_t debounce_ticks;
    rt_uint8_t active_bits;
    litter_hall_position_t raw_position;

    if (stable_position == RT_NULL)
    {
        return RT_FALSE;
    }

    *stable_position = g_hall_state.stable_position;
    now = rt_tick_get();
    sample_ticks = rt_tick_from_millisecond(g_hall_cfg.sample_period_ms);

    if ((g_hall_state.last_sample_tick != 0) &&
        (sample_ticks > 0) &&
        ((rt_int32_t)(now - g_hall_state.last_sample_tick) < (rt_int32_t)sample_ticks))
    {
        return RT_FALSE;
    }

    g_hall_state.last_sample_tick = now;
    active_bits = logic_hall_read_active_bits();
    raw_position = logic_hall_decode_position(active_bits);
    g_hall_state.raw_active_bits = active_bits;
    g_hall_state.raw_position = raw_position;

    if (g_hall_state.candidate_position != raw_position)
    {
        g_hall_state.candidate_position = raw_position;
        g_hall_state.candidate_started_tick = now;
        return RT_FALSE;
    }

    debounce_ticks = rt_tick_from_millisecond(g_hall_cfg.debounce_ms);
    if ((g_hall_state.stable_position != raw_position) &&
        ((debounce_ticks == 0) ||
         ((rt_int32_t)(now - g_hall_state.candidate_started_tick) >= (rt_int32_t)debounce_ticks)))
    {
        g_hall_state.stable_position = raw_position;
        *stable_position = raw_position;

        rt_kprintf("[LOGIC] hall stable=%s bits=0x%x\r\n",
                   logic_hall_position_name(raw_position),
                   active_bits);
        return RT_TRUE;
    }

    return RT_FALSE;
}

static void logic_dispatch_hall_event_if_needed(litter_hall_position_t stable_position,
                                                litter_fsm_state_t current_state,
                                                litter_clean_phase_t current_phase,
                                                rt_bool_t bin_full_now,
                                                rt_bool_t protect_now)
{
    litter_hall_position_t target_position;
    litter_fsm_event_t hall_event;

    if (current_state != FSM_STATE_CLEANING)
    {
        /* Bind dedupe to the cleaning lifecycle so a later cleaning run can
         * consume the current stable hall again if it already matches phase entry. */
        g_hall_state.last_reported_phase = FSM_CLEAN_PHASE_NONE;
        return;
    }

    if ((bin_full_now == RT_TRUE) || (protect_now == RT_TRUE))
    {
        return;
    }

    target_position = logic_hall_target_position_from_phase(current_phase);
    if ((target_position == HALL_POSITION_UNKNOWN) || (stable_position != target_position))
    {
        return;
    }

    /* Evaluate the current stable hall on every loop, not only on stable-position
     * changes, so phase entry can immediately consume an already-reached target. */
    hall_event = logic_hall_event_from_position(stable_position);
    if ((hall_event == EVT_NONE) ||
        ((g_hall_state.last_reported_phase == current_phase) &&
         (g_hall_state.last_reported_position == stable_position)))
    {
        return;
    }

    g_hall_state.last_reported_phase = current_phase;
    g_hall_state.last_reported_position = stable_position;
    litter_fsm_dispatch(&g_fsm_ctx, hall_event);
}

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
    rt_pin_mode(g_hall_cfg.pos1_pin, PIN_MODE_INPUT_PULLUP);
    rt_pin_mode(g_hall_cfg.pos2_pin, PIN_MODE_INPUT_PULLUP);
    rt_pin_mode(g_hall_cfg.pos3_pin, PIN_MODE_INPUT_PULLUP);

    occupied_now = logic_is_occupied();
    bin_full_now = logic_is_bin_full();
    logic_hall_init();

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

const char *Sensor_Logic_CleanPhaseName(void)
{
    return litter_fsm_clean_phase_name(litter_fsm_get_clean_phase(&g_fsm_ctx));
}

const char *Sensor_Logic_HallPositionName(void)
{
    return logic_hall_position_name(g_hall_state.stable_position);
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
    rt_kprintf("state=%s phase=%s hall=%s fault=%s(%d) bin_full=%d protect=%d mqtt_link=%d\r\n",
               Sensor_Logic_StateName(),
               Sensor_Logic_CleanPhaseName(),
               Sensor_Logic_HallPositionName(),
               Sensor_Logic_FaultName(),
               Sensor_Logic_GetFaultCode(),
               Sensor_Logic_IsBinFull(),
               Sensor_Logic_IsProtectActive(),
               mqtt_is_link_online());
}
MSH_CMD_EXPORT(litter_status, show litter box state/fault/link status);

static void litter_clean(void)
{
    Sensor_Logic_RequestClean();
}
MSH_CMD_EXPORT(litter_clean, queue a local clean request through the FSM);

static void litter_reset(void)
{
    Sensor_Logic_RequestReset();
}
MSH_CMD_EXPORT(litter_reset, request local litter fault recover);

static void litter_timeout(void)
{
    if (Sensor_Logic_GetState() != FSM_STATE_CLEANING)
    {
        rt_kprintf("litter_timeout ignored: state=%s (need CLEANING)\r\n",
                   Sensor_Logic_StateName());
        return;
    }

    rt_kprintf("[LOGIC] inject clean timeout fault\r\n");
    litter_fsm_dispatch(&g_fsm_ctx, EVT_STALL_OR_TIMEOUT);
}
MSH_CMD_EXPORT(litter_timeout, inject clean timeout fault while cleaning for local debug);
#endif

/* sensor control logic */
void Sensor_Logic_Running(void)
{
    litter_fsm_state_t current_state;
    litter_clean_phase_t current_phase;
    rt_bool_t occupied_now;
    rt_bool_t bin_full_now;
    rt_bool_t protect_now;
    litter_hall_position_t stable_hall_position;

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

    logic_hall_poll(&stable_hall_position);
    current_state = litter_fsm_get_state(&g_fsm_ctx);
    current_phase = litter_fsm_get_clean_phase(&g_fsm_ctx);
    logic_dispatch_hall_event_if_needed(stable_hall_position,
                                        current_state,
                                        current_phase,
                                        bin_full_now,
                                        protect_now);

    litter_fsm_tick(&g_fsm_ctx);

    g_prev_occupied = occupied_now;
    g_prev_bin_full = bin_full_now;
}
