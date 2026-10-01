package com.sukaengine.app;

import android.app.Activity;
import android.content.Context;
import android.content.pm.ActivityInfo;
import android.content.res.AssetManager;
import android.graphics.Bitmap;
import android.graphics.BitmapFactory;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Paint;
import android.graphics.Path;
import android.graphics.RadialGradient;
import android.graphics.RectF;
import android.graphics.Shader;
import android.graphics.Typeface;
import android.os.Bundle;
import android.text.InputType;
import android.view.MotionEvent;
import android.view.SurfaceHolder;
import android.view.SurfaceView;
import android.view.View;
import android.widget.EditText;
import android.app.AlertDialog;

import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.io.OutputStream;
import java.util.HashMap;
import java.util.Map;

public class MainActivity extends Activity {
    static { System.loadLibrary("suka"); }

    private static final String GAME_DIR = "DemoGame";
    private static final float LOGIC_W = 1280f;
    private static final float LOGIC_H = 720f;
    private static final String[] FALLBACK_ROOT = { "DemoGame","Game1","Game2","Game3","Game4","fonts","sounds" };

    private static volatile boolean g_initOk = false;
    private static volatile int g_fileCount = -1;
    private static volatile boolean g_hasProject = false, g_hasFont = false;
    private static volatile int g_stepLen = -1;
    private static volatile String g_stepHead = "";
    private static volatile boolean g_dialog = false;

    @Override protected void onCreate(Bundle b) {
        super.onCreate(b);
        setRequestedOrientation(ActivityInfo.SCREEN_ORIENTATION_LANDSCAPE);
        copyAssets(""); restructure();
        File root = getFilesDir();
        g_fileCount = countEntries(root);
        g_hasProject = new File(root, "projects/" + GAME_DIR + "/project.json").exists();
        g_hasFont = new File(root, "assets/fonts/Ubuntu-Regular.ttf").exists();
        g_initOk = nativeInit(root.getAbsolutePath(), GAME_DIR);
        setContentView(new GameView(this, root.getAbsolutePath()));
        hideSystemBars();
    }

    private int countEntries(File d) { File[] ch = d.listFiles(); return ch == null ? -1 : ch.length; }

    @Override public void onWindowFocusChanged(boolean hasFocus) {
        super.onWindowFocusChanged(hasFocus);
        if (hasFocus) hideSystemBars();
    }

    private void hideSystemBars() {
        getWindow().getDecorView().setSystemUiVisibility(
            View.SYSTEM_UI_FLAG_LAYOUT_STABLE | View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION | View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN
          | View.SYSTEM_UI_FLAG_HIDE_NAVIGATION | View.SYSTEM_UI_FLAG_FULLSCREEN | View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY);
    }

    private void restructure() {
        File root = getFilesDir(); File[] top = root.listFiles(); if (top == null) return;
        File proj = new File(root, "projects"); proj.mkdirs(); File ass = new File(root, "assets"); ass.mkdirs();
        for (File f : top) { if (!f.isDirectory()) continue; String n = f.getName();
            if (n.equals("projects") || n.equals("assets")) continue;
            boolean isProject = new File(f, "project.json").exists(); File dst = new File(isProject ? proj : ass, n);
            if (dst.exists()) deleteRecursive(f); else if (!f.renameTo(dst)) { copyRecursive(f, dst); deleteRecursive(f); } }
    }
    private void deleteRecursive(File f) { if (f.isDirectory()) { File[] ch = f.listFiles(); if (ch != null) for (File c : ch) deleteRecursive(c); } f.delete(); }
    private void copyRecursive(File src, File dst) {
        if (src.isDirectory()) { dst.mkdirs(); File[] ch = src.listFiles(); if (ch != null) for (File c : ch) copyRecursive(c, new File(dst, c.getName())); return; }
        try { InputStream in = new FileInputStream(src); OutputStream out = new FileOutputStream(dst); byte[] buf = new byte[8192]; int n; while ((n = in.read(buf)) > 0) out.write(buf, 0, n); out.close(); in.close(); } catch (Exception e) { }
    }
    private void copyAssets(String path) {
        try { AssetManager am = getAssets(); String[] list = am.list(path);
            if ((list == null || list.length == 0) && path.isEmpty()) list = FALLBACK_ROOT;
            if (list == null) return;
            if (list.length == 0) { File out = new File(getFilesDir(), path); if (out.getParentFile() != null) out.getParentFile().mkdirs();
                InputStream in = am.open(path); OutputStream os = new FileOutputStream(out); byte[] buf = new byte[8192]; int n; while ((n = in.read(buf)) > 0) os.write(buf, 0, n); os.close(); in.close(); return; }
            for (String s : list) copyAssets(path.isEmpty() ? s : path + "/" + s);
        } catch (Exception e) { }
    }

    // mode: 0=text, 1=name, 2=action, 3=number
    private void openDialog(final String title, final String current, final int mode) {
        g_dialog = true;
        runOnUiThread(() -> {
            final EditText et = new EditText(MainActivity.this);
            if (mode == 3) et.setInputType(InputType.TYPE_CLASS_NUMBER | InputType.TYPE_NUMBER_FLAG_DECIMAL | InputType.TYPE_NUMBER_FLAG_SIGNED);
            else et.setInputType(InputType.TYPE_CLASS_TEXT);
            et.setText(current); et.selectAll();
            et.setTextColor(Color.WHITE);
            new AlertDialog.Builder(MainActivity.this)
                .setTitle(title)
                .setView(et)
                .setPositiveButton("OK", (d, w) -> {
                    String v = et.getText().toString();
                    if (mode == 0) nativeSetText(v);
                    else if (mode == 1) nativeSetName(v);
                    else if (mode == 2) nativeSetAction(v);
                    else nativeSetNumber(v);
                    g_dialog = false;
                })
                .setNegativeButton("Cancel", (d, w) -> g_dialog = false)
                .setOnCancelListener(d -> g_dialog = false)
                .show();
        });
    }

    native boolean nativeInit(String root, String gameDir);
    native String nativeStep();
    native void nativeTouch(int action, float x, float y);
    native void nativeMultiTouch(int phase, float x0, float y0, float x1, float y1);
    native void nativeSetText(String text);
    native void nativeSetName(String text);
    native void nativeSetAction(String text);
    native void nativeSetNumber(String text);

    class GameView extends SurfaceView implements SurfaceHolder.Callback, Runnable {
        private Thread thread;
        private final Paint paint = new Paint();
        private final Map<String, Bitmap> bitmaps = new HashMap<>();
        private Typeface typeface;
        private final String root;

        GameView(Context c, String r) {
            super(c); root = r; paint.setAntiAlias(true); getHolder().addCallback(this);
            File f = new File(root + "/assets/fonts/Ubuntu-Regular.ttf");
            if (f.exists()) typeface = Typeface.createFromFile(f);
        }
        @Override public void surfaceCreated(SurfaceHolder h) { thread = new Thread(this); thread.start(); }
        @Override public void surfaceChanged(SurfaceHolder h, int f, int w, int ht) { }
        @Override public void surfaceDestroyed(SurfaceHolder h) { try { if (thread != null) thread.join(); } catch (Exception e) { } }

        @Override public void run() {
            while (true) {
                if (g_dialog) { try { Thread.sleep(33); } catch (Exception e) { return; } continue; }
                String frame = nativeStep(); if (frame == null) frame = "";
                g_stepLen = frame.length();
                g_stepHead = frame.replace("\n", "|");
                if (g_stepHead.length() > 70) g_stepHead = g_stepHead.substring(0, 70);

                int rt = frame.indexOf("REQ_TEXT|");
                int rn = frame.indexOf("REQ_NAME|");
                int ra = frame.indexOf("REQ_ACTION|");
                int rnum = frame.indexOf("REQ_NUM|");
                if (!g_dialog && (rt >= 0 || rn >= 0 || ra >= 0 || rnum >= 0)) {
                    int idx = rt >= 0 ? rt : (rn >= 0 ? rn : (ra >= 0 ? ra : rnum));
                    int mode = (idx == rt && rt >= 0) ? 0 : ((idx == rn && rn >= 0) ? 1 : ((idx == ra && ra >= 0) ? 2 : 3));
                    String rest = frame.substring(idx);
                    int nl = rest.indexOf('\n'); if (nl >= 0) rest = rest.substring(0, nl);
                    int bar = rest.indexOf('|'); String cur = bar >= 0 ? rest.substring(bar + 1) : "";
                    String title = mode == 0 ? "Text" : (mode == 1 ? "Name" : (mode == 2 ? "Action" : "Value"));
                    openDialog(title, cur, mode); continue;
                }

                Canvas c = getHolder().lockCanvas();
                if (c == null) { try { Thread.sleep(8); continue; } catch (Exception e) { return; } }
                int rw = getWidth(), rh = getHeight();
                c.drawColor(Color.rgb(18, 18, 24));
                if (rw > 0 && rh > 0) { c.save(); c.scale(rw / LOGIC_W, rh / LOGIC_H); for (String line : frame.split("\n")) drawLine(c, line); c.restore(); }
                else { for (String line : frame.split("\n")) drawLine(c, line); }

                boolean editor = frame.contains("Inspector") || frame.contains("FileSystem");
                boolean hubOrMenu = frame.contains("PROJECTS") || frame.contains("START") || frame.contains("Play")
                                 || frame.contains("NEW") || frame.contains("Theme") || frame.contains("MENU");
                if (editor || hubOrMenu) drawTitle(c, rw, rh, editor);
                if (frame.contains("PROJECTS")) drawDiag(c, rw, rh);
                getHolder().unlockCanvasAndPost(c);
                try { Thread.sleep(16); } catch (Exception e) { return; }
            }
        }

        private void drawTitle(Canvas c, int rw, int rh, boolean editor) {
            if (typeface != null) paint.setTypeface(typeface);
            else paint.setTypeface(Typeface.create(Typeface.DEFAULT, Typeface.BOLD));
            if (editor) {
                float size = Math.max(26f, rh * 0.035f);
                paint.setTextSize(size); paint.setTextAlign(Paint.Align.RIGHT);
                paint.setColor(Color.argb(150, 0, 0, 0)); c.drawText("SukaEngine", rw - 18 + 2, size + 4, paint);
                paint.setColor(Color.rgb(234, 242, 255)); c.drawText("SukaEngine", rw - 18, size + 2, paint);
            } else {
                float size = Math.max(40f, rh * 0.055f);
                paint.setTextSize(size); paint.setTextAlign(Paint.Align.CENTER);
                float cx = rw / 2f, cy = size + rh * 0.03f;
                paint.setColor(Color.argb(160, 0, 0, 0)); c.drawText("SukaEngine", cx + 3, cy + 3, paint);
                paint.setColor(Color.WHITE); c.drawText("SukaEngine", cx, cy, paint);
            }
            paint.setTextAlign(Paint.Align.LEFT);
        }

        private void drawDiag(Canvas c, int rw, int rh) {
            float sz = Math.max(20f, rh * 0.028f);
            paint.setTextSize(sz); paint.setTextAlign(Paint.Align.LEFT);
            if (typeface != null) paint.setTypeface(typeface);
            paint.setColor(Color.rgb(255, 224, 102));
            float x = 20f, y = rh - 50f;
            c.drawText("init=" + g_initOk + "  files=" + g_fileCount + "  proj=" + g_hasProject + "  font=" + g_hasFont, x, y, paint);
            c.drawText("step=" + g_stepLen + "  head=[" + g_stepHead + "]", x, y + sz + 6, paint);
        }

        private Bitmap loadBitmap(String path) {
            if (path == null || path.isEmpty()) return null;
            Bitmap bm = bitmaps.get(path);
            if (bm != null) return bm;
            if (path.startsWith("/")) bm = BitmapFactory.decodeFile(path);
            else {
                bm = BitmapFactory.decodeFile(root + "/projects/" + GAME_DIR + "/" + path);
                if (bm == null) bm = BitmapFactory.decodeFile(root + "/" + path);
            }
            if (bm != null) bitmaps.put(path, bm);
            return bm;
        }

        private void drawLine(Canvas c, String line) {
            if (!line.startsWith("DRAW ")) return;
            String[] p = line.substring(5).split("\\|", -1);
            try {
                if (p[0].equals("bg")) { c.drawColor(Color.parseColor(p[1])); return; }

                if (p[0].equals("text")) {
                    paint.setColor(Color.parseColor(p[5]));
                    float fs = Float.parseFloat(p[4]);
                    paint.setTextSize(fs); paint.setTextAlign(Paint.Align.LEFT);
                    if (typeface != null) paint.setTypeface(typeface);
                    float ang = p.length > 6 ? Float.parseFloat(p[6]) : 0f;
                    float x = Float.parseFloat(p[2]), y = Float.parseFloat(p[3]);
                    if (ang != 0f) { c.save(); c.translate(x, y); c.rotate(ang); c.drawText(p[1], 0, fs, paint); c.restore(); }
                    else c.drawText(p[1], x, y + fs, paint);
                }
                else if (p[0].equals("rect")) {
                    paint.setColor(Color.parseColor(p[5]));
                    float x = Float.parseFloat(p[1]), y = Float.parseFloat(p[2]);
                    float w = Float.parseFloat(p[3]), h = Float.parseFloat(p[4]);
                    float ang = p.length > 6 ? Float.parseFloat(p[6]) : 0f;
                    if (ang != 0f) { c.save(); c.translate(x + w/2, y + h/2); c.rotate(ang); c.drawRect(-w/2, -h/2, w/2, h/2, paint); c.restore(); }
                    else c.drawRect(new RectF(x, y, x + w, y + h), paint);
                }
                else if (p[0].equals("shape")) {
                    String shape = p[1];
                    float x = Float.parseFloat(p[2]), y = Float.parseFloat(p[3]);
                    float w = Float.parseFloat(p[4]), h = Float.parseFloat(p[5]);
                    paint.setColor(Color.parseColor(p[6]));
                    float ang = p.length > 7 ? Float.parseFloat(p[7]) : 0f;

                    if (shape.equals("glow")) {
                        int col = Color.parseColor(p[6]);
                        int r = (col >> 16) & 255, g2 = (col >> 8) & 255, b2 = col & 255;
                        RadialGradient rg = new RadialGradient(x + w/2, y + h/2, Math.max(w, h)/2,
                            Color.argb(200, r, g2, b2), Color.argb(0, r, g2, b2), Shader.TileMode.CLAMP);
                        paint.setShader(rg);
                        c.drawOval(new RectF(x, y, x + w, y + h), paint);
                        paint.setShader(null);
                        return;
                    }

                    c.save(); c.translate(x + w/2, y + h/2); c.rotate(ang);
                    if (shape.equals("circle")) c.drawOval(new RectF(-w/2, -h/2, w/2, h/2), paint);
                    else if (shape.equals("diamond")) { Path pa = new Path(); pa.moveTo(0, -h/2); pa.lineTo(w/2, 0); pa.lineTo(0, h/2); pa.lineTo(-w/2, 0); pa.close(); c.drawPath(pa, paint); }
                    else if (shape.equals("triangle")) { Path pa = new Path(); pa.moveTo(0, -h/2); pa.lineTo(w/2, h/2); pa.lineTo(-w/2, h/2); pa.close(); c.drawPath(pa, paint); }
                    else c.drawRect(new RectF(-w/2, -h/2, w/2, h/2), paint);
                    c.restore();
                }
                else if (p[0].equals("button")) {
                    if (p.length < 7) return;
                    float x = Float.parseFloat(p[2]), y = Float.parseFloat(p[3]);
                    float w = Float.parseFloat(p[4]), h = Float.parseFloat(p[5]);
                    int fill = Color.parseColor(p[6]);
                    float ang = p.length > 7 ? Float.parseFloat(p[7]) : 0f;
                    String tex = p.length > 8 ? p[8] : "";

                    c.save(); c.translate(x + w/2, y + h/2); c.rotate(ang);

                    if (!tex.isEmpty()) {
                        // TEX-ONLY: текстура заменяет фон и текст кнопки
                        Bitmap bm = loadBitmap(tex);
                        if (bm != null) {
                            c.drawBitmap(bm, null, new RectF(-w/2, -h/2, w/2, h/2), paint);
                            c.restore();
                            return;
                        }
                    }

                    paint.setColor(fill);
                    c.drawRoundRect(new RectF(-w/2, -h/2, w/2, h/2), 12f, 12f, paint);
                    double lum = 0.299 * ((fill >> 16) & 255) + 0.587 * ((fill >> 8) & 255) + 0.114 * (fill & 255);
                    paint.setColor(lum > 140 ? Color.rgb(26, 26, 46) : Color.WHITE);
                    paint.setTextSize(Math.min(30f, h * 0.45f));
                    paint.setTextAlign(Paint.Align.CENTER);
                    if (typeface != null) paint.setTypeface(typeface);
                    c.drawText(p[1], 0, paint.getTextSize() * 0.35f, paint);
                    paint.setTextAlign(Paint.Align.LEFT);
                    c.restore();
                }
                else if (p[0].equals("tex")) {
                    Bitmap bm = loadBitmap(p[1]);
                    if (bm != null) {
                        float x = Float.parseFloat(p[2]), y = Float.parseFloat(p[3]);
                        float w = Float.parseFloat(p[4]), h = Float.parseFloat(p[5]);
                        float ang = p.length > 6 ? Float.parseFloat(p[6]) : 0f;
                        if (ang != 0f) { c.save(); c.translate(x + w/2, y + h/2); c.rotate(ang); c.drawBitmap(bm, null, new RectF(-w/2, -h/2, w/2, h/2), paint); c.restore(); }
                        else c.drawBitmap(bm, null, new RectF(x, y, x + w, y + h), paint);
                    }
                }
            } catch (Exception e) { }
        }

        @Override public boolean onTouchEvent(MotionEvent e) {
            int a = e.getActionMasked();
            int count = e.getPointerCount();
            int rw = getWidth(), rh = getHeight();
            if (count >= 2) {
                float x0 = rw > 0 ? e.getX(0) * LOGIC_W / rw : e.getX(0);
                float y0 = rh > 0 ? e.getY(0) * LOGIC_H / rh : e.getY(0);
                float x1 = rw > 0 ? e.getX(1) * LOGIC_W / rw : e.getX(1);
                float y1 = rh > 0 ? e.getY(1) * LOGIC_H / rh : e.getY(1);
                int ph = (a == MotionEvent.ACTION_POINTER_DOWN || a == MotionEvent.ACTION_DOWN) ? 1
                       : (a == MotionEvent.ACTION_POINTER_UP   || a == MotionEvent.ACTION_UP)   ? 3 : 2;
                nativeMultiTouch(ph, x0, y0, x1, y1);
                return true;
            }
            if (a == MotionEvent.ACTION_DOWN || a == MotionEvent.ACTION_MOVE || a == MotionEvent.ACTION_UP) {
                float lx = rw > 0 ? e.getX() * LOGIC_W / rw : e.getX();
                float ly = rh > 0 ? e.getY() * LOGIC_H / rh : e.getY();
                nativeTouch(a, lx, ly);
            }
            return true;
        }
    }
                                                   }
