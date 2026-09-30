package com.sukaengine.app;

import android.app.Activity;
import android.content.Context;
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

import java.io.File;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.io.OutputStream;
import java.util.HashMap;
import java.util.Map;

public class MainActivity extends Activity {
    static { System.loadLibrary("suka"); }

    private static final String GAME_DIR = "DemoGame";

    @Override
    protected void onCreate(Bundle b) {
        super.onCreate(b);
        copyAssets("");
        String root = getFilesDir().getAbsolutePath();
        nativeInit(root, GAME_DIR);
        setContentView(new GameView(this, root));
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
                os.close();
                in.close();
                return;
            }
            for (String s : list) {
                copyAssets(path.isEmpty() ? s : path + "/" + s);
            }
        } catch (Exception e) { }
    }

    native boolean nativeInit(String root, String gameDir);
    native String nativeStep();
    native void nativeTouch(int action, float x, float y);

    class GameView extends SurfaceView implements Runnable {
        private Thread thread;
        private final Paint paint = new Paint();
        private final Map<String, Bitmap> bitmaps = new HashMap<>();
        private Typeface typeface;
        private final String root;

        GameView(Context c, String root) {
            super(c);
            this.root = root;
            paint.setAntiAlias(true);
            File f = new File(root + "/assets/fonts/Ubuntu-Regular.ttf");
            if (f.exists()) typeface = Typeface.createFromFile(f);
        }

        @Override public void surfaceCreated(SurfaceHolder h) {
            thread = new Thread(this);
            thread.start();
        }

        @Override public void surfaceDestroyed(SurfaceHolder h) {
            try { if (thread != null) thread.join(); } catch (Exception e) { }
        }

        @Override public void run() {
            while (true) {
                String frame = nativeStep();
                Canvas c = getHolder().lockCanvas();
                if (c == null) {
                    try { Thread.sleep(8); continue; } catch (Exception e) { return; }
                }
                c.drawColor(Color.rgb(18, 18, 24));
                for (String line : frame.split("\n")) drawLine(c, line);
                getHolder().unlockCanvasAndPost(c);
                try { Thread.sleep(16); } catch (Exception e) { return; }
            }
        }

        private void drawLine(Canvas c, String line) {
            if (!line.startsWith("DRAW ")) return;
            String[] p = line.substring(5).split("\\|");
            try {
                if (p[0].equals("text")) {
                    if (p[0].equals("bg")) {
                    c.drawColor(Color.parseColor(p[1]));
                    return;
                }
                    paint.setColor(Color.parseColor(p[5]));
                    paint.setTextSize(Float.parseFloat(p[4]));
                    paint.setTextAlign(Paint.Align.LEFT);
                    if (typeface != null) paint.setTypeface(typeface);
                    c.drawText(p[1], Float.parseFloat(p[2]),
                               Float.parseFloat(p[3]) + Float.parseFloat(p[4]), paint);
                }
                else if (p[0].equals("rect")) {
                    paint.setColor(Color.parseColor(p[5]));
                    float x = Float.parseFloat(p[1]), y = Float.parseFloat(p[2]);
                    c.drawRect(new RectF(x, y, x + Float.parseFloat(p[3]),
                                         y + Float.parseFloat(p[4])), paint);
                }
                else if (p[0].equals("shape")) {
                    String shape = p[1];
                    float x = Float.parseFloat(p[2]), y = Float.parseFloat(p[3]);
                    float w = Float.parseFloat(p[4]), h = Float.parseFloat(p[5]);
                    paint.setColor(Color.parseColor(p[6]));
                    if (shape.equals("circle")) {
                        c.drawOval(new RectF(x, y, x + w, y + h), paint);
                    } else if (shape.equals("diamond")) {
                        Path path = new Path();
                        path.moveTo(x + w / 2, y);
                        path.lineTo(x + w, y + h / 2);
                        path.lineTo(x + w / 2, y + h);
                        path.lineTo(x, y + h / 2);
                        path.close();
                        c.drawPath(path, paint);
                    } else if (shape.equals("triangle")) {
                        Path path = new Path();
                        path.moveTo(x + w / 2, y);
                        path.lineTo(x + w, y + h);
                        path.lineTo(x, y + h);
                        path.close();
                        c.drawPath(path, paint);
                    } else {
                        c.drawRect(new RectF(x, y, x + w, y + h), paint);
                    }
                }
                else if (p[0].equals("button")) {
                    float x = Float.parseFloat(p[2]), y = Float.parseFloat(p[3]);
                    float w = Float.parseFloat(p[4]), h = Float.parseFloat(p[5]);
                    paint.setColor(Color.parseColor(p[6]));
                    c.drawRoundRect(new RectF(x, y, x + w, y + h), 18, 18, paint);
                    int fill = Color.parseColor(p[6]);
                    int rr = (fill >> 16) & 255, gg = (fill >> 8) & 255, bb = fill & 255;
                    double lum = 0.299 * rr + 0.587 * gg + 0.114 * bb;
                    paint.setColor(lum > 140 ? Color.rgb(26, 26, 46) : Color.WHITE);
                    paint.setTextSize(30);
                    paint.setTextAlign(Paint.Align.CENTER);
                    if (typeface != null) paint.setTypeface(typeface);
                    c.drawText(p[1], x + w / 2, y + h / 2 + 10, paint);
                    paint.setTextAlign(Paint.Align.LEFT);
                }
                else if (p[0].equals("tex")) {
                    Bitmap bm = bitmaps.get(p[1]);
                    if (bm == null) {
                        bm = BitmapFactory.decodeFile(root + "/projects/" + GAME_DIR + "/" + p[1]);
                        if (bm != null) bitmaps.put(p[1], bm);
                    }
                    if (bm != null) {
                        float x = Float.parseFloat(p[2]), y = Float.parseFloat(p[3]);
                        c.drawBitmap(bm, null, new RectF(x, y, x + Float.parseFloat(p[4]),
                                                         y + Float.parseFloat(p[5])), paint);
                    }
                }
            } catch (Exception e) { }
        }

        @Override public boolean onTouchEvent(MotionEvent e) {
            int a = e.getActionMasked();
            if (a == MotionEvent.ACTION_DOWN || a == MotionEvent.ACTION_MOVE || a == MotionEvent.ACTION_UP) {
                nativeTouch(a, e.getX(), e.getY());
            }
            return true;
        }
    }
}