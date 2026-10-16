#include <thread>
#include <android/input.h>
#include <xhook/xhook.h>
#include "OverlayUI.h"
#include "SurfaceOverlay.h"

bool (*orig_MotionEvent_copyFrom)(void *thiz, void *other, bool keepHistory);

bool new_MotionEvent_copyFrom(void *thiz, void *other, bool keepHistory) {
    bool ret = orig_MotionEvent_copyFrom(thiz, other, keepHistory);
    if (!SurfaceOverlay_Ready()) return ret;

    AInputEvent *event = reinterpret_cast<AInputEvent *>(thiz);
    if (AInputEvent_getType(event) != AINPUT_EVENT_TYPE_MOTION) return ret;

    ImGuiIO &io = ImGui::GetIO();
    if (io.DisplaySize.x <= 0 || io.DisplaySize.y <= 0) return ret;

    int32_t actionMasked = AMotionEvent_getAction(event) & AMOTION_EVENT_ACTION_MASK;
    if (AMotionEvent_getPointerCount(event) > 0) {
        io.AddMousePosEvent(AMotionEvent_getX(event, 0), AMotionEvent_getY(event, 0));
        if (actionMasked == AMOTION_EVENT_ACTION_DOWN || actionMasked == AMOTION_EVENT_ACTION_POINTER_DOWN)
            io.AddMouseButtonEvent(0, true);
        else if (actionMasked == AMOTION_EVENT_ACTION_UP ||
                 actionMasked == AMOTION_EVENT_ACTION_POINTER_UP ||
                 actionMasked == AMOTION_EVENT_ACTION_CANCEL)
            io.AddMouseButtonEvent(0, false);
    }
    return ret;
}

void Hook() {
    xhook_register(".*\\.so$", "_ZN7android11MotionEvent8copyFromEPKS0_b",
                   (void *) new_MotionEvent_copyFrom, (void **) &orig_MotionEvent_copyFrom);
    xhook_refresh(0);
}

__attribute__((constructor)) void init() {
    std::thread(Hook).detach();
    SurfaceOverlay_Start();
}