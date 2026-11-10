package cn.zenghome.flashstudio;
import android.app.*;
import android.bluetooth.BluetoothAdapter;
import android.content.*;
import android.content.pm.ServiceInfo;
import android.os.*;
import android.util.Log;
import java.io.*;

/** Headless replacement for the Core nan/ble/camera service supervisor. */
public final class Green2Service extends Service {
    private HandlerThread thread;private Handler handler;
    private AwareController aware;private BleResponder ble;
    private final BroadcastReceiver bluetooth=new BroadcastReceiver(){
        @Override public void onReceive(Context c,Intent i){handler.post(()->restartBle());}
    };
    @Override public void onCreate(){
        super.onCreate();thread=new HandlerThread("flashstudio-services");thread.start();handler=new Handler(thread.getLooper());
        NotificationManager n=getSystemService(NotificationManager.class);
        n.createNotificationChannel(new NotificationChannel("services","Flash Studio services",NotificationManager.IMPORTANCE_LOW));
        Notification notification=new Notification.Builder(this,"services").setSmallIcon(android.R.drawable.stat_notify_sync)
                .setContentTitle("Flash Studio").setContentText("Wi-Fi Aware and BLE services").setOngoing(true).build();
        startForeground(8000,notification,ServiceInfo.FOREGROUND_SERVICE_TYPE_CONNECTED_DEVICE);
        // Bluetooth state is a protected broadcast from the Bluetooth UID (1002).
        registerReceiver(bluetooth,new IntentFilter(BluetoothAdapter.ACTION_STATE_CHANGED),Context.RECEIVER_EXPORTED);
    }
    @Override public int onStartCommand(Intent intent,int flags,int startId){
        SharedPreferences prefs=getSharedPreferences("config",0);
        if(intent!=null&&"cn.zenghome.flashstudio.STOP".equals(intent.getAction())){prefs.edit().putBoolean("enabled",false).apply();stopSelf();return START_NOT_STICKY;}
        if(intent!=null){SharedPreferences.Editor edit=prefs.edit().putBoolean("enabled",true);
            for(String key:new String[]{"ble","camera"})if(intent.hasExtra(key))edit.putBoolean(key,intent.getBooleanExtra(key,true));
            if(intent.hasExtra("pin")){String pin=intent.getStringExtra("pin");if(pin!=null&&pin.matches("[0-9]{6}"))edit.putString("pin",pin);else Log.e("Green2Services","PIN must contain six digits");}
            edit.apply();}
        handler.post(()->{if(aware!=null)aware.close();aware=new AwareController(this,handler,prefs.getString("pin","123456"),prefs.getBoolean("camera",true));restartBle();});
        return START_STICKY;
    }
    private void restartBle(){if(ble!=null){ble.close();ble=null;}if(getSharedPreferences("config",0).getBoolean("ble",true)){
        try{ble=new BleResponder(this);}catch(RuntimeException e){Log.w("Green2Services","BLE: "+e.getClass().getSimpleName());}}}
    @Override public IBinder onBind(Intent intent){return null;}
    @Override protected void dump(FileDescriptor fd,PrintWriter out,String[] args){
        out.println("Flash Studio headless services");out.println("package=cn.zenghome.flashstudio");
        out.println("aware="+(aware==null?"stopped":aware.state));out.println("ble="+(ble==null?"stopped":ble.state));
        out.println("TCP protocol=NAN-PING and NAN-THROUGHPUT/1 port=8000");out.println("video=SRT MJPEG multipart port=9000");
        out.println("camera_enabled="+getSharedPreferences("config",0).getBoolean("camera",true));
    }
    @Override public void onDestroy(){unregisterReceiver(bluetooth);handler.post(()->{if(aware!=null)aware.close();if(ble!=null)ble.close();thread.quitSafely();});super.onDestroy();}
}
