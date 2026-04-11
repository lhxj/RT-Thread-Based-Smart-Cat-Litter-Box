# 调试与演示

本文聚焦“怎么把当前分支跑起来并看懂它”，不重复讲整体架构。更完整的设计说明见 [`architecture.md`](architecture.md)。

## 1. 构建前准备

### 工具入口

- Keil：[`project.uvprojx`](../project.uvprojx)
- IAR：[`project.eww`](../project.eww) 或 [`project.ewp`](../project.ewp)
- SCons：仓库根目录执行 `scons`

### 本地依赖确认

建议确认以下项目已经就绪：

1. HC32F460 开发板或目标板
2. 下载器与 IDE 下载配置可用
3. 串口终端可连接控制台
4. 电机、红外、霍尔、称重、OLED、DHT11 已按当前代码接好

### 当前代码中的关键硬件假设

- 控制台设备：`uart4`
- 串口默认参数：`115200 8N1`
- 红外占用输入：`PE2`，低电平表示占用
- Hall 输入：`PA4 / PA5 / PA6`，低电平有效
- OLED：`i2c2`
- DHT11：`PE10`

如果你的实际接线不同，优先修改代码中的 GPIO 假设，再做功能验证。

## 2. 上电后应该先看什么

### OLED

OLED 每轮刷新 5 行摘要：

- `W/U`：重量与使用次数
- `ST`：当前状态
- `FC`：当前故障文本
- `LK/B/P`：MQTT 链路、满仓、保护标志
- `H/T`：湿度与温度

OLED 适合做“整体是否活着”的快速确认，但不适合看细节事件。

### 串口日志

串口是最重要的观察窗口。当前主线日志前缀包括：

- `[FSM]`：状态跳转、事件、故障进入、恢复
- `[LOGIC]`：清理请求、reset 请求、Hall 稳定位置变化
- `[ACT]`：电机正转 / 反转 / 停机
- `[MQTT]`：链路在线 / 离线

建议重点关注下面几类日志：

- `ENTER ...` / `EXIT ...`
- `EVT ... @ ...`
- `CLEANING phase=...`
- `SAFE_STOP reason=...`
- `FAULT enter ...`

### finsh

finsh 适合做开发者演示和局部验证。当前可用的项目命令有：

| 命令 | 说明 |
| --- | --- |
| `litter_status` | 打印状态、相位、Hall、故障、满仓、保护、MQTT |
| `litter_clean` | 本地排队一次清理请求 |
| `litter_reset` | 请求本地恢复 |
| `litter_timeout` | 在 `CLEANING` 中注入一次 timeout 故障 |

## 3. 推荐演示脚本

下面几组场景可以覆盖当前分支最关键的行为边界。

### 场景 A：确认系统空闲可观测

目标：确认系统能启动、能打印状态、能看见 Hall 和故障摘要。

步骤：

1. 上电
2. 观察 OLED 是否持续刷新
3. 在 finsh 中执行：

```text
litter_status
```

期望：

- `state=IDLE` 或 `state=OCCUPIED`
- `fault=NONE`
- `hall` 能显示一个稳定位置，理想情况是 `POS1_HOME`

如果此时就出现 `FAULT`，优先检查：

- 当前重量是否已经超过 `500`
- Hall 是否出现 `INVALID`
- 输入脚电平是否与代码假设相反

### 场景 B：闭环清理主线

目标：验证 `POS1 -> POS2 -> POS3 -> POS1` 闭环能走通。

步骤：

1. 确保当前没有占用，且满仓条件未触发
2. 执行：

```text
litter_clean
```

3. 观察串口是否出现类似日志：

```text
[FSM] ENTER CLEANING
[FSM] CLEANING phase=TO_POS2
[FSM] EVT EVT_POS2_REACHED @ CLEANING
[FSM] CLEANING phase=TO_POS3
[FSM] EVT EVT_POS3_REACHED @ CLEANING
[FSM] CLEANING phase=TO_POS1
[FSM] EVT EVT_POS1_REACHED @ CLEANING
[FSM] EVT EVT_CLEAN_DONE @ CLEANING
[FSM] ENTER IDLE
```

4. 再执行 `litter_status`

期望：

- `state=IDLE`
- `phase=NONE`
- `hall=POS1_HOME`

如果清理一直卡在某个相位，优先检查：

- 机械是否真的经过目标位置
- 对应 Hall 是否有稳定翻转
- 接线是否和 `POS1/POS2/POS3` 假设一致

### 场景 C：`SAFE_STOP` 验证

目标：验证清理中的占用保护是否立即停机。

步骤：

1. 执行 `litter_clean`
2. 在系统进入 `CLEANING` 之后，触发占用输入
3. 观察串口

期望：

- 出现 `EVT_PROTECT_TRIGGER`
- 进入 `SAFE_STOP`
- 电机立即停止

之后释放占用输入，观察系统是否从 `SAFE_STOP` 恢复到 `OCCUPIED` 或 `LEAVE_CONFIRM`。

这条路径用于验证“安全停机”链路。

### 场景 D：`CLEAN_TIMEOUT` 故障与恢复

目标：验证 timeout 故障与本地 reset 边界。

步骤：

1. 执行 `litter_clean`
2. 等系统进入 `CLEANING`
3. 执行：

```text
litter_timeout
```

4. 观察串口
5. 执行：

```text
litter_status
```

期望：

- `state=FAULT`
- `fault=CLEAN_TIMEOUT`

接着在确保没有占用保护、也没有满仓后执行：

```text
litter_reset
```

期望：

- 系统回到 `IDLE` 或 `OCCUPIED`
- `fault=NONE`

注意：

- 如果此时仍有保护条件，`RESET` 会被阻塞
- 这符合当前恢复边界设计

### 场景 E：满仓阻塞

目标：验证 `bin_full` 不会被绕过。

做法有两种：

1. 实物上让重量达到阈值
2. 在调试时直接观察 `cur_weight >= 500` 的效果

期望：

- 清理请求不能进入 `CLEANING`
- 系统进入或保持 `FAULT`
- 故障码为 `BIN_FULL`

当重量回落到阈值以下后，`BIN_FULL` 故障可以自动清除。

## 4. 常见排障点

### `litter_clean` 后没有进入 `CLEANING`

优先看：

- 当前是否是 `OCCUPIED`
- 当前是否 `bin_full=1`
- 当前是否处于 `SAFE_STOP` 或 `FAULT`

### Hall 一直是 `TRANSITION` 或 `INVALID`

优先看：

- Hall 接线顺序是否和 `PA4/PA5/PA6` 假设一致
- 是否确实为低电平有效
- 机械是否长时间停在两个位置之间
- 是否存在两个 Hall 同时导通

### `litter_timeout` 无效

该命令只有在 `CLEANING` 中才会注入 fault。若当前不在 `CLEANING`，命令会直接打印忽略信息。

### MQTT 离线

MQTT 离线不会阻止本地控制主线：

- `litter_clean`、`litter_status`、`litter_reset` 仍然可用
- OLED 和串口观察不受影响
- 只是远程清理与状态上报不可用

## 5. 回归时最值得保留的观察点

如果你后续要修改 Hall 映射、相位 timeout 或联锁逻辑，建议每次至少复测以下四条：

1. 上电后 `litter_status` 是否能稳定显示 `state/fault/hall`
2. 正常闭环是否还能按 `POS1 -> POS2 -> POS3 -> POS1` 走完
3. 清理中占用是否仍能进入 `SAFE_STOP`
4. `litter_timeout -> litter_reset` 是否还能验证 `FAULT` 恢复边界
