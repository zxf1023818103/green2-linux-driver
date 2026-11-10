package cn.zenghome.flashstudio;
import android.app.Instrumentation;
import android.app.Activity;
import android.net.wifi.aware.*;
import android.os.*;
import java.io.*;
import java.net.Socket;
import java.nio.charset.StandardCharsets;
import java.util.concurrent.*;

/** Hardware-backed API and wire-protocol checks, runnable with am instrument. */
public final class ServiceChecks extends Instrumentation {
    private Bundle arguments;
    @Override public void onCreate(Bundle args){arguments=args;start();}
    private static void require(boolean condition,String message){if(!condition)throw new AssertionError(message);}
    private static String line(DataInputStream in)throws IOException{ByteArrayOutputStream b=new ByteArrayOutputStream();for(int n=0;n<1024;n++){int v=in.read();if(v<0)throw new EOFException();if(v==10)return b.toString("US-ASCII");b.write(v);}throw new IOException("long line");}
    @Override public void onStart(){Bundle result=new Bundle();HandlerThread events=new HandlerThread("aware-checks");events.start();WifiAwareSession[] attached={null};PublishDiscoverySession[] published={null};
        try{
            if("video".equals(arguments.getString("mode"))){
                require(NativeTransport.checkVideo(),"SRT multipart loopback failed");result.putString("SRT","PASS: live connection, JPEG bytes, multipart headers and timestamps");
            }else if("protocol".equals(arguments.getString("mode"))){
                try(ProbeServer server=new ProbeServer(0,"127.0.0.1",0,18000)){
                    try(Socket socket=new Socket("127.0.0.1",18000)){socket.setSoTimeout(5000);socket.getOutputStream().write("NAN-PING\n".getBytes(StandardCharsets.US_ASCII));require(line(new DataInputStream(socket.getInputStream())).equals("NAN-PONG"),"PING reply mismatch");}
                    try(Socket socket=new Socket("127.0.0.1",18000)){socket.setSoTimeout(10000);DataOutputStream out=new DataOutputStream(socket.getOutputStream());DataInputStream in=new DataInputStream(socket.getInputStream());
                        out.writeBytes("NAN-THROUGHPUT/1 upload-check UPLOAD 1\n");out.flush();require(line(in).equals("READY"),"upload READY");out.writeInt(3);out.write(new byte[]{1,2,3});out.writeInt(0);out.flush();String stats=line(in);require(stats.contains("\"bytes\":3")&&stats.contains("\"counter\":\"received\""),"upload accounting");
                        out.writeBytes("NAN-THROUGHPUT/1 download-check DOWNLOAD 1\n");out.flush();require(line(in).equals("READY"),"download READY");long count=0;
                        for(;;){int n=in.readInt();if(n==0)break;require(n>0&&n<=262144,"download frame bound");byte[] b=new byte[n];in.readFully(b);for(int i=0;i<n;i++)require(b[i]==(byte)i,"download payload");count+=n;}
                        stats=line(in);require(count>0&&stats.contains("\"bytes\":"+count)&&stats.contains("\"counter\":\"submitted\""),"download accounting");
                    }
                }
                result.putString("protocol","PASS: ping, upload, download and connection reuse");
            }else{
                WifiAwareManager manager=getTargetContext().getSystemService(WifiAwareManager.class);require(manager!=null,"WifiAwareManager missing");require(manager.isAvailable(),"Wi-Fi Aware unavailable");
                CountDownLatch ready=new CountDownLatch(1);String[] error={null};
                manager.attach(new AttachCallback(){
                    @Override public void onAttached(WifiAwareSession session){attached[0]=session;session.publish(new PublishConfig.Builder().setServiceName("flashstudio-api-check").setPublishType(PublishConfig.PUBLISH_TYPE_UNSOLICITED).setTtlSec(10).build(),new DiscoverySessionCallback(){
                        @Override public void onPublishStarted(PublishDiscoverySession session){published[0]=session;ready.countDown();}
                        @Override public void onSessionConfigFailed(){error[0]="publish failed";ready.countDown();}
                    },new Handler(events.getLooper()));}
                    @Override public void onAttachFailed(){error[0]="attach failed";ready.countDown();}
                },new Handler(events.getLooper()));
                require(ready.await(20,TimeUnit.SECONDS),"attach/publish timeout");require(error[0]==null,error[0]);require(published[0]!=null,"no publish session");
                result.putString("WifiAwareManager","PASS: attach and real publish callback");result.putBoolean("pairing_supported",manager.getCharacteristics().isAwarePairingSupported());
            }
            result.putString("result","PASS");finish(Activity.RESULT_OK,result);
        }catch(Throwable e){result.putString("result","FAIL");result.putString("error",e.toString());finish(Activity.RESULT_CANCELED,result);}
        finally{if(published[0]!=null)published[0].close();if(attached[0]!=null)attached[0].close();events.quitSafely();}
    }
}
