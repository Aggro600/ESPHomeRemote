package de.esphomeremote.streamer;

import android.accessibilityservice.AccessibilityService;
import android.os.Handler;
import android.os.Looper;
import android.view.accessibility.AccessibilityEvent;
import android.view.accessibility.AccessibilityNodeInfo;
import android.view.accessibility.AccessibilityWindowInfo;

import java.util.List;

// Vorderste App und Bildschirmtastatur. Android meldet Fensterwechsel; danach einmal die Fensterliste ansehen:
// ein Fenster vom Typ "Eingabemethode" = Tastatur offen, das aktive App-Fenster = vorderste App. Bei offener
// Tastatur die Art des Eingabefelds (Zahl/Text) fuer den passenden Ziffernblock der Fernbedienung.
public class Bedienung extends AccessibilityService {
    private final Handler h = new Handler(Looper.getMainLooper());
    private final Runnable pruefen = this::pruefen;

    @Override
    protected void onServiceConnected() {
        pruefen();
    }

    @Override
    public void onAccessibilityEvent(AccessibilityEvent e) {
        // Fensterwechsel kommen oft in Bursts - einmal danach pruefen
        h.removeCallbacks(pruefen);
        h.postDelayed(pruefen, 150);
    }

    @Override
    public void onInterrupt() {
    }

    private void pruefen() {
        boolean tastatur = false;
        String top = null;
        int art = 1;
        try {
            List<AccessibilityWindowInfo> fenster = getWindows();
            for (AccessibilityWindowInfo w : fenster) {
                final int t = w.getType();
                if (t == AccessibilityWindowInfo.TYPE_INPUT_METHOD) {
                    tastatur = true;
                } else if (t == AccessibilityWindowInfo.TYPE_APPLICATION && top == null && (w.isActive() || w.isFocused())) {
                    AccessibilityNodeInfo r = w.getRoot();
                    if (r != null && r.getPackageName() != null) top = r.getPackageName().toString();
                }
            }
            if (tastatur) {
                AccessibilityNodeInfo f = findFocus(AccessibilityNodeInfo.FOCUS_INPUT);
                if (f != null && f.getInputType() != 0) art = f.getInputType();
            }
        } catch (Exception ignored) {
            return;
        }
        Zustand.get(this).setBedienung(top, tastatur, art);
    }
}
