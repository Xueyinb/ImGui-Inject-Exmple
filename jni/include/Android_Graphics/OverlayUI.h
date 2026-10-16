#ifndef OVERLAY_UI_H
#define OVERLAY_UI_H


#include <memory>
#include "ImGui/imgui.h"
#include "ImGui/imgui_internal.h"
#include "native_surface/ANativeWindowCreator.h"
#include "AndroidImgui.h"

extern std::unique_ptr<AndroidImgui> graphics;
extern ANativeWindow *window;
extern android::ANativeWindowCreator::DisplayInfo displayInfo;
extern ImGuiWindow *g_window;
extern int abs_ScreenX, abs_ScreenY;
extern int native_window_screen_x, native_window_screen_y;
extern float touch_block_x, touch_block_y, touch_block_w, touch_block_h;

void init_My_drawdata();

void Layout_tick_UI(bool *main_thread_flag);

#endif
