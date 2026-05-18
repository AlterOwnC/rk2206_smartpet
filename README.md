# 小凌派 RK2206 非官方维护仓库


这是一个非官方的小凌派 RK2206 仓库，主要添加原有代码没有的新特性、新案例，以及官方代码仓库中的一些错误修复。

## Bug 修复

- 修复 `//device/rockchip/tools/package/mkimage.sh` 的语法错误，解决部分 Linux 发行版中无法正确生成烧录镜像的问题

## 新特性

- 允许从局域网 NTP 服务器获取时间（秒级精度）
- 根据 NTP 时间生成设备 UUID（仅生成一次并写入 NOR FLASH，可作为 CHIP ID 使用）

## 案例列表

### 智慧小屋系列（x0 ~ x4）

| 编号 | 案例 | 说明 |
| :--- | :--- | :--- |
| x0_doorbell | 门铃系统 | 独立运行，无依赖 |
| x1_smartdoor | 智能门系统 | 依赖 x0 |
| x2_sensor | 传感器及 LCD 屏控制 | 依赖 x0, x1 |
| x3_voice | 语音控制系统 | 依赖 x0, x1, x2 |
| x4_netcontrol | MQTT 网络控制 | 依赖 x0, x1, x2, x3 |

### 智能宠物陪伴机器人（v0_screen_bot）

> 详细文档见 [v0_screen_bot/README_zh.md](vendor/lockzhiner/rk2206/samples/v0_screen_bot/README_zh.md)

基于 RK2206 的多线程智能宠物喂食与互动陪伴系统，功能包括：

- **LCD 表情动画** — ST7789V 2.4寸屏，灵动电子眼/喂食/逗猫多套动画表情
- **双轴舵机激光逗猫** — 随机运动算法（装死/试探/突进），屏幕眼神实时跟踪
- **四键 ADC 菜单导航** — 多级中文菜单，支持喂食份量/定时计划/设备控制/传感器数据/时间设置
- **语音模块控制** — UART 二进制协议，支持语音查询温湿度/时间，语音触发喂食/逗猫/风扇
- **手机 UDP 远程** — WiFi 接收传感器 JSON 数据，触发环境自动调控
- **华为云 IoTDA** — MQTT 每 6 秒上报温湿度/重量/水位，接收云端下发指令
- **自动环境调控** — 温度 ≥ 28°C 自动开风扇，水位 ≤ 20% 自动开水泵
- **5 组定时喂食** — 可独立配置时间与份量
- **远程逗猫安全机制** — 本地按键触发的逗猫模式不受云端误关影响

## 硬件平台

- 主控：瑞芯微 RK2206，200MHz，RAM 256KB，PSRAM 8M，FLASH 8M
- 操作系统：OpenHarmony 3.0 LTS (LiteOS-M)
- 编译器：gcc-arm-none-eabi-10.3

## 环境要求

- Python 3.11
- Git
- 交叉编译工具链（仓库已包含 `gcc-arm-none-eabi-10.3-2021.10-x86_64-linux.tar.bz2`）

## 使用方法

### 下载仓库

```bash
git clone https://github.com/AlterOwnC/rk2206.git
```

### 设置环境

```bash
source venv/bin/activate
hb set -root .
```

### 配置 NTP 时间同步

修改 `//third_party/ntpclient/src/ntpclient.c` 中的 `SERVER_IP` 为目标 NTP 服务器 IP 地址。

### 运行智慧小屋系统

1. 参考 `b7_wifi_tcp` 例程设置 WiFi SSID 和密码
2. 修改 `//vendor/lockzhiner/rk2206/samples/x4_netcontrol/src/mqttclient.c` 中的 MQTT 连接参数：
   - `MQTT_SERVER_IP` — MQTT 服务器 IP
   - `MQTT_SERVER_PORT` — MQTT 端口
   - `MQTT_USERNAME` — MQTT 用户名
   - `MQTT_PASSWORD` — MQTT 密码
3. 在 `//vendor/lockzhiner/rk2206/samples/BUILD.gn` 中取消 x0~x4 相关注释
4. 在 `//device/rockchip/rk2206/sdk_liteos/Makefile` 中添加 `-ldoorbell -lsmartdoor -lsensor -lvoice -lnetcontrol`

### 运行智能宠物陪伴机器人

在 `//vendor/lockzhiner/rk2206/samples/BUILD.gn` 中添加：

```gn
"./v0_screen_bot:screen_bot"
```

### 编译

```bash
hb build -f
```

### 烧录

```bash
python flash.py
```

## 源码目录简介

| 目录 | 描述 |
| :--- | :--- |
| applications | 应用程序样例 |
| base | 基础软件服务子系统集 & 硬件服务子系统集 |
| build | 组件化编译、构建和配置脚本 |
| domains | 增强软件服务子系统集 |
| drivers | 驱动子系统 |
| foundation | 系统基础能力子系统集 |
| kernel | 内核子系统 |
| prebuilts | 编译器及工具链子系统 |
| test | 测试子系统 |
| third_party | 开源第三方组件 |
| utils | 常用的工具集 |
| vendor | 厂商提供的软件（含案例代码） |
| build.py | 编译脚本文件 |

