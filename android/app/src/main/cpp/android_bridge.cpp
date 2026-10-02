#include <jni.h>
#include <string>

#include "Core.hpp"
#include "GameApp.hpp"

static suka::GameApp* g_app = nullptr;

extern "C" JNIEXPORT jboolean JNICALL
Java_com_sukaengine_app_MainActivity_nativeInit(JNIEnv* env, jobject, jstring root, jstring gameDir) {
    const char* r = env->GetStringUTFChars(root, nullptr);
    suka::setProjectRoot(r);
    env->ReleaseStringUTFChars(root, r);
    const char* g = env->GetStringUTFChars(gameDir, nullptr);
    std::string game = g;
    env->ReleaseStringUTFChars(gameDir, g);
    delete g_app;
    g_app = new suka::GameApp();
    return g_app->init(suka::GameApp::Mode::String, game) ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_sukaengine_app_MainActivity_nativeStep(JNIEnv* env, jobject) {
    std::string s = g_app ? g_app->stepFrame() : std::string("");
    return env->NewStringUTF(s.c_str());
}

extern "C" JNIEXPORT void JNICALL
Java_com_sukaengine_app_MainActivity_nativeTouch(JNIEnv* env, jobject, jint action, jfloat x, jfloat y) {
    if (g_app) g_app->feedTouch(action, x, y);
}

extern "C" JNIEXPORT void JNICALL
Java_com_sukaengine_app_MainActivity_nativeMultiTouch(JNIEnv* env, jobject, jint phase, jfloat x0, jfloat y0, jfloat x1, jfloat y1) {
    if (g_app) g_app->feedMultiTouch(phase, x0, y0, x1, y1);
}

extern "C" JNIEXPORT void JNICALL
Java_com_sukaengine_app_MainActivity_nativeSetText(JNIEnv* env, jobject, jstring text) {
    if (!g_app || !text) return;
    const char* t = env->GetStringUTFChars(text, nullptr);
    g_app->submitText(std::string(t));
    env->ReleaseStringUTFChars(text, t);
}

extern "C" JNIEXPORT void JNICALL
Java_com_sukaengine_app_MainActivity_nativeSetName(JNIEnv* env, jobject, jstring text) {
    if (!g_app || !text) return;
    const char* t = env->GetStringUTFChars(text, nullptr);
    g_app->submitName(std::string(t));
    env->ReleaseStringUTFChars(text, t);
}

extern "C" JNIEXPORT void JNICALL
Java_com_sukaengine_app_MainActivity_nativeSetAction(JNIEnv* env, jobject, jstring text) {
    if (!g_app || !text) return;
    const char* t = env->GetStringUTFChars(text, nullptr);
    g_app->submitAction(std::string(t));
    env->ReleaseStringUTFChars(text, t);
}

extern "C" JNIEXPORT void JNICALL
Java_com_sukaengine_app_MainActivity_nativeSetNumber(JNIEnv* env, jobject, jstring text) {
    if (!g_app || !text) return;
    const char* t = env->GetStringUTFChars(text, nullptr);
    g_app->submitNumber(std::string(t));
    env->ReleaseStringUTFChars(text, t);
}

extern "C" JNIEXPORT void JNICALL
Java_com_sukaengine_app_MainActivity_nativeScriptText(JNIEnv* env, jobject, jstring text) {
    if (!g_app || !text) return;
    const char* t = env->GetStringUTFChars(text, nullptr);
    g_app->submitScriptText(std::string(t));
    env->ReleaseStringUTFChars(text, t);
}

extern "C" JNIEXPORT void JNICALL
Java_com_sukaengine_app_MainActivity_nativeScriptCompose(JNIEnv* env, jobject, jstring text) {
    if (!g_app || !text) return;
    const char* t = env->GetStringUTFChars(text, nullptr);
    g_app->submitScriptCompose(std::string(t));
    env->ReleaseStringUTFChars(text, t);
}

extern "C" JNIEXPORT void JNICALL
Java_com_sukaengine_app_MainActivity_nativeScriptFinish(JNIEnv* env, jobject) {
    if (g_app) g_app->submitScriptFinish();
}

extern "C" JNIEXPORT void JNICALL
Java_com_sukaengine_app_MainActivity_nativeScriptKey(JNIEnv* env, jobject, jint key) {
    if (g_app) g_app->submitScriptKey((int)key);
}
