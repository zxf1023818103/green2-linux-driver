package cn.zenghome.flashstudio;
final class NativeTransport {
    static native boolean checkVideo();
    static { System.loadLibrary("green2services"); }
    static native int listen(long network, String address, int scopeId, int port);
    static native int accept(int listener);
    static native long startVideo(long network, String address, int scopeId, int port);
    static native void sendJpeg(long handle, byte[] jpeg, long timestampUs, long sequence);
    static native void stopVideo(long handle);
}
