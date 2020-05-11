package com.potterhsu.rtsplibrary;

import android.util.Log;

public class RtspClient {

    private static final String TAG = RtspClient.class.getSimpleName();

    static {
        System.loadLibrary("rtsp");
    }

    /**
     * Play stream synchronously.
     * @param endpoint resource endpoint
     * @return 0 if exit normally or -1 otherwise
     */
    public native int play(String endpoint, NativeCallback callback);
    public native void stop();
}
