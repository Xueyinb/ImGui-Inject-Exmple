#include "OverlayUI.h"
#include "my_imgui.h"

ImGuiWindow *g_window = NULL;
int abs_ScreenX = 0, abs_ScreenY = 0;
int native_window_screen_x = 0, native_window_screen_y = 0;
float touch_block_x = 0.0f, touch_block_y = 0.0f, touch_block_w = 0.0f, touch_block_h = 0.0f;

void init_My_drawdata() {
    ImGui::My_Android_LoadSystemFont(34.0f);
    ImGui::StyleColorsDark();
    ImGuiStyle &st = ImGui::GetStyle();
    st.WindowRounding = 8.0f;
    st.FrameRounding = 6.0f;
    st.ScrollbarSize = 20.0f;
    st.TouchExtraPadding = ImVec2(12, 12);
}

void Layout_tick_UI(bool *flag) {
    (void) flag;

    ImGuiIO &io = ImGui::GetIO();

    ImGui::SetNextWindowSize(ImVec2(430, 150), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImVec2(60, 60), ImGuiCond_FirstUseEver);
    ImGui::Begin("ImGui Overlay", nullptr, ImGuiWindowFlags_NoSavedSettings);

    ImGui::Text("Hello, world!");
    ImGui::Text("FPS: %.1f", io.Framerate);
    ImGui::Text("Renderer: %s", graphics ? graphics->RenderName : "none");

    g_window = ImGui::GetCurrentWindow();
    touch_block_x = g_window->Pos.x;
    touch_block_y = g_window->Pos.y;
    touch_block_w = g_window->Size.x;
    touch_block_h = g_window->Size.y;

    ImGui::End();
}
