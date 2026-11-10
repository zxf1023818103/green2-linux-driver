package cn.zenghome.flashstudio;
import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
public final class BootReceiver extends BroadcastReceiver {
    @Override public void onReceive(Context context, Intent intent) {
        if (Intent.ACTION_BOOT_COMPLETED.equals(intent.getAction()) &&
                context.getSharedPreferences("config", 0).getBoolean("enabled", true))
            context.startForegroundService(new Intent(context, Green2Service.class));
    }
}
