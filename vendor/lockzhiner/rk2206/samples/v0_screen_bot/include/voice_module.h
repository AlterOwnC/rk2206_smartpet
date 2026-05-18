#ifndef _VOICE_MODULE_H_
#define _VOICE_MODULE_H_

// 暴露语音模块 UART2 处理任务，供主程序创建线程
void voice_uart_thread(void *arg);

#endif /* _VOICE_MODULE_H_ */