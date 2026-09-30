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
import android.view.MotionEvent;
import android.view.SurfaceHolder;
import android.view.SurfaceView;
import android.view.View;

import java.io.File;
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

    @Override
    protected void onCreate(Bundle b) {
        super.onCreate(b);
        setRequestedOrientation(ActivityInfo.SCREEN_ORIENTATION_LANDSCAPE);
        copyAssets("");
        String root = getFilesDir().getAbsolutePath();
        nativeInit(root, GAME_DIR);
        setContentView(new GameView(this, root));
        hideSystemBars();
    }

    @Override
    public void onWindowFocusChanged(boolean hasFocus) {
        super.onWindowFocusChanged(hasFocus);
        if (hasFocus) hideSystemBars();
    }

    private void hideSystemBars() {
        getWindow().getDecorView().setSystemUiVisibility(
            View.SYSTEM_UI_FLAG_LAYOUT_STABLE
          | View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION
          | View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN
          | View.SYSTEM_UI_FLAG_HIDE_NAVIGATION
          | View.SYSTEM_UI_FLAG_FULLSCREEN
          | View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY);
    }

    private void copyAssets(String path) {
        try {
            AssetManager am = getAssets();
            String[] list = am.list(path);
            if (list == null) return;
            if (list.length == 0) {
                File out = new File(getFilesDir(), path);
                if (out.getParentFile() != null) out.getParentFile().mkdirs();
                InputStream in = am.open(path);
                OutputStream os = new FileOutputStream(out);
                byte[] buf = new byte[8192];
                int n;
                while ((n = in.read(buf)) > 0) os.write(buf, 0, n);
                os.close(); in.close();
                return;
            }
            for (String s : list) copyAssets(path.isEmpty() ? s : path + "/" + s);
        } catch (Exception e) { }
    }

    native boolean nativeInit(String root, String gameDir);
    native String nativeStep();
    native void nativeTouch(int action, float x, float y);

    class GameView extends SurfaceView implements SurfaceHolder.Callback, Runnable {
        private Thread thread;
        private final Paint paint = new Paint();
        private final Map<String, Bitmap> bitmaps = new HashMap<>();
        private Typeface typeface;
        private final String root;

        GameView(Context c, String r) {
            super(c);
            root = r;
            paint.setAntiAlias(true);
            getHolder().addCallback(this);
            File f = new File(root + "/assets/fonts/Ubuntu-Regular.ttf");
            if (f.exists()) typeface = Typeface.createFromFile(f);
        }

        @Override public void surfaceCreated(SurfaceHolder h) {
            thread = new Thread(this); thread.start();
        }
        @Override public void surfaceChanged(SurfaceHolder h, int f, int w, int ht) { }
        @Override public void surfaceDestroyed(SurfaceHolder h) {
            try { if (thread != null) thread.join(); } catch (Exception e) { }
        }

        @Override public void run() {
            while (true) {
                String frame = nativeStep();
                Canvas c = getHolder().lockCanvas();
                if (c == null) { try { Thread.sleep(8); continue; } catch (Exception e) { return; } }

                int rw = getWidth(), rh = getHeight();
                c.drawColor(Color.rgb(18, 18, 24));

                if (rw > 0 && rh > 0) {
                    c.save();
                    c.scale(rw / LOGIC_W, rh / LOGIC_H);
                    for (String line : frame.split("\n")) drawLine(c, line);
                    c.restore();
                } else {
                    for (String line : frame.split("\n")) drawLine(c, line);
                }

                drawTitle(c, rw, rh);
                getHolder().unlockCanvasAndPost(c);
                try { Thread.sleep(16); } catch (Exception e) { return; }
            }
        }

        private void drawTitle(Canvas c, int rw, int rh) {
            float size = Math.max(40f, rh * 0.055f);
            paint.setTextSize(size);
            paint.setTextAlign(Paint.Align.CENTER);
            if (typeface != null) paint.setTypeface(typeface);
            else paint.setTypeface(Typeface.create(Typeface.DEFAULT, Typeface.BOLD));
            float cx = rw / 2f, cy = size + rh * 0.03f;
            paint.setColor(Color.argb(160, 0, 0, 0));      // тень-подложка
            c.drawText("SukaEngine", cx + 3, cy + 3, paint);
            paint.setColor(Color.WHITE);                    // сам заголовок
            c.drawText("SukaEngine", cx, cy, paint);
            paint.setTextAlign(Paint.Align.LEFT);
        }

        private void drawLine(Canvas c, String line) {
            if (!line.startsWith("DRAW ")) return;
            String[] p = line.substring(5).split("\\|");
            try {
                if (p[0].equals("bg")) { c.drawColor(Color.parseColor(p[1])); return; }
                if (p[0].equals("text")) {
                    paint.setColor(Color.parseColor(p[5]));
                    paint.setTextSize(Float.parseFloat(p[4]));
                    paint.setTextAlign(Paint.Align.LEFT);
                    if (typeface != null) paint.setTypeface(typeface);
                    c.drawText(p[1], Float.parseFloat(p[2]),
                        Float.parseFloat(p[3]) + Float.parseFloat(p[4]), paint);
                } else if (p[0].equals("rect")) {
                    paint.setColor(Color.parseColor(p[5]));
                    float x = Float.parseFloat(p[1]), y = Float.parseFloat(p[2]);
                    c.drawRect(new RectF(x, y, x + Float.parseFloat(p[3]), y + Float.parseFloat(p[4])), paint);
                } else if (p[0].equals("shape")) {
                    String shape = p[1];
                    float x = Float.parseFloat(p[2]), y = Float.parseFloat(p[3]);
                    float w = Float.parseFloat(p[4]), h = Float.parseFloat(p[5]);
                    paint.setColor(Color.parseColor(p[6]));
                    if (shape.equals("circle")) {
                        c.drawOval(new RectF(x, y, x + w, y + h), paint);
                    } else if (shape.equals("diamond")) {
                        Path pa = new Path();
                        pa.moveTo(x + w / 2, y); pa.lineTo(x + w, y + h / 2);
                        pa.lineTo(x + w / 2, y + h); pa.lineTo(x, y + h / 2); pa.close();
                        c.drawPath(pa, paint);
                    } else if (shape.equals("triangle")) {
                        Path pa = new Path();
                        pa.moveTo(x + w / 2, y); pa.lineTo(x + w, y + h);
                        pa.lineTo(x, y + h); pa.close(); c.drawPath(pa, paint);
                    } else {
                        c.drawRect(new RectF(x, y, x + w, y + h), paint);
                    }
                } else if (p[0].equals("button")) {
                    float x = Float.parseFloat(p[2]), y = Float.parseFloat(p[3]);
                    float w = Float.parseFloat(p[4]), h = Float.parseFloat(p[5]);
                    paint.setColor(Color.parseColor(p[6]));
                    c.drawRoundRect(new RectF(x, y, x + w, y + h), 18, 18, paint);
                    int fill = Color.parseColor(p[6]);
                    double lum = 0.299 * ((fill >> 16) & 255) + 0.587 * ((fill >> 8) & 255) + 0.114 * (fill & 255);
                    paint.setColor(lum > 140 ? Color.rgb(26, 26, 46) : Color.WHITE);
                    paint.setTextSize(30);
                    paint.setTextAlign(Paint.Align.CENTER);
                    if (typeface != null) paint.setTypeface(typeface);
                    c.drawText(p[1], x + w / 2, y + h / 2 + 10, paint);
                    paint.setTextAlign(Paint.Align.LEFT);
                } else if (p[0].equals("tex")) {
                    Bitmap bm = bitmaps.get(p[1]);
                    if (bm == null) {
                        bm = BitmapFactory.decodeFile(root + "/projects/" + GAME_DIR + "/" + p[1]);
                        if (bm != null) bitmaps.put(p[1], bm);
                    }
                    if (bm != null) {
                        float x = Float.parseFloat(p[2]), y = Float.parseFloat(p[3]);
                        c.drawBitmap(bm, null, new RectF(x, y,
                            x + Float.parseFloat(p[4]), y + Float.parseFloat(p[5])), paint);
                    }
                }
            } catch (Exception e) { }
        }

        @Override public boolean onTouchEvent(MotionEvent e) {
            int a = e.getActionMasked();
            if (a == MotionEvent.ACTION_DOWN || a == MotionEvent.ACTION_MOVE || a == MotionEvent.ACTION_UP) {
                int rw = getWidth(), rh = getHeight();
                float lx = rw > 0 ? e.getX() * LOGIC_W / rw : e.getX();
                float ly = rh > 0 ? e.getY() * LOGIC_H / rh : e.getY();
                nativeTouch(a, lx, ly);
            }
            return true;
        }
    }
                }
