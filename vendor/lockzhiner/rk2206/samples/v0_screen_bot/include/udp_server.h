/***************************************************************
 * 文件名: udp_server.h
 * 说    明: WiFi UDP 传感器数据服务器 — 声明
 *           udp_server_thread: 优先级 6, 栈 10KB
 *           监听端口 6666, 接收 JSON 格式传感器数据并推送命令队列
 ***************************************************************/

#ifndef _UDP_SERVER_H_
#define _UDP_SERVER_H_

/***************************************************************
 * 线程函数: udp_server_thread
 * 说    明: UDP 服务器线程入口, 由 app_example_init 通过 LOS_TaskCreate 创建
 *           连接 WiFi → 创建 UDP Socket → 循环接收传感器 JSON →
 *           解析并发送 CMD_SENSOR_UPDATE 到命令队列
 * 参    数:
 *       @arg: 未使用
 ***************************************************************/
void udp_server_thread(void *arg);

#endif /* _UDP_SERVER_H_ */
