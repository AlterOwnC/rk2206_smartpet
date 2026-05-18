#ifndef _EYES_EMOTION_H_
#define _EYES_EMOTION_H_

/***************************************************************
 * 函数名称: eyes_play_normal_loop
 * 说    明: 播放正常的灵动待机动画 (包含随机眨眼和双连眨)
 * 参    数: 无
 * 返 回 值: 无
 * 注意事项: 该函数内部带有 while(1) 死循环，直接接管当前任务
 ***************************************************************/
void eyes_play_normal_loop(void);
/***************************************************************
 * 函数名称: eyes_play_cat_step
 * 说    明: 俏皮喂猫表情 (模式 1)，循序 1->2->1->3->1
 ***************************************************************/
void eyes_play_cat_step(void);


// 新增：静态 UI 绘制接口
void ui_draw_status_bar(void);
void ui_draw_footer(void);

void ui_update_time_only(void);
#endif /* _EYES_EMOTION_H_ */