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
/***************************************************************
 * 文件名: lcd.c
 * 说    明: ST7789V SPI LCD 驱动 + 点阵字库渲染引擎
 *           SPI0 总线 (PC0=CS, PC1=CLK, PC2=MOSI, PC3=RES, PA4=DC)
 *           硬件 SPI 模式 50MHz, 行缓冲 + DMA 批量直推实现极速渲染
 *
 *           支持功能:
 *           - 矩形/直线/圆/椭圆绘图原语
 *           - 12/16/24/32 号 ASCII 字库渲染
 *           - 12/16/24/32 号中文点阵字库 (UTF-8→索引码转换)
 *           - 图片数组高效直传 (Image2Lcd 格式)
 *           - 叠加/非叠加两种渲染模式
 *
 *           横屏模式: USE_HORIZONTAL=3 (320x240, 270°)
 ***************************************************************/
#include "lz_hardware.h"
#include "lcd.h"
#include "lcd_font.h"
#include <string.h>

/* 是否启用SPI通信
 * 0 => 禁用SPI，使用gpio模拟SPI通信
 * 1 => 启用SPI (已开启极限性能，批量DMA直传)
 */
#define LCD_ENABLE_SPI      1
#define LCD_SPI_BUS         0

#define LCD_PIN_CS          GPIO0_PC0
#define LCD_PIN_CLK         GPIO0_PC1
#define LCD_PIN_MOSI        GPIO0_PC2
#define LCD_PIN_RES         GPIO0_PC3
#define LCD_PIN_DC          GPIO0_PA4

#define LCD_CS_Clr()        LzGpioSetVal(LCD_PIN_CS, LZGPIO_LEVEL_LOW)
#define LCD_CS_Set()        LzGpioSetVal(LCD_PIN_CS, LZGPIO_LEVEL_HIGH)

#define LCD_CLK_Clr()       LzGpioSetVal(LCD_PIN_CLK, LZGPIO_LEVEL_LOW)
#define LCD_CLK_Set()       LzGpioSetVal(LCD_PIN_CLK, LZGPIO_LEVEL_HIGH)

#define LCD_MOSI_Clr()      LzGpioSetVal(LCD_PIN_MOSI, LZGPIO_LEVEL_LOW)
#define LCD_MOSI_Set()      LzGpioSetVal(LCD_PIN_MOSI, LZGPIO_LEVEL_HIGH)

#define LCD_RES_Clr()       LzGpioSetVal(LCD_PIN_RES, LZGPIO_LEVEL_LOW)
#define LCD_RES_Set()       LzGpioSetVal(LCD_PIN_RES, LZGPIO_LEVEL_HIGH)

#define LCD_DC_Clr()        LzGpioSetVal(LCD_PIN_DC, LZGPIO_LEVEL_LOW)
#define LCD_DC_Set()        LzGpioSetVal(LCD_PIN_DC, LZGPIO_LEVEL_HIGH)

#if LCD_ENABLE_SPI
static SpiBusIo m_spiBus = {
    .cs =   {.gpio = GPIO0_PC0, .func = MUX_FUNC4, .type = PULL_UP, .drv = DRIVE_KEEP, .dir = LZGPIO_DIR_KEEP, .val = LZGPIO_LEVEL_KEEP},
    .clk =  {.gpio = GPIO0_PC1, .func = MUX_FUNC4, .type = PULL_UP, .drv = DRIVE_KEEP, .dir = LZGPIO_DIR_KEEP, .val = LZGPIO_LEVEL_KEEP},
    .mosi = {.gpio = GPIO0_PC2, .func = MUX_FUNC4, .type = PULL_UP, .drv = DRIVE_KEEP, .dir = LZGPIO_DIR_KEEP, .val = LZGPIO_LEVEL_KEEP},
    .miso = {.gpio = INVALID_GPIO, .func = MUX_FUNC4, .type = PULL_UP, .drv = DRIVE_KEEP, .dir = LZGPIO_DIR_KEEP, .val = LZGPIO_LEVEL_KEEP},
    .id = FUNC_ID_SPI0,
    .mode = FUNC_MODE_M1,
};

static LzSpiConfig m_spiConf = {.bitsPerWord = SPI_PERWORD_8BITS, .firstBit = SPI_MSB, .mode = SPI_MODE_3,
                               .csm = SPI_CMS_ONE_CYCLES, .speed = 50000000, .isSlave = false};
#endif

/////////////////////////////////////////////////////////////////
// 内部静态函数区 (不对外暴露，主要用于底层总线通信)
/////////////////////////////////////////////////////////////////

static void lcd_write_bus(uint8_t dat)
{
#if LCD_ENABLE_SPI
    LzSpiWrite(LCD_SPI_BUS, 0, &dat, 1);
#else
    uint8_t i;
    LCD_CS_Clr();
    for (i=0; i<8; i++) {
        LCD_CLK_Clr();
        if (dat & 0x80) LCD_MOSI_Set();
        else LCD_MOSI_Clr();
        LCD_CLK_Set();
        dat<<=1;
    }   
    LCD_CS_Set();
#endif
}

static void lcd_wr_data8(uint8_t dat) { lcd_write_bus(dat); }

static void lcd_wr_data(uint16_t dat)
{
    lcd_write_bus(dat >> 8);
    lcd_write_bus(dat);
}

static void lcd_wr_reg(uint8_t dat)
{
    LCD_DC_Clr();
    lcd_write_bus(dat);
    LCD_DC_Set();
}

static void lcd_address_set(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2)
{
    lcd_wr_reg(0x2a); // 列地址设置
    lcd_wr_data(x1);
    lcd_wr_data(x2);
    lcd_wr_reg(0x2b); // 行地址设置
    lcd_wr_data(y1);
    lcd_wr_data(y2);
    lcd_wr_reg(0x2c); // 准备写入显存
}

static uint32_t mypow(uint8_t m, uint8_t n)
{
    uint32_t result = 1;
    while (n--) result *= m;
    return result;
}

// ========= 内部点阵字库渲染函数 =========
static void lcd_show_chinese_12x12(uint16_t x, uint16_t y, uint8_t *s, uint16_t fc, uint16_t bc, uint8_t sizey, uint8_t mode)
{
    uint8_t i, j, m=0;
    uint16_t k;
    uint16_t HZnum = sizeof(tfont12) / sizeof(typFNT_GB12);
    uint16_t TypefaceNum = (sizey/8+((sizey%8)?1:0)) * sizey;
    uint16_t x0 = x;

    for (k = 0; k < HZnum; k++) {
        if ((tfont12[k].Index[0] == *(s)) && (tfont12[k].Index[1] == *(s+1))) {
            if (!mode) {
                // 非叠加模式：局部内存画布极速渲染
                static uint16_t char_buf[144]; // 12x12 (static 节省栈空间)
                uint16_t idx = 0;
                uint16_t fc_swp = (fc >> 8) | (fc << 8);
                uint16_t bc_swp = (bc >> 8) | (bc << 8);
                
                for (i = 0; i < TypefaceNum; i++) {
                    for (j = 0; j < 8; j++) {
                        if (tfont12[k].Msk[i] & (0x01<<j)) char_buf[idx++] = fc_swp;
                        else char_buf[idx++] = bc_swp;
                        m++;
                        if ((m % sizey) == 0) { m = 0; break; }
                    }
                }
                lcd_address_set(x, y, x+sizey-1, y+sizey-1);
#if LCD_ENABLE_SPI
                LzSpiWrite(LCD_SPI_BUS, 0, (uint8_t *)char_buf, idx * 2);
#else
                for(i=0; i<idx; i++) lcd_wr_data((char_buf[i]>>8)|(char_buf[i]<<8));
#endif
            } else {
                // 叠加模式：保留透明背景的单点绘制
                for (i = 0; i < TypefaceNum; i++) {
                    for (j = 0; j < 8; j++) {
                        if (tfont12[k].Msk[i] & (0x01<<j)) lcd_draw_point(x,y,fc);
                        x++;
                        if ((x - x0) == sizey) { x = x0; y++; break; }
                    }
                }
            }
            break;
        }
    }
} 

static void lcd_show_chinese_16x16(uint16_t x, uint16_t y, uint8_t *s, uint16_t fc, uint16_t bc, uint8_t sizey, uint8_t mode)
{
    uint8_t i, j, m = 0;
    uint16_t k;
    uint16_t HZnum = sizeof(tfont16) / sizeof(typFNT_GB16);
    uint16_t TypefaceNum = (sizey/8 + ((sizey%8)?1:0)) * sizey;
    uint16_t x0 = x;

    for (k = 0; k < HZnum; k++) {
        if ((tfont16[k].Index[0] == *(s)) && (tfont16[k].Index[1] == *(s+1))) {
            if (!mode) {
                static uint16_t char_buf[256]; // 16x16 (static 节省栈空间)
                uint16_t idx = 0;
                uint16_t fc_swp = (fc >> 8) | (fc << 8);
                uint16_t bc_swp = (bc >> 8) | (bc << 8);
                
                for (i = 0; i < TypefaceNum; i++) {
                    for (j = 0; j < 8; j++) {
                        if (tfont16[k].Msk[i] & (0x01 << j)) char_buf[idx++] = fc_swp;
                        else char_buf[idx++] = bc_swp;
                        m++;
                        if (m%sizey == 0) { m = 0; break; }
                    }
                }
                lcd_address_set(x, y, x+sizey-1, y+sizey-1);
#if LCD_ENABLE_SPI
                LzSpiWrite(LCD_SPI_BUS, 0, (uint8_t *)char_buf, idx * 2);
#else
                for(i=0; i<idx; i++) lcd_wr_data((char_buf[i]>>8)|(char_buf[i]<<8));
#endif
            } else {
                for (i = 0; i < TypefaceNum; i++) {
                    for (j = 0; j < 8; j++) {
                        if (tfont16[k].Msk[i] & (0x01<<j)) lcd_draw_point(x,y,fc);
                        x++;
                        if ((x - x0) == sizey) { x = x0; y++; break; }
                    }
                }
            }
            break;
        }
    }
} 

static void lcd_show_chinese_24x24(uint16_t x, uint16_t y, uint8_t *s, uint16_t fc, uint16_t bc, uint8_t sizey, uint8_t mode)
{
    uint8_t i,j,m = 0;
    uint16_t k;
    uint16_t HZnum = sizeof(tfont24) / sizeof(typFNT_GB24);
    uint16_t TypefaceNum = (sizey/8 + ((sizey%8)?1:0)) * sizey;
    uint16_t x0 = x;

    for (k = 0; k < HZnum; k++) {
        if ((tfont24[k].Index[0] == *(s)) && (tfont24[k].Index[1] == *(s+1))) {
            if (!mode) {
                static uint16_t char_buf[576]; // 24x24 (static 节省栈空间)
                uint16_t idx = 0;
                uint16_t fc_swp = (fc >> 8) | (fc << 8);
                uint16_t bc_swp = (bc >> 8) | (bc << 8);
                for (i = 0; i < TypefaceNum; i++) {
                    for (j = 0; j < 8; j++) {
                        if (tfont24[k].Msk[i] & (0x01<<j)) char_buf[idx++] = fc_swp;
                        else char_buf[idx++] = bc_swp;
                        m++;
                        if (m%sizey == 0) { m = 0; break; }
                    }
                }
                lcd_address_set(x, y, x+sizey-1, y+sizey-1);
#if LCD_ENABLE_SPI
                LzSpiWrite(LCD_SPI_BUS, 0, (uint8_t *)char_buf, idx * 2);
#else
                for(i=0; i<idx; i++) lcd_wr_data((char_buf[i]>>8)|(char_buf[i]<<8));
#endif
            } else {
                for (i = 0; i < TypefaceNum; i++) {
                    for (j = 0; j < 8; j++) {
                        if (tfont24[k].Msk[i] & (0x01 << j)) lcd_draw_point(x, y, fc);
                        x++;
                        if ((x - x0) == sizey) { x = x0; y++; break; }
                    }
                }
            }
            break;
        }
    }
} 

static void lcd_show_chinese_32x32(uint16_t x, uint16_t y, uint8_t *s, uint16_t fc, uint16_t bc, uint8_t sizey, uint8_t mode)
{
    uint8_t i,j,m = 0;
    uint16_t k;
    uint16_t HZnum = sizeof(tfont32) / sizeof(typFNT_GB32);
    uint16_t TypefaceNum = (sizey/8 + ((sizey%8)?1:0)) * sizey;
    uint16_t x0 = x;

    for (k = 0; k < HZnum; k++) {
        if ((tfont32[k].Index[0] == *(s)) && (tfont32[k].Index[1] == *(s+1))) {
            if (!mode) {
                static uint16_t char_buf[1024]; // 32x32 (static 节省栈空间)
                uint16_t idx = 0;
                uint16_t fc_swp = (fc >> 8) | (fc << 8);
                uint16_t bc_swp = (bc >> 8) | (bc << 8);
                for (i = 0; i < TypefaceNum; i++) {
                    for (j = 0; j < 8; j++) {
                        if (tfont32[k].Msk[i] & (0x01 << j)) char_buf[idx++] = fc_swp;
                        else char_buf[idx++] = bc_swp;
                        m++;
                        if (m%sizey == 0) { m = 0; break; }
                    }
                }
                lcd_address_set(x, y, x+sizey-1, y+sizey-1);
#if LCD_ENABLE_SPI
                LzSpiWrite(LCD_SPI_BUS, 0, (uint8_t *)char_buf, idx * 2);
#else
                for(i=0; i<idx; i++) lcd_wr_data((char_buf[i]>>8)|(char_buf[i]<<8));
#endif
            } else {
                for (i = 0; i < TypefaceNum; i++) {
                    for (j = 0; j < 8; j++) {
                        if (tfont32[k].Msk[i] & (0x01 << j)) lcd_draw_point(x, y, fc);
                        x++;
                        if ((x-x0) == sizey) { x = x0; y++; break; }
                    }
                }
            }
            break;
        }
    }
}

/////////////////////////////////////////////////////////////////
// 外部调用API函数区 (带有详细参数说明)
/////////////////////////////////////////////////////////////////

/***************************************************************
 * 函数名称: lcd_init
 * 说    明: LCD硬件初始化，配置SPI引脚及ST7789驱动寄存器
 * 参    数: 无
 * 返 回 值: unsigned int (0表示成功，非0表示硬件初始化失败对应的代码行号)
 ***************************************************************/
unsigned int lcd_init()
{
#if LCD_ENABLE_SPI
    LzSpiDeinit(LCD_SPI_BUS);
    if (SpiIoInit(m_spiBus) != LZ_HARDWARE_SUCCESS) return __LINE__;
    if (LzSpiInit(LCD_SPI_BUS, m_spiConf) != LZ_HARDWARE_SUCCESS) return __LINE__;
#else
    LzGpioInit(LCD_PIN_CS); LzGpioSetDir(LCD_PIN_CS, LZGPIO_DIR_OUT); LzGpioSetVal(LCD_PIN_CS, LZGPIO_LEVEL_HIGH);
    LzGpioInit(LCD_PIN_CLK); LzGpioSetDir(LCD_PIN_CLK, LZGPIO_DIR_OUT); LzGpioSetVal(LCD_PIN_CLK, LZGPIO_LEVEL_LOW);
    LzGpioInit(LCD_PIN_MOSI); LzGpioSetDir(LCD_PIN_MOSI, LZGPIO_DIR_OUT); LzGpioSetVal(LCD_PIN_MOSI, LZGPIO_LEVEL_LOW);
#endif
    LzGpioInit(LCD_PIN_RES); LzGpioSetDir(LCD_PIN_RES, LZGPIO_DIR_OUT); LzGpioSetVal(LCD_PIN_RES, LZGPIO_LEVEL_HIGH);
    LzGpioInit(LCD_PIN_DC); LzGpioSetDir(LCD_PIN_DC, LZGPIO_DIR_OUT); LzGpioSetVal(LCD_PIN_DC, LZGPIO_LEVEL_LOW);

    /* 硬件复位 */
    LCD_RES_Clr(); LOS_Msleep(100);
    LCD_RES_Set(); LOS_Msleep(100); LOS_Msleep(500);
    
    /* 退出休眠 */
    lcd_wr_reg(0x11);
    LOS_Msleep(100);
    
    /* 显存扫描方向与颜色通道调整 */
    lcd_wr_reg(0X36);
    if (USE_HORIZONTAL == 0) lcd_wr_data8(0x08);      // 竖屏0度 (修正BGR)
    else if (USE_HORIZONTAL == 1) lcd_wr_data8(0xC8); // 竖屏180度 (修正BGR)
    else if (USE_HORIZONTAL == 2) lcd_wr_data8(0x78); // 横屏90度 (修正BGR)
    else lcd_wr_data8(0xA8);                          // 横屏270度 (修正BGR)
    
    /* 颜色格式与物理参数调优 */
    lcd_wr_reg(0X3A); lcd_wr_data8(0X05); // 16-bit RGB565
    lcd_wr_reg(0xb2); lcd_wr_data8(0x0c); lcd_wr_data8(0x0c); lcd_wr_data8(0x00); lcd_wr_data8(0x33); lcd_wr_data8(0x33);
    lcd_wr_reg(0xb7); lcd_wr_data8(0x35);
    lcd_wr_reg(0xbb); lcd_wr_data8(0x35);
    lcd_wr_reg(0xc0); lcd_wr_data8(0x2c);
    lcd_wr_reg(0xc2); lcd_wr_data8(0x01);
    lcd_wr_reg(0xc3); lcd_wr_data8(0x13);
    lcd_wr_reg(0xc4); lcd_wr_data8(0x20);
    lcd_wr_reg(0xc6); lcd_wr_data8(0x0f);
    lcd_wr_reg(0xca); lcd_wr_data8(0x0f);
    lcd_wr_reg(0xc8); lcd_wr_data8(0x08);
    lcd_wr_reg(0x55); lcd_wr_data8(0x90);
    lcd_wr_reg(0xd0); lcd_wr_data8(0xa4); lcd_wr_data8(0xa1);
    
    /* Gamma 曲线校准 */
    lcd_wr_reg(0xe0); lcd_wr_data8(0xd0); lcd_wr_data8(0x00); lcd_wr_data8(0x06); lcd_wr_data8(0x09); lcd_wr_data8(0x0b);
    lcd_wr_data8(0x2a); lcd_wr_data8(0x3c); lcd_wr_data8(0x55); lcd_wr_data8(0x4b); lcd_wr_data8(0x08); lcd_wr_data8(0x16);
    lcd_wr_data8(0x14); lcd_wr_data8(0x19); lcd_wr_data8(0x20);
    lcd_wr_reg(0xe1); lcd_wr_data8(0xd0); lcd_wr_data8(0x00); lcd_wr_data8(0x06); lcd_wr_data8(0x09); lcd_wr_data8(0x0b);
    lcd_wr_data8(0x29); lcd_wr_data8(0x36); lcd_wr_data8(0x54); lcd_wr_data8(0x4b); lcd_wr_data8(0x0d); lcd_wr_data8(0x16);
    lcd_wr_data8(0x14); lcd_wr_data8(0x21); lcd_wr_data8(0x20);
    
    /* 开启显示 */
    lcd_wr_reg(0x29);
    return 0;
}

/***************************************************************
 * 函数名称: lcd_deinit
 * 说    明: 释放 LCD 所占用的硬件 GPIO 和 SPI 总线资源
 * 参    数: 无
 * 返 回 值: unsigned int (0表示成功)
 ***************************************************************/
unsigned int lcd_deinit()
{
#if LCD_ENABLE_SPI
    LzSpiDeinit(LCD_SPI_BUS);
#else
    LzGpioDeinit(LCD_PIN_CS); LzGpioDeinit(LCD_PIN_CLK); LzGpioDeinit(LCD_PIN_MOSI);
#endif
    LzGpioDeinit(LCD_PIN_RES);
    LzGpioDeinit(LCD_PIN_DC);
    return 0;
}

/***************************************************************
 * 函数名称: lcd_fill
 * 说    明: 在屏幕指定矩形区域内快速填充统一的颜色
 * 参    数:
 * @xsta:  矩形区域左上角 X 坐标
 * @ysta:  矩形区域左上角 Y 坐标
 * @xend:  矩形区域右下角 X 坐标
 * @yend:  矩形区域右下角 Y 坐标
 * @color: RGB565 格式的16位颜色值 (例如 LCD_RED)
 * 返 回 值: 无
 ***************************************************************/
void lcd_fill(uint16_t xsta, uint16_t ysta, uint16_t xend, uint16_t yend, uint16_t color)
{
    uint16_t i, j;
    uint16_t width = xend - xsta;
    uint16_t height = yend - ysta;

    /* 设置显示范围的寻址窗口 */
    lcd_address_set(xsta, ysta, xend-1, yend-1);

#if LCD_ENABLE_SPI
    /* ====== 硬件 SPI 极速优化版 ====== */
    static uint16_t line_buf[320]; // 缓冲区容纳一整行 (static 节省栈空间)
    if (width > 320) width = 320; 
    
    // RK2206 是小端模式，SPI传输需要大端，故将颜色值高低8位互换
    uint16_t swap_color = (color >> 8) | (color << 8);
    for (i = 0; i < width; i++) line_buf[i] = swap_color;
    
    // 每次发送一整行的显存数据，利用 DMA/FIFO 特性加速
    for (i = 0; i < height; i++) {
        LzSpiWrite(LCD_SPI_BUS, 0, (uint8_t *)line_buf, width * 2);
    }
#else
    /* ====== 传统软SPI单像素渲染 ====== */
    for (i = ysta; i < yend; i++) {
        for (j = xsta; j < xend; j++) lcd_wr_data(color);
    }
#endif
}

/***************************************************************
 * 函数名称: lcd_draw_point
 * 说    明: 在屏幕指定坐标画一个点
 * 参    数:
 * @x:     点的 X 坐标
 * @y:     点的 Y 坐标
 * @color: RGB565 颜色值
 * 返 回 值: 无
 ***************************************************************/
void lcd_draw_point(uint16_t x, uint16_t y, uint16_t color)
{
    lcd_address_set(x, y, x, y);
    lcd_wr_data(color);
}

/***************************************************************
 * 函数名称: lcd_draw_line
 * 说    明: 使用 Bresenham 算法画任意两点间的直线
 * (若检测为纯水平或纯垂直线，会自动加速为矩形填充)
 * 参    数:
 * @x1:    起点 X 坐标
 * @y1:    起点 Y 坐标
 * @x2:    终点 X 坐标
 * @y2:    终点 Y 坐标
 * @color: RGB565 颜色值
 * 返 回 值: 无
 ***************************************************************/
void lcd_draw_line(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, uint16_t color)
{
    // 优化：垂直或水平线直接调用极限加速的 lcd_fill
    if (x1 == x2) {
        uint16_t y_min = y1 < y2 ? y1 : y2;
        uint16_t y_max = y1 < y2 ? y2 : y1;
        lcd_fill(x1, y_min, x1 + 1, y_max + 1, color);
        return;
    }
    if (y1 == y2) {
        uint16_t x_min = x1 < x2 ? x1 : x2;
        uint16_t x_max = x1 < x2 ? x2 : x1;
        lcd_fill(x_min, y1, x_max + 1, y1 + 1, color);
        return;
    }

    // 倾斜线使用算法绘制单点
    uint16_t t; 
    int xerr=0, yerr=0, delta_x, delta_y, distance;
    int incx, incy, uRow, uCol;

    delta_x = x2 - x1;
    delta_y = y2 - y1;
    uRow = x1; uCol = y1;
    
    if (delta_x > 0) incx = 1;
    else if (delta_x == 0) incx = 0;
    else { incx = -1; delta_x = -delta_x; }
    
    if (delta_y > 0) incy = 1;
    else if (delta_y == 0) incy = 0;
    else { incy = -1; delta_y = -delta_y; }
    
    if (delta_x > delta_y) distance = delta_x;
    else distance = delta_y;
    
    for (t = 0; t < distance+1; t++) {
        lcd_draw_point(uRow, uCol, color);
        xerr += delta_x; yerr += delta_y;
        if (xerr > distance) { xerr -= distance; uRow += incx; }
        if (yerr > distance) { yerr -= distance; uCol += incy; }
    }
}

/***************************************************************
 * 函数名称: lcd_draw_rectangle
 * 说    明: 画一个空心的矩形框
 * 参    数:
 * @x1:    矩形左上角 X 坐标
 * @y1:    矩形左上角 Y 坐标
 * @x2:    矩形右下角 X 坐标
 * @y2:    矩形右下角 Y 坐标
 * @color: 边框的 RGB565 颜色值
 * 返 回 值: 无
 ***************************************************************/
void lcd_draw_rectangle(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, uint16_t color)
{
    // 利用已优化的画线函数，画出四条高效率边框
    lcd_draw_line(x1, y1, x2, y1, color);
    lcd_draw_line(x1, y1, x1, y2, color);
    lcd_draw_line(x1, y2, x2, y2, color);
    lcd_draw_line(x2, y1, x2, y2, color);
}

/***************************************************************
 * 函数名称: lcd_draw_ellipse
 * 说    明: 指定位置画空心椭圆
 * 参    数:
 * @x0:    椭圆的中心点 X 坐标
 * @y0:    椭圆的中心点 Y 坐标
 * @rx:    椭圆的横向半轴长
 * @ry:    椭圆的纵向半轴长
 * @color: 边框的 RGB565 颜色值
 * 返 回 值: 无
 ***************************************************************/
void lcd_draw_ellipse(uint16_t x0, uint16_t y0, uint16_t rx, uint16_t ry, uint16_t color)
{
    long x = 0, y = ry;
    long rx2 = rx * rx, ry2 = ry * ry;
    long p = ry2 - rx2 * ry + rx2 / 4;

    while (ry2 * x < rx2 * y) {
        lcd_draw_point(x0 + x, y0 + y, color);
        lcd_draw_point(x0 - x, y0 + y, color);
        lcd_draw_point(x0 + x, y0 - y, color);
        lcd_draw_point(x0 - x, y0 - y, color);
        x++;
        if (p < 0) p += 2 * ry2 * x + ry2;
        else { y--; p += 2 * ry2 * x - 2 * rx2 * y + ry2; }
    }

    p = ry2 * (x * x + x) + rx2 * (y - 1) * (y - 1) - rx2 * ry2;
    while (y >= 0) {
        lcd_draw_point(x0 + x, y0 + y, color);
        lcd_draw_point(x0 - x, y0 + y, color);
        lcd_draw_point(x0 + x, y0 - y, color);
        lcd_draw_point(x0 - x, y0 - y, color);
        y--;
        if (p > 0) p -= 2 * rx2 * y + rx2;
        else { x++; p += 2 * ry2 * x - 2 * rx2 * y + rx2; }
    }
}

/***************************************************************
 * 函数名称: lcd_draw_circle
 * 说    明: 指定位置画空心正圆
 * 参    数:
 * @x0:    圆心 X 坐标
 * @y0:    圆心 Y 坐标
 * @r:     圆的半径
 * @color: 圆圈边框颜色
 * 返 回 值: 无
 ***************************************************************/
void lcd_draw_circle(uint16_t x0, uint16_t y0, uint8_t r, uint16_t color)
{
    int a = 0, b = r;
    while (a <= b) {
        lcd_draw_point(x0-b, y0-a, color); lcd_draw_point(x0+b, y0-a, color);
        lcd_draw_point(x0-a, y0+b, color); lcd_draw_point(x0-a, y0-b, color);
        lcd_draw_point(x0+b, y0+a, color); lcd_draw_point(x0+a, y0-b, color);
        lcd_draw_point(x0+a, y0+b, color); lcd_draw_point(x0-b, y0+a, color);
        a++;
        if ((a*a+b*b) > (r*r)) b--;
    }
}

/***************************************************************
 * 函数名称: lcd_show_chinese
 * 说    明: 在屏幕指定位置显示一串中文字符 (要求字符串为UTF-8编码)
 * 参    数:
 * @x:     字符串左上角起始 X 坐标
 * @y:     字符串左上角起始 Y 坐标
 * @s:     要显示的 UTF-8 字符串指针
 * @fc:    字体颜色 (Foreground Color)
 * @bc:    背景颜色 (Background Color)
 * @sizey: 字体大小，仅支持字库中含有的尺寸: 12, 16, 24, 32
 * @mode:  叠加模式。0=非叠加(渲染连带背景色, 极速), 1=叠加(透明底色)
 * 返 回 值: 无
 ***************************************************************/
void lcd_show_chinese(uint16_t x, uint16_t y, uint8_t *s, uint16_t fc, uint16_t bc, uint8_t sizey, uint8_t mode)
{
    uint8_t buffer[128];
    uint32_t buffer_len = 0;

    memset(buffer, 0, sizeof(buffer));
    // 将UTF-8转化为驱动字库需要的点阵索引码
    chinese_utf8_to_ascii(s, strlen(s), buffer, &buffer_len);

    for (uint32_t i = 0; i < buffer_len; i += 2, x += sizey) {
        if (sizey == 12) lcd_show_chinese_12x12(x, y, &buffer[i], fc, bc, sizey, mode);
        else if (sizey == 16) lcd_show_chinese_16x16(x, y, &buffer[i], fc, bc, sizey, mode);
        else if (sizey == 24) lcd_show_chinese_24x24(x, y, &buffer[i], fc, bc, sizey, mode);
        else if (sizey == 32) lcd_show_chinese_32x32(x, y, &buffer[i], fc, bc, sizey, mode);
        else return;
    }
}

/***************************************************************
 * 函数名称: lcd_show_char
 * 说    明: 显示单个 ASCII 字符
 * 参    数:
 * @x:     字符左上角 X 坐标
 * @y:     字符左上角 Y 坐标
 * @num:   要显示的字符 (如 'A')
 * @fc:    字体颜色
 * @bc:    背景颜色
 * @sizey: 字体大小: 12, 16, 24, 32
 * @mode:  叠加模式: 0=极速非叠加填充, 1=叠加透明背景
 * 返 回 值: 无
 ***************************************************************/
void lcd_show_char(uint16_t x, uint16_t y, uint8_t num, uint16_t fc, uint16_t bc, uint8_t sizey, uint8_t mode)
{
    uint8_t temp, sizex = sizey/2, t, m = 0;
    uint16_t i, TypefaceNum = (sizex/8 + ((sizex%8)?1:0)) * sizey;
    uint16_t x0 = x;
    
    num = num-' '; // 计算字库偏移量
    
    if (!mode) {
        // [内存画布优化]：非叠加模式下，将字体点阵转化为像素方阵直接推送，大幅减少耗时
        static uint16_t char_buf[512]; // 足够装下最大 16x32 的字符矩阵 (static 节省栈空间)
        uint16_t idx = 0;
        uint16_t fc_swp = (fc >> 8) | (fc << 8);
        uint16_t bc_swp = (bc >> 8) | (bc << 8);

        for (i = 0; i < TypefaceNum; i++) { 
            if (sizey == 12) temp = ascii_1206[num][i];
            else if (sizey == 16) temp = ascii_1608[num][i];
            else if (sizey == 24) temp = ascii_2412[num][i];
            else if (sizey == 32) temp = ascii_3216[num][i];
            else return;
            
            for (t = 0; t < 8; t++) {
                if (temp & (0x01 << t)) char_buf[idx++] = fc_swp;
                else char_buf[idx++] = bc_swp;
                m++;
                if (m%sizex == 0) { m = 0; break; }
            }
        } 
        lcd_address_set(x, y, x+sizex-1, y+sizey-1);
#if LCD_ENABLE_SPI
        LzSpiWrite(LCD_SPI_BUS, 0, (uint8_t *)char_buf, idx * 2);
#else
        for(i=0; i<idx; i++) lcd_wr_data((char_buf[i]>>8)|(char_buf[i]<<8));
#endif
    } else {
        // 叠加模式保留逐像素画点，不改变背景
        lcd_address_set(x, y, x+sizex-1, y+sizey-1);
        for (i = 0; i < TypefaceNum; i++) { 
            if (sizey == 12) temp = ascii_1206[num][i];
            else if (sizey == 16) temp = ascii_1608[num][i];
            else if (sizey == 24) temp = ascii_2412[num][i];
            else if (sizey == 32) temp = ascii_3216[num][i];
            else return;
            
            for (t = 0; t < 8; t++) {
                if (temp & (0x01 << t)) lcd_draw_point(x, y, fc);
                x++;
                if ((x - x0) == sizex) { x = x0; y++; break; }
            }
        }
    }
}

/***************************************************************
 * 函数名称: lcd_show_string
 * 说    明: 连续显示一串英文字符串
 * 参    数:
 * @x:     起点 X 坐标
 * @y:     起点 Y 坐标
 * @p:     英文字符串指针 (如 "Hello")
 * @fc:    字体颜色
 * @bc:    背景颜色
 * @sizey: 字体大小: 16, 24, 32
 * @mode:  叠加模式: 0=非叠加, 1=叠加
 * 返 回 值: 无
 ***************************************************************/
void lcd_show_string(uint16_t x, uint16_t y, const uint8_t *p, uint16_t fc, uint16_t bc, uint8_t sizey, uint8_t mode)
{        
    while (*p != '\0') {       
        lcd_show_char(x, y, *p, fc, bc, sizey, mode);
        x += (sizey / 2); // X坐标偏移单个字符的宽度
        p++;
    }  
}

/***************************************************************
 * 函数名称: lcd_show_int_num
 * 说    明: 在屏幕上显示一个无符号整数
 * 参    数:
 * @x:     起始 X 坐标
 * @y:     起始 Y 坐标
 * @num:   要显示的数值 (如 1234)
 * @len:   指定该数值占据的位数 (例如len=4, 数值为12，则前面补空格)
 * @fc:    字体颜色
 * @bc:    背景颜色
 * @sizey: 字体大小
 * 返 回 值: 无
 ***************************************************************/
void lcd_show_int_num(uint16_t x, uint16_t y, uint16_t num, uint8_t len, uint16_t fc, uint16_t bc, uint8_t sizey)
{
    uint8_t t, temp, enshow=0, sizex = sizey / 2;
    for (t=0; t<len; t++) {
        temp = (num/mypow(10,len-t-1)) % 10;
        if (enshow==0 && t<(len-1)) {
            if (temp == 0) {
                // 如果高位为 0 且还没遇到有效数字，用空格填充
                lcd_show_char(x+t*sizex, y, ' ', fc, bc, sizey, 0);
                continue;
            } else enshow = 1;
        }
        lcd_show_char(x+t*sizex, y, temp+48, fc, bc, sizey, 0); // 48 是 '0' 的 ASCII 码
    }
} 

/***************************************************************
 * 函数名称: lcd_show_float_num1
 * 说    明: 显示带有两位小数的浮点数 (如 3.14)
 * 参    数:
 * @x:     起始 X 坐标
 * @y:     起始 Y 坐标
 * @num:   浮点数值
 * @len:   整体显示的字符总长度 (包含小数点)
 * @fc:    字体颜色
 * @bc:    背景颜色
 * @sizey: 字体大小
 * 返 回 值: 无
 ***************************************************************/
void lcd_show_float_num1(uint16_t x, uint16_t y, float num, uint8_t len, uint16_t fc, uint16_t bc, uint8_t sizey)
{
    uint8_t t, temp, sizex = sizey / 2;
    /* 钳位: 防止 float→uint16_t 溢出 (uint16_t 最大 65535, 即 float < 655.36) */
    if (num < 0.0f) num = 0.0f;
    if (num > 655.35f) num = 655.35f;
    uint16_t num1 = (uint16_t)(num * 100.0f); // 放大100倍取出两位小数
    for (t=0; t<len; t++) {
        temp = (num1/mypow(10,len-t-1)) % 10;
        if (t == (len-2)) {
            lcd_show_char(x+(len-2)*sizex, y, '.', fc, bc, sizey, 0);
            t++; len += 1;
        }
        lcd_show_char(x+t*sizex, y, temp+48, fc, bc, sizey, 0);
    }
}

/***************************************************************
 * 函数名称: lcd_show_picture
 * 说    明: 高效显示图像数组 (使用 Image2Lcd 转换的 C 数组)
 * 参    数:
 * @x:      图片显示的起始 X 坐标
 * @y:      图片显示的起始 Y 坐标
 * @length: 图片宽度 (X轴跨度)
 * @width:  图片高度 (Y轴跨度)
 * @pic:    图片像素数据数组首地址
 * 返 回 值: 无
 ***************************************************************/
void lcd_show_picture(uint16_t x, uint16_t y, uint16_t length, uint16_t width, const uint8_t *pic)
{
    /* 在屏幕上划出一块专门接收图片的物理区域 */
    lcd_address_set(x, y, x+length-1, y+width-1);

#if LCD_ENABLE_SPI
    /* ====== 硬件 SPI 极速直传内存版 ====== */
    uint32_t bytes_per_line = length * 2; // 一行包含多少字节 (1像素=RGB565=2字节)
    uint16_t i;
    for (i = 0; i < width; i++) {
        // 直接将图像数组内存首地址分行移交给硬件DMA/SPI发送
        LzSpiWrite(LCD_SPI_BUS, 0, (uint8_t *)(pic + i * bytes_per_line), bytes_per_line);
    }
#else
    /* ====== GPIO 软模拟单点传送 ====== */
    uint16_t i, j;
    uint32_t k = 0;
    for (i = 0; i < length; i++) {
        for (j = 0; j < width; j++) {
            lcd_wr_data8(pic[k*2]);
            lcd_wr_data8(pic[k*2+1]);
            k++;
        }
    }
#endif
}