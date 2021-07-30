/**
 * File Description
 * @author
 * @date 2021-07-29
 * @description
 */
package com.demo.faceimagequality;

import android.content.Context;
import android.util.Log;

import java.io.File;
import java.io.FileOutputStream;
import java.io.InputStream;

class AssetManager {
    private static final String TAG = "AssetManager";
    private static final String folder = "resource";
    public static void copyFilesFromAssets(Context context,String oldPath,String newPath) {
        try {
            String fileNames[] = context.getAssets().list(oldPath);
            if (fileNames.length > 0) {
                File file = new File(newPath);
                file.mkdirs();
                for (String fileName : fileNames) {
                    copyFilesFromAssets(context,oldPath + "/" + fileName,newPath+"/"+fileName);
                }
            } else {
                InputStream is = context.getAssets().open(oldPath);
                FileOutputStream fos = new FileOutputStream(new File(newPath));
                byte[] buffer = new byte[1024];
                int byteCount=0;
                while((byteCount=is.read(buffer))!=-1) {
                    fos.write(buffer, 0, byteCount);
                }
                fos.flush();
                is.close();
                fos.close();
            }
            Log.d(TAG, "Copy success");
        } catch (Exception e) {
            e.printStackTrace();
        }
    }
    public static void copyFileFromAssets(Context context){
        String filesDir = context.getFilesDir().getPath();
        filesDir = filesDir + "/assets/" + folder;
        copyFilesFromAssets(context,folder,filesDir);
    }

}
