#ifndef LITEOS_LAB_IOT_LINK_OC_OC_MQTT_OC_MQTT_PROFILE_OC_MQTT_PROFILE_H_
#define LITEOS_LAB_IOT_LINK_OC_OC_MQTT_OC_MQTT_PROFILE_OC_MQTT_PROFILE_H_

#include <stdint.h>

#define OC_SERVER_URL              "tcp://9956d4524f.st1.iotda-device.cn-south-1.myhuaweicloud.com:1883"
#define OC_SERVER_IP                "9956d4524f.st1.iotda-device.cn-south-1.myhuaweicloud.com"
#define OC_SERVER_PORT              1883
#define OC_CLIENT_ID_LEN            128
#define OC_USERNAME_LEN             128
#define OC_PASSWORD_LEN             128

typedef enum
{
    en_msg_cmd = 0,
    en_msg_report,
} en_msg_type_t;

typedef struct
{
    char *request_id;
    char *payload;
} cmd_t;

///< UP means the device send data to the cloud
typedef enum
{
    EN_OC_MQTT_PROFILE_MSG_TYPE_UP_MSGUP = 0,
    EN_OC_MQTT_PROFILE_MSG_TYPE_UP_PROPERTYREPORT,
    EN_OC_MQTT_PROFILE_MSG_TYPE_UP_SUBPROPERTYREPORT,
    EN_OC_MQTT_PROFILE_MSG_TYPE_UP_PROPERTYSETRESPONSE,
    EN_OC_MQTT_PROFILE_MSG_TYPE_UP_PROPERTYGETRESPONSE,
    EN_OC_MQTT_PROFILE_MSG_TYPE_UP_CMDRESPONSE,
    EN_OC_MQTT_PROFILE_MSG_TYPE_UP_LAST,
}en_oc_mqtt_profile_msg_type_up_t;

///< DOWN means the cloud send data to the device
typedef enum
{
    EN_OC_MQTT_PROFILE_MSG_TYPE_DOWN_MSGDOWN = 0,
    EN_OC_MQTT_PROFILE_MSG_TYPE_DOWN_COMMANDS,
    EN_OC_MQTT_PROFILE_MSG_TYPE_DOWN_PROPERTYSET,
    EN_OC_MQTT_PROFILE_MSG_TYPE_DOWN_PROPERTYGET,
    EN_OC_MQTT_PROFILE_MSG_TYPE_DOWN_EVENT,
    EN_OC_MQTT_PROFILE_MSG_TYPE_DOWN_LAST,
}en_oc_mqtt_profile_msg_type_down_t;

////< enum all the data type for the oc profile
typedef enum
{
    EN_OC_MQTT_PROFILE_VALUE_INT = 0,
    EN_OC_MQTT_PROFILE_VALUE_LONG,
    EN_OC_MQTT_PROFILE_VALUE_FLOAT,
    EN_OC_MQTT_PROFILE_VALUE_STRING,
    EN_OC_MQTT_PROFILE_VALUE_LAST,
}en_oc_profile_data_t;

typedef struct
{
    void                 *nxt;
    char                 *key;
    en_oc_profile_data_t  type;
    void                 *value;
}oc_mqtt_profile_kv_t;

typedef struct
{
    en_oc_mqtt_profile_msg_type_down_t type;
    char *request_id;
    void *msg;
    int   msg_len;
}oc_mqtt_profile_msgrcv_t;

typedef int (*fn_oc_mqtt_profile_rcvdeal)(oc_mqtt_profile_msgrcv_t *payload);

typedef struct
{
    char *device_id;
    char *name;
    char *id;
    void *msg;
    int   msg_len;
}oc_mqtt_profile_msgup_t;

typedef enum
{
    en_mqtt_al_qos_0 = 0,
    en_mqtt_al_qos_1,
    en_mqtt_al_qos_2,
    en_mqtt_al_qos_err
}en_mqtt_al_qos_t;

typedef enum
{
    en_oc_mqtt_err_ok          = 0,
    en_oc_mqtt_err_parafmt,
    en_oc_mqtt_err_network,
    en_oc_mqtt_err_conversion,
    en_oc_mqtt_err_conclientid,
    en_oc_mqtt_err_conserver,
    en_oc_mqtt_err_conuserpwd,
    en_oc_mqtt_err_conclient,
    en_oc_mqtt_err_subscribe,
    en_oc_mqtt_err_unsubscribe,
    en_oc_mqtt_err_publish,
    en_oc_mqtt_err_configured,
    en_oc_mqtt_err_noconfigured,
    en_oc_mqtt_err_noconected,
    en_oc_mqtt_err_gethubaddrtimeout,
    en_oc_mqtt_err_sysmem,
    en_oc_mqtt_err_system,
    en_oc_mqtt_err_last,
}en_oc_mqtt_err_code_t;

struct bp_oc_info
{
    char client_id[OC_CLIENT_ID_LEN];
    char username[OC_USERNAME_LEN];
    char password[OC_PASSWORD_LEN];
    char user_device_id_flg;
};
typedef struct bp_oc_info *bp_oc_info_t;

int oc_mqtt_init(void);
int oc_mqtt_is_connected(void);
int oc_mqtt_reconnect(void);

void device_info_init(char *client_id, char * username, char *password);

void oc_set_cmd_rsp_cb(void (*cmd_rsp_cb)(uint8_t *recv_data, uint32_t recv_size, uint8_t **resp_data, uint32_t *resp_size));

int oc_mqtt_publish(char  *topic,uint8_t *msg,int msg_len,int qos);

int  oc_mqtt_yield(int timeout_ms);

int oc_mqtt_profile_msgup(char *deviceid,oc_mqtt_profile_msgup_t *payload);


typedef struct
{
   void *nxt;
   char *service_id;
   char *event_time;
   oc_mqtt_profile_kv_t *service_property;
}oc_mqtt_profile_service_t;

int oc_mqtt_profile_propertyreport(char *deviceid,oc_mqtt_profile_service_t *payload);

typedef struct
{
    void *nxt;
    char                                *subdevice_id;
    oc_mqtt_profile_service_t           *subdevice_property;
}oc_mqtt_profile_device_t;

int oc_mqtt_profile_gwpropertyreport(char *deviceid,oc_mqtt_profile_device_t *payload);

typedef struct
{
    int     ret_code;
    char   *ret_description;
    char   *request_id;
}oc_mqtt_profile_propertysetresp_t;

int oc_mqtt_profile_propertysetresp(char *deviceid,oc_mqtt_profile_propertysetresp_t *payload);

typedef struct
{
    char *request_id;
    oc_mqtt_profile_service_t  *services;
}oc_mqtt_profile_propertygetresp_t;

int oc_mqtt_profile_propertygetresp(char *deviceid,oc_mqtt_profile_propertygetresp_t *payload);

typedef struct
{
    int     ret_code;
    char   *ret_name;
    char   *request_id;
    oc_mqtt_profile_kv_t  *paras;
}oc_mqtt_profile_cmdresp_t;

int oc_mqtt_profile_cmdresp(char *deviceid,oc_mqtt_profile_cmdresp_t *payload);

#endif /* LITEOS_LAB_IOT_LINK_OC_OC_MQTT_OC_MQTT_PROFILE_OC_MQTT_PROFILE_H_ */
