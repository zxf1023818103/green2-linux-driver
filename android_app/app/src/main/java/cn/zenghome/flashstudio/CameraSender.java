package cn.zenghome.flashstudio;
import android.content.Context;
import android.graphics.ImageFormat;
import android.hardware.camera2.*;
import android.hardware.camera2.params.StreamConfigurationMap;
import android.media.Image;
import android.media.ImageReader;
import android.os.Handler;
import android.os.SystemClock;
import android.util.Log;
import android.util.Range;
import android.util.Size;
import java.nio.ByteBuffer;
import java.util.*;

/** Camera2 JPEG capture; keeps only the newest frame in the native SRT sender. */
final class CameraSender implements AutoCloseable {
    private final CameraManager manager;private final Handler handler;
    private CameraDevice camera;private CameraCaptureSession session;private ImageReader reader;
    private long video,sequence;private boolean closed,opening,realtime;
    private final CameraManager.AvailabilityCallback availability=new CameraManager.AvailabilityCallback(){
        @Override public void onCameraAvailable(String id){open(id);}
    };
    CameraSender(Context context,Handler h,long network,String address,int scope){
        handler=h;manager=context.getSystemService(CameraManager.class);
        video=NativeTransport.startVideo(network,address,scope,9000);
        if(video==0){Log.e("Green2Services","SRT listener failed");return;}
        if(manager==null){Log.w("Green2Services","waiting for camera service");return;}
        manager.registerAvailabilityCallback(availability,handler);
        try{String[] ids=manager.getCameraIdList();if(ids.length==0)Log.i("Green2Services","SRT waiting for camera");else open(ids[0]);}
        catch(CameraAccessException e){Log.w("Green2Services","camera enumeration failed");}
    }
    private void open(String id){
        if(closed||opening||camera!=null||video==0)return;
        opening=true;
        try{
            CameraCharacteristics c=manager.getCameraCharacteristics(id);StreamConfigurationMap map=c.get(CameraCharacteristics.SCALER_STREAM_CONFIGURATION_MAP);
            Size[] sizes=map==null?null:map.getOutputSizes(ImageFormat.JPEG);
            if(sizes==null||sizes.length==0){opening=false;return;}
            Size size=Arrays.stream(sizes).min(Comparator.comparingLong(s->Math.abs((long)s.getWidth()*s.getHeight()-1280L*720))).get();
            realtime=Objects.equals(c.get(CameraCharacteristics.SENSOR_INFO_TIMESTAMP_SOURCE),CameraCharacteristics.SENSOR_INFO_TIMESTAMP_SOURCE_REALTIME);
            reader=ImageReader.newInstance(size.getWidth(),size.getHeight(),ImageFormat.JPEG,2);
            reader.setOnImageAvailableListener(r->{try(Image image=r.acquireLatestImage()){
                if(image==null||closed||video==0)return;ByteBuffer buffer=image.getPlanes()[0].getBuffer();byte[] jpeg=new byte[buffer.remaining()];buffer.get(jpeg);
                long time=System.currentTimeMillis()*1000;
                if(realtime)time-=(SystemClock.elapsedRealtimeNanos()-image.getTimestamp())/1000;
                NativeTransport.sendJpeg(video,jpeg,time,sequence++);
            }catch(RuntimeException e){Log.w("Green2Services","JPEG delivery failed: "+e.getClass().getSimpleName());}},handler);
            manager.openCamera(id,new CameraDevice.StateCallback(){
                @Override public void onOpened(CameraDevice device){opening=false;if(closed){device.close();return;}camera=device;configure(c);}
                @Override public void onDisconnected(CameraDevice device){device.close();if(camera==device)camera=null;opening=false;}
                @Override public void onError(CameraDevice device,int error){onDisconnected(device);Log.w("Green2Services","camera error="+error);}
            },handler);
        }catch(CameraAccessException|SecurityException e){opening=false;if(reader!=null){reader.close();reader=null;}Log.w("Green2Services","camera: "+e.getClass().getSimpleName());}
    }
    private void configure(CameraCharacteristics c){
        try{camera.createCaptureSession(Collections.singletonList(reader.getSurface()),new CameraCaptureSession.StateCallback(){
            @Override public void onConfigured(CameraCaptureSession s){
                if(closed){s.close();return;}session=s;
                try{CaptureRequest.Builder request=camera.createCaptureRequest(CameraDevice.TEMPLATE_RECORD);request.addTarget(reader.getSurface());
                    Range<Integer>[] fps=c.get(CameraCharacteristics.CONTROL_AE_AVAILABLE_TARGET_FPS_RANGES);
                    if(fps!=null)for(Range<Integer> range:fps)if(range.getLower()==30&&range.getUpper()==30){request.set(CaptureRequest.CONTROL_AE_TARGET_FPS_RANGE,range);break;}
                    s.setRepeatingRequest(request.build(),null,handler);Log.i("Green2Services","Camera2 JPEG -> SRT port 9000 ready");
                }catch(CameraAccessException|IllegalArgumentException e){Log.w("Green2Services","JPEG capture start failed");}
            }
            @Override public void onConfigureFailed(CameraCaptureSession s){s.close();Log.w("Green2Services","JPEG session configuration failed");}
        },handler);}catch(CameraAccessException e){Log.w("Green2Services","camera session failed");}
    }
    @Override public void close(){closed=true;if(manager!=null)manager.unregisterAvailabilityCallback(availability);if(session!=null)session.close();if(camera!=null)camera.close();if(reader!=null)reader.close();if(video!=0){NativeTransport.stopVideo(video);video=0;}}
}
