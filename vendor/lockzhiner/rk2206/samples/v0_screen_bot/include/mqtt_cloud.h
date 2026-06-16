/***************************************************************
 * 文件名: mqtt_cloud.h
 * 说    明: 华为 IoTDA MQTT 云连接模块 — 声明
 *           mqtt_cloud_thread: 优先级 7, 栈 12KB
 *           每 6s 上报传感器属性, 接收云端 motor/laser 指令
 ***************************************************************/

#ifndef _MQTT_CLOUD_H_
#define _MQTT_CLOUD_H_

/***************************************************************
 * 线程函数: mqtt_cloud_thread
 * 说    明: MQTT 云连接线程入口, 由 app_example_init 通过 LOS_TaskCreate 创建
 * 参    数:
 *       @arg: 未使用 (LiteOS 线程函数签名要求)
 ***************************************************************/
void mqtt_cloud_thread(void *arg);

#endif /* _MQTT_CLOUD_H_ */
