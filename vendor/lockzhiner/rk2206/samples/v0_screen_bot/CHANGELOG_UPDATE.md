# CHANGELOG_UPDATE.md

## 修改记录 — v0_screen_bot 代码重构与硬件逻辑更新

> 修改时间: 2026-05-28
> 修改分支: main
> 修改人: AlterOwnC

---

## 一、RTOS API 统一化 (CMSIS-RTOS v2 → LiteOS 原生 API)

### 修改文件

| 文件 | 修改内容 |
|:---|:---|
| `include/command_queue.h` | 移除 `#include "cmsis_os2.h"`, 引入 `los_event.h`; `osMutexId_t` → `unsigned int`; `osMessageQueueId_t` → `unsigned int`; `osEventFlagsId_t` → `EVENT_CB_S` (静态结构体) |
| `src/command_queue.c` | 全部同步原语改 LiteOS API: `osMutexNew` → `LOS_MuxCreate`; `osMessageQueueNew` → `LOS_QueueCreate`; `osEventFlagsNew` → `LOS_EventInit`; `osMessageQueuePut` → `LOS_QueueWrite`; `osMessageQueueGet` → `LOS_QueueRead`; 增加返回值检查 |
| `screen_bot.c` | `osMutexAcquire/Release` → `LOS_MuxPend/Post` (6处); `osEventFlagsWait` → `LOS_EventRead` (使用 `&g_rtc_evt`, `LOS_WAITMODE_OR`, `LOS_MS2Tick(200)`); `osFlagsError` → `EVENT_RTC_SYNC`; `osOK` → `LOS_OK` |
| `src/ds3231.c` | `osMutexAcquire/Release` → `LOS_MuxPend/Post` (2处) |
| `src/mqtt_cloud.c` | 移除 `cmsis_os2.h`; `osMutexAcquire/Release` → `LOS_MuxPend/Post` (2处) |
| `src/voice_module.c` | 移除 `cmsis_os2.h`; `osMutexAcquire/Release` → `LOS_MuxPend/Post` (4处) |
| `src/menu_ui.c` | `osEventFlagsSet` → `LOS_EventWrite` (使用 `&g_rtc_evt`) |
| `src/oc_mqtt.c` | 移除 `#include "cmsis_os2.h"` (仅 include 未使用) |
| `src/udp_server.c` | 移除 `#include "cmsis_os2.h"` (仅 include 未使用) |

### 修改前后对比

| API | 修改前 (CMSIS v2) | 修改后 (LiteOS) |
|:---|:---|:---|
| 互斥锁创建 | `g_sensor_mutex = osMutexNew(NULL)` | `LOS_MuxCreate(&g_sensor_mutex)` |
| 互斥锁获取 | `osMutexAcquire(g_rtc_mutex, osWaitForever)` | `LOS_MuxPend(g_rtc_mutex, LOS_WAIT_FOREVER)` |
| 互斥锁释放 | `osMutexRelease(g_rtc_mutex)` | `LOS_MuxPost(g_rtc_mutex)` |
| 队列创建 | `osMessageQueueNew(16, sizeof(SystemCommand), NULL)` | `LOS_QueueCreate("cmd_queue", 16, &g_cmd_queue, 0, sizeof(SystemCommand))` |
| 队列写入 | `osMessageQueuePut(g_cmd_queue, cmd, 0, 0)` | `LOS_QueueWrite(g_cmd_queue, cmd, sizeof(SystemCommand), LOS_WAIT_FOREVER)` |
| 队列读取 | `osMessageQueueGet(g_cmd_queue, cmd, NULL, timeout_ms)` | `LOS_QueueRead(g_cmd_queue, cmd, sizeof(SystemCommand), timeout_ms)` |
| 事件创建 | `g_rtc_evt = osEventFlagsNew(NULL)` | `LOS_EventInit(&g_rtc_evt)` |
| 事件设置 | `osEventFlagsSet(g_rtc_evt, EVENT_RTC_SYNC)` | `LOS_EventWrite(&g_rtc_evt, EVENT_RTC_SYNC)` |
| 事件等待 | `osEventFlagsWait(g_rtc_evt, EVENT_RTC_SYNC, osFlagsWaitAny, 200)` | `LOS_EventRead(&g_rtc_evt, EVENT_RTC_SYNC, LOS_WAITMODE_OR, LOS_MS2Tick(200))` |

### 注意事项
- `EVENT_CB_S` 是静态结构体, 所有引用从 `g_rtc_evt` 变为 `&g_rtc_evt`
- `LOS_EventRead` 超时单位为系统 ticks, 需用 `LOS_MS2Tick` 转换
- `LOS_EventRead` 返回值含义与 `osEventFlagsWait` 不同: 成功时返回包含事件位的值, 需用 `(flags & EVENT_MASK) != 0` 检测

---

## 二、I2C 初始化重构

### 修改文件

| 文件 | 修改内容 |
|:---|:---|
| `src/ds3231.c` | 废弃两个分散的 `DevIo` 变量 (`m_i2c1_sda`, `m_i2c1_scl`), 改用官方标准 `I2cBusIo` 结构体 `m_i2c1_bus`; `DevIoInit()` → `I2cIoInit()`; 新增 `PinctrlSet()` 调用 |

### 修改前后对比

**修改前:**
```c
static DevIo m_i2c1_sda = { .ctrl1 = {.gpio = GPIO0_PB6, .func = MUX_FUNC4, ...} };
static DevIo m_i2c1_scl = { .ctrl1 = {.gpio = GPIO0_PB7, .func = MUX_FUNC4, ...} };

void ds3231_init(void) {
    DevIoInit(m_i2c1_sda);
    DevIoInit(m_i2c1_scl);
    LzI2cInit(I2C_BUS_PORT, 100000);
    // 缺少 PinctrlSet
}
```

**修改后:**
```c
static I2cBusIo m_i2c1_bus = {
    .scl  = {.gpio = GPIO0_PB7, .func = MUX_FUNC4, .type = PULL_UP, ...},
    .sda  = {.gpio = GPIO0_PB6, .func = MUX_FUNC4, .type = PULL_UP, ...},
    .id   = FUNC_ID_I2C1,
    .mode = FUNC_MODE_M2,
};

void ds3231_init(void) {
    I2cIoInit(m_i2c1_bus);
    LzI2cInit(I2C_BUS_PORT, m_i2c1_freq);
    PinctrlSet(GPIO0_PB6, MUX_FUNC4, PULL_UP, DRIVE_KEEP);
    PinctrlSet(GPIO0_PB7, MUX_FUNC4, PULL_UP, DRIVE_KEEP);
}
```

### 参照样例
- `b11_i2c_scan/i2c_scan_example.c` — 官方 `I2cBusIo` 标准用法

---

## 三、UART 模块健壮性优化

### 修改文件

| 文件 | 修改内容 |
|:---|:---|
| `src/voice_module.c` | 新增 256 字节静态累积缓冲区 `acc_buf` (解决粘包/半包); 新增空闲超时检测 (连续1秒无数据则清空缓冲区); 命令匹配成功后从缓冲区中移除已处理字节 |

### 修改前后对比

**修改前:**
```c
// 每次循环直接读 UART, 数据只在本轮有效
recv_length = LzUartRead(UART_ID, recv_buffer, sizeof(recv_buffer));
for (i = 0; i < recv_length - 1; i++) {
    if (recv_buffer[i] == 0x06 && recv_buffer[i+1] == 0x01) { /* 处理 */ break; }
}
// 无法处理跨帧的半包数据
```

**修改后:**
```c
// 累积缓冲区: 新数据追加到 static acc_buf, 解析后移除已处理字节
if (acc_len + recv_length < sizeof(acc_buf)) {
    memcpy(&acc_buf[acc_len], recv_buffer, recv_length);
    acc_len += recv_length;
}
// 超时清空: 连续 10 次无数据 → 清空累积缓冲区
idle_count++;
if (idle_count > 10) { acc_len = 0; idle_count = 0; }
```

### 备注
- `LzUartDeinit(UART_ID)` 已存在于原代码中, 无需额外添加

---

## 四、硬件驱动逻辑更新

### 4A. 水泵控制确认

| 文件 | 修改内容 |
|:---|:---|
| `src/hardware_control.c` | `pump_control()` 逻辑已是 HIGH=开 LOW=关 (无需修改); 更新注释标注 "因引入电机驱动模块改为高电平触发" |

### 4B. 电机从 PWM 改为 GPIO

| 文件 | 修改内容 |
|:---|:---|
| `include/feeder_motor.h` | `feeder_motor_start(uint8_t speed_percent)` → `feeder_motor_control(int on)`; 新增完整中文注释 |
| `src/feeder_motor.c` | PC6 引脚 MUX 从 `MUX_FUNC2`(PWM) 改为 `MUX_FUNC0`(GPIO); 删除所有 PWM 代码; 实现 `feeder_motor_control(int on)` (HIGH=开, LOW=关, 与水泵完全对齐) |

### 4C. 调用处更新

| 文件 | 修改内容 |
|:---|:---|
| `src/menu_ui.c` | `feeder_motor_start(g_feeder_speed)` → `feeder_motor_control(1)` (line 62); `feeder_motor_start(0)` → `feeder_motor_control(0)` (line 176) |
| `src/eyes_emotion.c` | `feeder_motor_start(0)` → `feeder_motor_control(0)` (line 105) |

### 修改前后对比

**修改前 (PWM):**
```c
// PWM 速度控制: 传入 0~100 百分比
void feeder_motor_start(uint8_t speed_percent) {
    if (speed_percent == 0) { LzPwmStart(FEEDER_MOTOR_PORT, 0, 1000000); return; }
    uint32_t duty_ns = (period_ns / 100) * speed_percent;
    LzPwmStart(FEEDER_MOTOR_PORT, duty_ns, period_ns);
}
```

**修改后 (GPIO):**
```c
// GPIO 开关控制: 与 pump_control 逻辑完全对齐
void feeder_motor_control(int on) {
    LzGpioSetVal(FEEDER_MOTOR_PIN, on ? LZGPIO_LEVEL_HIGH : LZGPIO_LEVEL_LOW);
}
```

---

## 五、全局中文注释补全

### 已补充注释的模块清单

| 文件 | 注释内容 |
|:---|:---|
| `screen_bot.c` | 文件头注释; 全局变量说明; 线程函数头注释 (adc_process/rtc_process/lcd_process/app_example_init); 自动调控逻辑行内注释; 页面状态机注释; 线程创建参数表格 |
| `src/adc_key.c` | 文件头注释 (按键电压映射表); 函数头注释 (adc_key_init/adc_key_scan); 寄存器操作行内注释 |
| `src/command_queue.c` | 函数头注释 (system_sync_init/cmd_send/cmd_recv); 同步原语初始化行内注释 |
| `src/ds3231.c` | I2cBusIo 结构体注释; ds3231_init 行内注释 (I2cIoInit/PinctrlSet) |
| `src/feeder_motor.c` | 文件头注释 (硬件变更说明); 函数头注释 (feeder_motor_init/feeder_motor_control) |
| `src/hardware_control.c` | GPIO 初始化行内注释 (标注高低电平含义); 控制函数分组注释 |
| `src/mqtt_cloud.c` | 文件头注释 (MQTT 线程概述); cmd_rsp_cb 函数头注释; 主循环行内注释 |
| `src/voice_module.c` | 累积缓冲区说明; 超时机制注释; 命令解析分组注释 |

---

## 六、总结

### 影响范围

- **修改文件数**: 13 个 (.h + .c)
- **新增变量**: 1 个 (I2cBusIo m_i2c1_bus)
- **重命名函数**: 1 个 (feeder_motor_start → feeder_motor_control)
- **删除变量**: 2 个 (m_i2c1_sda, m_i2c1_scl)
- **删除 include**: 5 处 (cmsis_os2.h)

### 验证建议

1. 编译检查: 执行 `hb build -f`
2. 运行时验证点:
   - RTOS 线程创建是否正常
   - I2C1 DS3231 时间读写
   - UART2 语音命令响应
   - 喂食电机 GPIO 启停
   - 水泵/风扇自动环境调控
