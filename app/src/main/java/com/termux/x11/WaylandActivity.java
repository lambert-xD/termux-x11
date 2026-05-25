package com.termux.x11;

import android.app.Activity;
import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.os.Bundle;
import android.os.IBinder;
import android.os.ParcelFileDescriptor;
import android.os.RemoteException;
import android.util.Log;
import android.view.Window;

public class WaylandActivity extends Activity {
    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        requestWindowFeature(Window.FEATURE_NO_TITLE);
        setContentView(R.layout.wayland_activity);
        findViewById(R.id.exit_button).setOnClickListener(v -> finish());
        handleStartIntent(getIntent());
        WaylandEntryPoint.start(new String[]{});
    }

    @Override protected void onNewIntent(Intent intent) {
        super.onNewIntent(intent);
        setIntent(intent);
        handleStartIntent(intent);
        WaylandEntryPoint.start(new String[]{});
    }

    @Override protected void onDestroy() {
        super.onDestroy();
        WaylandEntryPoint.stop();
    }

    private static void handleStartIntent(Intent intent) {
        if (intent == null) return;
        Bundle bundle = intent.getBundleExtra(null);
        if (bundle == null) return;
        IBinder binder = bundle.getBinder(null);
        if (binder == null) return;

        ICmdEntryInterface iface = ICmdEntryInterface.Stub.asInterface(binder);
        try {
            ParcelFileDescriptor pfd = iface.getWaylandSocketFd();
            if (pfd != null) {
                WaylandEntryPoint.setSocketFd(pfd.detachFd());
            }
        } catch (RemoteException e) {
            Log.e("WaylandActivity", "Failed to get Wayland socket fd", e);
        }
    }

    public static class StartReceiver extends BroadcastReceiver {
        @Override
        public void onReceive(Context context, Intent intent) {
            if (WaylandCmdEntryPoint.ACTION_START.equals(intent.getAction())) {
                Intent activityIntent = new Intent(context, WaylandActivity.class);
                activityIntent.putExtras(intent);
                activityIntent.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK | Intent.FLAG_ACTIVITY_CLEAR_TOP |
                    Intent.FLAG_ACTIVITY_SINGLE_TOP);
                context.startActivity(activityIntent);
            }
        }
    }
}
