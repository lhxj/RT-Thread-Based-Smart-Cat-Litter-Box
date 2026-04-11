#include <rtthread.h>

#include "fsm.h"

#define FSM_LOG_PREFIX "[FSM] "

static void fsm_motor_stop(const litter_fsm_ctx_t *ctx)
{
    if ((ctx != RT_NULL) && (ctx->ops != RT_NULL) && (ctx->ops->motor_stop != RT_NULL))
    {
        ctx->ops->motor_stop();
    }
}

static void fsm_enter_state(litter_fsm_ctx_t *ctx, litter_fsm_state_t next_state)
{
    rt_tick_t now;

    if (ctx == RT_NULL)
    {
        return;
    }

    now = rt_tick_get();

    if (ctx->state == next_state)
    {
        return;
    }

    if (ctx->state != next_state)
    {
        rt_kprintf(FSM_LOG_PREFIX "EXIT %s\r\n", litter_fsm_state_name(ctx->state));
    }

    ctx->state = next_state;
    ctx->state_enter_tick = now;
    ctx->last_event_tick = now;
    ctx->deadline_tick = 0;

    switch (next_state)
    {
    case FSM_STATE_IDLE:
        ctx->clean_phase = FSM_CLEAN_PHASE_NONE;
        ctx->cleaning_active = RT_FALSE;
        ctx->fault_code = FSM_FAULT_NONE;
        fsm_motor_stop(ctx);
        break;

    case FSM_STATE_OCCUPIED:
        ctx->clean_phase = FSM_CLEAN_PHASE_NONE;
        ctx->cleaning_active = RT_FALSE;
        fsm_motor_stop(ctx);
        break;

    case FSM_STATE_LEAVE_CONFIRM:
        ctx->clean_phase = FSM_CLEAN_PHASE_NONE;
        ctx->cleaning_active = RT_FALSE;
        ctx->deadline_tick = now + rt_tick_from_millisecond(ctx->leave_confirm_ms);
        fsm_motor_stop(ctx);
        break;

    case FSM_STATE_CLEAN_DELAY:
        ctx->clean_phase = FSM_CLEAN_PHASE_NONE;
        ctx->cleaning_active = RT_FALSE;
        ctx->deadline_tick = now + rt_tick_from_millisecond(ctx->clean_delay_ms);
        fsm_motor_stop(ctx);
        break;

    case FSM_STATE_CLEANING:
        ctx->cleaning_active = RT_TRUE;
        ctx->clean_started_tick = now;
        ctx->phase_started_tick = now;
        ctx->clean_phase = FSM_CLEAN_PHASE_FORWARD;
        if ((ctx->ops != RT_NULL) && (ctx->ops->motor_forward_start != RT_NULL))
        {
            ctx->ops->motor_forward_start();
        }
        rt_kprintf(FSM_LOG_PREFIX "CLEANING phase=FORWARD\r\n");
        break;

    case FSM_STATE_SAFE_STOP:
        ctx->clean_phase = FSM_CLEAN_PHASE_NONE;
        ctx->cleaning_active = RT_FALSE;
        fsm_motor_stop(ctx);
        break;

    case FSM_STATE_FAULT:
        ctx->clean_phase = FSM_CLEAN_PHASE_NONE;
        ctx->cleaning_active = RT_FALSE;
        fsm_motor_stop(ctx);
        if ((ctx->ops != RT_NULL) && (ctx->ops->on_fault != RT_NULL))
        {
            ctx->ops->on_fault(ctx->fault_code);
        }
        break;

    default:
        break;
    }

    rt_kprintf(FSM_LOG_PREFIX "ENTER %s\r\n", litter_fsm_state_name(next_state));
    if (next_state == FSM_STATE_FAULT)
    {
        rt_kprintf(FSM_LOG_PREFIX "FAULT code=%d\r\n", ctx->fault_code);
    }
}

static rt_bool_t fsm_deadline_expired(rt_tick_t now, rt_tick_t deadline)
{
    if (deadline == 0)
    {
        return RT_FALSE;
    }

    return ((rt_int32_t)(now - deadline) >= 0) ? RT_TRUE : RT_FALSE;
}

void litter_fsm_init(litter_fsm_ctx_t *ctx, const litter_fsm_ops_t *ops)
{
    if (ctx == RT_NULL)
    {
        return;
    }

    rt_memset(ctx, 0, sizeof(*ctx));
    ctx->ops = ops;
    ctx->leave_confirm_ms = 2000;
    ctx->clean_delay_ms = 5000;
    ctx->clean_forward_ms = 10000;
    ctx->clean_reverse_ms = 10000;
    ctx->clean_timeout_ms = 23000;
    ctx->state = FSM_STATE_IDLE;
    ctx->state_enter_tick = rt_tick_get();
    ctx->fault_code = FSM_FAULT_NONE;
    fsm_motor_stop(ctx);
    rt_kprintf(FSM_LOG_PREFIX "ENTER %s\r\n", litter_fsm_state_name(ctx->state));
}

void litter_fsm_sync_inputs(litter_fsm_ctx_t *ctx,
                            rt_bool_t occupied,
                            rt_bool_t bin_full,
                            rt_bool_t protect_active)
{
    if (ctx == RT_NULL)
    {
        return;
    }

    ctx->occupied = occupied;
    ctx->bin_full = bin_full;
    ctx->protect_active = protect_active;
}

void litter_fsm_dispatch(litter_fsm_ctx_t *ctx, litter_fsm_event_t event)
{
    if ((ctx == RT_NULL) || (event == EVT_NONE))
    {
        return;
    }

    ctx->last_event_tick = rt_tick_get();
    rt_kprintf(FSM_LOG_PREFIX "EVT %s @ %s\r\n",
               litter_fsm_event_name(event),
               litter_fsm_state_name(ctx->state));

    switch (event)
    {
    case EVT_OCCUPIED_ON:
        ctx->occupied = RT_TRUE;
        break;

    case EVT_OCCUPIED_OFF:
        ctx->occupied = RT_FALSE;
        break;

    case EVT_BIN_FULL:
        ctx->bin_full = RT_TRUE;
        ctx->fault_code = FSM_FAULT_BIN_FULL;
        fsm_enter_state(ctx, FSM_STATE_FAULT);
        return;

    case EVT_PROTECT_TRIGGER:
        ctx->protect_active = RT_TRUE;
        if (ctx->state != FSM_STATE_FAULT)
        {
            fsm_enter_state(ctx, FSM_STATE_SAFE_STOP);
        }
        return;

    case EVT_PROTECT_RELEASE:
        ctx->protect_active = RT_FALSE;
        break;

    case EVT_STALL_OR_TIMEOUT:
        ctx->fault_code = FSM_FAULT_CLEAN_TIMEOUT;
        fsm_enter_state(ctx, FSM_STATE_FAULT);
        return;

    default:
        break;
    }

    switch (ctx->state)
    {
    case FSM_STATE_IDLE:
        if (event == EVT_OCCUPIED_ON)
        {
            fsm_enter_state(ctx, FSM_STATE_OCCUPIED);
        }
        else if (event == EVT_CLEAN_START)
        {
            if (ctx->occupied == RT_TRUE)
            {
                fsm_enter_state(ctx, FSM_STATE_OCCUPIED);
            }
            else if ((ctx->bin_full == RT_FALSE) && (ctx->protect_active == RT_FALSE))
            {
                fsm_enter_state(ctx, FSM_STATE_CLEANING);
            }
        }
        break;

    case FSM_STATE_OCCUPIED:
        if (event == EVT_OCCUPIED_OFF)
        {
            fsm_enter_state(ctx, FSM_STATE_LEAVE_CONFIRM);
        }
        break;

    case FSM_STATE_LEAVE_CONFIRM:
        if (event == EVT_OCCUPIED_ON)
        {
            fsm_enter_state(ctx, FSM_STATE_OCCUPIED);
        }
        else if (event == EVT_DELAY_TIMEOUT)
        {
            fsm_enter_state(ctx, FSM_STATE_CLEAN_DELAY);
        }
        else if (event == EVT_RESET)
        {
            fsm_enter_state(ctx, FSM_STATE_IDLE);
        }
        break;

    case FSM_STATE_CLEAN_DELAY:
        if (event == EVT_OCCUPIED_ON)
        {
            fsm_enter_state(ctx, FSM_STATE_OCCUPIED);
        }
        else if ((event == EVT_DELAY_TIMEOUT) || (event == EVT_CLEAN_START))
        {
            if ((ctx->bin_full == RT_FALSE) && (ctx->protect_active == RT_FALSE))
            {
                fsm_enter_state(ctx, FSM_STATE_CLEANING);
            }
        }
        else if (event == EVT_RESET)
        {
            fsm_enter_state(ctx, FSM_STATE_IDLE);
        }
        break;

    case FSM_STATE_CLEANING:
        if ((event == EVT_OCCUPIED_ON) || (event == EVT_PROTECT_TRIGGER))
        {
            fsm_enter_state(ctx, FSM_STATE_SAFE_STOP);
        }
        else if (event == EVT_CLEAN_DONE)
        {
            fsm_enter_state(ctx, FSM_STATE_IDLE);
        }
        else if (event == EVT_RESET)
        {
            fsm_enter_state(ctx, FSM_STATE_IDLE);
        }
        break;

    case FSM_STATE_SAFE_STOP:
        if (event == EVT_PROTECT_RELEASE)
        {
            if (ctx->occupied == RT_TRUE)
            {
                fsm_enter_state(ctx, FSM_STATE_OCCUPIED);
            }
            else
            {
                fsm_enter_state(ctx, FSM_STATE_LEAVE_CONFIRM);
            }
        }
        else if ((event == EVT_RESET) &&
                 (ctx->occupied == RT_FALSE) &&
                 (ctx->bin_full == RT_FALSE) &&
                 (ctx->protect_active == RT_FALSE))
        {
            fsm_enter_state(ctx, FSM_STATE_IDLE);
        }
        break;

    case FSM_STATE_FAULT:
        if ((event == EVT_RESET) &&
            (ctx->occupied == RT_FALSE) &&
            (ctx->bin_full == RT_FALSE) &&
            (ctx->protect_active == RT_FALSE))
        {
            fsm_enter_state(ctx, FSM_STATE_IDLE);
        }
        break;

    default:
        break;
    }
}

void litter_fsm_tick(litter_fsm_ctx_t *ctx)
{
    rt_tick_t now;
    rt_tick_t clean_timeout_ticks;
    rt_tick_t clean_forward_ticks;
    rt_tick_t clean_reverse_ticks;

    if (ctx == RT_NULL)
    {
        return;
    }

    now = rt_tick_get();

    if ((ctx->state != FSM_STATE_FAULT) && (ctx->bin_full == RT_TRUE))
    {
        litter_fsm_dispatch(ctx, EVT_BIN_FULL);
        return;
    }

    if ((ctx->state == FSM_STATE_CLEANING) && (ctx->protect_active == RT_TRUE))
    {
        litter_fsm_dispatch(ctx, EVT_PROTECT_TRIGGER);
        return;
    }

    if ((ctx->state == FSM_STATE_SAFE_STOP) && (ctx->protect_active == RT_FALSE))
    {
        litter_fsm_dispatch(ctx, EVT_PROTECT_RELEASE);
        return;
    }

    switch (ctx->state)
    {
    case FSM_STATE_LEAVE_CONFIRM:
    case FSM_STATE_CLEAN_DELAY:
        if (fsm_deadline_expired(now, ctx->deadline_tick) == RT_TRUE)
        {
            litter_fsm_dispatch(ctx, EVT_DELAY_TIMEOUT);
        }
        break;

    case FSM_STATE_CLEANING:
        clean_timeout_ticks = rt_tick_from_millisecond(ctx->clean_timeout_ms);
        clean_forward_ticks = rt_tick_from_millisecond(ctx->clean_forward_ms);
        clean_reverse_ticks = rt_tick_from_millisecond(ctx->clean_reverse_ms);

        if ((rt_uint32_t)(now - ctx->clean_started_tick) >= clean_timeout_ticks)
        {
            litter_fsm_dispatch(ctx, EVT_STALL_OR_TIMEOUT);
            break;
        }

        if ((ctx->clean_phase == FSM_CLEAN_PHASE_FORWARD) &&
            ((rt_uint32_t)(now - ctx->phase_started_tick) >= clean_forward_ticks))
        {
            ctx->clean_phase = FSM_CLEAN_PHASE_REVERSE;
            ctx->phase_started_tick = now;
            if ((ctx->ops != RT_NULL) && (ctx->ops->motor_reverse_start != RT_NULL))
            {
                ctx->ops->motor_reverse_start();
            }
            rt_kprintf(FSM_LOG_PREFIX "CLEANING phase=REVERSE\r\n");
        }
        else if ((ctx->clean_phase == FSM_CLEAN_PHASE_REVERSE) &&
                 ((rt_uint32_t)(now - ctx->phase_started_tick) >= clean_reverse_ticks))
        {
            litter_fsm_dispatch(ctx, EVT_CLEAN_DONE);
        }
        break;

    default:
        break;
    }
}

litter_fsm_state_t litter_fsm_get_state(const litter_fsm_ctx_t *ctx)
{
    if (ctx == RT_NULL)
    {
        return FSM_STATE_FAULT;
    }

    return ctx->state;
}

int litter_fsm_get_fault_code(const litter_fsm_ctx_t *ctx)
{
    if (ctx == RT_NULL)
    {
        return FSM_FAULT_NONE;
    }

    return ctx->fault_code;
}

const char *litter_fsm_state_name(litter_fsm_state_t state)
{
    switch (state)
    {
    case FSM_STATE_IDLE:
        return "IDLE";
    case FSM_STATE_OCCUPIED:
        return "OCCUPIED";
    case FSM_STATE_LEAVE_CONFIRM:
        return "LEAVE_CONFIRM";
    case FSM_STATE_CLEAN_DELAY:
        return "CLEAN_DELAY";
    case FSM_STATE_CLEANING:
        return "CLEANING";
    case FSM_STATE_SAFE_STOP:
        return "SAFE_STOP";
    case FSM_STATE_FAULT:
        return "FAULT";
    default:
        return "UNKNOWN";
    }
}

const char *litter_fsm_event_name(litter_fsm_event_t event)
{
    switch (event)
    {
    case EVT_OCCUPIED_ON:
        return "EVT_OCCUPIED_ON";
    case EVT_OCCUPIED_OFF:
        return "EVT_OCCUPIED_OFF";
    case EVT_DELAY_TIMEOUT:
        return "EVT_DELAY_TIMEOUT";
    case EVT_CLEAN_START:
        return "EVT_CLEAN_START";
    case EVT_CLEAN_DONE:
        return "EVT_CLEAN_DONE";
    case EVT_PROTECT_TRIGGER:
        return "EVT_PROTECT_TRIGGER";
    case EVT_PROTECT_RELEASE:
        return "EVT_PROTECT_RELEASE";
    case EVT_STALL_OR_TIMEOUT:
        return "EVT_STALL_OR_TIMEOUT";
    case EVT_BIN_FULL:
        return "EVT_BIN_FULL";
    case EVT_RESET:
        return "EVT_RESET";
    default:
        return "EVT_NONE";
    }
}
