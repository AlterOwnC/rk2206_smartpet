# v0_screen_bot 深度代码审计报告

> 审计日期: 2026-06-04  
> 审计范围: 完整源代码 (15个头文件 + 16个源文件, ~3200行C代码)  
> 硬件平台: RK2206 Cortex-M4, LiteOS-M RTOS, 256KB RAM, 8MB PSRAM  
> 注意: 项目已有一份 [ANALYSIS_REPORT.md](ANALYSIS_REPORT.md) (2026-05-27), 其中许多问题已被部分修复。本报告聚焦于**新发现的隐患**及**尚未修复的遗留问题**。

---

## 严重程度定义

| 标记 | 含义 |
|:---|:---|
| 🔴 CRITICAL | 数据竞争/死锁/内存损坏 — 必须立即修复 |
| 🟠 HIGH | 逻辑错误/功能失效/安全漏洞 — 应尽快修复 |
| 🟡 MEDIUM | 健壮性不足/边界条件/资源泄漏 — 建议修复 |
| 🟢 LOW | 代码质量/可维护性/风格 — 可选改进 |
| ✅ FIXED | 已在当前代码中修复 (相对于旧报告) |

---

## 一、并发与线程安全问题

### 🔴 CRITICAL #1: 传感器变量写入无互斥锁保护 (数据竞争)

**文件:** [screen_bot.c:171-175](screen_bot.c#L171-L175)

```c
// lcd_process 线程 — 写入传感器变量 (无锁!)
case CMD_SENSOR_UPDATE:
    g_sensor_temp   = cmd.param1;
    g_sensor_hum    = cmd.param2;
    g_sensor_weight = cmd.param3;
    g_sensor_water  = cmd.param4;
    break;
```

而 `voice_module.c` 和 `mqtt_cloud.c` 的读取端**确实使用了互斥锁**:

```c
// voice_module.c:94-96 — 获取互斥锁读取
LOS_MuxPend(g_sensor_mutex, LOS_WAIT_FOREVER);
send_buf[3] = (unsigned char)g_sensor_temp;
LOS_MuxPost(g_sensor_mutex);

// mqtt_cloud.c:174-179 — 获取互斥锁读取
LOS_MuxPend(g_sensor_mutex, LOS_WAIT_FOREVER);
int temp = g_sensor_temp;
...
LOS_MuxPost(g_sensor_mutex);
```

**问题:** `g_sensor_mutex` 互斥锁只保护了读端，写端 (lcd_process) 直接裸写。这导致:
- 读取端可能读到半写入的值（虽然 `int` 在 Cortex-M4 上原子对齐，但没有内存屏障保证可见性）
- 互斥锁形同虚设，无法提供任何保护

**修复建议:**
```c
case CMD_SENSOR_UPDATE:
    LOS_MuxPend(g_sensor_mutex, LOS_WAIT_FOREVER);
    g_sensor_temp   = cmd.param1;
    g_sensor_hum    = cmd.param2;
    g_sensor_weight = cmd.param3;
    g_sensor_water  = cmd.param4;
    LOS_MuxPost(g_sensor_mutex);
    break;
```

---

### 🔴 CRITICAL #2: RTC 时间在 lcd_process 路径中无锁读取

**文件:** [screen_bot.c:204-205](screen_bot.c#L204-L205), [eyes_emotion.c:113-114](eyes_emotion.c#L113-L114)

`g_rtc_time` 由 `rtc_process` 线程写入 (持 `g_rtc_mutex`), 但在 `lcd_process` 中被直接读取:

```c
// screen_bot.c:204 — 无锁直接读取 g_rtc_time 成员
check_scheduled_feeding(g_rtc_time.hour, g_rtc_time.minute,
                        g_rtc_time.second);

// eyes_emotion.c:113 — 无锁直接读取 g_rtc_time.second
if (g_rtc_time.second != last_draw_sec) {
    last_draw_sec = g_rtc_time.second;
    ui_update_time_only();
}
```

而 `ds3231.h` 明确提供了线程安全接口 `rtc_time_read_safe()`:

```c
// ds3231.h:64-70 — 已有线程安全读取函数, 但未被使用!
void rtc_time_read_safe(RTC_Time *out)
{
    LOS_MuxPend(g_rtc_mutex, LOS_WAIT_FOREVER);
    *out = g_rtc_time;
    LOS_MuxPost(g_rtc_mutex);
}
```

**问题:** `RTC_Time` 是 8 字节结构体 (`uint16_t year` + 5×`uint8_t`), 在 `rtc_process` 写入过程中如果 lcd_process 读到半更新状态, 会出现年份/月份/日期/时间不一致的撕裂读 (torn read)。虽然小字段 (uint8_t) 在 Cortex-M4 上单字节对齐读取是原子的, 但跨字段一致性无法保证。

**修复建议:**
```c
// screen_bot.c:204 — 改用安全接口
RTC_Time t;
rtc_time_read_safe(&t);
check_scheduled_feeding(t.hour, t.minute, t.second);

// eyes_emotion.c:113 — 改用安全接口
RTC_Time t;
rtc_time_read_safe(&t);
if (t.second != last_draw_sec) { ... }
```

---

### 🟠 HIGH #3: `check_scheduled_feeding` last_trigger_min 在特定条件下导致漏触发

**文件:** [menu_ui.c:154-168](menu_ui.c#L154-L168)

```c
void check_scheduled_feeding(uint8_t h, uint8_t m, uint8_t s)
{
    static uint8_t last_trigger_min = 60;
    if (m != last_trigger_min && g_sys_page == PAGE_IDLE)
    {
        for (int i = 0; i < 5; i++) {
            if (g_schedules[i].active && g_schedules[i].hour == h
                && g_schedules[i].minute == m)
            {
                last_trigger_min = m;  // ← 在调用喂食之前就设置了
                ui_start_feeding(g_schedules[i].portion + 1);
                break;
            }
        }
    }
}
```

**问题:** 如果同一分钟内有多个匹配的定时计划, 只有第一个会触发 (因为 `break`)。更严重的是: 如果 `ui_start_feeding` 因为某种原因没能成功触发喂食 (例如 `CMD_FEED` 被 `g_sys_page != PAGE_SUB_FEEDING` 条件拒绝), `last_trigger_min` 已经被更新, 导致这一分钟的计划**永远丢失**。

虽然当前实现中 `ui_start_feeding` 无条件成功, 但如果在第 140 行的 `CMD_FEED` 处理逻辑中引入拒绝条件, 就会出现问题。

**修复建议:** 将 `last_trigger_min = m` 移到 `ui_start_feeding` 调用和 `break` 之后, 或检查返回状态:
```c
last_trigger_min = m;
ui_start_feeding(g_schedules[i].portion + 1);
break;
// 如果未来 ui_start_feeding 可能失败, 应:
// if (ui_start_feeding(...) == SUCCESS) { last_trigger_min = m; }
```

---

### 🟠 HIGH #4: 手动风扇/水泵命令被自动调控逻辑立即覆盖

**文件:** [screen_bot.c:157-195](screen_bot.c#L157-L195)

```c
// 步骤 1: 处理命令队列中的手动指令
case CMD_FAN_OFF:
    fan_control(0);   // 关闭风扇, g_fan_state = 0
    break;

// ... 其他命令处理 ...

// 步骤 2: 自动环境调控 — 可能立即覆盖手动指令!
if (g_sensor_temp >= TEMP_HIGH_START && g_fan_state == 0) {
    fan_control(1);   // ← 如果温度 >= 28°C, 立即重新开启!
}
```

**时间线示例:**
1. 云端下发 "关闭风扇" → `fan_control(0)` → `g_fan_state = 0`
2. 同一循环中 → 检测到 `g_sensor_temp >= 28 && g_fan_state == 0` → `fan_control(1)`
3. 用户期望风扇关闭, 但实际仍然运行

**修复建议:** 引入 "手动覆盖" 标志, 自动调控仅在无手动覆盖时生效:
```c
// 新增
volatile int g_fan_manual_override = 0;
volatile int g_pump_manual_override = 0;

// CMD_FAN_ON/OFF 处理中设置 g_fan_manual_override = 1
// 自动调控逻辑中:
if (!g_fan_manual_override) {
    if (g_sensor_temp >= TEMP_HIGH_START && g_fan_state == 0) fan_control(1);
    else if (g_sensor_temp < TEMP_OK_STOP && g_fan_state == 1) fan_control(0);
}
// 在合适的时机重置手动覆盖标志 (如传感器值恢复正常范围)
```

---

### 🟡 MEDIUM #5: 自动调控滞后区间存在边界抖动

**文件:** [screen_bot.c:34-39](screen_bot.c#L34-L39)

```c
#define TEMP_HIGH_START  28   // 温度 >= 28°C 开风扇
#define TEMP_OK_STOP     27   // 温度 < 27°C 关风扇
#define WATER_LOW_START  20   // 水位 <= 20% 开水泵
#define WATER_OK_STOP    80   // 水位 >= 80% 关水泵
```

**问题:** 温度滞后区间只有 1°C (27→28)。如果一个传感器采样周期内温度从 26.9 跳到 28.1, 风扇开启; 下一秒采样为 26.9, 风扇关闭。对于实际物理系统, 1°C 的滞后可能不足以防止频繁启停 (short-cycling), 可能损害继电器和电机寿命。

**修复建议:** 增大滞后区间, 例如 26/30 或根据实际测试数据调整:
```c
#define TEMP_HIGH_START  30
#define TEMP_OK_STOP     26
```

---

## 二、逻辑与正确性问题

### 🟠 HIGH #6: `rand()` 从未播种 — 动画行为完全可预测

**文件:** [eyes_emotion.c:163,166](eyes_emotion.c#L163-L166) 等多处

```c
int idle_time = 1000 + (rand() % 2000);  // 每次都相同!
int action_rand = rand() % 100;           // 每次都相同!
```

**问题:** 整个项目中从未调用 `srand()`, 导致 `rand()` 使用默认种子 (通常为 1)。这意味着:
- 每次上电后, 电子眼动画序列**完全相同**
- 逗猫模式激光运动轨迹**完全可预测**
- 失去了 "灵动" 和 "仿生" 的设计初衷

**修复建议:**
```c
// 在 ds3231_init() 后或 app_example_init() 中:
srand((unsigned int)(g_rtc_time.second * 100 + g_rtc_time.minute * 60));
// 或使用 ADC 噪声值作为种子
```

---

### 🟠 HIGH #7: UDP 服务器线程静默退出

**文件:** [udp_server.c:57-72](udp_server.c#L57-L72)

```c
server_fd = socket(AF_INET, SOCK_DGRAM, 0);
if (server_fd < 0) return;  // ← 静默退出, 无重试, 无日志

// ...

ret = bind(server_fd, ...);
if (ret < 0) {
    close(server_fd);
    return;  // ← 静默退出, 无重试, 传感器数据永久丢失
}
```

**问题:** 如果 WiFi 连接成功但 socket/bind 失败 (例如端口被占用), UDP 线程永久退出。系统继续运行但没有传感器数据, 且无任何告警或恢复机制。

**修复建议:** 增加重试循环:
```c
while (1) {
    server_fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (server_fd >= 0) {
        // bind...
        if (bind(...) == 0) break;
        close(server_fd);
    }
    printf("[UDP] socket/bind failed, retrying in 5s...\n");
    LOS_Msleep(5000);
}
```

---

### 🟡 MEDIUM #8: UART 指令解析中存在无关字节干扰

**文件:** [voice_module.c:82-192](voice_module.c#L82-L192)

```c
for (unsigned int i = 0; i < acc_len - 1; i++)
{
    // 逐字节扫描 2 字节指令模式
    if (acc_buf[i] == 0x06 && acc_buf[i+1] == 0x01) { ... }
    else if (acc_buf[i] == 0x06 && acc_buf[i+1] == 0x02) { ... }
    // ... 更多匹配 ...
}
```

**问题:** 
1. 2 字节指令模式过于简单, 容易在数据流中产生**误匹配** (例如语音模块发送的传感器数据恰好包含 `0x06 0x01`)
2. 没有使用帧头/帧尾/校验和等通信协议保护, 存在误触发风险
3. 静默丢弃溢出数据 (`acc_len = 0; memcpy(acc_buf, recv_buffer, recv_length);`), 不通知调用者

**修复建议:** 
- 为指令帧增加起始/结束标记和校验字节
- 或者使用长度-类型-值 (LTV) 格式
- 溢出时通过 printf 发出告警

---

### 🟡 MEDIUM #9: UART 空闲超时后丢弃全部累积数据

**文件:** [voice_module.c:73-78](voice_module.c#L73-L78)

```c
if (idle_count > 10) {
    acc_len = 0;       // ← 丢弃全部累积数据
    idle_count = 0;
}
```

**问题:** 如果语音模块正在发送一条多字节指令, 但中间有超过 1 秒的停顿 (例如模块在等待语音识别结果), 已累积的部分指令会被**直接丢弃**。下一次数据到达时, 将从半截指令的中间开始解析, 可能误匹配或丢失有效指令。

**修复建议:** 只在检测到完整帧边界时才清空, 而不是基于纯超时。例如使用帧头 (`0xAA 0x55`) 来定位帧边界。

---

### 🟡 MEDIUM #10: MQTT 断线后无 deinit 直接重试 init

**文件:** [mqtt_cloud.c:142-147](mqtt_cloud.c#L142-L147)

```c
while (1) {
    LOS_Msleep(10000);
    printf("[MQTT Cloud] Retrying init...\n");
    ret = oc_mqtt_init();  // ← 重新 init 但未先 deinit
    if (ret == 0) break;
}
```

**问题:** 如果第一次 `oc_mqtt_init()` 部分成功 (例如分配了 socket 但后续步骤失败), 重复调用 `oc_mqtt_init()` 而不先调用 `oc_mqtt_deinit()` 可能导致:
- Socket 描述符泄漏 (RK2206 上 socket 数量有限)
- 内部状态混乱
- 内存泄漏累积

**修复建议:**
```c
while (1) {
    LOS_Msleep(10000);
    oc_mqtt_deinit();  // 先清理
    ret = oc_mqtt_init();
    if (ret == 0) break;
}
```

---

### 🟡 MEDIUM #11: `cJSON_PrintUnformatted` 分配的内存可能泄漏

**文件:** [mqtt_cloud.c:109-116](mqtt_cloud.c#L109-L116)

```c
char *rsp_str = cJSON_PrintUnformatted(rsp);
// ...
if (rsp_str) {
    *resp_data = (uint8_t *)rsp_str;
    *resp_size = strlen(rsp_str);
}
// rsp_str 的释放责任转移给了 MQTT 协议栈
```

**问题:** `cJSON_PrintUnformatted` 使用 `malloc` 分配堆内存。`rsp_str` 被传递给 MQTT 协议栈, 但如果协议栈未在其内部释放该内存, 则每次命令下发将泄漏数十字节。在 256KB RAM 的系统上, 这可能在数小时运行后耗尽堆内存。

**修复建议:** 确认 Paho MQTT 库是否在发送响应后释放 `*resp_data`。如果否, 考虑使用静态缓冲区替代动态分配。

---

### 🟡 MEDIUM #12: LCD 初始化失败后硬件处于未定义状态

**文件:** [screen_bot.c:124](screen_bot.c#L124)

```c
void lcd_process(void *arg)
{
    if (lcd_init() != 0) return;  // ← 直接返回, servo/devices/motor 未初始化

    servo_init();        // ← 这行及以下永远不会执行
    devices_init();
    feeder_motor_init();
```

**问题:** 如果 LCD 初始化失败 (SPI 通信异常, 硬件故障):
1. 舵机/激光/水泵/风扇/喂食电机**全部未初始化** — GPIO 处于默认输入状态
2. GPIO 默认输入状态可能导致 MOSFET/继电器**意外导通**, 水泵或风扇意外开启
3. 线程直接退出, 无日志, 无告警

**修复建议:**
```c
if (lcd_init() != 0) {
    printf("[FATAL] LCD init failed, thread exiting!\n");
    // 至少初始化硬件到安全状态
    devices_init();      // 确保 GPIO 输出且关闭
    feeder_motor_init(); // 确保喂食电机关闭
    servo_init();        // 确保舵机回中
    return;
}
```

---

## 三、内存与栈安全

### 🟡 MEDIUM #13: LCD 中文字库栈缓冲区较大

**文件:** [lcd.c:277,234,190,145](lcd.c) (多处)

| 函数 | 栈缓冲区大小 |
|:---|:---|
| `lcd_show_chinese_32x32` | `uint16_t char_buf[1024]` = **2048 字节** |
| `lcd_show_chinese_24x24` | `uint16_t char_buf[576]` = **1152 字节** |
| `lcd_show_char` | `uint16_t char_buf[512]` = **1024 字节** |
| `lcd_fill` | `uint16_t line_buf[320]` = **640 字节** |

**问题:** `lcd_process` 线程栈为 20KB (20480 字节)。如果调用链较深 (例如 `menu_draw_current_page` → `lcd_show_string` → `lcd_show_char`), 同时还有 `delay_with_break` → `ui_update_time_only` → `rtc_time_read_safe`, 加上 ISR 可能使用同一栈 (Cortex-M4 PSP), 栈使用量可能在极端情况下接近上限。

**修复建议:** 
- 考虑将大缓冲区设为 `static` (牺牲少量全局内存, 换取栈安全)
- 或使用 `__attribute__((section(".ext_psram_buf")))` 将缓冲区放在 PSRAM 中

---

### 🟢 LOW #14: `lcd_show_float_num1` 中变量溢出风险

**文件:** [lcd.c:753](lcd.c#L753)

```c
uint16_t num1 = num * 100;  // float → uint16_t 隐式转换
```

如果 `num >= 655.36`, `uint16_t` 将溢出。虽然显示传感器值时不太可能达到, 但函数本身缺少范围检查。

---

## 四、安全问题

### 🟠 HIGH #15: 硬编码凭据泄露

**文件:** [udp_server.c:41-42](udp_server.c#L41-L42), [mqtt_cloud.c:38-40](mqtt_cloud.c#L38-L40)

```c
// WiFi 凭据硬编码
set_wifi_config_route_ssid(NULL, (uint8_t *)"鸿蒙研究院");
set_wifi_config_route_passwd(NULL, (uint8_t *)"12345678");

// 华为 IoTDA 设备三元组硬编码
#define CLIENT_ID  "69173ed2775d2a3818654e13_myNodeId_0_0_2026051206"
#define USERNAME   "69173ed2775d2a3818654e13_myNodeId"
#define PASSWORD   "6aba5738afd3d39b1000a18f6b0bcc65ca7423a2fc9f4489a775eb75873378dc"
```

**风险:** 固件二进制中包含完整的 WiFi 密码和云平台设备密钥。任何人都可以从 Flash 中提取这些凭据。

---

### 🟡 MEDIUM #16: UDP 端口无任何认证机制

**文件:** [udp_server.c](udp_server.c)

UDP 服务器监听端口 6666, 接收任何来源的传感器数据, 无认证/加密/来源验证。局域网内任何设备都可以注入虚假传感器数据, 触发错误的自动调控 (开水泵/开风扇)。

**修复建议:** 至少验证发送方 IP 地址, 或使用简单的共享密钥验证。

---

## 五、代码质量与可维护性

### 🟢 LOW #17: GPIO 有效电平不一致

**文件:** [hardware_control.c](hardware_control.c)

| 设备 | 引脚 | 开启电平 | 关闭电平 |
|:---|:---|:---|:---|
| 激光 | PC4 | LOW (0) | HIGH (1) |
| 水泵 | PB0 | HIGH (1) | LOW (0) |
| 风扇 | PB1 | LOW (0) | HIGH (1) |
| 喂食电机 | PC6 | HIGH (1) | LOW (0) |

激光和风扇是低电平有效, 水泵和电机是高电平有效。虽然这是由硬件设计决定的, 但代码中的 `on=1` 并不总是对应 "高电平", 容易导致维护混淆。

**修复建议:** 使用明确的宏定义:
```c
#define LASER_ON   LZGPIO_LEVEL_LOW
#define LASER_OFF  LZGPIO_LEVEL_HIGH
```

---

### 🟢 LOW #18: `check_scheduled_feeding` 未使用的参数

**文件:** [menu_ui.c:154](menu_ui.c#L154)

```c
void check_scheduled_feeding(uint8_t h, uint8_t m, uint8_t s)
//                                                         ^ 从未使用
```

参数 `s` (秒) 在函数体内从未使用。这不影响功能, 但可能误导维护者以为函数会用到秒级精度。

---

### 🟢 LOW #19: 传感器数据无范围验证

**文件:** [screen_bot.c:171-175](screen_bot.c#L171-L175)

UDP 解析的传感器数据直接写入 `g_sensor_temp/hum/weight/water`, 无任何范围检查。恶意或错误的 UDP 数据可以设置异常值 (如温度 = -999), 导致自动调控行为异常。

**修复建议:**
```c
if (cmd.param1 >= -40 && cmd.param1 <= 85) g_sensor_temp = cmd.param1;
if (cmd.param2 >= 0 && cmd.param2 <= 100) g_sensor_hum = cmd.param2;
// ...
```

---

## 六、与旧分析报告 (ANALYSIS_REPORT.md) 的对比

以下是旧报告中已修复或实际情况的追踪:

| 旧报告问题 | 当前状态 |
|:---|:---|
| CMSIS v2 / LiteOS API 混用 | ✅ **已修复** — command_queue.c 已统一使用 LiteOS API |
| ds3231.c 未使用 I2cBusIo | ✅ **已修复** — 当前使用 `I2cBusIo` 结构体 |
| ds3231.c 缺少 PinctrlSet | ✅ **已修复** — ds3231_init 中已添加 |
| voice_module.c 缺少 LzUartDeinit | ✅ **已修复** — 第29行已添加 |
| voice_uart 栈大小 4KB | ✅ **已修复** — 当前为 8KB (screen_bot.c:308) |
| servo_init 缺少错误检查 | ✅ **已修复** — servo_drive.c 新增 `s_servo_ready` 锁死机制 |
| UDP 缺少 SO_REUSEADDR | ✅ **已修复** — udp_server.c:61 已添加 |
| eyes_emotion.h 声明不一致 | ✅ **已修复** — 头文件已更新为 `_step` |
| g_feeding_countdown 未在 menu_ui.h 导出 | ✅ **已修复** — menu_ui.h:34 已添加 extern 声明 |
| feeder_motor 无 stop 函数 | ✅ **已修复** — 改为 `feeder_motor_control(on)` |

---

## 修复优先级总览

| 优先级 | 编号 | 问题 | 修复难度 | 影响范围 |
|:---|:---|:---|:---|:---|
| 🔴 CRITICAL | #1 | 传感器变量无锁写入 | 低 (加3行) | 数据一致性 |
| 🔴 CRITICAL | #2 | RTC 时间无锁读取 | 低 (改函数调用) | 时间撕裂 |
| 🟠 HIGH | #4 | 手动/自动控制冲突 | 中 (引入标志位) | 用户体验 |
| 🟠 HIGH | #6 | rand() 未播种 | 低 (加1行) | 功能特性 |
| 🟠 HIGH | #7 | UDP 线程静默退出 | 低 (加重试循环) | 可用性 |
| 🟠 HIGH | #15 | 硬编码凭据 | 中 (配置化) | 安全性 |
| 🟡 MEDIUM | #3 | 定时喂食漏触发风险 | 低 | 可靠性 |
| 🟡 MEDIUM | #5 | 滞后区间过窄 | 低 (改宏) | 硬件寿命 |
| 🟡 MEDIUM | #8 | UART 指令误匹配 | 中 (协议设计) | 可靠性 |
| 🟡 MEDIUM | #9 | UART 超时丢数据 | 低 | 可靠性 |
| 🟡 MEDIUM | #10 | MQTT 重连前未 deinit | 低 (加一行) | 资源泄漏 |
| 🟡 MEDIUM | #11 | cJSON 内存泄漏 | 中 (需分析调用链) | 内存耗尽 |
| 🟡 MEDIUM | #12 | LCD 失败后外设未初始化 | 低 (调整代码顺序) | 硬件安全 |
| 🟡 MEDIUM | #13 | 栈缓冲区较大 | 中 (改为static) | 栈溢出风险 |
| 🟡 MEDIUM | #16 | UDP 无认证 | 中 | 安全性 |
| 🟢 LOW | #14,17,18,19 | 代码质量问题 | 低 | 可维护性 |

---

> **总结:** 项目整体架构设计良好, 命令队列模式实现了线程间解耦, `g_remote_cat_mode` 安全锁机制是优秀实践。最严重的问题是**两处数据竞争 (#1, #2)** — 互斥锁只在读端使用而写端裸写, 属于典型的 "加锁加一半" 错误, 应优先修复。其次, 手动/自动控制冲突 (#4) 和 rand() 未播种 (#6) 会直接影响用户体验。
