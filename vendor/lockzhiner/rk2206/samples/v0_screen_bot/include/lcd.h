/*
 * Copyright (c) 2022 FuZhou Lockzhiner Electronic Co., Ltd. All rights reserved.
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#ifndef _LCD_H_
#define _LCD_H_

#include <stdint.h>

/* 设置横屏或者竖屏显示 0或1为竖屏 2或3为横屏 */
#define USE_HORIZONTAL      3

/* 根据LCD是横屏或者竖屏，设置LCD的宽度和高度 */
#if ((USE_HORIZONTAL==0) || (USE_HORIZONTAL==1))
#define LCD_W 240
#define LCD_H 320
#else
#define LCD_W 320
#define LCD_H 240
#endif

/* 画笔颜色 - 16位 RGB565 格式 */
#define LCD_WHITE           0xFFFF
#define LCD_BLACK           0x0000
#define LCD_BLUE            0x001F
#define LCD_BRED            0XF81F
#define LCD_GRED            0XFFE0
#define LCD_GBLUE           0X07FF
#define LCD_RED             0xF800
#define LCD_MAGENTA         0xF81F
#define LCD_GREEN           0x07E0
#define LCD_CYAN            0x7FFF
#define LCD_YELLOW          0xFFE0
#define LCD_BROWN           0XBC40 // 棕色
#define LCD_BRRED           0XFC07 // 棕红色
#define LCD_GRAY            0X8430 // 灰色
#define LCD_DARKBLUE        0X01CF // 深蓝色
#define LCD_LIGHTBLUE       0X7D7C // 浅蓝色
#define LCD_GRAYBLUE        0X5458 // 灰蓝色
#define LCD_LIGHTGREEN      0X841F // 浅绿色
#define LCD_LGRAY           0XC618 // 浅灰色(PANNEL),窗体背景色
#define LCD_LGRAYBLUE       0XA651 // 浅灰蓝色(中间层颜色)
#define LCD_LBBLUE          0X2B12 // 浅棕蓝色(选择条目的反色)


/***************************************************************
 * @brief  LCD硬件初始化
 * @note   配置SPI引脚、ST7789驱动寄存器、颜色通道(RGB/BGR)及扫描方向。
 * @return 0表示成功，非0值表示硬件初始化失败对应的代码行号
 ***************************************************************/
unsigned int lcd_init();

/***************************************************************
 * @brief  LCD硬件注销
 * @note   释放 LCD 所占用的硬件 GPIO 和 SPI 总线资源。
 * @return 0表示成功
 ***************************************************************/
unsigned int lcd_deinit();

/***************************************************************
 * @brief  指定区域填充纯色 (硬件SPI极速优化版)
 * @note   利用内存缓冲区和硬件 DMA 机制，实现无缝极速色块覆盖。
 * @param  xsta:  矩形区域左上角 X 坐标
 * @param  ysta:  矩形区域左上角 Y 坐标
 * @param  xend:  矩形区域右下角 X 坐标
 * @param  yend:  矩形区域右下角 Y 坐标
 * @param  color: 填充的 RGB565 16位颜色值 (例如 LCD_RED)
 * @return 无
 ***************************************************************/
void lcd_fill(uint16_t xsta, uint16_t ysta, uint16_t xend, uint16_t yend, uint16_t color);

/***************************************************************
 * @brief  在屏幕指定坐标画一个单像素点
 * @note   由于频繁发送寻址指令，速度较慢，仅适合算法微调使用。
 * @param  x:     点的 X 坐标
 * @param  y:     点的 Y 坐标
 * @param  color: 点的 RGB565 颜色值
 * @return 无
 ***************************************************************/
void lcd_draw_point(uint16_t x, uint16_t y, uint16_t color);

/***************************************************************
 * @brief  画任意两点间的直线
 * @note   内部已做智能优化：若是纯水平或垂直线，会自动降维使用极速的 lcd_fill。
 * @param  x1:    起点 X 坐标
 * @param  y1:    起点 Y 坐标
 * @param  x2:    终点 X 坐标
 * @param  y2:    终点 Y 坐标
 * @param  color: 直线的 RGB565 颜色值
 * @return 无
 ***************************************************************/
void lcd_draw_line(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, uint16_t color);

/***************************************************************
 * @brief  画一个空心的矩形框
 * @note   四周边框全部启用硬件 SPI 极速填充优化，非常适合画 UI 菜单边框。
 * @param  x1:    矩形左上角 X 坐标
 * @param  y1:    矩形左上角 Y 坐标
 * @param  x2:    矩形右下角 X 坐标
 * @param  y2:    矩形右下角 Y 坐标
 * @param  color: 边框的 RGB565 颜色值
 * @return 无
 ***************************************************************/
void lcd_draw_rectangle(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, uint16_t color);

/***************************************************************
 * @brief  指定位置画空心椭圆
 * @param  x0:    椭圆的中心点 X 坐标
 * @param  y0:    椭圆的中心点 Y 坐标
 * @param  rx:    椭圆的横向半轴长
 * @param  ry:    椭圆的纵向半轴长
 * @param  color: 边框的 RGB565 颜色值
 * @return 无
 ***************************************************************/
void lcd_draw_ellipse(uint16_t x0, uint16_t y0, uint16_t rx, uint16_t ry, uint16_t color);

/***************************************************************
 * @brief  指定位置画空心正圆
 * @param  x0:    圆心 X 坐标
 * @param  y0:    圆心 Y 坐标
 * @param  r:     圆的半径
 * @param  color: 圆圈边框的 RGB565 颜色值
 * @return 无
 ***************************************************************/
void lcd_draw_circle(uint16_t x0, uint16_t y0, uint8_t r, uint16_t color);

/***************************************************************
 * @brief  极速显示 UTF-8 中文字符串
 * @note   非叠加模式下启用局部内存画布直推，速度呈指数级提升。
 * @param  x:     字符串左上角起始 X 坐标
 * @param  y:     字符串左上角起始 Y 坐标
 * @param  s:     要显示的 UTF-8 中文字符串指针
 * @param  fc:    字体颜色 (Foreground Color)
 * @param  bc:    背景颜色 (Background Color)
 * @param  sizey: 字体大小，仅支持字库中含有的尺寸: 12, 16, 24, 32
 * @param  mode:  叠加模式: 0=非叠加(连带背景色一起渲染, 极速), 1=叠加(透明底色逐点渲染)
 * @return 无
 ***************************************************************/
void lcd_show_chinese(uint16_t x, uint16_t y, uint8_t *s, uint16_t fc, uint16_t bc, uint8_t sizey, uint8_t mode);

/***************************************************************
 * @brief  极速显示单个 ASCII 字符
 * @note   非叠加模式下启用局部内存画布直推。
 * @param  x:     字符左上角 X 坐标
 * @param  y:     字符左上角 Y 坐标
 * @param  num:   要显示的字符 (如 'A')
 * @param  fc:    字体颜色
 * @param  bc:    背景颜色
 * @param  sizey: 字体大小: 12, 16, 24, 32
 * @param  mode:  叠加模式: 0=非叠加(极速), 1=叠加(透明背景)
 * @return 无
 ***************************************************************/
void lcd_show_char(uint16_t x, uint16_t y, uint8_t num, uint16_t fc, uint16_t bc, uint8_t sizey, uint8_t mode);

/***************************************************************
 * @brief  连续显示一串英文字符串
 * @param  x:     起点 X 坐标
 * @param  y:     起点 Y 坐标
 * @param  p:     英文字符串指针 (如 "Hello")
 * @param  fc:    字体颜色
 * @param  bc:    背景颜色
 * @param  sizey: 字体大小: 12, 16, 24, 32
 * @param  mode:  叠加模式: 0=非叠加(极速), 1=叠加(透明背景)
 * @return 无
 ***************************************************************/
void lcd_show_string(uint16_t x, uint16_t y, const uint8_t *p, uint16_t fc, uint16_t bc, uint8_t sizey, uint8_t mode);

/***************************************************************
 * @brief  在屏幕上显示一个无符号整数
 * @param  x:     起始 X 坐标
 * @param  y:     起始 Y 坐标
 * @param  num:   要显示的数值 (如 1234)
 * @param  len:   指定该数值占据的位数 (例如len=4, 数值为12，则前面补空格)
 * @param  fc:    字体颜色
 * @param  bc:    背景颜色
 * @param  sizey: 字体大小
 * @return 无
 ***************************************************************/
void lcd_show_int_num(uint16_t x, uint16_t y, uint16_t num, uint8_t len, uint16_t fc, uint16_t bc, uint8_t sizey);

/***************************************************************
 * @brief  显示带有两位小数的浮点数 (如 3.14)
 * @param  x:     起始 X 坐标
 * @param  y:     起始 Y 坐标
 * @param  num:   浮点数值
 * @param  len:   整体显示的字符总长度 (包含小数点)
 * @param  fc:    字体颜色
 * @param  bc:    背景颜色
 * @param  sizey: 字体大小
 * @return 无
 ***************************************************************/
void lcd_show_float_num1(uint16_t x, uint16_t y, float num, uint8_t len, uint16_t fc, uint16_t bc, uint8_t sizey);

/***************************************************************
 * @brief  高效显示图像数组 (使用 Image2Lcd 转换的 C 数组)
 * @note   [极其重要]：内部使用硬件 SPI + DMA 机制按行批量直推内存，
 * 极大提升了图片渲染帧率，告别肉眼可见的扫描线撕裂感。
 * @param  x:      图片显示的左上角起始 X 坐标
 * @param  y:      图片显示的左上角起始 Y 坐标
 * @param  length: 图片的宽度 (X轴跨度)
 * @param  width:  图片的高度 (Y轴跨度)
 * @param  pic:    图片像素数据数组首地址
 * @return 无
 ***************************************************************/
void lcd_show_picture(uint16_t x, uint16_t y, uint16_t length, uint16_t width, const uint8_t *pic);

#endif /* _LCD_H_ */