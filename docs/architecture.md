# 架构说明

本文是对 README 的补充，重点回答三个问题：

1. 控制主线在代码里是如何分层的
2. 状态机、联锁、故障恢复之间如何配合
3. 三霍尔位置反馈为什么只细化 `CLEANING`，而不改写顶层状态机

## 代码入口

建议先对照以下文件阅读：

- [`applications/main.c`](../applications/main.c)：系统入口与 OLED 摘要显示
- [`app_logic/app_init.c`](../app_logic/app_init.c)：线程、邮箱、信号量、MQTT 线程启动
- [`app_logic/logic.c`](../app_logic/logic.c)：输入同步、Hall 采样去抖、finsh 命令
- [`app_logic/fsm.c`](../app_logic/fsm.c)：状态机、联锁、故障、恢复
- [`app_logic/mqtt.c`](../app_logic/mqtt.c)：最小 MQTT 上下行

## 运行时结构

### 线程分工

| 线程 | 文件 | 角色 |
| --- | --- | --- |
| `sensor_th` | `app_init.c` | 读取 DHT11，向邮箱投递温湿度 |
| `con_th` | `app_init.c` | 定期执行 `Sensor_Logic_UpdateInputs()` 和 `Sensor_Logic_Running()` |
| `mqtt_th` | `app_init.c` | 建链、订阅、上报和重连 |

状态机不是独立线程，而是被控制线程周期调用。这样做的好处是：

- 所有输入同步和状态推进都集中在一个控制上下文里
- 传感数据采样和控制决策解耦
- MQTT 不直接控制执行器，只是通过“请求清理”这一入口与本地主线交互

### 控制主线

```text
Sensor_Logic_UpdateInputs()
        |
        v
Sensor_Logic_Running()
        |
        +--> 同步 occupied / bin_full / protect
        +--> 处理远程或本地 clean/reset 请求
        +--> 轮询 Hall 并按需要派发位置事件
        +--> 调用 litter_fsm_tick() 处理超时和恢复
```

可以把 [`app_logic/logic.c`](../app_logic/logic.c) 看成“输入编排层”，把 [`app_logic/fsm.c`](../app_logic/fsm.c) 看成“状态与行为层”。

## 状态机主线

### 为什么用显式状态机

这个项目的核心难点不在于“把电机转起来”，而在于“在占用、延时、联锁、恢复和位置闭环同时存在时，系统仍然可解释”。显式状态机带来的收益是：

- 日志能直接回答“现在在哪”
- 每个状态的允许事件边界清晰
- 故障与恢复可以从 if/else 逻辑中剥离出来

### 状态角色

| 状态 | 关注点 |
| --- | --- |
| `IDLE` | 待机，允许发起清理 |
| `OCCUPIED` | 占用成立，禁止清理 |
| `LEAVE_CONFIRM` | 离开确认窗口 |
| `CLEAN_DELAY` | 正式清理前的等待窗口 |
| `CLEANING` | 执行机械轨迹 |
| `SAFE_STOP` | 保护触发后的停机等待 |
| `FAULT` | 故障锁定状态 |

### 正常清理路径

```text
IDLE
  -> OCCUPIED
  -> LEAVE_CONFIRM
  -> CLEAN_DELAY
  -> CLEANING
  -> IDLE
```

这里要注意两点：

- `OCCUPIED -> LEAVE_CONFIRM -> CLEAN_DELAY` 是“先确认离开，再延迟清理”的设计，不会在离开瞬间立刻动作
- 真正的机械闭环只发生在 `CLEANING` 内部

## 联锁与恢复边界

### 进入清理前

`fsm_try_enter_cleaning()` 会先检查：

- `occupied`
- `bin_full`
- `protect_active`

只要其中任何一项不满足，清理请求就不会真正进入 `CLEANING`。

### 清理中

`CLEANING` 状态下有两类高优先级打断：

1. 保护触发
   - 进入 `SAFE_STOP`
   - 立即停机
   - 等保护释放后再决定恢复到 `OCCUPIED` 还是 `LEAVE_CONFIRM`

2. 满仓 / 超时
   - 进入 `FAULT`
   - 不再自动恢复到清理流程

### 恢复边界

恢复逻辑采用以下边界：

- `SAFE_STOP`：保护释放后自动恢复，但仍要重新受占用/满仓约束
- `BIN_FULL`：重量回落后可自动清故障
- `CLEAN_TIMEOUT`：需要本地 `RESET`，且 `protect_active` 与 `bin_full` 都必须已清除
- 远程 MQTT 不参与故障恢复

这使得“安全停机”和“故障恢复”在语义上保持分层，而不是混成一个大状态。

## Hall 位置闭环设计

### 软件里的 Hall 抽象

[`app_logic/logic.c`](../app_logic/logic.c) 里定义了：

- `HALL_POSITION_POS1_HOME`
- `HALL_POSITION_POS2_DUMP`
- `HALL_POSITION_POS3_SAND_RETURN`
- `HALL_POSITION_TRANSITION`
- `HALL_POSITION_INVALID`

它们不是顶层业务状态，而是机械位置状态。

### one-hot + 去抖

当前编码假设是：

- 单个 Hall 低电平有效时，表示一个稳定位置
- 三个都无效时，表示机构处于过渡区
- 多个同时有效时，视为 `INVALID`

再叠加：

- `10 ms` 采样周期
- `20 ms` 去抖

目的是把机械边沿抖动和软件事件消费解耦。

### 为什么只细化 `CLEANING`

Hall 的职责是回答“机构走到哪里了”，但它不回答：

- 现在是不是可以清理
- 当前是否有人 / 猫占用
- 满仓是否允许动作
- 保护是否应该立即停机

所以当前设计把 Hall 定位为：

- 顶层状态机的一个输入来源
- 只影响 `CLEANING` 内部相位推进

而不是让系统顶层状态变成“POS1/2/3 导向”的状态机。这种分层对后续维护更友好。

### Hall 事件派发策略

`logic_dispatch_hall_event_if_needed()` 有两个关键点：

1. 只有当顶层状态是 `CLEANING` 时，Hall 才会推进状态机
2. 只有当稳定位置与当前清理相位目标位置一致时，才派发 `EVT_POSx_REACHED`

这避免了：

- 非清理阶段误消费 Hall
- 同一个稳定位置被重复消费
- 刚进入相位时遗漏“已经到位”的情况

## timeout 为什么不能删

虽然已经有 Hall 到位反馈，但 timeout 仍然是必须保留的工程兜底：

- 相位 timeout：防止某一段机械动作卡死
- 总 timeout：防止整轮清理长期不退出

当前默认预算是：

- `TO_POS2`：6 s
- `TO_POS3`：5 s
- `TO_POS1`：6 s
- 总清理预算：20 s

它们的意义是“兜底保护”，不是主闭环。

## MQTT 在架构中的位置

MQTT 不属于控制闭环核心，只是外围接口：

- 上行：定期上报温湿度、重量、运行次数、状态、故障、链路、满仓、保护
- 下行：接收远程清理请求

特别注意：

- MQTT 断线只影响 `mqtt_link` 可见性和远程能力
- 本地控制线程和状态机不依赖 MQTT 在线
- 远程 reset 被显式忽略，恢复边界保留在本地

## 可观测性入口

本项目提供以下本地可观测性入口：

| 入口 | 适合看什么 |
| --- | --- |
| OLED | 状态摘要、故障摘要、链路 / 满仓 / 保护布尔量 |
| 串口日志 | 状态跳转、动作方向、故障进入与恢复 |
| finsh | 主动查询状态，触发清理，本地恢复，注入 timeout 验证 |

如果你要继续开发，最推荐的方式是：

1. 先用 `litter_status` 和串口日志把状态机走通
2. 再修改 Hall 映射或机械动作参数
3. 最后才考虑扩展新的联网或 UI 能力
