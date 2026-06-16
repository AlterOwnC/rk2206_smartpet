/***************************************************************
 * 文件名: mqtt_cloud.c
 * 说    明: 华为 IoTDA MQTT 云连接模块
 *           负责连接华为云 IoT 平台, 每 6 秒上报传感器数据
 *           (温度/湿度/食盆重量/水位百分比), 并接收云端下发的
 *           指令 (motor 喂食 / laser 逗猫)。
 *
 *           MQTT 线程 (mqtt_cloud_thread):
 *             - 优先级 7, 栈 12KB
 *             - 连接失败时自动重试 (10秒间隔)
 *             - 断线时自动重连 (含退避计数)
 *             - 属性上报使用 QOS0 (最多一次, 不保证送达)
 ***************************************************************/

#include "mqtt_cloud.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "los_task.h"
#include "los_mux.h"
#include "lz_hardware.h"
#include "config_network.h"
#include "MQTTClient.h"
#include "cJSON.h"

#include "command_queue.h"
#include "menu_ui.h"
#include "ds3231.h"
#include "oc_mqtt.h"
#include "oc_mqtt_profile_package.h"

/* 属性上报周期: 6000ms = 6 秒 */
#define REPORT_PERIOD  6000
/* MQTT 协议栈驱动间隔: 200ms */
#define YIELD_MS       200

/* 华为 IoTDA 设备三元组 (设备ID / 用户名 / 密码) */
#define CLIENT_ID      "69173ed2775d2a3818654e13_myNodeId_0_0_2026051206"
#define USERNAME       "69173ed2775d2a3818654e13_myNodeId"
#define PASSWORD       "6aba5738afd3d39b1000a18f6b0bcc65ca7423a2fc9f4489a775eb75873378dc"
/* 物联网模型中定义的服务 ID */
#define SERVICE_ID     "smokeDetector"

/***************************************************************
 * 函数名称: cmd_rsp_cb
 * 说    明: 华为 IoTDA 云端命令下发回调函数
 *           解析 JSON 格式的命令消息, 支持:
 *           - "motor":  触发喂食 (CMD_FEED)
 *           - "laser":  控制逗猫模式 (CMD_CAT_MODE_ENTER/EXIT)
 * 参    数:
 *       @recv_data:  接收到的 JSON 数据指针
 *       @recv_size:  接收到的数据长度
 *       @resp_data:  [输出] 响应数据的指针地址, 由回调分配
 *       @resp_size:  [输出] 响应数据的长度
 * 返 回 值: 无
 ***************************************************************/
static void cmd_rsp_cb(uint8_t *recv_data, uint32_t recv_size,
                       uint8_t **resp_data, uint32_t *resp_size)
{
    cJSON *root = cJSON_ParseWithLength((const char *)recv_data, recv_size);
    if (!root) {
        printf("[MQTT] cmd_rsp: JSON parse fail\n");
        return;
    }

    cJSON *cmd_name = cJSON_GetObjectItem(root, "command_name");
    if (!cmd_name || !cmd_name->valuestring) {
        printf("[MQTT] cmd_rsp: no command_name\n");
        cJSON_Delete(root);
        return;
    }

    const char *name = cmd_name->valuestring;
    printf("[MQTT] cmd_rsp: cmd=%s\n", name);

    /* 处理 "motor" 命令 — 触发喂食 */
    if (strcmp(name, "motor") == 0) {
        SystemCommand cmd = {.type = CMD_FEED, .param1 = 1};
        cmd_send(&cmd);
        printf("[MQTT] Motor cmd -> queue\n");
    }
    /* 处理 "laser" 命令 — 控制逗猫激光 */
    else if (strcmp(name, "laser") == 0) {
        cJSON *paras = cJSON_GetObjectItem(root, "paras");
        int laser_on = 1;
        if (paras) {
            cJSON *val = cJSON_GetObjectItem(paras, "value");
            if (val && (cJSON_IsFalse(val) || (cJSON_IsNumber(val) && val->valueint == 0))) {
                laser_on = 0;
            }
        }
        if (laser_on) {
            SystemCommand cmd = {.type = CMD_CAT_MODE_ENTER, .param1 = 1}; // is_remote=1, 允许云端关闭
            cmd_send(&cmd);
            printf("[MQTT] Laser ON -> queue\n");
        } else {
            SystemCommand cmd = {.type = CMD_CAT_MODE_EXIT};
            cmd_send(&cmd);
            printf("[MQTT] Laser OFF -> queue\n");
        }
    }
    /* 处理 "fan" 命令 — 远程开关风扇 (value: true=开, false=关) */
    else if (strcmp(name, "fan") == 0) {
        cJSON *paras = cJSON_GetObjectItem(root, "paras");
        int fan_on = 1;
        if (paras) {
            cJSON *val = cJSON_GetObjectItem(paras, "value");
            if (val && (cJSON_IsFalse(val) || (cJSON_IsNumber(val) && val->valueint == 0))) {
                fan_on = 0;
            }
        }
        if (fan_on) {
            SystemCommand cmd = {.type = CMD_FAN_ON};
            cmd_send(&cmd);
            printf("[MQTT] Fan ON -> queue\n");
        } else {
            SystemCommand cmd = {.type = CMD_FAN_OFF};
            cmd_send(&cmd);
            printf("[MQTT] Fan OFF -> queue\n");
        }
    }

    /* 构建命令响应 JSON (result_code=0 表示成功) */
    cJSON *rsp = cJSON_CreateObject();
    cJSON_AddNumberToObject(rsp, "result_code", 0);
    cJSON_AddStringToObject(rsp, "response_name", name);
    cJSON_AddItemToObject(rsp, "paras", cJSON_CreateObject());

    char *rsp_str = cJSON_PrintUnformatted(rsp);
    cJSON_Delete(rsp);
    cJSON_Delete(root);

    if (rsp_str) {
        *resp_data = (uint8_t *)rsp_str;
        *resp_size = strlen(rsp_str);
        /* 注意: rsp_str 由 cJSON_PrintUnformatted 通过 malloc 分配,
           内存释放责任转移给 MQTT 协议栈。需确认 Paho MQTT 发送后是否
           调用 free(*resp_data)。若否, 每条云端命令将泄漏数十字节。 */
    }
}

/***************************************************************
 * 线程名称: mqtt_cloud_thread
 * 说    明: MQTT 云连接线程 — 连接华为 IoTDA 并循环上报传感器数据
 *           1. 连接 WiFi (SetWifiModeOn)
 *           2. 初始化 OC MQTT (含重试机制)
 *           3. 主循环: Yield 协议栈 → 检测连接 → 读取传感器 → 上报属性
 *           4. 断线自动重连 (退避计数 fail_count)
 * 参    数: arg — 未使用
 * 返 回 值: 无
 ***************************************************************/
void mqtt_cloud_thread(void *arg)
{
    printf("\n===== [MQTT Cloud] Thread start =====\n");
    SetWifiModeOn();

    /* 注册设备信息并设置命令回调 */
    device_info_init(CLIENT_ID, USERNAME, PASSWORD);
    oc_set_cmd_rsp_cb(cmd_rsp_cb);

    /* 首次连接, 含重试循环 (10秒间隔) */
    int ret = oc_mqtt_init();
    if (ret != 0) {
        printf("[MQTT Cloud] Init FAIL (ret=%d), entering retry loop...\n", ret);
        while (1) {
            LOS_Msleep(10000);
            printf("[MQTT Cloud] Retrying init...\n");
            oc_mqtt_deinit(); /* 先清理可能部分初始化的资源, 防止泄漏 */
            ret = oc_mqtt_init();
            if (ret == 0) break;
        }
    }
    printf("[MQTT Cloud] Connected to Huawei IoTDA!\n");

    int fail_count = 0; // 连续失败计数 (用于退避)
    while (1)
    {
        /* 驱动 MQTT 协议栈: 收包/发心跳/检测断线 */
        oc_mqtt_yield(200);

        /* 断线重连 */
        if (!oc_mqtt_is_connected()) {
            printf("[MQTT Cloud] Connection LOST! fail_count=%d, reconnecting...\n",
                   fail_count);
            fail_count++;
            LOS_Msleep(2000);
            int rc = oc_mqtt_reconnect();
            if (rc != 0) {
                printf("[MQTT Cloud] Reconnect FAIL rc=%d, retry in 10s...\n", rc);
                LOS_Msleep(10000);
                continue;
            }
            printf("[MQTT Cloud] Reconnect OK!\n");
            fail_count = 0;
        }

        /* 互斥锁保护读取传感器全局变量 */
        LOS_MuxPend(g_sensor_mutex, LOS_WAIT_FOREVER);
        int temp = g_sensor_temp;
        int hum  = g_sensor_hum;
        int wgt  = g_sensor_weight;
        int wat  = g_sensor_water;
        LOS_MuxPost(g_sensor_mutex);

        printf("[MQTT Cloud] Sensors: T=%d H=%d W=%d WL=%d\n",
               temp, hum, wgt, wat);

        /* 构建属性上报 JSON (温度/湿度/食盆重量/水位百分比) */
        oc_mqtt_profile_kv_t kv_temp   = {NULL, "temperature",               EN_OC_MQTT_PROFILE_VALUE_INT, &temp};
        oc_mqtt_profile_kv_t kv_hum    = {&kv_temp, "humidity",             EN_OC_MQTT_PROFILE_VALUE_INT, &hum};
        oc_mqtt_profile_kv_t kv_weight = {&kv_hum, "food_basin_weight",     EN_OC_MQTT_PROFILE_VALUE_INT, &wgt};
        oc_mqtt_profile_kv_t kv_water  = {&kv_weight, "pet_water_basin_level_percent", EN_OC_MQTT_PROFILE_VALUE_INT, &wat};

        oc_mqtt_profile_service_t svc;
        memset(&svc, 0, sizeof(svc));
        svc.service_id       = SERVICE_ID;
        svc.service_property = &kv_water;
        svc.event_time       = NULL;

        printf("[MQTT Cloud] Publishing properties...\n");
        ret = oc_mqtt_profile_propertyreport(USERNAME, &svc);
        printf("[MQTT Cloud] Publish ret=%d connected=%d\n",
               ret, oc_mqtt_is_connected());

        if (ret == 0) {
            fail_count = 0; // 上报成功, 重置失败计数
        }

        LOS_Msleep(REPORT_PERIOD); // 等待 6 秒后再次上报
    }
}
