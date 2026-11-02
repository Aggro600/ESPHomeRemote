package de.esphomeremote.streamer;

import android.accessibilityservice.AccessibilityService;
import android.content.Context;
import android.content.Intent;
import android.graphics.Color;
import android.graphics.PixelFormat;
import android.graphics.drawable.GradientDrawable;
import android.os.Handler;
import android.os.Looper;
import android.util.DisplayMetrics;
import android.util.Log;
import android.view.Gravity;
import android.view.WindowManager;
import android.webkit.WebChromeClient;
import android.webkit.WebSettings;
import android.webkit.WebView;
import android.widget.FrameLayout;

import org.json.JSONObject;

// Kamera als Overlay ueber allem (2026-11-01). Nutzer: "nur als Overlay, keine App, die sich oeffnet - das laufende
// Video (YouTube ...) soll nicht pausieren", verschiedene Groessen und (fast) Vollbild.
//
// Das Fenster gehoert dem Bedienungshilfe-Dienst (TYPE_ACCESSIBILITY_OVERLAY - braucht kein "Ueber anderen Apps
// anzeigen") und nimmt nie Fokus oder Beruehrung: die App im Vordergrund merkt nichts davon, das Video ist stumm.
// Darin eine kleine Webseite: WebRTC ueber Home Assistant (tvkamera.py), sonst HLS, sonst Standbilder.
//
// Gesteuert per ADB (Home Assistant, funktionen.py):
//   am broadcast -a de.esphomeremote.streamer.CAMERA -p de.esphomeremote.streamer --es aktion zeigen
//      --es kamera camera.x --es name 'Haustuer' --es basis http://<ha>/api/esphomeremote/tvkamera/<geheim>
//      --es groesse Klein|Mittel|Groß|'Fast Vollbild' --es position top-right|top-left|bottom-right|bottom-left|center
//      --ei sekunden 0   (0 = bleibt, bis "aus")
//   ... --es aktion aus
final class Camera {
    static final String AKTION = "de.esphomeremote.streamer.CAMERA";
    private static final Handler h = new Handler(Looper.getMainLooper());
    private static FrameLayout rahmen;
    private static WebView web;
    private static String kamera = "", basis = "";
    private static final Runnable schliessen = Camera::aus;

    static void befehl(AccessibilityService s, Intent i) {
        final String a = i.getStringExtra("aktion");
        if ("aus".equals(a)) {
            aus();
            return;
        }
        if (!"zeigen".equals(a)) return;
        final String k = nz(i.getStringExtra("kamera")), b = nz(i.getStringExtra("basis"));
        if (!k.startsWith("camera.") || !b.startsWith("http")) return;
        try {
            zeigen(s, k, nz(i.getStringExtra("name")), b, nz(i.getStringExtra("groesse")), nz(i.getStringExtra("position")));
        } catch (Exception ex) {
            Log.w(State.TAG, "Kamera: " + ex);
        }
        h.removeCallbacks(schliessen);
        final int sek = i.getIntExtra("sekunden", 0);
        if (sek > 0) h.postDelayed(schliessen, sek * 1000L);
    }

    private static String nz(String s) { return s == null ? "" : s.trim(); }

    private static WindowManager.LayoutParams lage(Context c, String groesse, String position) {
        DisplayMetrics m = c.getResources().getDisplayMetrics();
        final int W = m.widthPixels, H = m.heightPixels, rand = Math.round(W * 0.025f);
        int w, hh;
        if (groesse.startsWith("Fast")) {   // fast Vollbild: schmaler Rand rundum
            w = W - 2 * rand;
            hh = H - 2 * rand;
        } else {
            final float f = groesse.equals("Klein") ? 0.24f : groesse.startsWith("Gro") ? 0.5f : 0.34f;
            w = Math.round(W * f);
            hh = Math.round(w * 9f / 16f);
        }
        WindowManager.LayoutParams p = new WindowManager.LayoutParams(w, hh,
                WindowManager.LayoutParams.TYPE_ACCESSIBILITY_OVERLAY,
                WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE | WindowManager.LayoutParams.FLAG_NOT_TOUCHABLE
                        | WindowManager.LayoutParams.FLAG_LAYOUT_IN_SCREEN | WindowManager.LayoutParams.FLAG_HARDWARE_ACCELERATED,
                PixelFormat.TRANSLUCENT);
        int g;
        switch (groesse.startsWith("Fast") ? "center" : position) {
            case "top-left": g = Gravity.TOP | Gravity.START; break;
            case "bottom-left": g = Gravity.BOTTOM | Gravity.START; break;
            case "bottom-right": g = Gravity.BOTTOM | Gravity.END; break;
            case "center": g = Gravity.CENTER; break;
            default: g = Gravity.TOP | Gravity.END;
        }
        p.gravity = g;
        p.x = g == Gravity.CENTER ? 0 : rand;
        p.y = g == Gravity.CENTER ? 0 : rand;
        p.setTitle("ESPHomeRemote Kamera");
        return p;
    }

    private static void zeigen(AccessibilityService s, String k, String name, String b, String groesse, String position) throws Exception {
        WindowManager wm = (WindowManager) s.getSystemService(Context.WINDOW_SERVICE);
        WindowManager.LayoutParams p = lage(s, groesse, position);
        if (rahmen != null && k.equals(kamera) && b.equals(basis)) {   // dieselbe Kamera: nur Groesse/Ecke/Name
            wm.updateViewLayout(rahmen, p);
            web.evaluateJavascript("titel(" + JSONObject.quote(name) + ")", null);
            return;
        }
        aus();
        kamera = k;
        basis = b;
        rahmen = new FrameLayout(s);
        // Runde Karte wie im HA-Theme (Nutzer 2026-11-01: "Ecken schoen rund, moderner, der duenne weisse Rahmen
        // gefaellt mir"). Das Bild liegt mit Abstand innerhalb, nur leicht gerundet - so schneidet die grosse Rundung
        // nichts vom Kamerabild ab (vorher fehlte unten rechts ein Teil des Kamera-Namens).
        final float dp = s.getResources().getDisplayMetrics().density;
        GradientDrawable bg = new GradientDrawable();
        bg.setColor(0xFF1C1C1E);
        bg.setCornerRadius(18 * dp);
        bg.setStroke(Math.max(1, Math.round(1.2f * dp)), 0x80FFFFFF);
        rahmen.setBackground(bg);
        final int pad = Math.round(6 * dp);
        rahmen.setPadding(pad, pad, pad, pad);
        FrameLayout innen = new FrameLayout(s);
        GradientDrawable ib = new GradientDrawable();
        ib.setColor(Color.BLACK);
        ib.setCornerRadius(6 * dp);
        innen.setBackground(ib);
        innen.setClipToOutline(true);
        web = new WebView(s);
        web.setBackgroundColor(Color.BLACK);
        WebSettings ws = web.getSettings();
        ws.setJavaScriptEnabled(true);
        ws.setMediaPlaybackRequiresUserGesture(false);
        ws.setMixedContentMode(WebSettings.MIXED_CONTENT_ALWAYS_ALLOW);
        ws.setDomStorageEnabled(true);
        // Seite genau so breit wie das Fenster (2026-11-01: mit viewport=device-width war sie bildschirmbreit und das
        // Bild in kleinen Fenstern beschnitten - "je groesser das Fenster, desto weniger abgeschnitten")
        ws.setUseWideViewPort(false);
        ws.setLoadWithOverviewMode(false);
        web.setWebChromeClient(new WebChromeClient());
        web.setFocusable(false);
        innen.addView(web, new FrameLayout.LayoutParams(FrameLayout.LayoutParams.MATCH_PARENT, FrameLayout.LayoutParams.MATCH_PARENT));
        rahmen.addView(innen, new FrameLayout.LayoutParams(FrameLayout.LayoutParams.MATCH_PARENT, FrameLayout.LayoutParams.MATCH_PARENT));
        wm.addView(rahmen, p);
        // Basis = HA: Anfragen der Seite gehen an denselben Ursprung (kein CORS noetig)
        final int i = b.indexOf("/api/");
        web.loadDataWithBaseURL(i > 0 ? b.substring(0, i + 1) : b, seite(k, name, b), "text/html", "utf-8", null);
        Log.i(State.TAG, "Kamera " + k + " (" + groesse + ", " + position + ")");
    }

    static void aus() {
        h.removeCallbacks(schliessen);
        if (rahmen == null) return;
        final FrameLayout r = rahmen;
        final WebView w = web;
        rahmen = null;
        web = null;
        kamera = "";
        try { w.evaluateJavascript("schluss()", null); } catch (Exception ignored) { }
        h.postDelayed(() -> {
            try {
                ((WindowManager) r.getContext().getSystemService(Context.WINDOW_SERVICE)).removeView(r);
            } catch (Exception ignored) { }
            try { w.destroy(); } catch (Exception ignored) { }
        }, 150);
    }

    // Webseite im Overlay: WebRTC (Angebot an HA, Antwort + Kandidaten zurueck), sonst HLS, sonst Standbilder
    private static String seite(String k, String name, String b) {
        return "<!doctype html><html><head><style>"
                + "@font-face{font-family:A;src:url(/local/fonts/Audiowide-Regular.ttf)}"
                + "html,body{margin:0;height:100%;background:#000;overflow:hidden;font-family:A,sans-serif}"
                + "video,img{position:fixed;left:0;top:0;width:100vw;height:100vh;object-fit:contain;background:#000}"
                + "#t{position:absolute;left:0;right:0;top:0;padding:6px 10px;color:#fff;font-size:15px;"
                + "background:linear-gradient(#000a,#0000);font-weight:600}#t:empty{display:none}"
                + "#m{position:absolute;inset:0;display:flex;align-items:center;justify-content:center;color:#aaa;font-size:14px}"
                + "</style></head><body><div id=m>Verbinde …</div><video id=v autoplay muted playsinline></video>"
                + "<img id=i hidden><div id=t></div><script>"
                + "const B=" + JSONObject.quote(b) + ",K=" + JSONObject.quote(k) + ";let N=" + JSONObject.quote(name) + ",pc=null,sid='',bild=0;"
                + "const v=document.getElementById('v'),im=document.getElementById('i'),m=document.getElementById('m'),t=document.getElementById('t');"
                + "function zeige(){t.textContent=N}"   // nur der Name, keine Uhr (Nutzer 2026-11-01)
                + "function titel(n){N=n;zeige()}zeige();"
                + "function schluss(){try{pc&&pc.close()}catch(e){}if(sid)fetch(B+'/schluss',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({kamera:K,sitzung:sid})})}"
                + "function laeuft(ms){return new Promise((ok,nein)=>{if(v.readyState>=2)return ok();const z=setTimeout(()=>nein('zeit'),ms);"
                + "v.addEventListener('playing',()=>{clearTimeout(z);ok()},{once:true})})}"
                + "async function webrtc(){pc=new RTCPeerConnection();pc.addTransceiver('video',{direction:'recvonly'});"
                + "pc.addTransceiver('audio',{direction:'recvonly'});pc.ontrack=e=>{if(!v.srcObject)v.srcObject=e.streams[0]||new MediaStream([e.track])};"
                + "await pc.setLocalDescription(await pc.createOffer());"
                + "await new Promise(ok=>{if(pc.iceGatheringState==='complete')return ok();const z=setTimeout(ok,1500);"
                + "pc.addEventListener('icegatheringstatechange',()=>{if(pc.iceGatheringState==='complete'){clearTimeout(z);ok()}})});"
                + "const r=await(await fetch(B+'/offer',{method:'POST',headers:{'Content-Type':'application/json'},"
                + "body:JSON.stringify({kamera:K,sdp:pc.localDescription.sdp})})).json();if(r.fehler)throw r.fehler;sid=r.sitzung||'';"
                + "await pc.setRemoteDescription({type:'answer',sdp:r.sdp});for(const c of r.kandidaten||[]){try{await pc.addIceCandidate(c)}catch(e){}}"
                + "await laeuft(8000)}"
                + "async function hls(){schluss();pc=null;sid='';v.srcObject=null;const r=await(await fetch(B+'/hls?kamera='+encodeURIComponent(K))).json();"
                + "if(!r.url)throw r.fehler||'hls';v.src=r.url;v.play().catch(()=>{});await laeuft(12000)}"
                + "function bilder(){v.hidden=true;im.hidden=false;const lade=()=>{const n=new Image();n.onload=()=>{im.src=n.src;m.textContent='';setTimeout(lade,1200)};"
                + "n.onerror=()=>setTimeout(lade,3000);n.src=B+'/bild?kamera='+encodeURIComponent(K)+'&t='+Date.now()};lade()}"
                + "(async()=>{try{await webrtc();m.textContent='';return}catch(e){console.log('webrtc',e)}"
                + "try{await hls();m.textContent='';return}catch(e){console.log('hls',e)}m.textContent='';bilder()})();"
                + "</script></body></html>";
    }
}
