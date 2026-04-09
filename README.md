# 基于 RT-Thread 的智能猫砂盆

## 项目简介

本项目基于 RT-Thread 操作系统与 HC32F460 平台，实现了智能猫砂盆的本地感知、自动清理、状态显示和云端联网控制。

核心能力如下：

- 温湿度采集（DHT11）
- 体重读取（称重模块）
- 红外检测触发清理
- 电机控制清理动作
- 蜂鸣器与臭氧联动告警
- OLED 本地显示（体重、温湿度、使用次数）
- MQTT 云端上报与远程触发

## 目录结构

```text
sourcecode/
├── app_logic/          # 业务逻辑（线程、传感、控制、MQTT）
├── applications/       # main 入口与显示刷新
├── board/              # 板级配置
├── libraries/          # 芯片驱动与 BSP 驱动
├── packages/           # 第三方软件包（如 dht11、balance）
├── rt-thread/          # RT-Thread 内核与组件
├── project.uvprojx     # Keil 工程
├── project.ewp         # IAR 工程
├── SConstruct          # SCons 构建入口
└── README.md
```

## 业务逻辑架构

系统运行由 3 个核心线程组成：

1. 传感线程（`ReadSensor_Task`）
- 周期读取 DHT11 数据
- 通过邮箱发送温湿度给控制线程
- 通过信号量通知控制线程处理

2. 控制线程（`StartControl_Task`）
- 等待传感线程信号
- 执行业务决策（超重告警、红外/远程触发清理）

3. MQTT 线程（`Mqtt_Task`）
- 建立 MQTT 连接并订阅下行主题
- 周期上报设备属性
- 断线重连并在断连时进入安全状态

模块关系：

- `applications/main.c`：系统入口、GPIO 模式配置、OLED 刷新
- `app_logic/app_init.c`：线程与 IPC 初始化
- `app_logic/logic.c`：清理控制策略
- `app_logic/mqtt.c`：云端上下行协议处理

## 关键业务流程

### 1. 本地数据采集与显示

- 温湿度由 DHT11 采集
- 体重由称重模块更新
- OLED 持续显示：`weight`、`used`、`hum`、`tem`

### 2. 清理触发逻辑

- 触发条件：
  - 红外检测到猫进入
  - 或 MQTT 下行命令触发
- 执行动作：
  - 延时确认
  - 增加使用次数计数
  - 电机按固定时序正反转并停机

### 3. 告警逻辑

- 当重量超过阈值（当前代码为 `>= 500`）时：
  - 蜂鸣器鸣叫
  - 臭氧控制引脚联动

### 4. 云端通信

- 上行主题：`/sys/{pk}/{dn}/thing/event/property/post`
- 下行主题：`/sys/{pk}/{dn}/thing/service/property/set`
- 支持下行控制字段：`CleanNow`、`clean_now`、`flag`

## 开发环境

- MCU：HC32F460（Cortex-M4）
- RTOS：RT-Thread
- 工具链：
  - Keil MDK（`project.uvprojx`）
  - IAR（`project.ewp`）
  - SCons（命令行构建）

## 构建方式

### 方式一：Keil

1. 打开 `project.uvprojx`
2. 编译并下载到开发板

### 方式二：IAR

1. 打开 `project.ewp`
2. 编译并下载到开发板

### 方式三：SCons

在 `sourcecode` 目录执行：

```bash
scons
```

## 运行说明

下载后系统启动，行为包括：

- 指示灯闪烁
- OLED 实时显示数据
- 串口可看到 RT-Thread 与业务日志
- MQTT 联网后可接收远程清理命令

## 注意事项

- `GET_PIN` 宏依赖底层 GPIO 头文件和配置宏，若编辑器提示 `GPIO_PORT_D/E` 未定义但编译通过，通常是 IntelliSense 配置问题。
- 远程触发标志在控制逻辑中按“一次性消费”处理，防止重复执行。
- 若需替换传感器或云平台，请同步更新 `app_logic` 模块中的设备名和 Topic 模板。

## 许可证

本项目包含 RT-Thread 与芯片厂商驱动代码，请遵循对应目录下原始许可证文件。