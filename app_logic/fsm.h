#ifndef APPLICATIONS_APP_LOGIC_FSM_H_
#define APPLICATIONS_APP_LOGIC_FSM_H_

#include <rtthread.h>

typedef enum
{
    FSM_STATE_IDLE = 0,
    FSM_STATE_OCCUPIED,
    FSM_STATE_LEAVE_CONFIRM,
    FSM_STATE_CLEAN_DELAY,
    FSM_STATE_CLEANING,
    FSM_STATE_SAFE_STOP,
    FSM_STATE_FAULT,
} litter_fsm_state_t;

typedef enum
{
    EVT_NONE = 0,
    EVT_OCCUPIED_ON,
    EVT_OCCUPIED_OFF,
    EVT_DELAY_TIMEOUT,
    EVT_CLEAN_START,
    EVT_CLEAN_DONE,
    EVT_PROTECT_TRIGGER,
    EVT_PROTECT_RELEASE,
    EVT_STALL_OR_TIMEOUT,
    EVT_BIN_FULL,
    EVT_RESET,
} litter_fsm_event_t;

typedef enum
{
    FSM_FAULT_NONE = 0,
    FSM_FAULT_BIN_FULL,
    FSM_FAULT_CLEAN_TIMEOUT,
    FSM_FAULT_PROTECT_TRIGGER,
} litter_fsm_fault_t;

typedef enum
{
    FSM_CLEAN_PHASE_NONE = 0,
    FSM_CLEAN_PHASE_FORWARD,
    FSM_CLEAN_PHASE_REVERSE,
} litter_clean_phase_t;

typedef struct
{
    void (*motor_forward_start)(void);
    void (*motor_reverse_start)(void);
    void (*motor_stop)(void);
    void (*on_fault)(int fault_code);
} litter_fsm_ops_t;

typedef struct
{
    litter_fsm_state_t state;
    rt_tick_t state_enter_tick;
    rt_tick_t last_event_tick;
    rt_tick_t clean_started_tick;
    rt_tick_t phase_started_tick;
    rt_tick_t deadline_tick;
    rt_bool_t occupied;
    rt_bool_t bin_full;
    rt_bool_t protect_active;
    rt_bool_t cleaning_active;
    litter_clean_phase_t clean_phase;
    int fault_code;
    rt_uint32_t leave_confirm_ms;
    rt_uint32_t clean_delay_ms;
    rt_uint32_t clean_forward_ms;
    rt_uint32_t clean_reverse_ms;
    rt_uint32_t clean_timeout_ms;
    const litter_fsm_ops_t *ops;
} litter_fsm_ctx_t;

void litter_fsm_init(litter_fsm_ctx_t *ctx, const litter_fsm_ops_t *ops);
void litter_fsm_sync_inputs(litter_fsm_ctx_t *ctx,
                            rt_bool_t occupied,
                            rt_bool_t bin_full,
                            rt_bool_t protect_active);
void litter_fsm_dispatch(litter_fsm_ctx_t *ctx, litter_fsm_event_t event);
void litter_fsm_tick(litter_fsm_ctx_t *ctx);
litter_fsm_state_t litter_fsm_get_state(const litter_fsm_ctx_t *ctx);
int litter_fsm_get_fault_code(const litter_fsm_ctx_t *ctx);
rt_bool_t litter_fsm_is_bin_full(const litter_fsm_ctx_t *ctx);
rt_bool_t litter_fsm_is_protect_active(const litter_fsm_ctx_t *ctx);
const char *litter_fsm_state_name(litter_fsm_state_t state);
const char *litter_fsm_event_name(litter_fsm_event_t event);
const char *litter_fsm_fault_name(int fault_code);

#endif /* APPLICATIONS_APP_LOGIC_FSM_H_ */
