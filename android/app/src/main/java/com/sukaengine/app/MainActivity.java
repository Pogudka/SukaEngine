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
import android.graphics.RectF;
import android.graphics.Typeface;
import android.os.Bundle;
import android.text.InputType;
import android.view.MotionEvent;
import android.view.SurfaceHolder;
import android.view.SurfaceView;
import android.view.View;
import android.widget.EditText;
import androidx.appcompat.app.AlertDialog;

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
    @Override public void onWindowFocusChanged(boolean hasFocus) { super.onWindowFocusChanged(hasFocus); if (hasFocus) hideSystemBars(); }
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

    private void openTextDialog(final String current) {
        g_dialog = true;
        runOnUiThread(() -> {
            final EditText et = new EditText(MainActivity.this);
            et.setInputType(InputType.TYPE_CLASS_TEXT);
            et.setText(current); et.selectAll();
            et.setTextColor(Color.WHITE);
            new AlertDialog.Builder(MainActivity.this)
                .setTitle("Text")
                .setView(et)
                .setPositiveButton("OK", (d, w) -> { nativeSetText(et.getText().toString()); g_dialog = false; })
                .setNegativeButton("Cancel", (d, w) -> g_dialog = false)
                .setOnCancelListener(d -> g_dialog = false)
                .show();
        });
    }

    native boolean nativeInit(String root, String gameDir);
    native String nativeStep();
    native void nativeTouch(int action, float x, float y);
    native void nativeSetText(String text);

    class GameView extends SurfaceView implements SurfaceHolder.Callback, Runnable {
        private Thread thread; private final Paint paint = new Paint();
        private final Map<String, Bitmap> bitmaps = new HashMap<>(); private Typeface typeface; private final String root;
        GameView(Context c, String r) { super(c); root = r; paint.setAntiAlias(true); getHolder().addCallback(this);
            File f = new File(root + "/assets/fonts/Ubuntu-Regular.ttf"); if (f.exists()) typeface = Typeface.createFromFile(f); }
        @Override public void surfaceCreated(SurfaceHolder h) { thread = new Thread(this); thread.start(); }
        @Override public void surfaceChanged(SurfaceHolder h, int f, int w, int ht) { }
        @Override public void surfaceDestroyed(SurfaceHolder h) { try { if (thread != null) thread.join(); } catch (Exception e) { } }

        @Override public void run() {
            while (true) {
                if (g_dialog) { try { Thread.sleep(33); } catch (Exception e) { return; } continue; }   // поток спит -> нет гонки с диалогом
                String frame = nativeStep(); if (frame == null) frame = "";
                g_stepLen = frame.length(); g_stepHead = frame.replace("\n", "|"); if (g_stepHead.length() > 70) g_stepHead = g_stepHead.substring(0, 70);

                int ri = frame.indexOf("REQ_TEXT");
                if (ri >= 0 && !g_dialog) { String rest = frame.substring(ri); int nl = rest.indexOf('\n'); if (nl >= 0) rest = rest.substring(0, nl);
                    int bar = rest.indexOf('|'); String cur = bar >= 0 ? rest.substring(bar + 1) : ""; openTextDialog(cur); continue; }

                Canvas c = getHolder().lockCanvas(); if (c == null) { try { Thread.sleep(8); continue; } catch (Exception e) { return; } }
                int rw = getWidth(), rh = getHeight(); c.drawColor(Color.rgb(18, 18, 24));
                if (rw > 0 && rh > 0) { c.save(); c.scale(rw / LOGIC_W, rh / LOGIC_H); for (String line : frame.split("\n")) drawLine(c, line); c.restore(); }
                else { for (String line : frame.split("\n")) drawLine(c, line); }
                boolean editor = frame.contains("Inspector") || frame.contains("FileSystem");
                boolean hubOrMenu = frame.contains("PROJECTS") || frame.contains("START") || frame.contains("Play") || frame.contains("NEW") || frame.contains("Theme") || frame.contains("MENU");
                if (editor || hubOrMenu) drawTitle(c, rw, rh, editor);
                if (frame.contains("PROJECTS")) drawDiag(c, rw, rh);
                getHolder().unlockCanvasAndPost(c);
                try { Thread.sleep(16); } catch (Exception e) { return; }
            }
        }
        private void drawTitle(Canvas c, int rw, int rh, boolean editor) {
            if (typeface != null) paint.setTypeface(typeface); else paint.setTypeface(Typeface.create(Typeface.DEFAULT, Typeface.BOLD));
            if (editor) { float size = Math.max(26f, rh * 0.035f); paint.setTextSize(size); paint.setTextAlign(Paint.Align.RIGHT);
                paint.setColor(Color.argb(150, 0, 0, 0)); c.drawText("SukaEngine", rw - 18 + 2, size + 4, paint);
                paint.setColor(Color.rgb(234, 242, 255)); c.drawText("SukaEngine", rw - 18, size + 2, paint); }
            else { float size = Math.max(40f, rh * 0.055f); paint.setTextSize(size); paint.setTextAlign(Paint.Align.CENTER);
                float cx = rw / 2f, cy = size + rh * 0.03f; paint.setColor(Color.argb(160, 0, 0, 0)); c.drawText("SukaEngine", cx + 3, cy + 3, paint);
                paint.setColor(Color.WHITE); c.drawText("SukaEngine", cx, cy, paint); }
            paint.setTextAlign(Paint.Align.LEFT);
        }
        private void drawDiag(Canvas c, int rw, int rh) {
            float sz = Math.max(20f, rh * 0.028f); paint.setTextSize(sz); paint.setTextAlign(Paint.Align.LEFT);
            if (typeface != null) paint.setTypeface(typeface); paint.setColor(Color.rgb(255, 224, 102));
            float x = 20f, y = rh - 50f;
            c.drawText("init=" + g_initOk + "  files=" + g_fileCount + "  proj=" + g_hasProject + "  font=" + g_hasFont, x, y, paint);
            c.drawText("step=" + g_stepLen + "  head=[" + g_stepHead + "]", x, y + sz + 6, paint);
        }
        private void drawLine(Canvas c, String line) {
            if (!line.startsWith("DRAW ")) return; String[] p = line.substring(5).split("\\|");
            try {
                if (p[0].equals("bg")) { c.drawColor(Color.parseColor(p[1])); return; }
                if (p[0].equals("text")) { paint.setColor(Color.parseColor(p[5])); paint.setTextSize(Float.parseFloat(p[4])); paint.setTextAlign(Paint.Align.LEFT);
                    if (typeface != null) paint.setTypeface(typeface); c.drawText(p[1], Float.parseFloat(p[2]), Float.parseFloat(p[3]) + Float.parseFloat(p[4]), paint); }
                else if (p[0].equals("rect")) { paint.setColor(Color.parseColor(p[5])); float x = Float.parseFloat(p[1]), y = Float.parseFloat(p[2]);
                    c.drawRect(new RectF(x, y, x + Float.parseFloat(p[3]), y + Float.parseFloat(p[4])), paint); }
                else if (p[0].equals("shape")) { String shape = p[1]; float x = Float.parseFloat(p[2]), y = Float.parseFloat(p[3]), w = Float.parseFloat(p[4]), h = Float.parseFloat(p[5]);
                    paint.setColor(Color.parseColor(p[6]));
                    if (shape.equals("circle")) c.drawOval(new RectF(x, y, x + w, y + h), paint);
                    else if (shape.equals("diamond")) { Path pa = new Path(); pa.moveTo(x+w/2,y); pa.lineTo(x+w,y+h/2); pa.lineTo(x+w/2,y+h); pa.lineTo(x,y+h/2); pa.close(); c.drawPath(pa, paint); }
                    else if (shape.equals("triangle")) { Path pa = new Path(); pa.moveTo(x+w/2,y); pa.lineTo(x+w,y+h); pa.lineTo(x,y+h); pa.close(); c.drawPath(pa, paint); }
                    else c.drawRect(new RectF(x, y, x + w, y + h), paint); }
                else if (p[0].equals("button")) { float x = Float.parseFloat(p[2]), y = Float.parseFloat(p[3]), w = Float.parseFloat(p[4]), h = Float.parseFloat(p[5]);
                    paint.setColor(Color.parseColor(p[6])); float r = 12f; c.drawRoundRect(new RectF(x, y, x + w, y + h), r, r, paint);
                    int fill = Color.parseColor(p[6]); double lum = 0.299*((fill>>16)&255)+0.587*((fill>>8)&255)+0.114*(fill&255);
                    paint.setColor(lum > 140 ? Color.rgb(26,26,46) : Color.WHITE); paint.setTextSize(Math.min(30f, h*0.45f)); paint.setTextAlign(Paint.Align.CENTER);
                    if (typeface != null) paint.setTypeface(typeface); c.drawText(p[1], x+w/2, y+h/2+paint.getTextSize()*0.35f, paint); paint.setTextAlign(Paint.Align.LEFT); }
                else if (p[0].equals("tex")) { Bitmap bm = bitmaps.get(p[1]);
                    if (bm == null) { bm = BitmapFactory.decodeFile(root + "/projects/" + GAME_DIR + "/" + p[1]); if (bm != null) bitmaps.put(p[1], bm); }
                    if (bm != null) { float x = Float.parseFloat(p[2]), y = Float.parseFloat(p[3]); c.drawBitmap(bm, null, new RectF(x, y, x + Float.parseFloat(p[4]), y + Float.parseFloat(p[5])), paint); } }
            } catch (Exception e) { }
        }
        @Override public boolean onTouchEvent(MotionEvent e) {
            int a = e.getActionMasked();
            if (a == MotionEvent.ACTION_DOWN || a == MotionEvent.ACTION_MOVE || a == MotionEvent.ACTION_UP) {
                int rw = getWidth(), rh = getHeight(); float lx = rw > 0 ? e.getX()*LOGIC_W/rw : e.getX(); float ly = rh > 0 ? e.getY()*LOGIC_H/rh : e.getY(); nativeTouch(a, lx, ly); }
            return true;
        }
    }
                }
