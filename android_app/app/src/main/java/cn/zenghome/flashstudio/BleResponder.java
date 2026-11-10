package cn.zenghome.flashstudio;
import android.bluetooth.*;
import android.bluetooth.le.*;
import android.content.Context;
import android.os.ParcelUuid;
import android.util.Log;
import java.nio.charset.StandardCharsets;
import java.util.*;

/** Same GATT UUIDs and BLE-PING/BLE-PONG protocol as the Core service. */
final class BleResponder implements AutoCloseable {
    static final UUID SERVICE=UUID.fromString("7b183224-9168-443e-a927-7aeea07e0001"),CHAR=UUID.fromString("7b183224-9168-443e-a927-7aeea07e0002"),CCCD=UUID.fromString("00002902-0000-1000-8000-00805f9b34fb");
    private final Set<BluetoothDevice> subscribers=Collections.synchronizedSet(new HashSet<>());
    private BluetoothGattServer server;private BluetoothLeAdvertiser advertiser;private BluetoothGattCharacteristic characteristic;
    private volatile byte[] value="NAN-READY\n".getBytes(StandardCharsets.US_ASCII);private volatile boolean closed;
    volatile String state="waiting for Android Bluetooth adapter";
    private final AdvertiseCallback advertising=new AdvertiseCallback(){
        @Override public void onStartSuccess(AdvertiseSettings settings){state="advertising";Log.i("Green2Services","BLE advertising ready");}
        @Override public void onStartFailure(int error){state="advertising error="+error;}
    };
    BleResponder(Context context){
        BluetoothManager manager=context.getSystemService(BluetoothManager.class);BluetoothAdapter adapter=manager==null?null:manager.getAdapter();
        if(adapter==null||!adapter.isEnabled())return;
        advertiser=adapter.getBluetoothLeAdvertiser();if(advertiser==null){state="BLE advertising unsupported";return;}
        server=manager.openGattServer(context,new BluetoothGattServerCallback(){
            @Override public void onServiceAdded(int status,BluetoothGattService service){
                if(closed)return;
                if(status!=BluetoothGatt.GATT_SUCCESS){state="GATT service failed";return;}
                AdvertiseData data=new AdvertiseData.Builder().addServiceUuid(new ParcelUuid(SERVICE)).build();
                advertiser.startAdvertising(new AdvertiseSettings.Builder().setConnectable(true).setAdvertiseMode(AdvertiseSettings.ADVERTISE_MODE_LOW_LATENCY).build(),data,advertising);
            }
            @Override public void onConnectionStateChange(BluetoothDevice device,int status,int state){if(state==BluetoothProfile.STATE_DISCONNECTED)subscribers.remove(device);}
            @Override public void onCharacteristicReadRequest(BluetoothDevice device,int request,int offset,BluetoothGattCharacteristic c){
                int result=offset>=0&&offset<=value.length?BluetoothGatt.GATT_SUCCESS:BluetoothGatt.GATT_INVALID_OFFSET;
                server.sendResponse(device,request,result,offset,result==BluetoothGatt.GATT_SUCCESS?Arrays.copyOfRange(value,offset,value.length):null);
            }
            @Override public void onCharacteristicWriteRequest(BluetoothDevice device,int request,BluetoothGattCharacteristic c,boolean prepared,boolean response,int offset,byte[] data){
                String text=new String(data,StandardCharsets.US_ASCII);int result=BluetoothGatt.GATT_REQUEST_NOT_SUPPORTED;
                if(offset!=0)result=BluetoothGatt.GATT_INVALID_OFFSET;
                else if(!prepared&&(text.equals("BLE-PING")||text.equals("BLE-PING\n")||text.equals("BLE-PING\r\n"))){value="BLE-PONG\n".getBytes(StandardCharsets.US_ASCII);result=BluetoothGatt.GATT_SUCCESS;}
                if(response)server.sendResponse(device,request,result,offset,null);
                if(result==BluetoothGatt.GATT_SUCCESS)synchronized(subscribers){for(BluetoothDevice peer:subscribers)server.notifyCharacteristicChanged(peer,characteristic,false,value);}
            }
            @Override public void onDescriptorReadRequest(BluetoothDevice device,int request,int offset,BluetoothGattDescriptor descriptor){
                byte[] bytes=subscribers.contains(device)?BluetoothGattDescriptor.ENABLE_NOTIFICATION_VALUE:BluetoothGattDescriptor.DISABLE_NOTIFICATION_VALUE;
                server.sendResponse(device,request,offset>=0&&offset<=2?BluetoothGatt.GATT_SUCCESS:BluetoothGatt.GATT_INVALID_OFFSET,offset,offset>=0&&offset<=2?Arrays.copyOfRange(bytes,offset,2):null);
            }
            @Override public void onDescriptorWriteRequest(BluetoothDevice device,int request,BluetoothGattDescriptor descriptor,boolean prepared,boolean response,int offset,byte[] bytes){
                int result=BluetoothGatt.GATT_REQUEST_NOT_SUPPORTED;
                if(offset!=0)result=BluetoothGatt.GATT_INVALID_OFFSET;
                else if(!prepared&&Arrays.equals(bytes,BluetoothGattDescriptor.ENABLE_NOTIFICATION_VALUE)){subscribers.add(device);result=BluetoothGatt.GATT_SUCCESS;}
                else if(!prepared&&Arrays.equals(bytes,BluetoothGattDescriptor.DISABLE_NOTIFICATION_VALUE)){subscribers.remove(device);result=BluetoothGatt.GATT_SUCCESS;}
                if(response)server.sendResponse(device,request,result,offset,null);
            }
        });
        if(server==null){state="GATT server unavailable";return;}
        BluetoothGattService service=new BluetoothGattService(SERVICE,BluetoothGattService.SERVICE_TYPE_PRIMARY);
        characteristic=new BluetoothGattCharacteristic(CHAR,BluetoothGattCharacteristic.PROPERTY_READ|BluetoothGattCharacteristic.PROPERTY_WRITE|BluetoothGattCharacteristic.PROPERTY_NOTIFY,BluetoothGattCharacteristic.PERMISSION_READ|BluetoothGattCharacteristic.PERMISSION_WRITE);
        characteristic.addDescriptor(new BluetoothGattDescriptor(CCCD,BluetoothGattDescriptor.PERMISSION_READ|BluetoothGattDescriptor.PERMISSION_WRITE));service.addCharacteristic(characteristic);server.addService(service);
    }
    @Override public void close(){closed=true;if(advertiser!=null){try{advertiser.stopAdvertising(advertising);}catch(IllegalStateException|SecurityException ignored){}}if(server!=null){try{server.close();}catch(IllegalStateException|SecurityException ignored){}}subscribers.clear();state="stopped";}
}
