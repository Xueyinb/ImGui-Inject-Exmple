#include <atomic>
#include <chrono>
#include <cstdlib>
#include <thread>
#include <unistd.h>
#include <dlfcn.h>
#include <jni.h>
#include <android/native_window.h>
#include <android/native_window_jni.h>
#include <EGL/egl.h>

#include "SurfaceOverlay.h"
#include "OverlayUI.h"
#include "GraphicsManager.h"
#include "vulkan_wrapper.h"

#define OVERLAY_USE_VULKAN 0
#define OVERLAY_VULKAN_MARK "/data/local/tmp/imgui_use_vulkan"

std::unique_ptr<AndroidImgui> graphics = nullptr;
ANativeWindow *window = nullptr;
android::ANativeWindowCreator::DisplayInfo displayInfo{};

static JavaVM *g_vm = nullptr;
static jobject g_control_ref = nullptr;
static jobject g_surface_ref = nullptr;
static jobject g_parent_ref = nullptr;

static std::atomic<bool> g_overlayRunning{false};
static std::atomic<bool> g_overlayReady{false};
static std::atomic<long> g_frameCount{0};
static std::atomic<bool> g_needRebuild{false};

static bool sane_size(int w, int h) {
    return w >= 240 && h >= 240 && w <= 20000 && h <= 20000;
}

static bool want_vulkan() {
#if OVERLAY_USE_VULKAN
    return true;
#else
    return access(OVERLAY_VULKAN_MARK, F_OK) == 0;
#endif
}

static bool init_render_backend(ANativeWindow *win, int width, int height) {
    GraphicsManager::GraphicsAPI order[2];
    if (want_vulkan()) {
        order[0] = GraphicsManager::VULKAN;
        order[1] = GraphicsManager::OPENGL;
    } else {
        order[0] = GraphicsManager::OPENGL;
        order[1] = GraphicsManager::VULKAN;
    }

    for (int i = 0; i < 2; ++i) {
        if (order[i] == GraphicsManager::VULKAN && InitVulkan() != 1) {
            continue;
        }
        graphics = GraphicsManager::getGraphicsInterface(order[i]);
        if (graphics == nullptr) continue;
        if (!graphics->Init_Render(win, (float) width, (float) height)) {
            graphics.reset();
            continue;
        }
        return true;
    }
    return false;
}

static jclass find_class(JNIEnv *env, const char *name) {
    jclass cls = env->FindClass(name);
    if (cls == nullptr) {
        env->ExceptionClear();
    }
    return cls;
}

static jmethodID find_method(JNIEnv *env, jclass cls, bool is_static, const char *name, const char *sig) {
    if (cls == nullptr) return nullptr;
    jmethodID mid = is_static ? env->GetStaticMethodID(cls, name, sig) : env->GetMethodID(cls, name, sig);
    if (mid == nullptr) {
        env->ExceptionClear();
    }
    return mid;
}

static jfieldID find_field(JNIEnv *env, jclass cls, const char *name, const char *sig) {
    if (cls == nullptr) return nullptr;
    jfieldID fid = env->GetFieldID(cls, name, sig);
    if (fid == nullptr) {
        env->ExceptionClear();
    }
    return fid;
}

static jobject call_obj(JNIEnv *env, const char *step, jobject obj, jmethodID mid, ...) {
    if (obj == nullptr || mid == nullptr) {
        return nullptr;
    }
    va_list ap;
    va_start(ap, mid);
    jobject ret = env->CallObjectMethodV(obj, mid, ap);
    va_end(ap);
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
        return nullptr;
    }
    return ret;
}

static jobject call_static_obj(JNIEnv *env, const char *step, jclass cls, jmethodID mid, ...) {
    if (cls == nullptr || mid == nullptr) {
        return nullptr;
    }
    va_list ap;
    va_start(ap, mid);
    jobject ret = env->CallStaticObjectMethodV(cls, mid, ap);
    va_end(ap);
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
        return nullptr;
    }
    return ret;
}

static jint call_int(JNIEnv *env, const char *step, jobject obj, jmethodID mid, ...) {
    if (obj == nullptr || mid == nullptr) return 0;
    va_list ap;
    va_start(ap, mid);
    jint ret = env->CallIntMethodV(obj, mid, ap);
    va_end(ap);
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
        return 0;
    }
    return ret;
}

static void call_void(JNIEnv *env, const char *step, jobject obj, jmethodID mid, ...) {
    if (obj == nullptr || mid == nullptr) {
        return;
    }
    va_list ap;
    va_start(ap, mid);
    env->CallVoidMethodV(obj, mid, ap);
    va_end(ap);
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
    }
}

static jobject new_object(JNIEnv *env, const char *step, jclass cls, jmethodID mid, ...) {
    if (cls == nullptr || mid == nullptr) {
        return nullptr;
    }
    va_list ap;
    va_start(ap, mid);
    jobject ret = env->NewObjectV(cls, mid, ap);
    va_end(ap);
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
        return nullptr;
    }
    return ret;
}

static JNIEnv *jni_env(bool *out_attached) {
    *out_attached = false;
    if (g_vm == nullptr) {
        typedef jint (*GetVMsFunc)(JavaVM **, jsize, jsize *);
        auto get_vms = reinterpret_cast<GetVMsFunc>(dlsym(RTLD_DEFAULT, "JNI_GetCreatedJavaVMs"));
        if (get_vms == nullptr) {
            void *runtime = dlopen("libandroid_runtime.so", RTLD_LAZY);
            if (runtime != nullptr)
                get_vms = reinterpret_cast<GetVMsFunc>(dlsym(runtime, "JNI_GetCreatedJavaVMs"));
        }
        if (get_vms == nullptr) return nullptr;

        JavaVM *vm = nullptr;
        jsize vm_count = 0;
        if (get_vms(&vm, 1, &vm_count) != JNI_OK || vm == nullptr || vm_count < 1) return nullptr;
        g_vm = vm;
    }

    JNIEnv *env = nullptr;
    jint state = g_vm->GetEnv(reinterpret_cast<void **>(&env), JNI_VERSION_1_6);
    if (state == JNI_EDETACHED) {
        if (g_vm->AttachCurrentThread(&env, nullptr) != JNI_OK) return nullptr;
        *out_attached = true;
    } else if (state != JNI_OK) {
        return nullptr;
    }
    return env;
}

static bool read_metrics(JNIEnv *env, jobject metrics, int *out_w, int *out_h) {
    if (env == nullptr || metrics == nullptr) return false;
    jclass cls_metrics = env->FindClass("android/util/DisplayMetrics");
    if (cls_metrics == nullptr) return false;
    jfieldID fid_w = env->GetFieldID(cls_metrics, "widthPixels", "I");
    jfieldID fid_h = env->GetFieldID(cls_metrics, "heightPixels", "I");
    if (fid_w == nullptr || fid_h == nullptr) return false;
    *out_w = env->GetIntField(metrics, fid_w);
    *out_h = env->GetIntField(metrics, fid_h);
    return sane_size(*out_w, *out_h);
}

static bool JavaScreenSize(int *out_w, int *out_h) {
    bool attached = false;
    JNIEnv *env = jni_env(&attached);
    if (env == nullptr) return false;

    bool ok = false;
    jclass cls_thread = env->FindClass("android/app/ActivityThread");
    if (cls_thread == nullptr) env->ExceptionClear();
    if (cls_thread != nullptr) {
        jmethodID mid_app = env->GetStaticMethodID(cls_thread, "currentApplication",
                                                  "()Landroid/app/Application;");
        if (mid_app == nullptr) env->ExceptionClear();
        jobject app = mid_app != nullptr ? env->CallStaticObjectMethod(cls_thread, mid_app) : nullptr;
        if (env->ExceptionCheck()) {
            env->ExceptionClear();
            app = nullptr;
        }
        if (app != nullptr) {
            jclass cls_context = env->FindClass("android/content/Context");
            jclass cls_metrics = env->FindClass("android/util/DisplayMetrics");
            jmethodID mid_ctor = cls_metrics != nullptr
                                 ? env->GetMethodID(cls_metrics, "<init>", "()V") : nullptr;

            jmethodID mid_service = env->GetMethodID(cls_context, "getSystemService", "(Ljava/lang/String;)Ljava/lang/Object;");
            jfieldID fid_service = env->GetStaticFieldID(cls_context, "WINDOW_SERVICE", "Ljava/lang/String;");
            jclass cls_wm = env->FindClass("android/view/WindowManager");
            jmethodID mid_display = cls_wm != nullptr ? env->GetMethodID(cls_wm, "getDefaultDisplay", "()Landroid/view/Display;") : nullptr;
            jclass cls_display = env->FindClass("android/view/Display");
            jmethodID mid_real = cls_display != nullptr ? env->GetMethodID(cls_display, "getRealMetrics", "(Landroid/util/DisplayMetrics;)V") : nullptr;

            if (mid_service != nullptr && fid_service != nullptr && mid_display != nullptr &&
                mid_real != nullptr && mid_ctor != nullptr) {
                jstring service = reinterpret_cast<jstring>(env->GetStaticObjectField(cls_context, fid_service));
                jobject wm = env->CallObjectMethod(app, mid_service, service);
                jobject display = wm != nullptr ? env->CallObjectMethod(wm, mid_display) : nullptr;
                if (display != nullptr) {
                    jobject metrics = env->NewObject(cls_metrics, mid_ctor);
                    env->CallVoidMethod(display, mid_real, metrics);
                    ok = read_metrics(env, metrics, out_w, out_h);
                }
            }
            if (env->ExceptionCheck()) env->ExceptionClear();

            if (!ok) {
                jmethodID mid_res = env->GetMethodID(cls_context, "getResources", "()Landroid/content/res/Resources;");
                jobject res = mid_res != nullptr ? env->CallObjectMethod(app, mid_res) : nullptr;
                jclass cls_res = env->FindClass("android/content/res/Resources");
                jmethodID mid_metrics = (cls_res != nullptr && res != nullptr)
                                        ? env->GetMethodID(cls_res, "getDisplayMetrics", "()Landroid/util/DisplayMetrics;")
                                        : nullptr;
                if (mid_metrics != nullptr)
                    ok = read_metrics(env, env->CallObjectMethod(res, mid_metrics), out_w, out_h);
                if (env->ExceptionCheck()) env->ExceptionClear();
            }
        }
    }
    if (env->ExceptionCheck()) env->ExceptionClear();
    if (attached) g_vm->DetachCurrentThread();
    return ok;
}

static jobject try_get_sc(JNIEnv *env, jobject obj) {
    if (obj == nullptr) return nullptr;
    jclass cls = env->GetObjectClass(obj);
    if (cls == nullptr) {
        env->ExceptionClear();
        return nullptr;
    }
    jobject sc = nullptr;
    jmethodID mid = env->GetMethodID(cls, "getSurfaceControl", "()Landroid/view/SurfaceControl;");
    if (mid == nullptr) {
        env->ExceptionClear();
    } else {
        sc = env->CallObjectMethod(obj, mid);
        if (env->ExceptionCheck()) {
            env->ExceptionClear();
            sc = nullptr;
        }
    }
    if (sc == nullptr) {
        jfieldID fid = env->GetFieldID(cls, "mSurfaceControl", "Landroid/view/SurfaceControl;");
        if (fid == nullptr) {
            env->ExceptionClear();
        } else {
            sc = env->GetObjectField(obj, fid);
            if (env->ExceptionCheck()) {
                env->ExceptionClear();
                sc = nullptr;
            }
        }
    }
    env->DeleteLocalRef(cls);
    return sc;
}

static bool sc_valid(JNIEnv *env, jobject sc) {
    if (sc == nullptr) return false;
    jclass cls = env->GetObjectClass(sc);
    if (cls == nullptr) {
        env->ExceptionClear();
        return true;
    }
    jmethodID mid = env->GetMethodID(cls, "isValid", "()Z");
    bool ok = true;
    if (mid == nullptr) {
        env->ExceptionClear();
    } else {
        jboolean valid = env->CallBooleanMethod(sc, mid);
        if (env->ExceptionCheck()) env->ExceptionClear();
        else ok = (valid == JNI_TRUE);
    }
    env->DeleteLocalRef(cls);
    return ok;
}

static int view_area(JNIEnv *env, jobject view) {
    if (view == nullptr) return -1;
    int area = -1;
    jclass cls = env->GetObjectClass(view);
    if (cls == nullptr) {
        env->ExceptionClear();
        return -1;
    }
    jmethodID mid_w = env->GetMethodID(cls, "getWidth", "()I");
    jmethodID mid_h = env->GetMethodID(cls, "getHeight", "()I");
    if (mid_w == nullptr || mid_h == nullptr) {
        env->ExceptionClear();
    } else {
        jint w = env->CallIntMethod(view, mid_w);
        if (env->ExceptionCheck()) env->ExceptionClear();
        jint h = env->CallIntMethod(view, mid_h);
        if (env->ExceptionCheck()) env->ExceptionClear();
        if (w > 0 && h > 0) area = (int) w * (int) h;
    }
    jmethodID mid_view = env->GetMethodID(cls, "getView", "()Landroid/view/View;");
    if (mid_view == nullptr) {
        env->ExceptionClear();
    } else if (area < 0) {
        jobject decor = env->CallObjectMethod(view, mid_view);
        if (env->ExceptionCheck()) {
            env->ExceptionClear();
            decor = nullptr;
        }
        if (decor != nullptr) area = view_area(env, decor);
    }
    env->DeleteLocalRef(cls);
    return area;
}

static jobject pick_better(jobject cur_best, int cur_area, bool cur_valid,
                           jobject sc, int area, bool valid) {
    if (sc == nullptr) return cur_best;
    if (cur_best == nullptr) return sc;
    if (valid && !cur_valid) return sc;
    if (valid == cur_valid && area > cur_area) return sc;
    return cur_best;
}

static int list_size(JNIEnv *env, jobject list, jmethodID mid_size) {
    if (list == nullptr || mid_size == nullptr) return 0;
    jint n = env->CallIntMethod(list, mid_size);
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
        return 0;
    }
    return (int) n;
}

static jobject find_window_surface_control(JNIEnv *env) {
    jclass cls_global = find_class(env, "android/view/WindowManagerGlobal");
    if (cls_global == nullptr) return nullptr;

    jmethodID mid_instance = find_method(env, cls_global, true, "getInstance", "()Landroid/view/WindowManagerGlobal;");
    jobject global = call_static_obj(env, "wmg.getInstance", cls_global, mid_instance);
    if (global == nullptr) return nullptr;

    jclass cls_list = find_class(env, "java/util/ArrayList");
    jmethodID mid_size = find_method(env, cls_list, false, "size", "()I");
    jmethodID mid_get = find_method(env, cls_list, false, "get", "(I)Ljava/lang/Object;");

    jobject best = nullptr;
    int best_area = -1;
    bool best_valid = false;

    jfieldID fid_roots = find_field(env, cls_global, "mRoots", "Ljava/util/ArrayList;");
    jobject roots = nullptr;
    if (fid_roots != nullptr) {
        roots = env->GetObjectField(global, fid_roots);
        if (env->ExceptionCheck()) {
            env->ExceptionClear();
            roots = nullptr;
        }
    }
    int root_count = list_size(env, roots, mid_size);
    for (int i = 0; i < root_count; ++i) {
        jobject root = call_obj(env, "roots.get", roots, mid_get, i);
        if (root == nullptr) continue;
        jobject sc = try_get_sc(env, root);
        if (sc != nullptr) {
            int area = view_area(env, root);
            bool valid = sc_valid(env, sc);
            best = pick_better(best, best_area, best_valid, sc, area, valid);
            if (best == sc) {
                best_area = area;
                best_valid = valid;
            }
        }
    }

    if (best == nullptr) {
        jmethodID mid_views = find_method(env, cls_global, false, "getWindowViews", "()Ljava/util/ArrayList;");
        jobject views = call_obj(env, "getWindowViews", global, mid_views);
        int view_count = list_size(env, views, mid_size);
        jclass cls_vp = find_class(env, "android/view/View");
        jmethodID mid_parent = find_method(env, cls_vp, false, "getParent", "()Landroid/view/ViewParent;");
        for (int i = 0; i < view_count && best == nullptr; ++i) {
            jobject view = call_obj(env, "views.get", views, mid_get, i);
            if (view == nullptr) continue;
            int area = view_area(env, view);
            jobject node = view;
            for (int hop = 0; hop < 12 && node != nullptr; ++hop) {
                jobject sc = try_get_sc(env, node);
                if (sc != nullptr) {
                    bool valid = sc_valid(env, sc);
                    best = pick_better(best, best_area, best_valid, sc, area, valid);
                    if (best == sc) {
                        best_area = area;
                        best_valid = valid;
                    }
                    break;
                }
                node = call_obj(env, "getParent", node, mid_parent);
            }
        }
    }
    return best;
}

static ANativeWindow *create_overlay_layer(JNIEnv *env, int width, int height) {
    if (env == nullptr) return nullptr;

    jclass cls_vmruntime = find_class(env, "dalvik/system/VMRuntime");
    jmethodID mid_runtime = find_method(env, cls_vmruntime, true, "getRuntime", "()Ldalvik/system/VMRuntime;");
    jmethodID mid_exempt = find_method(env, cls_vmruntime, false, "setHiddenApiExemptions", "([Ljava/lang/String;)V");
    jobject runtime = call_static_obj(env, "vmruntime.getRuntime", cls_vmruntime, mid_runtime);
    if (runtime != nullptr && mid_exempt != nullptr) {
        jclass cls_string = find_class(env, "java/lang/String");
        jstring exemption = env->NewStringUTF("L");
        jobjectArray exemptions = (cls_string != nullptr && exemption != nullptr)
                                  ? env->NewObjectArray(1, cls_string, exemption) : nullptr;
        if (env->ExceptionCheck()) {
            env->ExceptionClear();
            exemptions = nullptr;
        }
        call_void(env, "setHiddenApiExemptions", runtime, mid_exempt, exemptions);
    }

    static int s_no_window = 0;
    jobject parent = find_window_surface_control(env);
    if (parent == nullptr) {
        return nullptr;
    }
    s_no_window = 0;

    jclass cls_builder = find_class(env, "android/view/SurfaceControl$Builder");
    if (cls_builder == nullptr) return nullptr;

    jmethodID mid_builder_void = find_method(env, cls_builder, false, "<init>", "()V");
    jobject builder = new_object(env, "builder()", cls_builder, mid_builder_void);
    if (builder == nullptr) {
        jclass cls_session = find_class(env, "android/view/SurfaceSession");
        jmethodID mid_session_ctor = find_method(env, cls_session, false, "<init>", "()V");
        jmethodID mid_builder_session = find_method(env, cls_builder, false, "<init>", "(Landroid/view/SurfaceSession;)V");
        if (mid_session_ctor != nullptr && mid_builder_session != nullptr) {
            jobject session = new_object(env, "new SurfaceSession", cls_session, mid_session_ctor);
            builder = new_object(env, "builder(session)", cls_builder, mid_builder_session, session);
        }
    }
    if (builder == nullptr) return nullptr;

    jmethodID mid_name = find_method(env, cls_builder, false, "setName", "(Ljava/lang/String;)Landroid/view/SurfaceControl$Builder;");
    jmethodID mid_buffer = find_method(env, cls_builder, false, "setBufferSize", "(II)Landroid/view/SurfaceControl$Builder;");
    jmethodID mid_format = find_method(env, cls_builder, false, "setFormat", "(I)Landroid/view/SurfaceControl$Builder;");
    jmethodID mid_hidden = find_method(env, cls_builder, false, "setHidden", "(Z)Landroid/view/SurfaceControl$Builder;");
    jmethodID mid_parent = find_method(env, cls_builder, false, "setParent", "(Landroid/view/SurfaceControl;)Landroid/view/SurfaceControl$Builder;");
    jmethodID mid_build = find_method(env, cls_builder, false, "build", "()Landroid/view/SurfaceControl;");

    jstring layer_name = env->NewStringUTF("imgui_overlay");
    jobject step;
    if ((step = call_obj(env, "setName", builder, mid_name, layer_name)) != nullptr) builder = step;
    if ((step = call_obj(env, "setBufferSize", builder, mid_buffer, width, height)) != nullptr) builder = step;
    if ((step = call_obj(env, "setFormat", builder, mid_format, 1 )) != nullptr) builder = step;
    if ((step = call_obj(env, "setHidden", builder, mid_hidden, (jboolean) JNI_FALSE)) != nullptr) builder = step;
    if ((step = call_obj(env, "setParent", builder, mid_parent, parent)) != nullptr) builder = step;

    jobject control = call_obj(env, "build", builder, mid_build);
    if (control == nullptr) {
        return nullptr;
    }

    jclass cls_txn = find_class(env, "android/view/SurfaceControl$Transaction");
    jmethodID mid_txn_ctor = find_method(env, cls_txn, false, "<init>", "()V");
    jmethodID mid_set_layer = find_method(env, cls_txn, false, "setLayer", "(Landroid/view/SurfaceControl;I)Landroid/view/SurfaceControl$Transaction;");
    jmethodID mid_show = find_method(env, cls_txn, false, "show", "(Landroid/view/SurfaceControl;)Landroid/view/SurfaceControl$Transaction;");
    jmethodID mid_apply = find_method(env, cls_txn, false, "apply", "()V");
    if (mid_apply == nullptr) mid_apply = find_method(env, cls_txn, false, "commit", "()V");

    jobject txn = new_object(env, "new Transaction", cls_txn, mid_txn_ctor);
    if (txn != nullptr) {
        if ((step = call_obj(env, "setLayer", txn, mid_set_layer, control, 0x7FFFFFFF)) != nullptr) txn = step;
        if ((step = call_obj(env, "show", txn, mid_show, control)) != nullptr) txn = step;
        call_void(env, "apply", txn, mid_apply);
    }

    jclass cls_surface = find_class(env, "android/view/Surface");
    jmethodID mid_surface_ctor = find_method(env, cls_surface, false, "<init>", "(Landroid/view/SurfaceControl;)V");
    jobject surface = new_object(env, "new Surface(sc)", cls_surface, mid_surface_ctor, control);
    if (surface == nullptr) {
        return nullptr;
    }

    ANativeWindow *native_window = ANativeWindow_fromSurface(env, surface);
    if (native_window == nullptr) {
        return nullptr;
    }
    g_control_ref = env->NewGlobalRef(control);
    g_surface_ref = env->NewGlobalRef(surface);
    g_parent_ref = env->NewGlobalRef(parent);
    return native_window;
}

static void teardown_overlay() {
    g_overlayReady.store(false);
    if (graphics) {
        graphics->Shutdown();
        graphics.reset();
    }
    if (window) {
        ANativeWindow_release(window);
        window = nullptr;
    }
}

static void release_overlay_refs(JNIEnv *env) {
    if (env == nullptr) return;
    if (g_control_ref != nullptr) { env->DeleteGlobalRef(g_control_ref); g_control_ref = nullptr; }
    if (g_surface_ref != nullptr) { env->DeleteGlobalRef(g_surface_ref); g_surface_ref = nullptr; }
    if (g_parent_ref != nullptr) { env->DeleteGlobalRef(g_parent_ref); g_parent_ref = nullptr; }
}

static bool setup_overlay(JNIEnv *env, int width, int height) {
    if (env == nullptr) return false;

    teardown_overlay();
    release_overlay_refs(env);
    g_needRebuild.store(false);

    window = create_overlay_layer(env, width, height);
    if (window == nullptr) return false;

    int lw = ANativeWindow_getWidth(window);
    int lh = ANativeWindow_getHeight(window);
    if (!sane_size(lw, lh)) {
        teardown_overlay();
        release_overlay_refs(env);
        return false;
    }

    if (!init_render_backend(window, lw, lh)) {
        teardown_overlay();
        release_overlay_refs(env);
        return false;
    }

    init_My_drawdata();
    g_overlayReady.store(true);
    return true;
}

bool SurfaceOverlay_Ready() {
    return g_overlayReady.load();
}

static void OverlayWatchdog() {
    bool attached = false;
    JNIEnv *env = jni_env(&attached);
    if (env == nullptr) {
        return;
    }

    jclass cls_txn = find_class(env, "android/view/SurfaceControl$Transaction");
    jmethodID mid_ctor = find_method(env, cls_txn, false, "<init>", "()V");
    jmethodID mid_set_layer = find_method(env, cls_txn, false, "setLayer", "(Landroid/view/SurfaceControl;I)Landroid/view/SurfaceControl$Transaction;");
    jmethodID mid_reparent = find_method(env, cls_txn, false, "reparent", "(Landroid/view/SurfaceControl;Landroid/view/SurfaceControl;)Landroid/view/SurfaceControl$Transaction;");
    jmethodID mid_show = find_method(env, cls_txn, false, "show", "(Landroid/view/SurfaceControl;)Landroid/view/SurfaceControl$Transaction;");
    jmethodID mid_apply = find_method(env, cls_txn, false, "apply", "()V");

    int tick = 0;
    int seen = -1;
    long last_frames = -1;
    int stuck = 0;
    int cooldown = 0;

    while (g_overlayRunning.load()) {
        sleep(1);
        if (!g_overlayReady.load()) {
            seen = -1;
            last_frames = -1;
            stuck = 0;
            continue;
        }
        if (cooldown > 0) --cooldown;

        if (env->PushLocalFrame(64) != JNI_OK) continue;

        jobject fresh = find_window_surface_control(env);
        bool now = (fresh != nullptr);
        const char *why = nullptr;

        if (now) {
            if (g_parent_ref != nullptr && !env->IsSameObject(g_parent_ref, fresh))
                why = "游戏窗口换了对象";
            else if (seen == 0)
                why = "游戏窗口回来了";
        } else if (seen == 1) {
        }
        seen = now ? 1 : 0;

        if (now && g_control_ref != nullptr && !g_needRebuild.load()) {
            jobject txn = new_object(env, "watchdog txn", cls_txn, mid_ctor);
            if (txn != nullptr) {
                jobject step;
                if ((step = call_obj(env, "setLayer", txn, mid_set_layer, g_control_ref, 0x7FFFFFFF)) != nullptr) txn = step;
                if ((step = call_obj(env, "show", txn, mid_show, g_control_ref)) != nullptr) txn = step;
                if ((step = call_obj(env, "reparent", txn, mid_reparent, g_control_ref, fresh)) != nullptr) txn = step;
                call_void(env, "apply", txn, mid_apply);
            }
        }

        long f = g_frameCount.load();
        stuck = (f == last_frames) ? stuck + 1 : 0;
        last_frames = f;
        if (why == nullptr && now && stuck >= 10 && cooldown == 0) why = "帧数卡住";

        if (why != nullptr && !g_needRebuild.load()) {
            g_needRebuild.store(true);
            cooldown = 20;
        }

        env->PopLocalFrame(nullptr);
    }

    if (attached) g_vm->DetachCurrentThread();
}

static void OverlayRenderLoop() {
    displayInfo = android::ANativeWindowCreator::GetDisplayInfo();

    bool attached = false;
    JNIEnv *env = jni_env(&attached);
    if (env == nullptr) {
        g_overlayRunning.store(false);
        return;
    }

    int width = 0, height = 0;
    for (int attempt = 0; attempt < 5 && !JavaScreenSize(&width, &height); ++attempt)
        sleep(1);
    if (!sane_size(width, height) && sane_size(displayInfo.width, displayInfo.height)) {
        width = displayInfo.width;
        height = displayInfo.height;
    }
    if (!sane_size(width, height)) {
        g_overlayRunning.store(false);
        if (attached) g_vm->DetachCurrentThread();
        return;
    }

    int wait_ticks = 0;
    while (g_overlayRunning.load() && !g_overlayReady.load()) {
        if (setup_overlay(env, width, height)) break;
        sleep(1);
    }
    if (!g_overlayReady.load()) {
        g_overlayRunning.store(false);
        if (attached) g_vm->DetachCurrentThread();
        return;
    }

    if (attached) {
        g_vm->DetachCurrentThread();
        attached = false;
        env = nullptr;
    }

    bool flag = true;
    while (g_overlayRunning.load()) {
        if (g_needRebuild.exchange(false)) {
            if (env == nullptr && (env = jni_env(&attached)) == nullptr) {
                g_needRebuild.store(true);
                sleep(1);
                continue;
            }
            teardown_overlay();
            int rticks = 0;
            while (g_overlayRunning.load() && !g_overlayReady.load()) {
                if (setup_overlay(env, width, height)) break;
                sleep(1);
            }
            g_vm->DetachCurrentThread();
            attached = false;
            env = nullptr;
            continue;
        }

        auto frame_begin = std::chrono::steady_clock::now();

        graphics->NewFrame(true);
        ImGuiIO &io = ImGui::GetIO();
        if (!sane_size((int) io.DisplaySize.x, (int) io.DisplaySize.y))
            io.DisplaySize = ImVec2((float) width, (float) height);
        Layout_tick_UI(&flag);
        graphics->EndFrame();
        g_frameCount.fetch_add(1);

        auto used = std::chrono::steady_clock::now() - frame_begin;
        if (used < std::chrono::milliseconds(8))
            std::this_thread::sleep_for(std::chrono::milliseconds(8) - used);
    }

    teardown_overlay();
}

void SurfaceOverlay_Start() {
    if (g_overlayRunning.exchange(true)) return;

    if (getenv("IMGUI_OVERLAY_ACTIVE") != nullptr) {
        g_overlayRunning.store(false);
        return;
    }
    setenv("IMGUI_OVERLAY_ACTIVE", "1", 1);

    std::thread(OverlayRenderLoop).detach();
    std::thread(OverlayWatchdog).detach();
}
