#include <rtthread.h>

#include "fsm.h"

#define FSM_LOG_PREFIX "[FSM] "

static void fsm_enter_state(litter_fsm_ctx_t *ctx, litter_fsm_state_t next_state);
static void fsm_start_clean_phase(litter_fsm_ctx_t *ctx, litter_clean_phase_t phase);
static rt_uint32_t fsm_clean_phase_timeout_ms(const litter_fsm_ctx_t *ctx);
static void fsm_finish_cleaning(litter_fsm_ctx_t *ctx);

static void fsm_motor_stop(const litter_fsm_ctx_t *ctx)
{
    if ((ctx != RT_NULL) && (ctx->ops != RT_NULL) && (ctx->ops->motor_stop != RT_NULL))
    {
        ctx->ops->motor_stop();
    }
}

static litter_fsm_state_t fsm_resolve_idle_or_occupied(const litter_fsm_ctx_t *ctx)
{
    if ((ctx != RT_NULL) && (ctx->occupied == RT_TRUE))
    {
        return FSM_STATE_OCCUPIED;
    }

    return FSM_STATE_IDLE;
}

static void fsm_start_clean_phase(litter_fsm_ctx_t *ctx, litter_clean_phase_t phase)
{
    if (ctx == RT_NULL)
    {
        return;
    }

    /* Keep phase semantics as destination positions; motor direction stays local here. */
    ctx->clean_phase = phase;
    ctx->phase_started_tick = rt_tick_get();

    switch (phase)
    {
    case FSM_CLEAN_PHASE_TO_POS2:
        if ((ctx->ops != RT_NULL) && (ctx->ops->motor_forward_start != RT_NULL))
        {
            ctx->ops->motor_forward_start();
        }
        break;

    case FSM_CLEAN_PHASE_TO_POS3:
        if ((ctx->ops != RT_NULL) && (ctx->ops->motor_reverse_start != RT_NULL))
        {
            ctx->ops->motor_reverse_start();
        }
        break;

    case FSM_CLEAN_PHASE_TO_POS1:
        if ((ctx->ops != RT_NULL) && (ctx->ops->motor_forward_start != RT_NULL))
        {
            ctx->ops->motor_forward_start();
        }
        break;

    case FSM_CLEAN_PHASE_NONE:
    default:
        fsm_motor_stop(ctx);
        break;
    }

    rt_kprintf(FSM_LOG_PREFIX "CLEANING phase=%s\r\n",
               litter_fsm_clean_phase_name(ctx->clean_phase));
}

static rt_uint32_t fsm_clean_phase_timeout_ms(const litter_fsm_ctx_t *ctx)
{
    if (ctx == RT_NULL)
    {
        return 0;
    }

    switch (ctx->clean_phase)
    {
    case FSM_CLEAN_PHASE_TO_POS2:
        return ctx->timeout_to_pos2_ms;

    case FSM_CLEAN_PHASE_TO_POS3:
        return ctx->timeout_to_pos3_ms;

    case FSM_CLEAN_PHASE_TO_POS1:
        return ctx->timeout_to_pos1_ms;

    case FSM_CLEAN_PHASE_NONE:
    default:
        return 0;
    }
}

static void fsm_finish_cleaning(litter_fsm_ctx_t *ctx)
{
    if (ctx == RT_NULL)
    {
        return;
    }

    fsm_motor_stop(ctx);
    litter_fsm_dispatch(ctx, EVT_CLEAN_DONE);
}

static void fsm_log_bin_full_active(void)
{
    rt_kprintf(FSM_LOG_PREFIX "BIN_FULL active\r\n");
}

static void fsm_log_clean_block(const char *reason)
{
    rt_kprintf(FSM_LOG_PREFIX "INTERLOCK block clean: %s\r\n", reason);
}

static void fsm_recover_to_state(litter_fsm_ctx_t *ctx,
                                 litter_fsm_state_t next_state,
                                 const char *reason)
{
    if (ctx == RT_NULL)
    {
        return;
    }

    if (reason != RT_NULL)
    {
        rt_kprintf(FSM_LOG_PREFIX "%s %s -> %s\r\n",
                   reason,
                   litter_fsm_fault_name(ctx->fault_code),
                   litter_fsm_state_name(next_state));
    }

    ctx->fault_code = FSM_FAULT_NONE;
    fsm_enter_state(ctx, next_state);
}

static void fsm_enter_fault(litter_fsm_ctx_t *ctx,
                            litter_fsm_fault_t fault_code,
                            const char *reason)
{
    if (ctx == RT_NULL)
    {
        return;
    }

    if (reason != RT_NULL)
    {
        rt_kprintf(FSM_LOG_PREFIX "%s\r\n", reason);
    }

    ctx->fault_code = fault_code;

    if (ctx->state == FSM_STATE_FAULT)
    {
        fsm_motor_stop(ctx);
        if ((ctx->ops != RT_NULL) && (ctx->ops->on_fault != RT_NULL))
        {
            ctx->ops->on_fault(ctx->fault_code);
        }
        rt_kprintf(FSM_LOG_PREFIX "FAULT active %s(%d)\r\n",
                   litter_fsm_fault_name(ctx->fault_code),
                   ctx->fault_code);
        return;
    }

    fsm_enter_state(ctx, FSM_STATE_FAULT);
}

static void fsm_block_clean_request(litter_fsm_ctx_t *ctx, const char *reason)
{
    if (ctx == RT_NULL)
    {
        return;
    }

    fsm_log_clean_block(reason);
    fsm_enter_state(ctx, fsm_resolve_idle_or_occupied(ctx));
}

static void fsm_block_clean_for_bin_full(litter_fsm_ctx_t *ctx)
{
    if (ctx == RT_NULL)
    {
        return;
    }

    rt_kprintf(FSM_LOG_PREFIX "BIN_FULL block clean\r\n");
    fsm_enter_fault(ctx, FSM_FAULT_BIN_FULL, RT_NULL);
}

static void fsm_try_enter_cleaning(litter_fsm_ctx_t *ctx)
{
    if (ctx == RT_NULL)
    {
        return;
    }

    if (ctx->occupied == RT_TRUE)
    {
        fsm_block_clean_request(ctx, "occupied");
    }
    else if (ctx->bin_full == RT_TRUE)
    {
        fsm_block_clean_for_bin_full(ctx);
    }
    else if (ctx->protect_active == RT_TRUE)
    {
        fsm_block_clean_request(ctx, "protect active");
    }
    else
    {
        fsm_enter_state(ctx, FSM_STATE_CLEANING);
    }
}

static void fsm_stop_cleaning_for_protect(litter_fsm_ctx_t *ctx)
{
    if (ctx == RT_NULL)
    {
        return;
    }

    ctx->fault_code = FSM_FAULT_PROTECT_TRIGGER;
    rt_kprintf(FSM_LOG_PREFIX "PROTECT stop -> SAFE_STOP\r\n");
    fsm_enter_state(ctx, FSM_STATE_SAFE_STOP);
}

static void fsm_recover_from_safe_stop(litter_fsm_ctx_t *ctx)
{
    litter_fsm_state_t next_state;

    if (ctx == RT_NULL)
    {
        return;
    }

    if (ctx->bin_full == RT_TRUE)
    {
        rt_kprintf(FSM_LOG_PREFIX "SAFE_STOP release blocked: bin full\r\n");
        fsm_enter_fault(ctx, FSM_FAULT_BIN_FULL, RT_NULL);
        return;
    }

    if (ctx->occupied == RT_TRUE)
    {
        next_state = FSM_STATE_OCCUPIED;
    }
    else
    {
        next_state = FSM_STATE_LEAVE_CONFIRM;
    }

    fsm_recover_to_state(ctx, next_state, "SAFE_STOP recover");
}

static void fsm_handle_reset_recover(litter_fsm_ctx_t *ctx)
{
    litter_fsm_state_t next_state;

    if (ctx == RT_NULL)
    {
        return;
    }

    if (ctx->protect_active == RT_TRUE)
    {
        rt_kprintf(FSM_LOG_PREFIX "RESET blocked: protect active\r\n");
        return;
    }

    if (ctx->bin_full == RT_TRUE)
    {
        rt_kprintf(FSM_LOG_PREFIX "RESET blocked: bin full\r\n");
        return;
    }

    next_state = fsm_resolve_idle_or_occupied(ctx);
    fsm_recover_to_state(ctx, next_state, "RESET recover");
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
        ctx->fault_code = FSM_FAULT_NONE;
        fsm_motor_stop(ctx);
        break;

    case FSM_STATE_LEAVE_CONFIRM:
        ctx->clean_phase = FSM_CLEAN_PHASE_NONE;
        ctx->cleaning_active = RT_FALSE;
        ctx->fault_code = FSM_FAULT_NONE;
        ctx->deadline_tick = now + rt_tick_from_millisecond(ctx->leave_confirm_ms);
        fsm_motor_stop(ctx);
        break;

    case FSM_STATE_CLEAN_DELAY:
        ctx->clean_phase = FSM_CLEAN_PHASE_NONE;
        ctx->cleaning_active = RT_FALSE;
        ctx->fault_code = FSM_FAULT_NONE;
        ctx->deadline_tick = now + rt_tick_from_millisecond(ctx->clean_delay_ms);
        fsm_motor_stop(ctx);
        break;

    case FSM_STATE_CLEANING:
        ctx->cleaning_active = RT_TRUE;
        ctx->fault_code = FSM_FAULT_NONE;
        ctx->clean_started_tick = now;
        fsm_start_clean_phase(ctx, FSM_CLEAN_PHASE_TO_POS2);
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
        rt_kprintf(FSM_LOG_PREFIX "FAULT enter %s(%d)\r\n",
                   litter_fsm_fault_name(ctx->fault_code),
                   ctx->fault_code);
    }
    else if ((next_state == FSM_STATE_SAFE_STOP) && (ctx->fault_code != FSM_FAULT_NONE))
    {
        rt_kprintf(FSM_LOG_PREFIX "SAFE_STOP reason=%s(%d)\r\n",
                   litter_fsm_fault_name(ctx->fault_code),
                   ctx->fault_code);
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
    ctx->timeout_to_pos2_ms = 6000;
    ctx->timeout_to_pos3_ms = 5000;
    ctx->timeout_to_pos1_ms = 6000;
    ctx->clean_total_timeout_ms = 20000;
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
        break;

    case EVT_PROTECT_TRIGGER:
        ctx->protect_active = RT_TRUE;
        if (ctx->state == FSM_STATE_CLEANING)
        {
            fsm_stop_cleaning_for_protect(ctx);
        }
        return;

    case EVT_PROTECT_RELEASE:
        ctx->protect_active = RT_FALSE;
        break;

    case EVT_STALL_OR_TIMEOUT:
        rt_kprintf(FSM_LOG_PREFIX "CLEAN timeout\r\n");
        fsm_enter_fault(ctx, FSM_FAULT_CLEAN_TIMEOUT, RT_NULL);
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
            fsm_try_enter_cleaning(ctx);
        }
        else if (event == EVT_BIN_FULL)
        {
            fsm_log_bin_full_active();
            fsm_enter_fault(ctx, FSM_FAULT_BIN_FULL, RT_NULL);
        }
        break;

    case FSM_STATE_OCCUPIED:
        if (event == EVT_OCCUPIED_OFF)
        {
            fsm_enter_state(ctx, FSM_STATE_LEAVE_CONFIRM);
        }
        else if (event == EVT_CLEAN_START)
        {
            fsm_try_enter_cleaning(ctx);
        }
        else if (event == EVT_BIN_FULL)
        {
            fsm_log_bin_full_active();
            fsm_enter_fault(ctx, FSM_FAULT_BIN_FULL, RT_NULL);
        }
        break;

    case FSM_STATE_LEAVE_CONFIRM:
        if (event == EVT_OCCUPIED_ON)
        {
            rt_kprintf(FSM_LOG_PREFIX "INTERLOCK abort prep: occupied\r\n");
            fsm_enter_state(ctx, FSM_STATE_OCCUPIED);
        }
        else if (event == EVT_DELAY_TIMEOUT)
        {
            if (ctx->bin_full == RT_TRUE)
            {
                fsm_block_clean_for_bin_full(ctx);
            }
            else if (ctx->protect_active == RT_TRUE)
            {
                fsm_block_clean_request(ctx, "protect active");
            }
            else if (ctx->occupied == RT_TRUE)
            {
                rt_kprintf(FSM_LOG_PREFIX "INTERLOCK abort prep: occupied\r\n");
                fsm_enter_state(ctx, FSM_STATE_OCCUPIED);
            }
            else
            {
                fsm_enter_state(ctx, FSM_STATE_CLEAN_DELAY);
            }
        }
        else if (event == EVT_RESET)
        {
            fsm_handle_reset_recover(ctx);
        }
        else if (event == EVT_BIN_FULL)
        {
            fsm_block_clean_for_bin_full(ctx);
        }
        break;

    case FSM_STATE_CLEAN_DELAY:
        if (event == EVT_OCCUPIED_ON)
        {
            rt_kprintf(FSM_LOG_PREFIX "INTERLOCK abort prep: occupied\r\n");
            fsm_enter_state(ctx, FSM_STATE_OCCUPIED);
        }
        else if ((event == EVT_DELAY_TIMEOUT) || (event == EVT_CLEAN_START))
        {
            fsm_try_enter_cleaning(ctx);
        }
        else if (event == EVT_RESET)
        {
            fsm_handle_reset_recover(ctx);
        }
        else if (event == EVT_BIN_FULL)
        {
            fsm_block_clean_for_bin_full(ctx);
        }
        break;

    case FSM_STATE_CLEANING:
        if ((event == EVT_OCCUPIED_ON) || (event == EVT_PROTECT_TRIGGER))
        {
            fsm_stop_cleaning_for_protect(ctx);
        }
        else if (event == EVT_BIN_FULL)
        {
            fsm_block_clean_for_bin_full(ctx);
        }
        else if ((event == EVT_POS2_REACHED) &&
                 (ctx->clean_phase == FSM_CLEAN_PHASE_TO_POS2))
        {
            fsm_start_clean_phase(ctx, FSM_CLEAN_PHASE_TO_POS3);
        }
        else if ((event == EVT_POS3_REACHED) &&
                 (ctx->clean_phase == FSM_CLEAN_PHASE_TO_POS3))
        {
            fsm_start_clean_phase(ctx, FSM_CLEAN_PHASE_TO_POS1);
        }
        else if ((event == EVT_POS1_REACHED) &&
                 (ctx->clean_phase == FSM_CLEAN_PHASE_TO_POS1))
        {
            fsm_finish_cleaning(ctx);
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
            fsm_recover_from_safe_stop(ctx);
        }
        else if (event == EVT_BIN_FULL)
        {
            fsm_log_bin_full_active();
        }
        else if (event == EVT_RESET)
        {
            fsm_handle_reset_recover(ctx);
        }
        break;

    case FSM_STATE_FAULT:
        if (event == EVT_BIN_FULL)
        {
            fsm_log_bin_full_active();
        }
        if (event == EVT_RESET)
        {
            fsm_handle_reset_recover(ctx);
        }
        break;

    default:
        break;
    }
}

void litter_fsm_tick(litter_fsm_ctx_t *ctx)
{
    rt_tick_t now;
    rt_tick_t clean_total_timeout_ticks;
    rt_tick_t clean_phase_timeout_ticks;

    if (ctx == RT_NULL)
    {
        return;
    }

    now = rt_tick_get();

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

    if ((ctx->state == FSM_STATE_FAULT) &&
        (ctx->fault_code == FSM_FAULT_BIN_FULL) &&
        (ctx->bin_full == RT_FALSE))
    {
        fsm_recover_to_state(ctx, fsm_resolve_idle_or_occupied(ctx), "FAULT clear");
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
        clean_total_timeout_ticks = rt_tick_from_millisecond(ctx->clean_total_timeout_ms);
        clean_phase_timeout_ticks = rt_tick_from_millisecond(fsm_clean_phase_timeout_ms(ctx));

        if ((clean_total_timeout_ticks > 0) &&
            ((rt_uint32_t)(now - ctx->clean_started_tick) >= clean_total_timeout_ticks))
        {
            litter_fsm_dispatch(ctx, EVT_STALL_OR_TIMEOUT);
            break;
        }

        if ((clean_phase_timeout_ticks > 0) &&
            ((rt_uint32_t)(now - ctx->phase_started_tick) >= clean_phase_timeout_ticks))
        {
            litter_fsm_dispatch(ctx, EVT_STALL_OR_TIMEOUT);
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

litter_clean_phase_t litter_fsm_get_clean_phase(const litter_fsm_ctx_t *ctx)
{
    if (ctx == RT_NULL)
    {
        return FSM_CLEAN_PHASE_NONE;
    }

    return ctx->clean_phase;
}

int litter_fsm_get_fault_code(const litter_fsm_ctx_t *ctx)
{
    if (ctx == RT_NULL)
    {
        return FSM_FAULT_NONE;
    }

    return ctx->fault_code;
}

rt_bool_t litter_fsm_is_bin_full(const litter_fsm_ctx_t *ctx)
{
    if (ctx == RT_NULL)
    {
        return RT_FALSE;
    }

    return ctx->bin_full;
}

rt_bool_t litter_fsm_is_protect_active(const litter_fsm_ctx_t *ctx)
{
    if (ctx == RT_NULL)
    {
        return RT_FALSE;
    }

    return ctx->protect_active;
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
    case EVT_POS1_REACHED:
        return "EVT_POS1_REACHED";
    case EVT_POS2_REACHED:
        return "EVT_POS2_REACHED";
    case EVT_POS3_REACHED:
        return "EVT_POS3_REACHED";
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

const char *litter_fsm_fault_name(int fault_code)
{
    switch ((litter_fsm_fault_t)fault_code)
    {
    case FSM_FAULT_NONE:
        return "NONE";
    case FSM_FAULT_BIN_FULL:
        return "BIN_FULL";
    case FSM_FAULT_CLEAN_TIMEOUT:
        return "CLEAN_TIMEOUT";
    case FSM_FAULT_PROTECT_TRIGGER:
        return "PROTECT_TRIGGER";
    default:
        return "UNKNOWN";
    }
}

const char *litter_fsm_clean_phase_name(litter_clean_phase_t phase)
{
    switch (phase)
    {
    case FSM_CLEAN_PHASE_NONE:
        return "NONE";
    case FSM_CLEAN_PHASE_TO_POS2:
        return "TO_POS2";
    case FSM_CLEAN_PHASE_TO_POS3:
        return "TO_POS3";
    case FSM_CLEAN_PHASE_TO_POS1:
        return "TO_POS1";
    default:
        return "UNKNOWN";
    }
}
