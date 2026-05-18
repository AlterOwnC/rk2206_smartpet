#ifndef _UDP_SERVER_H_
#define _UDP_SERVER_H_

// 暴露 UDP 监听与智能调度任务，供主程序创建线程
void udp_server_thread(void *arg);

#endif /* _UDP_SERVER_H_ */