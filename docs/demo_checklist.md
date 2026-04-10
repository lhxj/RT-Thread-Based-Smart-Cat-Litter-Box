# 演示 / 面试速查清单

## 演示前检查

- 开发板、OLED、电机、红外、称重模块连接正常
- 串口终端已打开，能看到 `[FSM]`、`[LOGIC]`、`[STAT]`、`[MQTT]`
- `finsh` 可用，先执行一次 `litter_status`
- 初始状态建议满足：`state=IDLE`、`fault=NONE`、`bin_full=0`
- 若演示联网场景，再确认 MQTT 可连接
- 若只演示本地自治，可直接断网，验证 `mqtt_link=0`

## 演示命令

- `litter_status`：查看当前状态摘要
- `litter_clean`：本地发起一次清理请求
- `litter_reset`：本地恢复
- `litter_timeout`：仅 bench 演示用，在 `CLEANING` 期间注入现有超时故障路径

## 六个关键场景

### 1. 正常清理闭环

- 操作：`litter_clean`
- 观察：`IDLE -> CLEANING -> IDLE`
- 重点看：OLED 状态变化、`[ACT] motor forward/reverse/stop`、`fault=NONE`

### 2. 占用保护停机

- 操作：进入 `CLEANING` 后遮挡红外
- 观察：立即停机，进入 `SAFE_STOP`
- 重点看：`protect=1`、`fault=PROTECT_TRIGGER`、释放后自动恢复

### 3. 清理超时 / 故障进入

- 操作：进入 `CLEANING` 后执行 `litter_timeout`
- 观察：进入 `FAULT`
- 重点看：`fault=CLEAN_TIMEOUT`、reset 前不自动恢复

### 4. 满仓禁启

- 操作：让重量超过阈值，再执行 `litter_clean`
- 观察：清理被阻止
- 重点看：`bin_full=1`、`fault=BIN_FULL`、串口出现 `block clean: bin full`

### 5. 断网本地可运行

- 操作：断网后执行 `litter_clean`
- 观察：虽然 `mqtt_link=0`，本地状态机仍能跑完整流程
- 重点看：本地自治，不依赖云端

### 6. 故障恢复 / RESET 恢复

- 操作：制造故障后执行 `litter_reset`
- 观察：故障清除，状态回到 `IDLE` 或 `OCCUPIED`
- 重点看：`RESET` 只在安全条件满足时生效

## 面试时最值得强调的技术点

- 不是简单电机 Demo，而是显式状态机驱动的控制流程
- 安全联锁优先于动作执行，先判断“能不能动”，再判断“怎么动”
- 故障被分成可自动恢复和需要本地 reset 的两类
- MQTT 只是附加能力，不是控制系统的单点依赖
- OLED、串口、finsh 三种观察面让状态和故障都能被直接看到

## 面试时最值得讲的设计取舍

- 先做小而稳的 MVP，不追求功能堆叠
- 先把状态、联锁、故障、恢复做清楚，再谈复杂 App 和高级算法
- 保持故障模型收敛，只保留最有价值的几个关键故障
- 把本地自治放在云端能力之前，提升系统韧性和可演示性

## 本项目这次刻意不做什么

- 不做 AI、多猫识别、复杂 App
- 不做新传感器接入和大规模硬件扩展
- 不做更复杂的协议和大架构重构
- 不做超出 MVP 阶段的全平台产品化包装

## 一句话讲法

“这个项目的价值不在花哨功能，而在于把一个嵌入式清理设备整理成了具备显式状态机、安全联锁、故障恢复、关键告警和本地自治能力的控制系统 MVP。”
