/**
 * File Description
 * @author CheckerBoard
 * @date 29-07-2021
 * @description
 */
package com.demo.faceimagequality;
import android.graphics.Bitmap;

class NativeLibrary {
    // Used to load the 'native-lib' library on application startup.
    static {
        System.loadLibrary("native");
    }

    private volatile static NativeLibrary mInstance;

    public static NativeLibrary getInstance(){
        if (mInstance == null) {
            synchronized (NativeLibrary.class) {
                if (mInstance == null) {
                    mInstance = new NativeLibrary();
                }
            }
        }
        return mInstance;
    }

    public native float processImage(Bitmap bitmap);
}
