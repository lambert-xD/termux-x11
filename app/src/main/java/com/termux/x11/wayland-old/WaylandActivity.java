package com.termux.x11;

import static android.Manifest.permission.WRITE_SECURE_SETTINGS;
import static android.content.pm.PackageManager.PERMISSION_GRANTED;
import static android.os.Build.VERSION.SDK_INT;
import static android.view.KeyEvent.*;
import static android.view.WindowManager.LayoutParams.*;
import static com.termux.x11.WaylandEntryPoint.ACTION_START;
import static com.termux.x11.LoriePreferences.ACTION_PREFERENCES_CHANGED;

import android.Manifest;
import android.annotation.SuppressLint;
import android.app.AppOpsManager;
import android.app.Notification;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.content.BroadcastReceiver;
import android.content.ClipData;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.content.SharedPreferences;
import android.content.pm.ActivityInfo;
import android.content.res.Configuration;
import android.graphics.Canvas;
import android.graphics.Color;
import android.net.Uri;
import android.os.Build;
import android.os.Build.VERSION_CODES;
import android.os.Bundle;
import android.os.Handler;
import android.os.IBinder;
import android.os.ParcelFileDescriptor;
import android.os.RemoteException;
import android.os.SystemClock;
import android.service.notification.StatusBarNotification;
import android.util.DisplayMetrics;
import android.util.Log;
import android.view.Display;
import android.view.DragEvent;
import android.view.InputDevice;
import android.view.KeyEvent;
import android.view.MotionEvent;
import android.view.PointerIcon;
import android.view.View;
import android.view.ViewConfiguration;
import android.view.ViewGroup;
import android.view.ViewTreeObserver;
import android.view.Window;
import android.view.inputmethod.InputMethodManager;
import android.widget.Button;
import android.widget.EditText;
import android.widget.FrameLayout;
import android.widget.ImageButton;
import android.widget.LinearLayout;

import androidx.annotation.NonNull;
import androidx.appcompat.app.AppCompatActivity;
import androidx.core.app.NotificationCompat;
import androidx.core.math.MathUtils;
import androidx.viewpager.widget.ViewPager;

import com.termux.x11.input.InputEventSender;
import com.termux.x11.input.InputStub;
import com.termux.x11.input.TouchInputHandler;
import com.termux.x11.utils.FullscreenWorkaround;
import com.termux.x11.utils.KeyInterceptor;
import com.termux.x11.utils.SamsungDexUtils;
import com.termux.x11.utils.TermuxX11ExtraKeys;
import com.termux.x11.utils.X11ToolbarViewPager;

import java.util.Map;

/**
 * Main activity for Wayland compositor mode.
 * Similar to MainActivity but launches the Wayland compositor instead of X11.
 */
@SuppressLint("ApplySharedPref")
@SuppressWarnings({"deprecation", "unused"})
public class WaylandActivity extends AppCompatActivity {
    public static final String ACTION_STOP = "com.termux.x11.ACTION_STOP_WAYLAND";
    public static final String ACTION_CUSTOM = "com.termux.x11.ACTION_CUSTOM_WAYLAND";

    public static Handler handler = new Handler();
    FrameLayout frm;
    private TouchInputHandler mInputHandler;
    protected ICmdEntryInterface service = null;
    public TermuxX11ExtraKeys mExtraKeys;
    private Notification mNotification;
    private final int mNotificationId = 7893;
    NotificationManager mNotificationManager;
    static InputMethodManager inputMethodManager;
    private static boolean showIMEWhileExternalConnected = true;
    private static boolean externalKeyboardConnected = false;
    private View.OnKeyListener mLorieKeyListener;
    private boolean filterOutWinKey = false;
    boolean useTermuxEKBarBehaviour = false;
    private boolean isInPictureInPictureMode = false;

    public static Prefs prefs = null;

    private static boolean oldFullscreen = false, oldHideCutout = false;
    private final SharedPreferences.OnSharedPreferenceChangeListener preferencesChangedListener = (__, key) -> onPreferencesChanged(key);

    private final BroadcastReceiver receiver = new BroadcastReceiver() {
        @SuppressLint("UnspecifiedRegisterReceiverFlag")
        @Override
        public void onReceive(Context context, Intent intent) {
            prefs.recheckStoringSecondaryDisplayPreferences();
            if (ACTION_START.equals(intent.getAction())) {
                try {
                    Log.v("WaylandBroadcastReceiver", "Got new ACTION_START intent");
                    onReceiveConnection(intent);
                } catch (Exception e) {
                    Log.e("WaylandActivity", "Something went wrong while we extracted connection details from binder.", e);
                }
            } else if (ACTION_STOP.equals(intent.getAction())) {
                finishAffinity();
            } else if (ACTION_PREFERENCES_CHANGED.equals(intent.getAction())) {
                Log.d("WaylandActivity", "preference: " + intent.getStringExtra("key"));
                if (!"additionalKbdVisible".equals(intent.getStringExtra("key")))
                    onPreferencesChanged("");
            } else if (ACTION_CUSTOM.equals(intent.getAction())) {
                android.util.Log.d("ACTION_CUSTOM", "action " + intent.getStringExtra("what"));
                mInputHandler.extractUserActionFromPreferences(prefs, intent.getStringExtra("what")).accept(0, true);
            }
        }
    };

    ViewTreeObserver.OnPreDrawListener mOnPredrawListener = new ViewTreeObserver.OnPreDrawListener() {
        @Override
        public boolean onPreDraw() {
            if (LorieWaylandView.connected())
                handler.post(() -> findViewById(android.R.id.content).getViewTreeObserver().removeOnPreDrawListener(mOnPredrawListener));
            return false;
        }
    };

    @SuppressLint("StaticFieldLeak")
    private static WaylandActivity instance;

    public WaylandActivity() {
        instance = this;
    }

    public static Prefs getPrefs() {
        return prefs;
    }

    public static WaylandActivity getInstance() {
        return instance;
    }

    @Override
    @SuppressLint({"AppCompatMethod", "ObsoleteSdkInt", "ClickableViewAccessibility", "WrongConstant", "UnspecifiedRegisterReceiverFlag"})
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);

        prefs = new Prefs(this);
        int modeValue = Integer.parseInt(prefs.touchMode.get()) - 1;
        if (modeValue > 2)
            prefs.touchMode.put("1");

        oldFullscreen = prefs.fullscreen.get();
        oldHideCutout = prefs.hideCutout.get();

        prefs.get().registerOnSharedPreferenceChangeListener(preferencesChangedListener);

        getWindow().setFlags(FLAG_DRAWS_SYSTEM_BAR_BACKGROUNDS | FLAG_KEEP_SCREEN_ON | FLAG_TRANSLUCENT_STATUS, 0);
        requestWindowFeature(Window.FEATURE_NO_TITLE);
        setContentView(R.layout.wayland_activity);

        frm = findViewById(R.id.frame);
        findViewById(R.id.preferences_button).setOnClickListener((l) -> startActivity(new Intent(this, LoriePreferences.class) {{ setAction(Intent.ACTION_MAIN); }}));
        findViewById(R.id.help_button).setOnClickListener((l) -> startActivity(new Intent(Intent.ACTION_VIEW, Uri.parse("https://github.com/termux/termux-x11/blob/master/README.md#running-graphical-applications"))));
        findViewById(R.id.exit_button).setOnClickListener((l) -> finish());
        findViewById(R.id.switch_x11_button).setOnClickListener((l) -> {
            // Switch back to X11 mode
            Intent intent = new Intent(this, MainActivity.class);
            intent.setFlags(Intent.FLAG_ACTIVITY_NEW_TASK | Intent.FLAG_ACTIVITY_CLEAR_TOP);
            startActivity(intent);
            finish();
        });

        LorieWaylandView lorieView = findViewById(R.id.lorieView);
        View lorieParent = (View) lorieView.getParent();

        mInputHandler = new TouchInputHandler(this, new InputEventSender(lorieView));
        mLorieKeyListener = (v, k, e) -> {
            InputDevice dev = e.getDevice();
            boolean result = mInputHandler.sendKeyEvent(e);

            if (useTermuxEKBarBehaviour && mExtraKeys != null && (dev == null || dev.isVirtual()))
                mExtraKeys.unsetSpecialKeys();
            return result;
        };

        lorieParent.setOnTouchListener((v, e) -> {
            if (e.getAction() == MotionEvent.ACTION_DOWN)
                lorieParent.requestUnbufferedDispatch(e);

            return mInputHandler.handleTouchEvent(lorieParent, lorieView, e);
        });
        lorieParent.setOnHoverListener((v, e) -> mInputHandler.handleTouchEvent(lorieParent, lorieView, e));
        lorieParent.setOnGenericMotionListener((v, e) -> mInputHandler.handleTouchEvent(lorieParent, lorieView, e));
        lorieView.setOnCapturedPointerListener((v, e) -> mInputHandler.handleTouchEvent(lorieView, lorieView, e));
        lorieParent.setOnCapturedPointerListener((v, e) -> mInputHandler.handleTouchEvent(lorieView, lorieView, e));
        lorieView.setOnKeyListener(mLorieKeyListener);

        lorieView.setCallback((sfcWidth, sfcHeight, screenWidth, screenHeight) -> {
            handler.post(() -> {
                if (mExtraKeys != null) {
                    mExtraKeys.setWidth(sfcWidth);
                    mExtraKeys.setHeight(sfcHeight);
                }
            });
        });

        View.OnApplyWindowInsetsListener insetsListener = (v, insets) -> {
            int cutout = 0;
            if (SDK_INT >= VERSION_CODES.P)
                cutout = insets.getDisplayCutout() != null ? insets.getDisplayCutout().getSafeInsetTop() : 0;

            int paddingLeft = insets.getSystemWindowInsetLeft();
            int paddingTop = Math.max(insets.getSystemWindowInsetTop(), cutout);
            int paddingRight = insets.getSystemWindowInsetRight();
            int paddingBottom = insets.getSystemWindowInsetBottom();

            lorieView.setContentInsets(paddingLeft, paddingTop, paddingRight, paddingBottom);
            return insets.consumeSystemWindowInsets();
        };

        frm.setOnApplyWindowInsetsListener(insetsListener);

        if (SDK_INT >= VERSION_CODES.S) {
            getWindow().getAttributes().layoutInDisplayCutoutMode = LAYOUT_IN_DISPLAY_CUTOUT_MODE_ALWAYS;
        }

        findViewById(android.R.id.content).getViewTreeObserver().addOnPreDrawListener(mOnPredrawListener);

        setTermuxCursorVisibility();

        // Register broadcast receiver
        if (SDK_INT >= VERSION_CODES.TIRAMISU) {
            registerReceiver(receiver, new IntentFilter(ACTION_START) {{ addAction(ACTION_STOP); addAction(ACTION_PREFERENCES_CHANGED); addAction(ACTION_CUSTOM); }}, RECEIVER_EXPORTED);
        } else {
            registerReceiver(receiver, new IntentFilter(ACTION_START) {{ addAction(ACTION_STOP); addAction(ACTION_PREFERENCES_CHANGED); addAction(ACTION_CUSTOM); }});
        }

        checkForClipboardSharing();
    }

    @Override
    public void onWindowFocusChanged(boolean hasFocus) {
        super.onWindowFocusChanged(hasFocus);
        View decorView = getWindow().getDecorView();
        if (hasFocus) {
            if (prefs.fullscreen.get()) {
                getWindow().setFlags(FLAG_FULLSCREEN, FLAG_FULLSCREEN);
                getWindow().getDecorView().setSystemUiVisibility(
                        View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY
                                | View.SYSTEM_UI_FLAG_FULLSCREEN
                                | View.SYSTEM_UI_FLAG_HIDE_NAVIGATION
                                | View.SYSTEM_UI_FLAG_LAYOUT_STABLE
                                | View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION
                                | View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN);
            } else {
                getWindow().clearFlags(FLAG_FULLSCREEN);
                getWindow().getDecorView().setSystemUiVisibility(
                        View.SYSTEM_UI_FLAG_VISIBLE);
            }
        }
    }

    @Override
    protected void onResume() {
        super.onResume();
        if (prefs.fullscreen.get() != oldFullscreen || prefs.hideCutout.get() != oldHideCutout) {
            oldFullscreen = prefs.fullscreen.get();
            oldHideCutout = prefs.hideCutout.get();
            onWindowFocusChanged(true);
        }
    }

    @Override
    protected void onDestroy() {
        super.onDestroy();
        unregisterReceiver(receiver);
    }

    @Override
    protected void onPause() {
        super.onPause();
    }

    void onReceiveConnection(Intent intent) {
        ParcelFileDescriptor parcelFD = intent.getParcelableExtra("fd");
        if (parcelFD == null) {
            Log.e("WaylandActivity", "Something went wrong: parcelFD == null");
            return;
        }

        int fd = parcelFD.detachFd();
        Log.d("WaylandActivity", "Got fd: " + fd);
        LorieWaylandView.connect(fd);
    }

    void checkForClipboardSharing() {
        // Wayland clipboard sharing placeholder
    }

    void setTermuxCursorVisibility() {
        // Placeholder for cursor visibility handling
    }

    public boolean handleKey(KeyEvent e) {
        // Handle hardware keyboard events
        return false;
    }

    public void clientConnectedStateChanged() {
        handler.post(() -> {
            View stub = findViewById(R.id.stub);
            if (stub != null) {
                stub.setVisibility(LorieWaylandView.connected() ? View.GONE : View.VISIBLE);
            }
        });
    }

    void onPreferencesChanged(String key) {
        LorieWaylandView lorieView = findViewById(R.id.lorieView);
        if (lorieView != null)
            lorieView.reloadPreferences(prefs);
    }
}
