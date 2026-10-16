#by.Xueyi
#2026.10.17

# Android ImGui 注入模板

把 ImGui 覆盖层注入到任意 Android 应用里，不依赖目标游戏用的是 OpenGL 还是 Vulkan。

## 原理

so 加载时的 constructor 做两件事：

1. hook `android.view.MotionEvent.copyFrom`，把触摸事件喂给 ImGui。
2. 起线程，在目标进程里自己建一块 SurfaceControl 图层，自己一套渲染上下文画 ImGui。

覆盖层是独立图层加独立渲染上下文，不去 hook 目标游戏的渲染，所以什么渲染后端的游戏都能显示。

建图层走 JNI：读 WindowManagerGlobal 的 mRoots，挑面积最大且有效的 ViewRootImpl 的 SurfaceControl 当父层，
用 SurfaceControl.Builder（默认构造，也就是进程默认 client）建一个叫 imgui_overlay 的层，
setHidden(false)，在 Transaction 里 setLayer 到最上层再 show、apply，然后 new Surface(sc) 拿到 ANativeWindow 交给渲染后端。

看门狗线程每秒做两件事：一是把 setLayer、show、reparent 重新下发一遍，保住可见性；
二是发现游戏窗口换了对象、消失过又回来、或者帧数卡住 10 秒，就把整套拆掉重建。
所以按 Home 切后台再回来能自己恢复，只要进程没死就不会掉。

## 目录

- inject/inject 注入器，root 执行，dlopen 进目标进程
- jni/src/main.cpp 注入入口，hook 触摸
- jni/src/Android_Graphics/SurfaceOverlay.cpp 建图层、渲染循环、看门狗
- jni/src/Android_Graphics/OverlayUI.cpp 每帧画的内容
- jni/src/Android_Graphics/GraphicsManager.cpp 渲染后端选择
- jni/src/ImGui ImGui 本体
- jni/xhook hook 框架

## 编译

    sh 编译工具.sh

产物在 obj/local/arm64-v8a/libimgui_demo.so

## 注入

    sh start注入.sh

提示输入包名，回车。脚本自己拉起应用、把 inject 和 so 拷到 /data/data/包名/ 再注入。
也可以直接带参数：

    sh start注入.sh com.xxx.yyy

## 换渲染后端

默认 OpenGL。用 Vulkan 两种办法：

- 建一个空文件 /data/local/tmp/imgui_use_vulkan
- 或者把 SurfaceOverlay.cpp 里 OVERLAY_USE_VULKAN 改成 1 重新编译

设备上没有 libvulkan 时会自动退回 OpenGL。

## 说明

- 只作用于目标进程，进程被杀掉就没了，要重新注入。
- 同一个进程重复注入会被环境变量 IMGUI_OVERLAY_ACTIVE 挡掉，重新注入前先杀掉进程。
- 代码里没有输出日志。排查问题看 logcat 的 AndroidRuntime（崩溃）和 dumpsys SurfaceFlinger 里的 imgui_overlay 图层。
