package cn.zenghome.flashstudio;

import android.content.*;
import android.net.*;
import android.net.wifi.aware.*;
import android.os.Handler;
import android.util.Log;
import java.io.ByteArrayOutputStream;
import java.net.*;
import java.nio.charset.StandardCharsets;
import java.util.*;

/** Owns discovery, pairing and NDP through the public Android service APIs. */
final class AwareController implements AutoCloseable {
    private static final String TAG="Green2Services", SERVICE="_flashstudio._tcp", VIDEO_SERVICE="_flashstudio._udp";
    private final Context context; private final Handler handler;
    private final WifiAwareManager manager; private final ConnectivityManager connectivity;
    private final String pin; private final boolean cameraEnabled;
    private final Map<Network, Endpoint> endpoints=new HashMap<>();
    private ProbeServer earlyProbe;private int listenerAttempts;
    private WifiAwareSession attached; private PublishDiscoverySession published,videoPublished;
    private final Map<PublishDiscoverySession,ConnectivityManager.NetworkCallback> requests=new HashMap<>();
    private boolean closed,attaching;private long generation;
    String state="starting";
    private final BroadcastReceiver availability=new BroadcastReceiver(){
        @Override public void onReceive(Context c,Intent i){if(manager!=null&&manager.isAvailable())attach();else retire("aware unavailable");}
    };
    AwareController(Context c,Handler h,String password,boolean camera){
        context=c;handler=h;pin=password;cameraEnabled=camera;
        manager=c.getSystemService(WifiAwareManager.class);connectivity=c.getSystemService(ConnectivityManager.class);
        c.registerReceiver(availability,new IntentFilter(WifiAwareManager.ACTION_WIFI_AWARE_STATE_CHANGED),Context.RECEIVER_NOT_EXPORTED);
        attach();
    }
    private void status(String s){state=s;Log.i(TAG,"aware: "+s);}
    private void retry(){handler.postDelayed(()->{if(!closed)attach();},5000);}
    private void attach(){
        if(closed||attaching||attached!=null)return;
        if(manager==null||!manager.isAvailable()){status("waiting for Wi-Fi Aware");return;}
        Characteristics caps=manager.getCharacteristics();
        if(caps==null||!caps.isAwarePairingSupported()){status("waiting for HAL PIN pairing support");retry();return;}
        attaching=true;final long token=++generation;
        try{manager.attach(new AttachCallback(){
            @Override public void onAttached(WifiAwareSession session){if(closed||token!=generation){session.close();return;}attaching=false;attached=session;publish();}
            @Override public void onAttachFailed(){if(token!=generation)return;attaching=false;status("attach failed");retry();}
            @Override public void onAwareSessionTerminated(){if(token!=generation)return;attaching=false;retire("session terminated");retry();}
        },handler);}catch(RuntimeException e){attaching=false;status("attach: "+e.getClass().getSimpleName());retry();}
    }
    private void publish(){
        if(closed||attached==null)return;
        if(earlyProbe==null){
            try{
                NetworkInterface iface=NetworkInterface.getByName("aware_data0");
                byte[] mac=iface==null?null:iface.getHardwareAddress();
                if(iface==null||mac==null||mac.length!=6)throw new SocketException("NAN interface not ready");
                byte[] address=new byte[16];address[0]=(byte)0xfe;address[1]=(byte)0x80;
                address[8]=(byte)(mac[0]^2);address[9]=mac[1];address[10]=mac[2];address[11]=(byte)0xff;
                address[12]=(byte)0xfe;address[13]=mac[3];address[14]=mac[4];address[15]=mac[5];
                earlyProbe=new ProbeServer(0,InetAddress.getByAddress(address).getHostAddress(),iface.getIndex(),8000);
                listenerAttempts=0;Log.i(TAG,"TCP 8000 listening on NAN before publication");
            }catch(Exception e){
                if(++listenerAttempts<100){final long token=generation;handler.postDelayed(()->{if(token==generation)publish();},50);return;}
                retire("early TCP listener: "+e.getClass().getSimpleName());retry();return;
            }
        }
        Characteristics caps=manager.getCharacteristics();
        if(caps==null||!caps.isAwarePairingSupported()){
            status("waiting for HAL PIN pairing support");
            // Preserve the existing authenticated client protocol: never publish it as open.
            retire(state);retry();return;
        }
        AwarePairingConfig pairing=new AwarePairingConfig.Builder().setPairingSetupEnabled(true)
                .setPairingVerificationEnabled(true).setPairingCacheEnabled(true)
                .setBootstrappingMethods(AwarePairingConfig.PAIRING_BOOTSTRAPPING_PIN_CODE_DISPLAY).build();
        publishOne(SERVICE,pairing);publishOne(VIDEO_SERVICE,pairing);
    }
    private void publishOne(String name,AwarePairingConfig pairing){
        PublishConfig config=new PublishConfig.Builder().setServiceName(name)
                .setPublishType(PublishConfig.PUBLISH_TYPE_UNSOLICITED).setTtlSec(0)
                .setServiceSpecificInfo(serviceInfo(name)).setPairingConfig(pairing).build();
        final long token=generation;
        attached.publish(config,new DiscoverySessionCallback(){
            private PublishDiscoverySession current;
            @Override public void onPublishStarted(PublishDiscoverySession session){
                if(token!=generation||closed){session.close();return;}
                current=session;if(name.equals(SERVICE))published=session;else videoPublished=session;
                status(published!=null&&videoPublished!=null?"published "+SERVICE+" + "+VIDEO_SERVICE:"publishing services");
                requestResponder(session,null);
            }
            @Override public void onSessionConfigFailed(){if(token!=generation)return;retire("publish failed: "+name);retry();}
            @Override public void onSessionTerminated(){if(token!=generation)return;retire("publish terminated: "+name);retry();}
            @Override public void onPairingSetupRequestReceived(PeerHandle peer,int request){
                if(token==generation&&current!=null)current.acceptPairingRequest(request,peer,"FlashStudio",
                        Characteristics.WIFI_AWARE_CIPHER_SUITE_NCS_PK_PASN_128,pin);
            }
            @Override public void onPairingSetupSucceeded(PeerHandle peer,String alias){if(token!=generation)return;status("paired");requestResponder(current,peer);}
            @Override public void onPairingVerificationSucceed(PeerHandle peer,String alias){if(token!=generation)return;status("pairing verified");requestResponder(current,peer);}
            @Override public void onPairingSetupFailed(PeerHandle peer){if(token==generation)status("pairing failed");}
            @Override public void onMessageReceived(PeerHandle peer,byte[] message){
                if(current!=null&&Arrays.equals(message,"NAN-PING\n".getBytes(StandardCharsets.US_ASCII)))
                    current.sendMessage(peer,1,"NAN-PONG\n".getBytes(StandardCharsets.US_ASCII));
            }
        },handler);
    }
    private void requestResponder(PublishDiscoverySession session,PeerHandle peer){
        try{
            if(session==null||requests.containsKey(session))return;
            // HAL enforces verified PASN before accepting an encrypted NDP.
            WifiAwareNetworkSpecifier spec=(peer==null?new WifiAwareNetworkSpecifier.Builder(session):new WifiAwareNetworkSpecifier.Builder(session,peer)).build();
            final long token=generation;
            ConnectivityManager.NetworkCallback callback=new ConnectivityManager.NetworkCallback(){
                @Override public void onLinkPropertiesChanged(Network network,LinkProperties properties){if(token==generation)bind(network,properties);}
                @Override public void onLost(Network network){if(token!=generation)return;Endpoint endpoint=endpoints.remove(network);if(endpoint!=null)endpoint.close();status("peer disconnected");}
                @Override public void onUnavailable(){if(token==generation)status("NDP request unavailable");}
            };
            NetworkRequest request=new NetworkRequest.Builder().addTransportType(NetworkCapabilities.TRANSPORT_WIFI_AWARE)
                    .removeCapability(NetworkCapabilities.NET_CAPABILITY_INTERNET).setNetworkSpecifier(spec).build();
            connectivity.requestNetwork(request,callback,handler);requests.put(session,callback);
        }catch(RuntimeException e){status("NDP: "+e.getClass().getSimpleName());}
    }
    private void bind(Network network,LinkProperties properties){
        if(endpoints.containsKey(network)||closed)return;
        for(LinkAddress link:properties.getLinkAddresses()){
            if(!(link.getAddress() instanceof Inet6Address)||!link.getAddress().isLinkLocalAddress())continue;
            try{
                NetworkInterface iface=NetworkInterface.getByName(properties.getInterfaceName());
                if(iface==null)return;
                Endpoint endpoint=new Endpoint(network,link.getAddress().getHostAddress().split("%")[0],iface.getIndex());
                endpoints.put(network,endpoint);status("NDP connected; TCP 8000 ready");return;
            }catch(Exception e){status("bind: "+e.getClass().getSimpleName());}
        }
    }
    private final class Endpoint implements AutoCloseable {
        final ProbeServer probe; final CameraSender camera;
        Endpoint(Network network,String address,int scope)throws Exception{
            probe=earlyProbe==null?new ProbeServer(network.getNetworkHandle(),address,scope,8000):null;
            try{camera=cameraEnabled?new CameraSender(context,handler,network.getNetworkHandle(),address,scope):null;}
            catch(RuntimeException|Error e){if(probe!=null)probe.close();throw e;}
        }
        public void close(){if(camera!=null)camera.close();if(probe!=null)probe.close();}
    }
    private void retire(String reason){
        generation++;attaching=false;listenerAttempts=0;if(earlyProbe!=null){earlyProbe.close();earlyProbe=null;}
        for(ConnectivityManager.NetworkCallback callback:requests.values()){try{connectivity.unregisterNetworkCallback(callback);}catch(IllegalArgumentException ignored){}}requests.clear();
        for(Endpoint e:endpoints.values())e.close();endpoints.clear();
        if(published!=null){published.close();published=null;}if(videoPublished!=null){videoPublished.close();videoPublished=null;}
        if(attached!=null){attached.close();attached=null;}
        status(reason);
    }
    private static void field(ByteArrayOutputStream b,int type,byte[] data){b.write(type);b.write(data.length&255);b.write(data.length>>8);b.write(data,0,data.length);}
    static byte[] serviceInfo(String service){
        ByteArrayOutputStream b=new ByteArrayOutputStream();b.write(0x50);b.write(0x6f);b.write(0x9a);b.write(2);
        field(b,2,service.getBytes(StandardCharsets.UTF_8));field(b,3,"Green2 NAN".getBytes(StandardCharsets.UTF_8));
        ByteArrayOutputStream txt=new ByteArrayOutputStream();
        for(String value:new String[]{"vendorName=RTK","pairingName=Green2NAN","modelName=RTL8711Dx"}){
            byte[] v=value.getBytes(StandardCharsets.UTF_8);txt.write(v.length);txt.write(v,0,v.length);
        }
        field(b,4,txt.toByteArray());return b.toByteArray();
    }
    @Override public void close(){closed=true;context.unregisterReceiver(availability);retire("stopped");}
}
