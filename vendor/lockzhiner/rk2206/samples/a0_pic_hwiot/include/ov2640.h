#ifndef __OV2640_H__
#define __OV2640_H__

/* 初始化 OV2640，配置为 JPEG 输出，分辨率为 QVGA (320x240) */
int OV2640_Init(void);

/* 抓取一帧 JPEG 图像，存入 buf 中，返回实际大小 */
int OV2640_Capture_JPEG(unsigned char *buf, int max_len);

#endif /* __OV2640_H__ */