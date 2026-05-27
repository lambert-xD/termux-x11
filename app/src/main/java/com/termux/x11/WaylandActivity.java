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
    private Thread connectionBridgeThread;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        requestWindowFeature(Window.FEATURE_NO_TITLE);
        setContentView(R.layout.wayland_activity);
        findViewById(R.id.exit_button).setOnClickListener(v -> finish());
        WaylandEntryPoint.start(new String[]{});
        handleStartIntent(getIntent());
    }

    @Override protected void onNewIntent(Intent intent) {
        super.onNewIntent(intent);
        setIntent(intent);
        WaylandEntryPoint.start(new String[]{});
        handleStartIntent(intent);
    }

    @Override protected void onDestroy() {
        if (connectionBridgeThread != null) {
            connectionBridgeThread.interrupt();
            connectionBridgeThread = null;
        }
        super.onDestroy();
        WaylandEntryPoint.stop();
    }

    private void handleStartIntent(Intent intent) {
        if (intent == null) return;
        Bundle bundle = intent.getBundleExtra(null);
        if (bundle == null) return;
        IBinder binder = bundle.getBinder(null);
        if (binder == null) return;

        ICmdEntryInterface iface = ICmdEntryInterface.Stub.asInterface(binder);
        startConnectionBridge(iface);
    }

    private synchronized void startConnectionBridge(ICmdEntryInterface iface) {
        if (connectionBridgeThread != null && connectionBridgeThread.isAlive()) return;

        connectionBridgeThread = new Thread(() -> {
            while (!Thread.currentThread().isInterrupted()) {
                try {
                    ParcelFileDescriptor pfd = iface.getWaylandConnection();
                    if (pfd != null) {
                        WaylandEntryPoint.addClientFd(pfd.detachFd());
                    } else {
                        Thread.sleep(50);
                    }
                } catch (RemoteException e) {
                    Log.e("WaylandActivity", "Wayland connection bridge lost", e);
                    return;
                } catch (InterruptedException e) {
                    Thread.currentThread().interrupt();
                }
            }
        }, "WaylandConnectionBridge");
        connectionBridgeThread.start();
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
