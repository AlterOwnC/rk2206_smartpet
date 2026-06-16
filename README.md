# RK2206 SmartPet — 智能宠物陪伴机器人

基于瑞芯微 RK2206 (OpenHarmony 3.0 LTS / LiteOS-M) 的多线程智能宠物喂食与互动陪伴系统。

---

## 硬件平台

| 项目 | 参数 |
| :--- | :--- |
| 主控 | 瑞芯微 RK2206, Cortex-M4 200MHz |
| 内存 | RAM 256KB, PSRAM 8MB, FLASH 8MB |
| 屏幕 | ST7789V 2.4寸 SPI LCD (320×240) |
| 舵机 | 双轴 SG90 (激光逗猫 X/Y 轴) |
| 时钟 | DS3231 RTC (I2C1) |
| 语音 | UART2 语音识别模块 |
| 网络 | WiFi (UDP + MQTT) |
| 操作系统 | OpenHarmony 3.0 LTS (LiteOS-M) |
| 编译器 | gcc-arm-none-eabi-10.3 |

---

## 系统架构

```
┌─────────────────────────────────────────────────────────────┐
│                    app_example_init()                        │
│            screen_bot.c  — 系统启动入口                       │
├─────────────────────────────────────────────────────────────┤
│  ┌──────────┐  ┌──────────┐  ┌──────────┐                  │
│  │ adc_key  │  │ rtc_task │  │ lcd_ui   │  ← 核心调度线程   │
│  │ 优先级23  │  │ 优先级25  │  │ 优先级24  │                  │
│  │ 2KB 栈   │  │ 2KB 栈   │  │ 20KB 栈  │                  │
│  └──────────┘  └──────────┘  └──────────┘                  │
│                                  │                          │
│     ┌────────────────────────────┼─────────────────────┐    │
│     │          命令队列 (cmd_queue, 长度16)              │    │
│     └────────────────────────────┼─────────────────────┘    │
│                                  │                          │
│  ┌──────────┐  ┌────────────┐  ┌──────────┐               │
│  │udp_server│  │voice_uart  │  │mqtt_cloud│               │
│  │ 优先级6  │  │  优先级24   │  │ 优先级7   │               │
│  │ 10KB 栈  │  │  8KB 栈    │  │ 12KB 栈  │               │
│  └──────────┘  └────────────┘  └──────────┘               │
└─────────────────────────────────────────────────────────────┘
```

---

## 源码目录

> 所有源码位于 [`vendor/lockzhiner/rk2206/samples/v0_screen_bot/`](vendor/lockzhiner/rk2206/samples/v0_screen_bot/)

```
v0_screen_bot/
├── screen_bot.c              # 主控制模块 (RTOS 线程创建 + 主循环调度)
├── BUILD.gn                  # GN 编译配置
├── include/                  # 头文件
│   ├── adc_key.h             # ADC 按键枚举与接口
│   ├── command_queue.h       # 命令队列类型定义与同步原语
│   ├── ds3231.h              # DS3231 RTC 数据结构与 API
│   ├── eyes_emotion.h        # 表情动画引擎接口
│   ├── feeder_motor.h        # 喂食电机控制接口
│   ├── hardware_control.h    # 激光/水泵/风扇硬件控制接口
│   ├── lcd.h                 # ST7789 LCD 绘图 API (331行)
│   ├── lcd_font.h            # 中文字库
│   ├── menu_ui.h             # 菜单页面状态机 + 全局变量声明
│   ├── mqtt_cloud.h          # 华为 IoTDA MQTT 云连接接口
│   ├── oc_mqtt.h             # OC MQTT 底层封装
│   ├── oc_mqtt_profile_package.h  # MQTT 属性上报封装
│   ├── picture.h             # 图片数据声明
│   ├── servo_drive.h         # 双轴舵机 PWM 驱动接口
│   ├── udp_server.h          # WiFi UDP 传感器服务器接口
│   └── voice_module.h        # 语音模块 UART 指令解析接口
└── src/                      # 源文件
    ├── adc_key.c             # ADC 按键扫描实现
    ├── command_queue.c       # 命令队列 + 互斥锁 + 事件组初始化
    ├── ds3231.c              # DS3231 RTC 驱动实现
    ├── eyes_emotion.c        # 表情动画引擎 + UI 绘制组件 (359行)
    ├── feeder_motor.c        # 喂食电机 GPIO 控制
    ├── hardware_control.c    # 激光/水泵/风扇 GPIO 控制
    ├── lcd.c                 # ST7789 LCD 驱动实现
    ├── menu_ui.c             # 多级菜单 UI 系统 (554行)
    ├── mqtt_cloud.c          # 华为 IoTDA MQTT 云连接实现 (232行)
    ├── oc_mqtt.c             # OC MQTT 底层实现
    ├── oc_mqtt_profile_package.c  # MQTT 属性上报实现
    ├── picture.c             # 图片像素数据
    ├── servo_drive.c         # 双轴舵机 PWM 驱动实现
    ├── udp_server.c          # WiFi UDP 传感器接收实现 (155行)
    └── voice_module.c        # 语音 UART 指令解析实现 (204行)
```

---

## 核心模块详解

### 1. 主控制模块 — [`screen_bot.c`](vendor/lockzhiner/rk2206/samples/v0_screen_bot/screen_bot.c)

系统入口，创建 6 个 RTOS 线程并运行主循环调度。

| 函数 | 行号 | 说明 |
| :--- | :--- | :--- |
| [`adc_process()`](vendor/lockzhiner/rk2206/samples/v0_screen_bot/screen_bot.c#L50-L67) | 50-67 | ADC 按键扫描线程，20ms 周期，带消抖 |
| [`rtc_process()`](vendor/lockzhiner/rk2206/samples/v0_screen_bot/screen_bot.c#L78-L116) | 78-116 | RTC 时钟读写线程，监听时间同步事件 |
| [`lcd_process()`](vendor/lockzhiner/rk2206/samples/v0_screen_bot/screen_bot.c#L130-L293) | 130-293 | **LCD 主循环** — 核心调度线程，消费命令队列、自动环境调控、按键分发、页面状态机 |
| [`app_example_init()`](vendor/lockzhiner/rk2206/samples/v0_screen_bot/screen_bot.c#L313-L365) | 313-365 | 系统启动入口 — 创建 6 个 RTOS 线程 |

**主循环调度流程** ([`lcd_process`](vendor/lockzhiner/rk2206/samples/v0_screen_bot/screen_bot.c#L145-L293))：

```
while(1):
  1. 消费命令队列 (CMD_FEED / CMD_CAT_MODE / CMD_FAN / CMD_PUMP / CMD_SENSOR_UPDATE)
  2. 自动环境调控 (温度→风扇, 水位→水泵, 滞后控制算法)
  3. 按键处理 → menu_process_key()
  4. 定时喂食检测 → check_scheduled_feeding()
  5. 页面切换激光联动 (进出逗猫模式)
  6. UI 时间刷新 → ui_update_time_only()
  7. 页面状态机 → 动画引擎 / 菜单渲染
```

**自动环境调控** ([滞后控制算法](vendor/lockzhiner/rk2206/samples/v0_screen_bot/screen_bot.c#L206-L234))：

| 规则 | 阈值 | 说明 |
| :--- | :--- | :--- |
| 温度 ≥ 30°C → 开风扇 | [`TEMP_HIGH_START 30`](vendor/lockzhiner/rk2206/samples/v0_screen_bot/screen_bot.c#L35) | 温度降至 26°C 以下关闭 (4°C 滞后防频繁启停) |
| 水位 ≤ 20% → 开水泵 | [`WATER_LOW_START 20`](vendor/lockzhiner/rk2206/samples/v0_screen_bot/screen_bot.c#L39) | 水位升至 80% 以上关闭 (60% 滞后防频繁启停) |
| 手动覆盖自动清零 | [温度恢复安全](vendor/lockzhiner/rk2206/samples/v0_screen_bot/screen_bot.c#L211-L213) / [水位恢复安全](vendor/lockzhiner/rk2206/samples/v0_screen_bot/screen_bot.c#L225-L227) | 传感器回到安全范围时自动恢复自动模式 |

---

### 2. 命令队列 — [`command_queue.c`](vendor/lockzhiner/rk2206/samples/v0_screen_bot/src/command_queue.c) / [`.h`](vendor/lockzhiner/rk2206/samples/v0_screen_bot/include/command_queue.h)

线程间通信基础设施，生产者-消费者模式。

| 组件 | 定义 | 说明 |
| :--- | :--- | :--- |
| `SystemCommand` | [command_queue.h#L20-L26](vendor/lockzhiner/rk2206/samples/v0_screen_bot/include/command_queue.h#L20-L26) | 命令结构体 (type + 4个参数) |
| `CommandType` 枚举 | [command_queue.h#L8-L18](vendor/lockzhiner/rk2206/samples/v0_screen_bot/include/command_queue.h#L8-L18) | 7 种命令类型 |
| [`system_sync_init()`](vendor/lockzhiner/rk2206/samples/v0_screen_bot/src/command_queue.c#L18-L47) | 18-47 | 创建互斥锁、队列、事件组 |
| [`cmd_send()`](vendor/lockzhiner/rk2206/samples/v0_screen_bot/src/command_queue.c#L56-L60) | 56-60 | 向队列发送命令 (阻塞) |
| [`cmd_recv()`](vendor/lockzhiner/rk2206/samples/v0_screen_bot/src/command_queue.c#L70-L75) | 70-75 | 从队列取命令 (可超时) |

**生产者**：UDP Server / Voice UART / MQTT Cloud → `cmd_send()` → 命令队列  
**消费者**：[`lcd_process()`](vendor/lockzhiner/rk2206/samples/v0_screen_bot/screen_bot.c#L149-L204) → `cmd_recv()` → 执行硬件动作

---

### 3. 表情动画引擎 — [`eyes_emotion.c`](vendor/lockzhiner/rk2206/samples/v0_screen_bot/src/eyes_emotion.c) / [`.h`](vendor/lockzhiner/rk2206/samples/v0_screen_bot/include/eyes_emotion.h)

三大动画系统 + UI 绘制组件。

| 函数 | 行号 | 说明 |
| :--- | :--- | :--- |
| [`delay_with_break()`](vendor/lockzhiner/rk2206/samples/v0_screen_bot/src/eyes_emotion.c#L105-L143) | 105-143 | **可打断延时函数** — 动画引擎核心调度器，5ms 粒度检测按键/页面切换/倒计时 |
| [`eyes_play_normal_step()`](vendor/lockzhiner/rk2206/samples/v0_screen_bot/src/eyes_emotion.c#L156-L219) | 156-219 | 灵动待机动画 (45%看右+眨眼 / 45%看左+眨眼 / 10%彩蛋连招) |
| [`eyes_play_cat_step()`](vendor/lockzhiner/rk2206/samples/v0_screen_bot/src/eyes_emotion.c#L238-L321) | 238-321 | 逗猫模式动画 (20%装死 / 40%试探抖动 / 40%突进跳跃 + 眼神联动) |
| [`eyes_play_feeding_step()`](vendor/lockzhiner/rk2206/samples/v0_screen_bot/src/eyes_emotion.c#L332-L359) | 332-359 | 喂食 8 帧专属动画 |
| [`ui_draw_status_bar()`](vendor/lockzhiner/rk2206/samples/v0_screen_bot/src/eyes_emotion.c#L78-L85) | 78-85 | 状态栏绘制 (时间 + WiFi 状态) |
| [`ui_draw_footer()`](vendor/lockzhiner/rk2206/samples/v0_screen_bot/src/eyes_emotion.c#L87-L90) | 87-90 | 底栏提示文字 |
| [`ui_update_time_only()`](vendor/lockzhiner/rk2206/samples/v0_screen_bot/src/eyes_emotion.c#L66-L76) | 66-76 | 状态栏时间局部刷新 |

---

### 4. 多级菜单系统 — [`menu_ui.c`](vendor/lockzhiner/rk2206/samples/v0_screen_bot/src/menu_ui.c) / [`.h`](vendor/lockzhiner/rk2206/samples/v0_screen_bot/include/menu_ui.h)

按键驱动的 9 页面状态机。

| 页面 | 枚举值 | 说明 |
| :--- | :--- | :--- |
| `PAGE_IDLE` | [menu_ui.h#L8](vendor/lockzhiner/rk2206/samples/v0_screen_bot/include/menu_ui.h#L8) | 待机动画页 (灵动电子眼) |
| `PAGE_MAIN_MENU` | [menu_ui.h#L9](vendor/lockzhiner/rk2206/samples/v0_screen_bot/include/menu_ui.h#L9) | 主菜单 (6 项) |
| `PAGE_SUB_FEED_AMT` | [menu_ui.h#L10](vendor/lockzhiner/rk2206/samples/v0_screen_bot/include/menu_ui.h#L10) | 喂食份量选择 (1~3 份) |
| `PAGE_SUB_SCHEDULE` | [menu_ui.h#L11](vendor/lockzhiner/rk2206/samples/v0_screen_bot/include/menu_ui.h#L11) | 定时计划列表 (5 组) |
| `PAGE_SUB_SCHEDULE_EDIT` | [menu_ui.h#L12](vendor/lockzhiner/rk2206/samples/v0_screen_bot/include/menu_ui.h#L12) | 定时计划编辑子页 |
| `PAGE_SUB_DEVICES` | [menu_ui.h#L14](vendor/lockzhiner/rk2206/samples/v0_screen_bot/include/menu_ui.h#L14) | 设备控制 (水泵/风扇/模式切换) |
| `PAGE_CAT_MODE` | [menu_ui.h#L15](vendor/lockzhiner/rk2206/samples/v0_screen_bot/include/menu_ui.h#L15) | 逗猫互动模式 |
| `PAGE_SUB_ENV` | [menu_ui.h#L16](vendor/lockzhiner/rk2206/samples/v0_screen_bot/include/menu_ui.h#L16) | 传感器数据页 (温度/湿度/重量/水位) |
| `PAGE_SUB_SETTINGS` | [menu_ui.h#L17](vendor/lockzhiner/rk2206/samples/v0_screen_bot/include/menu_ui.h#L17) | 系统时间设置 (年/月/日/时/分 + 保存同步) |

**核心函数**：

| 函数 | 行号 | 说明 |
| :--- | :--- | :--- |
| [`menu_process_key()`](vendor/lockzhiner/rk2206/samples/v0_screen_bot/src/menu_ui.c#L205-L405) | 205-405 | **按键状态机** — 根据当前页面处理 UP/DOWN/ENTER/BACK |
| [`menu_draw_current_page()`](vendor/lockzhiner/rk2206/samples/v0_screen_bot/src/menu_ui.c#L422-L554) | 422-554 | LCD 页面渲染 |
| [`ui_start_feeding()`](vendor/lockzhiner/rk2206/samples/v0_screen_bot/src/menu_ui.c#L115-L120) | 115-120 | 启动喂食：切换到喂食页 + 设置倒计时 + 开启电机 |
| [`ui_jump_to_cat_mode()`](vendor/lockzhiner/rk2206/samples/v0_screen_bot/src/menu_ui.c#L103-L107) | 103-107 | 进入逗猫模式，标记来源 (本地/远程) |
| [`check_scheduled_feeding()`](vendor/lockzhiner/rk2206/samples/v0_screen_bot/src/menu_ui.c#L167-L186) | 167-186 | 定时喂食检测 — 每分钟匹配 5 组计划 |
| [`api_set_schedule()`](vendor/lockzhiner/rk2206/samples/v0_screen_bot/src/menu_ui.c#L147-L156) | 147-156 | 外部 API — 设置定时计划 (供 UDP/MQTT 调用) |

**逗猫远程安全机制**：本地按键进入的逗猫模式 (`g_remote_cat_mode=0`) 免疫云端关闭指令，防止远程误关。（[menu_ui.c#L103-L107](vendor/lockzhiner/rk2206/samples/v0_screen_bot/src/menu_ui.c#L103-L107) / [screen_bot.c#L164-L167](vendor/lockzhiner/rk2206/samples/v0_screen_bot/screen_bot.c#L164-L167)）

---

### 5. 双轴舵机驱动 — [`servo_drive.c`](vendor/lockzhiner/rk2206/samples/v0_screen_bot/src/servo_drive.c) / [`.h`](vendor/lockzhiner/rk2206/samples/v0_screen_bot/include/servo_drive.h)

PWM 控制 SG90 舵机，驱动激光逗猫红点。

| 函数 | 行号 | 说明 |
| :--- | :--- | :--- |
| [`servo_init()`](vendor/lockzhiner/rk2206/samples/v0_screen_bot/src/servo_drive.c#L26-L57) | 26-57 | 初始化 X/Y 双轴 PWM，回中 90°，失败锁死 |
| [`servo_set_angle()`](vendor/lockzhiner/rk2206/samples/v0_screen_bot/src/servo_drive.c#L59-L68) | 59-68 | 设置角度 (0°~180°)，未就绪时拒绝操作 |

| 轴 | GPIO | PWM 端口 | 说明 |
| :--- | :--- | :--- | :--- |
| X 轴 | PB4 → PWM0_M1 | [`SERVO_X_PWM_PORT 0`](vendor/lockzhiner/rk2206/samples/v0_screen_bot/include/servo_drive.h#L15) | 激光水平方向 |
| Y 轴 | PB5 → PWM1_M1 | [`SERVO_Y_PWM_PORT 1`](vendor/lockzhiner/rk2206/samples/v0_screen_bot/include/servo_drive.h#L16) | 激光垂直方向 |

---

### 6. WiFi UDP 服务器 — [`udp_server.c`](vendor/lockzhiner/rk2206/samples/v0_screen_bot/src/udp_server.c) / [`.h`](vendor/lockzhiner/rk2206/samples/v0_screen_bot/include/udp_server.h)

接收手机端传感器 JSON 数据。

| 函数 | 行号 | 说明 |
| :--- | :--- | :--- |
| [`udp_server_thread()`](vendor/lockzhiner/rk2206/samples/v0_screen_bot/src/udp_server.c#L30-L155) | 30-155 | WiFi 连接 → 创建 Socket (端口 6666) → 循环接收 JSON → 解析入队 |

**数据流**：手机 App → WiFi UDP (JSON) → [`CMD_SENSOR_UPDATE`](vendor/lockzhiner/rk2206/samples/v0_screen_bot/src/udp_server.c#L113-L131) → 命令队列 → LCD 主循环

---

### 7. 语音模块 — [`voice_module.c`](vendor/lockzhiner/rk2206/samples/v0_screen_bot/src/voice_module.c) / [`.h`](vendor/lockzhiner/rk2206/samples/v0_screen_bot/include/voice_module.h)

UART2 二进制协议指令解析。

| 函数 | 行号 | 说明 |
| :--- | :--- | :--- |
| [`voice_uart_thread()`](vendor/lockzhiner/rk2206/samples/v0_screen_bot/src/voice_module.c#L16-L204) | 16-204 | 初始化 UART2 → 循环接收 → 累积缓冲区 → 指令解析 |

**支持的 9 种语音指令**：

| 指令 | 十六进制 | 功能 | 代码位置 |
| :--- | :--- | :--- | :--- |
| 查询温度 | `06 01` | UART 返回当前温度 | [voice_module.c#L98-L106](vendor/lockzhiner/rk2206/samples/v0_screen_bot/src/voice_module.c#L98-L106) |
| 查询湿度 | `06 02` | UART 返回当前湿度 | [voice_module.c#L109-L117](vendor/lockzhiner/rk2206/samples/v0_screen_bot/src/voice_module.c#L109-L117) |
| 查询时间 | `07 01` | UART 返回当前时间 | [voice_module.c#L120-L129](vendor/lockzhiner/rk2206/samples/v0_screen_bot/src/voice_module.c#L120-L129) |
| 查询日期 | `07 02` | UART 返回当前日期 | [voice_module.c#L132-L142](vendor/lockzhiner/rk2206/samples/v0_screen_bot/src/voice_module.c#L132-L142) |
| 喂食 | `02 01` | 推入 CMD_FEED | [voice_module.c#L147-L152](vendor/lockzhiner/rk2206/samples/v0_screen_bot/src/voice_module.c#L147-L152) |
| 开启逗猫 | `03 01` | 推入 CMD_CAT_MODE_ENTER | [voice_module.c#L155-L160](vendor/lockzhiner/rk2206/samples/v0_screen_bot/src/voice_module.c#L155-L160) |
| 退出逗猫 | `03 02` | 推入 CMD_CAT_MODE_EXIT | [voice_module.c#L163-L168](vendor/lockzhiner/rk2206/samples/v0_screen_bot/src/voice_module.c#L163-L168) |
| 开启风扇 | `01 01` | 推入 CMD_FAN_ON | [voice_module.c#L171-L176](vendor/lockzhiner/rk2206/samples/v0_screen_bot/src/voice_module.c#L171-L176) |
| 关闭风扇 | `01 02` | 推入 CMD_FAN_OFF | [voice_module.c#L179-L184](vendor/lockzhiner/rk2206/samples/v0_screen_bot/src/voice_module.c#L179-L184) |

---

### 8. 华为云 IoTDA — [`mqtt_cloud.c`](vendor/lockzhiner/rk2206/samples/v0_screen_bot/src/mqtt_cloud.c) / [`.h`](vendor/lockzhiner/rk2206/samples/v0_screen_bot/include/mqtt_cloud.h)

MQTT 连接华为云 IoT 平台。

| 函数 | 行号 | 说明 |
| :--- | :--- | :--- |
| [`cmd_rsp_cb()`](vendor/lockzhiner/rk2206/samples/v0_screen_bot/src/mqtt_cloud.c#L57-L140) | 57-140 | 云端命令回调 — 解析 motor/laser/fan 指令 |
| [`mqtt_cloud_thread()`](vendor/lockzhiner/rk2206/samples/v0_screen_bot/src/mqtt_cloud.c#L152-L231) | 152-231 | MQTT 主线程 — 连接/重连/属性上报 (6s 周期) |

**上报属性**：温度 / 湿度 / 食盆重量 / 水位百分比  
**云端指令**：`motor`(喂食) / `laser`(逗猫开关) / `fan`(风扇开关)

---

### 9. 硬件控制 — [`hardware_control.c`](vendor/lockzhiner/rk2206/samples/v0_screen_bot/src/hardware_control.c) / [`.h`](vendor/lockzhiner/rk2206/samples/v0_screen_bot/include/hardware_control.h)

| 设备 | GPIO | 触发逻辑 | 控制函数 |
| :--- | :--- | :--- | :--- |
| 激光 | PC4 | 低电平 = 亮 | [`laser_control()`](vendor/lockzhiner/rk2206/samples/v0_screen_bot/include/hardware_control.h#L35) |
| 水泵 | PB0 | 高电平 = 开 (电机驱动模块) | [`pump_control()`](vendor/lockzhiner/rk2206/samples/v0_screen_bot/include/hardware_control.h#L43) |
| 风扇 | PB1 | 低电平 = 开 (继电器) | [`fan_control()`](vendor/lockzhiner/rk2206/samples/v0_screen_bot/include/hardware_control.h#L51) |

**控制模式** ([F-3 AUTO/MANUAL](vendor/lockzhiner/rk2206/samples/v0_screen_bot/include/menu_ui.h#L38-L41))：AUTO 模式由传感器自动调控，MANUAL 模式由用户手动控制。

---

## 线程总览

| 线程名 | 优先级 | 栈大小 | 入口函数 | 职责 |
| :--- | :--- | :--- | :--- | :--- |
| `lcd_ui` | 24 | 20KB | [`lcd_process()`](vendor/lockzhiner/rk2206/samples/v0_screen_bot/screen_bot.c#L130) | LCD 主循环 (核心调度) |
| `adc_key` | 23 | 2KB | [`adc_process()`](vendor/lockzhiner/rk2206/samples/v0_screen_bot/screen_bot.c#L50) | ADC 按键扫描 (20ms) |
| `rtc_task` | 25 | 2KB | [`rtc_process()`](vendor/lockzhiner/rk2206/samples/v0_screen_bot/screen_bot.c#L78) | DS3231 RTC 时钟读写 |
| `udp_server_task` | 6 | 10KB | [`udp_server_thread()`](vendor/lockzhiner/rk2206/samples/v0_screen_bot/src/udp_server.c#L30) | WiFi UDP 传感器接收 |
| `voice_uart_task` | 24 | 8KB | [`voice_uart_thread()`](vendor/lockzhiner/rk2206/samples/v0_screen_bot/src/voice_module.c#L16) | 语音模块 UART 指令解析 |
| `mqtt_cloud` | 7 | 12KB | [`mqtt_cloud_thread()`](vendor/lockzhiner/rk2206/samples/v0_screen_bot/src/mqtt_cloud.c#L152) | 华为 IoTDA MQTT 云连接 |

---

## 快速开始

### 环境要求

- Python 3.11
- Git
- 交叉编译工具链（仓库已包含 `gcc-arm-none-eabi-10.3-2021.10-x86_64-linux.tar.bz2`）

### 克隆仓库

```bash
git clone https://github.com/AlterOwnC/rk2206_smartpet.git
cd rk2206_smartpet
```

### 编译

```bash
source venv/bin/activate
hb set -root .
hb build -f
```

### 烧录

```bash
python flash.py
```

### 配置

1. 在 [`vendor/lockzhiner/rk2206/samples/BUILD.gn`](vendor/lockzhiner/rk2206/samples/BUILD.gn) 中启用 `v0_screen_bot` 编译目标
2. 修改 [`udp_server.c`](vendor/lockzhiner/rk2206/samples/v0_screen_bot/src/udp_server.c#L41-L42) 中的 WiFi SSID 和密码
3. 修改 [`mqtt_cloud.c`](vendor/lockzhiner/rk2206/samples/v0_screen_bot/src/mqtt_cloud.c#L38-L40) 中的华为云 IoTDA 设备三元组

---

## 关键设计亮点

- **滞后控制算法** — 温度 4°C / 水位 60% 滞回区间，防止继电器频繁振荡
- **可打断动画引擎** — `delay_with_break()` 以 5ms 粒度轮询按键和页面切换，确保 UI 响应不卡顿
- **逗猫远程安全锁** — 本地按键进入的逗猫模式免疫云端关闭，防止宠物互动意外中断
- **生产者-消费者解耦** — UDP/语音/MQTT 三个外部输入源通过命令队列与 LCD 主循环解耦
- **手动覆盖自动清零** — 传感器回到安全范围时自动恢复自动模式
- **累积缓冲区** — 语音 UART 使用累积缓冲区解决非阻塞模式下粘包/半包问题
- **线程安全** — 所有共享数据通过 LiteOS 互斥锁保护（传感器、RTC 时间）
