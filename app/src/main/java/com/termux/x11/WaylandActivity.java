package com.termux.x11;

import android.app.Activity;
import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.os.Bundle;
import android.view.Window;

public class WaylandActivity extends Activity {
    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        requestWindowFeature(Window.FEATURE_NO_TITLE);
        setContentView(R.layout.wayland_activity);
        findViewById(R.id.exit_button).setOnClickListener(v -> finish());
        WaylandEntryPoint.start(new String[]{});
    }

    @Override protected void onPause() {
        super.onPause();
        WaylandEntryPoint.stop();
    }

    @Override protected void onResume() {
        super.onResume();
        WaylandEntryPoint.start(new String[]{});
    }

    @Override protected void onDestroy() {
        super.onDestroy();
        WaylandEntryPoint.stop();
    }

    public static class StartReceiver extends BroadcastReceiver {
        @Override
        public void onReceive(Context context, Intent intent) {
            if (WaylandCmdEntryPoint.ACTION_START.equals(intent.getAction())) {
                Intent activityIntent = new Intent(context, WaylandActivity.class);
                activityIntent.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK | Intent.FLAG_ACTIVITY_CLEAR_TOP);
                context.startActivity(activityIntent);
            }
        }
    }
}
