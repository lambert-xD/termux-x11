package com.termux.x11;

import android.app.Activity;
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
}
