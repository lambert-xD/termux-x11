package com.termux.x11;

import android.app.Activity;
import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.os.Bundle;
import android.os.IBinder;
import android.os.DeadObjectException;
import android.os.ParcelFileDescriptor;
import android.os.RemoteException;
import android.util.Log;
import android.view.Window;
import java.util.concurrent.atomic.AtomicReference;

public class WaylandActivity extends Activity {
    private final AtomicReference<ICmdEntryInterface> currentIface = new AtomicReference<>();
    private Thread connectionBridgeThread;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        requestWindowFeature(Window.FEATURE_NO_TITLE);
        setContentView(R.layout.wayland_activity);
        findViewById(R.id.lorieView).requestFocus();
        findViewById(R.id.exit_button).setOnClickListener(v -> finish());
        WaylandEntryPoint.start(new String[]{}, getFilesDir().getAbsolutePath());
        handleStartIntent(getIntent());
    }

    @Override protected void onNewIntent(Intent intent) {
        super.onNewIntent(intent);
        setIntent(intent);
        WaylandEntryPoint.start(new String[]{}, getFilesDir().getAbsolutePath());
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
        currentIface.set(iface);
        if (connectionBridgeThread != null && connectionBridgeThread.isAlive()) return;

        connectionBridgeThread = new Thread(() -> {
            while (!Thread.currentThread().isInterrupted()) {
                ICmdEntryInterface current = currentIface.get();
                if (current == null) {
                    try { Thread.sleep(50); } catch (InterruptedException e) { Thread.currentThread().interrupt(); }
                    continue;
                }
                try {
                    ParcelFileDescriptor pfd = current.getWaylandConnection();
                    if (pfd != null) {
                        WaylandEntryPoint.addClientFd(pfd.detachFd());
                    } else {
                        Thread.sleep(50);
                    }
                } catch (DeadObjectException e) {
                    Log.e("WaylandActivity", "Wayland connection died", e);
                    currentIface.compareAndSet(current, null);
                } catch (RemoteException e) {
                    Log.e("WaylandActivity", "Wayland connection bridge lost", e);
                    try { Thread.sleep(500); } catch (InterruptedException ie) { Thread.currentThread().interrupt(); }
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
