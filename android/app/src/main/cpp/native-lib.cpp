/**
 * File Description
 * @author CheckerBoard
 * @date 29-07-2021
 * @description
 */
#include <jni.h>
#include <string>
#include <android/bitmap.h>
#include "mInterface.h"
/* Header for class com_demo_face_image_quality_NativeLibrary */
#ifndef _Included_com_demo_face_image_quality_NativeLibrary
#define _Included_com_demo_face_image_quality_NativeLibrary

JavaVM *javaVM = nullptr;
mInterface interface;

extern "C" {
jint JNI_OnLoad(JavaVM *vm, void *reserved) {
    JNIEnv *env;
    javaVM = vm;
    if (vm->GetEnv((void **) &env, JNI_VERSION_1_6) != JNI_OK) {
        return -1;
    }
    return JNI_VERSION_1_6;
}

JNIEXPORT jfloat JNICALL Java_com_demo_faceimagequality_NativeLibrary_processImage(JNIEnv *env, jobject obj, jobject jbitmap) {

    void *bitmapPixels = 0;
    AndroidBitmapInfo bitmapInfo;
    CV_Assert(AndroidBitmap_getInfo(env, jbitmap, &bitmapInfo) >= 0);
    CV_Assert(bitmapInfo.format == ANDROID_BITMAP_FORMAT_RGBA_8888 || bitmapInfo.format == ANDROID_BITMAP_FORMAT_RGB_565);
    CV_Assert(AndroidBitmap_lockPixels(env, jbitmap, &bitmapPixels) >= 0);
    cv::Mat src(bitmapInfo.height, bitmapInfo.width, bitmapInfo.format == ANDROID_BITMAP_FORMAT_RGBA_8888 ? CV_8UC4 : CV_8UC2,
                bitmapPixels);
    cv::Mat binBit = src.clone();
    AndroidBitmap_unlockPixels(env, jbitmap);
    float quality = interface.process(binBit);
    return quality;
}

}
#endif
