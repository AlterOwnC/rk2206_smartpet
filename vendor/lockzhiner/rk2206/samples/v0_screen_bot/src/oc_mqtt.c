#include <string.h>
#include <stdio.h>
#include "MQTTClient.h"
#include <unistd.h>

#include "cJSON.h"

#include "cmsis_os2.h"

#include <oc_mqtt.h>
#include <oc_mqtt_profile_package.h>
#include "lz_hardware.h"

typedef struct
{
    char                        *device_id;
    fn_oc_mqtt_profile_rcvdeal   rcvfunc;
}oc_mqtt_profile_cb_t;

static oc_mqtt_profile_cb_t s_oc_mqtt_profile_cb;


static char init_ok = FALSE;
static MQTTClient mq_client;
struct bp_oc_info oc_info;
struct oc_device
{
    struct bp_oc_info *oc_info;

    void(*cmd_rsp_cb)(uint8_t *recv_data, size_t recv_size, uint8_t **resp_data, size_t *resp_size);

} oc_mqtt;

/*
 * 从 topic 中提取 request_id
 * IoTDA 格式: $oc/devices/{id}/sys/commands/request_id={rid}
 */
static int extract_rid(const char *data, int len, char *rid_out, int rid_size)
{
    const char *key = "request_id=";
    int key_len = strlen(key);
    for (int i = 0; i <= len - key_len; i++) {
        if (memcmp(&data[i], key, key_len) == 0) {
            int j = 0;
            i += key_len;
            while (i < len && data[i] && j < rid_size - 1)
                rid_out[j++] = data[i++];
            rid_out[j] = '\0';
            return 0;
        }
    }
    return -1;
}

/*
 * msgHandler — 兼容老平台 ($crsp/) 与新 IoTDA ($oc/devices/) 格式
 */
void msgHandler(MessageData *msg_data)
{
    size_t res_len = 0;
    uint8_t *response_buf = NULL;

    if (!msg_data || !msg_data->topicName) return;

    const char *topic_data = msg_data->topicName->lenstring.data;
    int topic_len = msg_data->topicName->lenstring.len;

    char rsp_topic[256] = {0};

    /* 判断 IoTDA 格式: $oc/devices/{id}/sys/commands/ */
    if (strstr(topic_data, "$oc/devices/") &&
        strstr(topic_data, "/sys/commands/")) {
        char request_id[64] = {0};
        if (extract_rid(topic_data, topic_len,
                        request_id, sizeof(request_id)) == 0) {
            // 从 topic 中提取 device_id
            const char *dev_prefix = "$oc/devices/";
            const char *dev_start = topic_data + strlen(dev_prefix);
            const char *dev_end = strstr(dev_start, "/sys/commands/");
            char device_id[128] = {0};
            int dl = dev_end - dev_start;
            if (dl > 127) dl = 127;
            memcpy(device_id, dev_start, dl);

            snprintf(rsp_topic, sizeof(rsp_topic),
                     "$oc/devices/%s/sys/commands/response/request_id=%s",
                     device_id, request_id);
        }
    } else if (strncmp(topic_data, "$crsp/", 6) == 0) {
        // 老平台兼容
        strcpy(rsp_topic, "$crsp/");
        strncat(rsp_topic, topic_data + 6, topic_len - 6);
    }

    if (oc_mqtt.cmd_rsp_cb != NULL) {
        oc_mqtt.cmd_rsp_cb((uint8_t *)msg_data->message->payload,
                           msg_data->message->payloadlen,
                           &response_buf, &res_len);
    }

    if (response_buf && res_len > 0) {
        if (rsp_topic[0] != '\0') {
            oc_mqtt_publish(rsp_topic, response_buf,
                            strlen((const char *)response_buf),
                            (int)en_mqtt_al_qos_1);
            printf("[MQTT] Rsp sent to %s\n", rsp_topic);
        } else {
            printf("[MQTT] WARN: no rsp_topic, discarding response\n");
        }
        free(response_buf);
    }
}

unsigned char *oc_mqtt_buf;
unsigned char *oc_mqtt_readbuf;
int buf_size;

Network n;
MQTTPacket_connectData data = MQTTPacket_connectData_initializer;

static int oc_mqtt_entry(void)
{
    int rc = 0;
    int retry_count = 0;

    NetworkInit(&n);

connect_retry:
    printf("[MQTT] Connecting to %s:%d (retry=%d)...\n",
           OC_SERVER_IP, OC_SERVER_PORT, retry_count);
    rc = NetworkConnect(&n, OC_SERVER_IP, OC_SERVER_PORT);
    if (0 != rc) {
        printf("[MQTT] NetworkConnect %s:%d FAIL rc=%d, retrying in 5s...\n",
               OC_SERVER_IP, OC_SERVER_PORT, rc);
        retry_count++;
        osDelay(5000);
        goto connect_retry;
    }
    printf("[MQTT] TCP connected to %s:%d OK\n", OC_SERVER_IP, OC_SERVER_PORT);

    buf_size  = 2048;
    oc_mqtt_buf = (unsigned char *) malloc(buf_size);
    oc_mqtt_readbuf = (unsigned char *) malloc(buf_size);
    if (!(oc_mqtt_buf && oc_mqtt_readbuf)) {
        printf("[MQTT] FATAL: No memory for MQTT buffer!\n");
        return -2;
    }

    MQTTClientInit(&mq_client, &n, 5000, oc_mqtt_buf, buf_size, oc_mqtt_readbuf, buf_size);
    printf("[MQTT] Client init OK, cmd_timeout_ms=%d\n", 5000);

    mq_client.defaultMessageHandler = msgHandler;

    data.keepAliveInterval = 30;
    data.cleansession = 1;
    data.clientID.cstring = oc_info.client_id;
    data.username.cstring = oc_info.username;
    data.password.cstring = oc_info.password;
    data.MQTTVersion = 3;
    printf("[MQTT] Sending CONNECT: clientID=%s keepAlive=%d cleanSession=%d\n",
           oc_info.client_id, data.keepAliveInterval, data.cleansession);

    rc = MQTTConnect(&mq_client, &data);
    if (0 != rc) {
        printf("[MQTT] MQTTConnect FAIL rc=%d, retry=%d\n", rc, retry_count);
        free(oc_mqtt_buf);
        free(oc_mqtt_readbuf);
        oc_mqtt_buf = NULL;
        oc_mqtt_readbuf = NULL;
        NetworkDisconnect(&n);
        retry_count++;
        if (retry_count < 3) {
            osDelay(5000);
            goto connect_retry;
        }
        printf("[MQTT] Too many MQTTConnect failures, giving up\n");
        return -1;
    }

    printf("[MQTT] MQTT CONNECTED to Huawei IoTDA successfully!\n");

    init_ok = 1;
    return 0;
}


void device_info_init(char *client_id, char * username, char *password)
{
    oc_info.user_device_id_flg = 1;
    snprintf(oc_info.client_id, sizeof(oc_info.client_id), "%s", client_id);
    snprintf(oc_info.username,    sizeof(oc_info.username),    "%s", username);
    snprintf(oc_info.password,   sizeof(oc_info.password),   "%s", password);
    printf("[MQTT] device_info: client_id=%s username=%s\n",
           oc_info.client_id, oc_info.username);
}

int oc_mqtt_init(void)
{
    if (init_ok) {
        printf("[MQTT] Already initialized\n");
        return 0;
    }
    return oc_mqtt_entry();
}

int oc_mqtt_is_connected(void)
{
    return MQTTIsConnected(&mq_client);
}

int oc_mqtt_reconnect(void)
{
    printf("[MQTT] ===== Reconnect attempt =====\n");
    // 不在这里 free 旧缓冲区: MQTTRun 后台线程可能还在通过 mq_client
    // 使用旧缓冲区, free 会导致 use-after-free 堆损坏。
    // 置 NULL 让 oc_mqtt_entry() 分配新缓冲区, 旧缓冲区泄漏 4KB
    // (仅重连时发生, 频率极低, 安全优先于内存)
    oc_mqtt_buf = NULL;
    oc_mqtt_readbuf = NULL;
    NetworkDisconnect(&n);
    init_ok = 0;
    return oc_mqtt_entry();
}

void oc_set_cmd_rsp_cb(void (*cmd_rsp_cb)(uint8_t *recv_data, uint32_t recv_size, uint8_t **resp_data, uint32_t *resp_size))
{
    oc_mqtt.cmd_rsp_cb = cmd_rsp_cb;
}


int oc_mqtt_publish(char  *topic,uint8_t *msg,int msg_len,int qos)
{
    MQTTMessage message;

    if (!oc_mqtt_is_connected()) {
        printf("[MQTT] Publish FAIL: not connected! topic=%s\n", topic);
        return -1;
    }

    message.qos = qos;
    message.retained = 0;
    message.payload = (void *) msg;
    message.payloadlen = msg_len;

    printf("[MQTT] Publish topic=%s len=%d qos=%d\n", topic, msg_len, qos);
    int rc = MQTTPublish(&mq_client, topic, &message);
    printf("[MQTT] Publish rc=%d is_connected=%d\n", rc, oc_mqtt_is_connected());
    if (rc < 0) {
        printf("[MQTT] Publish FAIL: rc=%d (connection may be dead)\n", rc);
        return -1;
    }

    return 0;
}

int oc_mqtt_yield(int timeout_ms)
{
    return MQTTYield(&mq_client, timeout_ms);
}

static char *topic_make(char *fmt, char *device_id, char *request_id)
{
    int len;
    char *ret = NULL;

    if(NULL == device_id) {
        return ret;
    }
    len = strlen(fmt) + strlen(device_id) + 8;
    if(NULL != request_id) {
        len += strlen(request_id);
    }

    ret = malloc(len);
    if(NULL != ret) {
        (void) snprintf(ret, len, fmt, device_id, request_id);
    }
    return ret;
}

#define CN_OC_MQTT_PROFILE_MSGUP_TOPICFMT   "$oc/devices/%s/sys/messages/up"
int oc_mqtt_profile_msgup(char *deviceid,oc_mqtt_profile_msgup_t *payload)
{
    int ret = (int)en_oc_mqtt_err_parafmt;
    char *topic;
    char *msg;

    if(NULL == deviceid) {
        if(NULL == s_oc_mqtt_profile_cb.device_id) {
            return ret;
        } else {
            deviceid = s_oc_mqtt_profile_cb.device_id;
        }
    }

    if((NULL == payload) || (NULL == payload->msg)) {
        return ret;
    }

    topic = topic_make(CN_OC_MQTT_PROFILE_MSGUP_TOPICFMT, deviceid,NULL);
    msg = oc_mqtt_profile_package_msgup(payload);

    if((NULL != topic) && (NULL != msg)) {
        ret = oc_mqtt_publish(topic,(uint8_t *)msg,strlen(msg),(int)en_mqtt_al_qos_1);
    } else {
        ret = (int)en_oc_mqtt_err_sysmem;
    }

    free(topic);
    free(msg);

    return ret;
}

#define CN_OC_MQTT_PROFILE_PROPERTYREPORT_TOPICFMT   "$oc/devices/%s/sys/properties/report"
int oc_mqtt_profile_propertyreport(char *deviceid,oc_mqtt_profile_service_t *payload)
{
    int ret = (int)en_oc_mqtt_err_parafmt;
    char *topic;
    char *msg;

    if(NULL == deviceid) {
        if(NULL == s_oc_mqtt_profile_cb.device_id) {
            printf("[OC] prop report err: no deviceid\n");
            return ret;
        } else {
            deviceid = s_oc_mqtt_profile_cb.device_id;
        }
    }

    if((NULL== payload) || (NULL== payload->service_id) || (NULL == payload->service_property)) {
        printf("[OC] prop report err: null payload/svc_id/svc_prop p=%p id=%s prop=%p\n",
               (void*)payload, payload?payload->service_id:"(null)",
               payload?(void*)payload->service_property:NULL);
        return ret;
    }

    topic = topic_make(CN_OC_MQTT_PROFILE_PROPERTYREPORT_TOPICFMT, deviceid,NULL);
    if(!topic) printf("[MQTT] prop report FAIL: topic_make NULL (deviceid=%s)\n", deviceid);

    msg = oc_mqtt_profile_package_propertyreport(payload);
    if(!msg) printf("[MQTT] prop report FAIL: package NULL\n");

    if((NULL != topic) && (NULL != msg)) {
        printf("[MQTT] prop report: topic=%s json=%s\n", topic, msg);
        // QOS0: 周期性传感器上报不需要 PUBACK 确认, 避免网络延迟导致阻塞
        ret = oc_mqtt_publish(topic,(uint8_t *)msg,strlen(msg),(int)en_mqtt_al_qos_0);
        if (ret != 0) {
            printf("[MQTT] prop report: Publish FAILED ret=%d\n", ret);
        }
    } else {
        ret = (int)en_oc_mqtt_err_sysmem;
        printf("[MQTT] prop report FAIL: topic=%p msg=%p\n", (void*)topic, (void*)msg);
    }

    free(topic);
    free(msg);

    return ret;
}

#define CN_OC_MQTT_PROFILE_GWPROPERTYREPORT_TOPICFMT   "$oc/devices/%s/sys/gateway/sub_devices/properties/report"
int oc_mqtt_profile_gwpropertyreport(char *deviceid,oc_mqtt_profile_device_t *payload)
{
    int ret = (int)en_oc_mqtt_err_parafmt;
    char *topic;
    char *msg;

    if(NULL == deviceid) {
        if(NULL == s_oc_mqtt_profile_cb.device_id) {
            return ret;
        } else {
            deviceid = s_oc_mqtt_profile_cb.device_id;
        }
    }

    if((NULL== payload) || (NULL == payload->subdevice_id)||(NULL== payload->subdevice_property) ||\
       (NULL== payload->subdevice_property->service_id)||(NULL== payload->subdevice_property->service_property)) {
        return ret;
    }

    topic = topic_make(CN_OC_MQTT_PROFILE_GWPROPERTYREPORT_TOPICFMT, deviceid,NULL);
    msg = oc_mqtt_profile_package_gwpropertyreport(payload);

    if((NULL != topic) && (NULL != msg)) {
        ret = oc_mqtt_publish(topic,(uint8_t *)msg,strlen(msg),(int)en_mqtt_al_qos_1);
    } else {
        ret = (int)en_oc_mqtt_err_sysmem;
    }

    free(topic);
    free(msg);

    return ret;
}


#define CN_OC_MQTT_PROFILE_ROPERTYSETRESP_TOPICFMT   "$oc/devices/%s/sys/properties/set/response/request_id=%s"
int oc_mqtt_profile_propertysetresp(char *deviceid,oc_mqtt_profile_propertysetresp_t *payload)
{
    int ret = (int)en_oc_mqtt_err_parafmt;
    char *topic;
    char *msg;

    if(NULL == deviceid) {
        if(NULL == s_oc_mqtt_profile_cb.device_id) {
            return ret;
        } else {
            deviceid = s_oc_mqtt_profile_cb.device_id;
        }
    }

    if((NULL == payload) || (NULL == payload->request_id)) {
        return ret;
    }
    topic = topic_make(CN_OC_MQTT_PROFILE_ROPERTYSETRESP_TOPICFMT, deviceid,payload->request_id);
    msg = oc_mqtt_profile_package_propertysetresp(payload);

    if((NULL != topic) && (NULL != msg)) {
        ret = oc_mqtt_publish(topic,(uint8_t *)msg,strlen(msg),(int)en_mqtt_al_qos_1);
    } else {
        ret = (int)en_oc_mqtt_err_sysmem;
    }

    free(topic);
    free(msg);

    return ret;
}


#define CN_OC_MQTT_PROFILE_ROPERTYGETRESP_TOPICFMT   "$oc/devices/%s/sys/properties/get/response/request_id=%s"
int oc_mqtt_profile_propertygetresp(char *deviceid,oc_mqtt_profile_propertygetresp_t *payload)
{
    int ret = (int)en_oc_mqtt_err_parafmt;
    char *topic;
    char *msg;

    if(NULL == deviceid) {
        if(NULL == s_oc_mqtt_profile_cb.device_id) {
            return ret;
        } else {
            deviceid = s_oc_mqtt_profile_cb.device_id;
        }
    }

    if((NULL== payload) || (NULL == payload->request_id) || \
       (NULL== payload->services->service_id) || (NULL == payload->services->service_property)) {
        return ret;
    }

    topic = topic_make(CN_OC_MQTT_PROFILE_ROPERTYGETRESP_TOPICFMT, deviceid,payload->request_id);
    msg = oc_mqtt_profile_package_propertygetresp(payload);

    if((NULL != topic) && (NULL != msg)) {
        ret = oc_mqtt_publish(topic,(uint8_t *)msg,strlen(msg),(int)en_mqtt_al_qos_1);
    } else {
        ret = (int)en_oc_mqtt_err_sysmem;
    }

    free(topic);
    free(msg);

    return ret;
}

#define CN_OC_MQTT_PROFILE_CMDRESP_TOPICFMT   "$oc/devices/%s/sys/commands/response/request_id=%s"
int oc_mqtt_profile_cmdresp(char *deviceid,oc_mqtt_profile_cmdresp_t *payload)
{
    int ret = (int)en_oc_mqtt_err_parafmt;
    char *topic;
    char *msg;

    if(NULL == deviceid) {
        if(NULL == s_oc_mqtt_profile_cb.device_id) {
            return ret;
        } else {
            deviceid = s_oc_mqtt_profile_cb.device_id;
        }
    }

    if((NULL == payload) || (NULL == payload->request_id)) {
        return ret;
    }

    topic = topic_make(CN_OC_MQTT_PROFILE_CMDRESP_TOPICFMT, deviceid,payload->request_id);
    msg = oc_mqtt_profile_package_cmdresp(payload);

    if((NULL != topic) && (NULL != msg)) {
        ret = oc_mqtt_publish(topic,(uint8_t *)msg,strlen(msg),(int)en_mqtt_al_qos_1);
    } else {
        ret = (int)en_oc_mqtt_err_sysmem;
    }

    free(topic);
    free(msg);

    return ret;
}
