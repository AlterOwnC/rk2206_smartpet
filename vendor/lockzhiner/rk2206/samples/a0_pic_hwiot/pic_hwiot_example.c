#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include "los_task.h"
#include "ohos_init.h"
#include "lwip/sockets.h"
#include "lwip/netdb.h"
#include "lz_hardware.h"
#include "config_network.h"

// ==================== 1. 硬件引脚与 PWM 配置 ====================
#define SERVO_X_PWM_PORT  0  // 对应 PB6
#define SERVO_Y_PWM_PORT  1  // 对应 PB7
#define PWM_PERIOD_NS     20000000  

static DevIo m_pwm2_io = {
    .isr =   {.gpio = INVALID_GPIO},
    .rst =   {.gpio = INVALID_GPIO},
    .ctrl1 = {.gpio = GPIO0_PB4, .func = MUX_FUNC1, .type = PULL_NONE, .drv = DRIVE_KEEP, .dir = LZGPIO_DIR_KEEP, .val = LZGPIO_LEVEL_KEEP},
    .ctrl2 = {.gpio = INVALID_GPIO},
};

static DevIo m_pwm3_io = {
    .isr =   {.gpio = INVALID_GPIO},
    .rst =   {.gpio = INVALID_GPIO},
    .ctrl1 = {.gpio = GPIO0_PB5, .func = MUX_FUNC1, .type = PULL_NONE, .drv = DRIVE_KEEP, .dir = LZGPIO_DIR_KEEP, .val = LZGPIO_LEVEL_KEEP},
    .ctrl2 = {.gpio = INVALID_GPIO},
};

// ==================== 2. 网络底层声明 ====================
extern int SetApModeOn(void);
extern WifiErrorCode SetWifiModeOn(void);
extern int get_wifi_info(void);
extern void set_wifi_config_ssid(void* pfn, unsigned char *s);
extern void set_wifi_config_passwd(void* pfn, unsigned char *p);
extern void set_wifi_config_ip(void* pfn, unsigned char *ip);
extern void set_wifi_config_gw(void* pfn, unsigned char *gw);
extern void set_wifi_config_mask(void* pfn, unsigned char *m);
// STA模式需要用到的路由配置函数
extern void set_wifi_config_route_ssid(void* pfn, unsigned char *s);
extern void set_wifi_config_route_passwd(void* pfn, unsigned char *p);

// ==================== 3. 舵机控制函数 ====================
void set_servo_angle(uint32_t port, uint8_t angle)
{
    if (angle > 180) angle = 180;
    uint32_t duty_ns = 500000 + (angle * 2000000 / 180);
    LzPwmStart(port, duty_ns, PWM_PERIOD_NS);
}

// ==================== 4. 主控任务 (边缘网关核心逻辑) ====================
void gimbal_gateway_task(void *arg)
{
    int ret;

    printf("\r\n====== [1] 初始化云台舵机硬件 ======\r\n");
    DevIoInit(m_pwm2_io);
    if (LzPwmInit(SERVO_X_PWM_PORT) != LZ_HARDWARE_SUCCESS) {
        printf("❌ PB6 PWM 初始化失败！\r\n");
    }
    DevIoInit(m_pwm3_io);
    if (LzPwmInit(SERVO_Y_PWM_PORT) != LZ_HARDWARE_SUCCESS) {
        printf("❌ PB7 PWM 初始化失败！\r\n");
    }
    
    // 上电自检动作
    set_servo_angle(SERVO_X_PWM_PORT, 90);
    set_servo_angle(SERVO_Y_PWM_PORT, 90);
    LOS_Msleep(1000); 
    printf("✅ 舵机回中完成！\n");

    printf("\r\n====== [2] 启动网络：AP + STA 共存模式 ======\r\n");
    
    // 【2.1 先开启 STA 模式，连接你家里的路由器获取外网】
    // ⚠️⚠️⚠️ 注意：请将这里的账号密码改成你当前环境真实的 Wi-Fi ⚠️⚠️⚠️
    printf(">>>> 准备连接外部路由器...\n");
    set_wifi_config_route_ssid(NULL, (unsigned char *)"MyHomeWifi_Name"); 
    set_wifi_config_route_passwd(NULL, (unsigned char *)"MyHomeWifi_Password");
    SetWifiModeOn(); 

    // 阻塞等待获取到家里路由器分配的外网 IP
    while(get_wifi_info() != 0) {
        LOS_Msleep(1500);
        printf(">>>> 正在等待路由器分配 IP...\n");
    }
    printf("✅ STA 模式成功！板子 A 已具备访问互联网的能力！\n");


    // 【2.2 再开启 AP 模式，为板子 B 提供热点局域网】
    printf("\r\n>>>> 准备开启本地 AP 热点...\n");
    set_wifi_config_ssid(NULL, (unsigned char *)"alterowncx");
    set_wifi_config_passwd(NULL, (unsigned char *)"11112222");

    // ⚠️ 关键操作：将热点网段强制设为 192.168.5.1，防止与外网路由冲突！
    uint8_t ip[4]      = {192, 168, 5, 1};
    uint8_t gw[4]      = {192, 168, 5, 1};
    uint8_t mask[4]    = {255, 255, 255, 0};
    set_wifi_config_ip(NULL, ip);
    set_wifi_config_gw(NULL, gw);
    set_wifi_config_mask(NULL, mask);

    ret = SetApModeOn();
    if (ret == 0) {
        printf("✅ AP 模式开启成功！热点 alterowncx (IP: 192.168.5.1) 已就绪！\n");
    } else {
        printf("❌ AP 模式开启失败，错误码: %d\n", ret);
        return; 
    }
    LOS_Msleep(3000); // 稍作延时等待网络底层稳定

    printf("\r\n====== [3] 建立 UDP 监听服务器 ======\r\n");
    int sock_fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock_fd < 0) {
        printf("❌ Socket 创建失败！\n");
        return;
    }

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY); 
    server_addr.sin_port = htons(9999);              

    if (bind(sock_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        printf("❌ 端口 9999 绑定失败！\n");
        close(sock_fd);
        return;
    }
    printf("✅ UDP 服务器已就绪，正在监听端口 9999...\n\n");

    char recv_buf[256];
    struct sockaddr_in client_addr;
    socklen_t client_addr_len; 

    // ==================== 5. 核心事件循环 ====================
    while (1) {
        client_addr_len = sizeof(client_addr);
        memset(&client_addr, 0, sizeof(client_addr));
        memset(recv_buf, 0, sizeof(recv_buf));

        int recv_len = recvfrom(sock_fd, recv_buf, sizeof(recv_buf) - 1, 0, 
                               (struct sockaddr*)&client_addr, &client_addr_len);
        
        if (recv_len > 0) {
            recv_buf[recv_len] = '\0'; 
            printf("📩 收到局域网节点数据: [%s]\n", recv_buf);

            // 解析来自板子 B 的数据控制舵机
            int parsed_x = -1;
            int parsed_y = -1;
            int match_count = sscanf(recv_buf, "X:%d,Y:%d", &parsed_x, &parsed_y);

            if (match_count == 2) {
                if (parsed_x < 0) parsed_x = 0;
                if (parsed_x > 180) parsed_x = 180;
                if (parsed_y < 0) parsed_y = 0;
                if (parsed_y > 180) parsed_y = 180;
                parsed_x = 180 - parsed_x; // 根据你之前的逻辑做的倒置
                set_servo_angle(SERVO_X_PWM_PORT, parsed_x);
                set_servo_angle(SERVO_Y_PWM_PORT, parsed_y);
            } else {
                printf("⚠️ 解析失败，不符合 X:角度,Y:角度 格式\n");
            }
            
            // TODO: 未来你可以在这里加上华为云 MQTT 的发布代码
            // 把收到的 recv_buf 打包后，通过外网发给华为云
            // mqtt_publish_to_huawei_cloud(recv_buf);

        } else if (recv_len < 0) {
            LOS_Msleep(100); 
        }
    }
}

// ==================== 5. 系统启动入口 ====================
void gimbal_system_init(void)
{
    unsigned int thread_id;
    TSK_INIT_PARAM_S task = {0};

    task.pfnTaskEntry = (TSK_ENTRY_FUNC)gimbal_gateway_task;
    task.uwStackSize = 8192; // 保持 8KB 大栈空间
    task.pcName = "gimbal_gateway_task";
    task.usTaskPrio = 6;
    LOS_TaskCreate(&thread_id, &task);
}

APP_FEATURE_INIT(gimbal_system_init);