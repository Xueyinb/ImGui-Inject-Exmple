LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)
LOCAL_MODULE := lib_git_freetype
LOCAL_SRC_FILES := src/ImGui/misc/git_freetype/$(TARGET_ARCH_ABI)/libfreetype.a
include $(PREBUILT_STATIC_LIBRARY)

include $(CLEAR_VARS)

LOCAL_MODULE := imgui_demo

LOCAL_CFLAGS := -std=c17
LOCAL_CFLAGS += -fvisibility=hidden
LOCAL_CPPFLAGS := -std=c++17
LOCAL_CPPFLAGS += -fvisibility=hidden
LOCAL_CPPFLAGS += -DVK_USE_PLATFORM_ANDROID_KHR
LOCAL_CPPFLAGS += -DIMGUI_IMPL_VULKAN_NO_PROTOTYPES
LOCAL_CPPFLAGS += -DIMGUI_DISABLE_DEBUG_TOOLS
LOCAL_CPPFLAGS += -DIMGUI_ENABLE_FREETYPE

LOCAL_C_INCLUDES += $(LOCAL_PATH)/include
LOCAL_C_INCLUDES += $(LOCAL_PATH)/include/Android_Graphics
LOCAL_C_INCLUDES += $(LOCAL_PATH)/include/Android_touch
LOCAL_C_INCLUDES += $(LOCAL_PATH)/include/ImGui
LOCAL_C_INCLUDES += $(LOCAL_PATH)/include/ImGui/backends
LOCAL_C_INCLUDES += $(LOCAL_PATH)/include/ImGui/My_Utils
LOCAL_C_INCLUDES += $(LOCAL_PATH)/include/ImGui/My_Utils/stb_image
LOCAL_C_INCLUDES += $(LOCAL_PATH)/include/ImGui/misc/freetype
LOCAL_C_INCLUDES += $(LOCAL_PATH)/include/ImGui/misc/git_freetype

LOCAL_C_INCLUDES += $(LOCAL_PATH)/xhook

LOCAL_SRC_FILES := src/main.cpp
LOCAL_SRC_FILES += src/Android_Graphics/OverlayUI.cpp
LOCAL_SRC_FILES += src/Android_touch/TouchHelperA.cpp
LOCAL_SRC_FILES += src/Android_Graphics/GraphicsManager.cpp
LOCAL_SRC_FILES += src/Android_Graphics/OpenGLGraphics.cpp
LOCAL_SRC_FILES += src/Android_Graphics/VulkanGraphics.cpp
LOCAL_SRC_FILES += src/Android_Graphics/vulkan_wrapper.cpp
LOCAL_SRC_FILES += src/Android_Graphics/AndroidImgui.cpp
LOCAL_SRC_FILES += src/Android_Graphics/my_imgui.cpp
LOCAL_SRC_FILES += src/Android_Graphics/my_imgui_impl_android.cpp
LOCAL_SRC_FILES += src/Android_Graphics/SurfaceOverlay.cpp
LOCAL_SRC_FILES += src/ImGui/imgui.cpp
LOCAL_SRC_FILES += src/ImGui/imgui_draw.cpp
LOCAL_SRC_FILES += src/ImGui/imgui_tables.cpp
LOCAL_SRC_FILES += src/ImGui/imgui_widgets.cpp
LOCAL_SRC_FILES += src/ImGui/backends/imgui_impl_android.cpp
LOCAL_SRC_FILES += src/ImGui/backends/imgui_impl_opengl3.cpp
LOCAL_SRC_FILES += src/ImGui/backends/imgui_impl_vulkan.cpp
LOCAL_SRC_FILES += src/ImGui/misc/freetype/imgui_freetype.cpp
LOCAL_SRC_FILES += src/ImGui/My_Utils/stb_image/stb_image.cpp
LOCAL_SRC_FILES += xhook/xh_core.c
LOCAL_SRC_FILES += xhook/xh_elf.c
LOCAL_SRC_FILES += xhook/xh_jni.c
LOCAL_SRC_FILES += xhook/xh_log.c
LOCAL_SRC_FILES += xhook/xh_util.c
LOCAL_SRC_FILES += xhook/xh_version.c
LOCAL_SRC_FILES += xhook/xhook.c

LOCAL_LDLIBS := -llog -landroid -lEGL -lGLESv3
LOCAL_LDLIBS += -lz
LOCAL_STATIC_LIBRARIES := lib_git_freetype

include $(BUILD_SHARED_LIBRARY)