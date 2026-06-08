package com.termux.x11;

import org.junit.Test;
import org.junit.After;
import org.junit.Before;
import static org.junit.Assert.*;
import static org.mockito.Mockito.*;
import org.mockito.MockedStatic;

import android.os.DeadObjectException;
import android.os.ParcelFileDescriptor;
import android.os.RemoteException;
import android.os.IBinder;
import android.util.Log;

import java.lang.reflect.Field;
import java.lang.reflect.Method;
import java.util.concurrent.atomic.AtomicReference;

public class WaylandActivityTest {
    MockedStatic<Log> logMock;

    @Before
    public void setUp() {
        logMock = mockStatic(Log.class);
    }

    @After
    public void tearDown() {
        if (logMock != null) {
            logMock.close();
        }
    }

    @Test
    public void testDeadObjectExceptionClearsInterface() throws Exception {
        WaylandActivity activity = mock(WaylandActivity.class, withSettings().defaultAnswer(CALLS_REAL_METHODS));
        
        ICmdEntryInterface iface = mock(ICmdEntryInterface.class);
        when(iface.getWaylandConnection()).thenThrow(new DeadObjectException());

        Field ifaceField = WaylandActivity.class.getDeclaredField("currentIface");
        ifaceField.setAccessible(true);
        ifaceField.set(activity, new AtomicReference<>());

        Method startMethod = WaylandActivity.class.getDeclaredMethod("startConnectionBridge", ICmdEntryInterface.class);
        startMethod.setAccessible(true);
        startMethod.invoke(activity, iface);

        Field threadField = WaylandActivity.class.getDeclaredField("connectionBridgeThread");
        threadField.setAccessible(true);

        Thread thread = (Thread) threadField.get(activity);
        assertNotNull("Thread should be started", thread);

        Thread.sleep(200);

        AtomicReference<ICmdEntryInterface> current = (AtomicReference<ICmdEntryInterface>) ifaceField.get(activity);
        assertNull("Interface should be cleared on DeadObjectException", current.get());
        assertTrue("Thread should still be alive", thread.isAlive());

        thread.interrupt();
    }

    @Test
    public void testRemoteExceptionBacksOff() throws Exception {
        WaylandActivity activity = mock(WaylandActivity.class, withSettings().defaultAnswer(CALLS_REAL_METHODS));
        
        ICmdEntryInterface iface = mock(ICmdEntryInterface.class);
        when(iface.getWaylandConnection()).thenThrow(new RemoteException());

        Field ifaceField = WaylandActivity.class.getDeclaredField("currentIface");
        ifaceField.setAccessible(true);
        ifaceField.set(activity, new AtomicReference<>());

        Method startMethod = WaylandActivity.class.getDeclaredMethod("startConnectionBridge", ICmdEntryInterface.class);
        startMethod.setAccessible(true);
        startMethod.invoke(activity, iface);

        Field threadField = WaylandActivity.class.getDeclaredField("connectionBridgeThread");
        threadField.setAccessible(true);

        Thread thread = (Thread) threadField.get(activity);
        assertNotNull("Thread should be started", thread);

        Thread.sleep(200);

        AtomicReference<ICmdEntryInterface> current = (AtomicReference<ICmdEntryInterface>) ifaceField.get(activity);
        assertEquals("Interface should NOT be cleared on RemoteException", iface, current.get());
        assertTrue("Thread should still be alive", thread.isAlive());

        thread.interrupt();
    }
}
