#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include "los_task.h"
#include "ohos_init.h"
#include "lwip/sockets.h"
#include "lwip/netdb.h"
#include "lz_hardware.h" 

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
extern void set_wifi_config_ssid(void* pfn, unsigned char *s);
extern void set_wifi_config_passwd(void* pfn, unsigned char *p);
extern void set_wifi_config_ip(void* pfn, unsigned char *ip);
extern void set_wifi_config_gw(void* pfn, unsigned char *gw);
extern void set_wifi_config_mask(void* pfn, unsigned char *m);

// ==================== 3. 舵机控制函数 ====================
void set_servo_angle(uint32_t port, uint8_t angle)
{
    if (angle > 180) angle = 180;
    uint32_t duty_ns = 500000 + (angle * 2000000 / 180);
    LzPwmStart(port, duty_ns, PWM_PERIOD_NS);
}

// ==================== 4. 主控任务 ====================
void gimbal_control_task(void *arg)
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
    
    // 【新增】上电自检动作
    printf("\r\n====== [上电自检] 舵机运动测试 ======\r\n");
    printf(">>>> 1. 转至 75°\n");
    set_servo_angle(SERVO_X_PWM_PORT, 75);
    set_servo_angle(SERVO_Y_PWM_PORT, 75);
    LOS_Msleep(1000); // 停顿1秒

    printf(">>>> 2. 回中至 90°\n");
    set_servo_angle(SERVO_X_PWM_PORT, 90);
    set_servo_angle(SERVO_Y_PWM_PORT, 90);
    LOS_Msleep(1000); // 停顿1秒
    printf("✅ 舵机自检完成！\n");

    printf("\r\n====== [2] 配置并开启 AP 模式 ======\r\n");
    set_wifi_config_ssid(NULL, (unsigned char *)"alterowncx");
    set_wifi_config_passwd(NULL, (unsigned char *)"11112222");

    uint8_t ip[4]      = {192, 168, 2, 1};
    uint8_t gw[4]      = {192, 168, 2, 1};
    uint8_t mask[4]    = {255, 255, 255, 0};
    set_wifi_config_ip(NULL, ip);
    set_wifi_config_gw(NULL, gw);
    set_wifi_config_mask(NULL, mask);

    ret = SetApModeOn();
    if (ret == 0) {
        printf("✅ AP 模式开启成功！请连接 Wi-Fi: alterowncx\n");
    } else {
        printf("❌ AP 模式开启失败，错误码: %d\n", ret);
        return; 
    }
    LOS_Msleep(5000); 

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
    printf("✅ UDP 服务器已就绪，正在监听...\n\n");

    char recv_buf[256];
    struct sockaddr_in client_addr;
    socklen_t client_addr_len; 

    while (1) {
        client_addr_len = sizeof(client_addr);
        memset(&client_addr, 0, sizeof(client_addr));
        memset(recv_buf, 0, sizeof(recv_buf));

        int recv_len = recvfrom(sock_fd, recv_buf, sizeof(recv_buf) - 1, 0, 
                               (struct sockaddr*)&client_addr, &client_addr_len);
        
        if (recv_len > 0) {
            recv_buf[recv_len] = '\0'; 
            
            // 【恢复】第一时间打印收到的原始数据，确定网络到底通没通！
            printf("📩 收到原始数据: [%s]\n", recv_buf);

            int parsed_x = -1;
            int parsed_y = -1;
            int match_count = sscanf(recv_buf, "X:%d,Y:%d", &parsed_x, &parsed_y);

            if (match_count == 2) {
                if (parsed_x < 0) parsed_x = 0;
                if (parsed_x > 180) parsed_x = 180;
                if (parsed_y < 0) parsed_y = 0;
                if (parsed_y > 180) parsed_y = 180;
                parsed_x = 180 - parsed_x;
                set_servo_angle(SERVO_X_PWM_PORT, parsed_x);
                set_servo_angle(SERVO_Y_PWM_PORT, parsed_y);
                
                // 成功驱动时不再刷屏，或者你可以把这行注释掉
                // printf("🎯 舵机已转至 -> X:%d°, Y:%d°\n", parsed_x, parsed_y);
            } else {
                printf("⚠️ 解析失败，不符合 X:角度,Y:角度 格式\n");
            }

        } else if (recv_len < 0) {
            printf("⚠️ 数据接收异常！\n");
            LOS_Msleep(500); 
        }
    }
}

// ==================== 5. 系统启动入口 ====================
void gimbal_system_init()
{
    unsigned int thread_id;
    TSK_INIT_PARAM_S task = {0};

    task.pfnTaskEntry = (TSK_ENTRY_FUNC)gimbal_control_task;
    // 【关键修复】栈空间翻倍到 8192，防止 sscanf 导致栈溢出卡死
    task.uwStackSize = 8192; 
    task.pcName = "gimbal_control_task";
    task.usTaskPrio = 6;
    LOS_TaskCreate(&thread_id, &task);
}

APP_FEATURE_INIT(gimbal_system_init);