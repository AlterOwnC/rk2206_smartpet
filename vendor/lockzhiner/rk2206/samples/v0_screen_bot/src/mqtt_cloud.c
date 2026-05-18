#include "mqtt_cloud.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "los_task.h"
#include "cmsis_os2.h"
#include "lz_hardware.h"
#include "config_network.h"
#include "MQTTClient.h"
#include "cJSON.h"

#include "command_queue.h"
#include "menu_ui.h"
#include "ds3231.h"
#include "oc_mqtt.h"
#include "oc_mqtt_profile_package.h"

#define REPORT_PERIOD  6000
#define YIELD_MS       200

#define CLIENT_ID      "69173ed2775d2a3818654e13_myNodeId_0_0_2026051206"
#define USERNAME       "69173ed2775d2a3818654e13_myNodeId"
#define PASSWORD       "6aba5738afd3d39b1000a18f6b0bcc65ca7423a2fc9f4489a775eb75873378dc"
#define SERVICE_ID     "smokeDetector"

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

    if (strcmp(name, "motor") == 0) {
        SystemCommand cmd = {.type = CMD_FEED, .param1 = 1};
        cmd_send(&cmd);
        printf("[MQTT] Motor cmd → queue\n");
    } else if (strcmp(name, "laser") == 0) {
        cJSON *paras = cJSON_GetObjectItem(root, "paras");
        int laser_on = 1;
        if (paras) {
            cJSON *val = cJSON_GetObjectItem(paras, "value");
            if (val && (cJSON_IsFalse(val) || (cJSON_IsNumber(val) && val->valueint == 0))) {
                laser_on = 0;
            }
        }
        if (laser_on) {
            SystemCommand cmd = {.type = CMD_CAT_MODE_ENTER, .param1 = 1};
            cmd_send(&cmd);
            printf("[MQTT] Laser ON → queue\n");
        } else {
            SystemCommand cmd = {.type = CMD_CAT_MODE_EXIT};
            cmd_send(&cmd);
            printf("[MQTT] Laser OFF → queue\n");
        }
    }

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
    }
}

void mqtt_cloud_thread(void *arg)
{
    printf("\n===== [MQTT Cloud] Thread start =====\n");
    SetWifiModeOn();

    device_info_init(CLIENT_ID, USERNAME, PASSWORD);
    oc_set_cmd_rsp_cb(cmd_rsp_cb);

    int ret = oc_mqtt_init();
    if (ret != 0) {
        printf("[MQTT Cloud] Init FAIL (ret=%d), entering retry loop...\n", ret);
        while (1) {
            LOS_Msleep(10000);
            printf("[MQTT Cloud] Retrying init...\n");
            ret = oc_mqtt_init();
            if (ret == 0) break;
        }
    }
    printf("[MQTT Cloud] Connected to Huawei IoTDA!\n");

    int fail_count = 0;
    while (1)
    {
        // 主动驱动 MQTT 协议栈: 收包/发心跳/检测断线
        oc_mqtt_yield(200);

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

        osMutexAcquire(g_sensor_mutex, osWaitForever);
        int temp = g_sensor_temp;
        int hum  = g_sensor_hum;
        int wgt  = g_sensor_weight;
        int wat  = g_sensor_water;
        osMutexRelease(g_sensor_mutex);

        printf("[MQTT Cloud] Sensors: T=%d H=%d W=%d WL=%d\n",
               temp, hum, wgt, wat);

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
            fail_count = 0;
        }

        LOS_Msleep(REPORT_PERIOD);
    }
}
