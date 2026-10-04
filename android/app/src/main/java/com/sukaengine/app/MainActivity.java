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
import android.os.Build;
import android.os.Bundle;
import android.text.InputType;
import android.view.KeyEvent;
import android.view.MotionEvent;
import android.view.SurfaceHolder;
import android.view.SurfaceView;
import android.view.View;
import android.view.WindowManager;
import android.view.inputmethod.BaseInputConnection;
import android.view.inputmethod.EditorInfo;
import android.view.inputmethod.InputConnection;
import android.view.inputmethod.InputMethodManager;
import android.widget.EditText;
import android.app.AlertDialog;
import android.content.Intent;
import android.net.Uri;
import android.database.Cursor;
import android.provider.OpenableColumns;

import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.io.OutputStream;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.Map;

public class MainActivity extends Activity {
    static { System.loadLibrary("suka"); }

    private static final String GAME_DIR = "DemoGame";
    private static final float DEFAULT_LOGIC_W = 1280f;
    private static final float DEFAULT_LOGIC_H = 720f;
    private static final String[] FALLBACK_ROOT = { "DemoGame","fonts","sounds" };

    private static final float CODE_X0 = 300f, CODE_X1 = 892f, CODE_Y0 = 64f, CODE_Y1 = 556f;
    private static final float CODE_TEXT_X = 340f;
    private static final float CODE_FONT = 14f;

    // Границы вьюпорта редактора (совпадают с C++: 300,64 .. 892,712).
    private static final float EDV_X0 = 300f, EDV_Y0 = 64f, EDV_X1 = 892f, EDV_Y1 = 712f;

    // 0 = hub, 1 = editor, 2 = game. hub/editor -> STRETCH на весь экран (инструмент).
    // game -> CONTAIN; при совпадении пропорций кадра и экрана полос нет вообще.
    private static final int MODE_HUB = 0, MODE_EDITOR = 1, MODE_GAME = 2;

    private static volatile boolean g_initOk = false;
    private static volatile int g_fileCount = -1;
    private static volatile boolean g_hasProject = false, g_hasFont = false;
    private static volatile int g_stepLen = -1;
    private static volatile String g_stepHead = "";
    private static volatile boolean g_dialog = false;

    private static final int IMPORT_REQUEST_CODE = 1001;
    private volatile String pendingImportCategory = "";
    private volatile String pendingImportRoot = "";
    private volatile String lastImportMsg_ = "";

    private volatile boolean hasImportResult_ = false;
    private volatile String importCatRes_ = "";
    private volatile String importNameRes_ = "";

    @Override protected void onCreate(Bundle b) {
        super.onCreate(b);
        setRequestedOrientation(ActivityInfo.SCREEN_ORIENTATION_LANDSCAPE);
        // Рисуем ПОД вырезом экрана (cutout), иначе в ландшафте система оставляет
        // чёрную полосу с одного края и contain считается по урезанной ширине.
        try {
            if (Build.VERSION.SDK_INT >= 28) {
                getWindow().getAttributes().layoutInDisplayCutoutMode =
                    WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_SHORT_EDGES;
            }
        } catch (Throwable t) { }
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
                .setNegativeButton("Cancel", (d, w) -> {
                    if (mode == 1) nativeSetName("");
                    g_dialog = false;
                })
                .setOnCancelListener(d -> {
                    if (mode == 1) nativeSetName("");
                    g_dialog = false;
                })
                .show();
        });
    }

    private void startImport(final String category, final String projectRoot) {
        pendingImportCategory = category;
        pendingImportRoot = projectRoot;
        g_dialog = true;
        runOnUiThread(() -> {
            try {
                Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT);
                intent.addCategory(Intent.CATEGORY_OPENABLE);
                if (category.equals("sprites")) intent.setType("image/*");
                else if (category.equals("videos")) intent.setType("video/*");
                else if (category.equals("sounds")) intent.setType("audio/*");
                else intent.setType("*/*");
                startActivityForResult(intent, IMPORT_REQUEST_CODE);
            } catch (Throwable t) {
                lastImportMsg_ = "no file picker";
                g_dialog = false;
            }
        });
    }

    @Override protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        super.onActivityResult(requestCode, resultCode, data);
        if (requestCode != IMPORT_REQUEST_CODE) return;
        if (resultCode != RESULT_OK || data == null || data.getData() == null) {
            lastImportMsg_ = "import cancelled";
            g_dialog = false;
            return;
        }
        final Uri uri = data.getData();
        final String category = pendingImportCategory;
        final String projRoot = pendingImportRoot;
        pendingImportCategory = "";
        pendingImportRoot = "";

        String origName = getFileName(uri);
        if (origName == null || origName.isEmpty()) origName = "imported_" + System.currentTimeMillis();
        String safeOrig = origName.replaceAll("[^a-zA-Z0-9._-]", "_");

        String ext = "";
        int dot = safeOrig.lastIndexOf('.');
        if (dot >= 0 && dot < safeOrig.length() - 1) { ext = safeOrig.substring(dot); safeOrig = safeOrig.substring(0, dot); }
        if (ext.isEmpty()) ext = category.equals("sprites") ? ".png" : ".bin";

        final String finalExt = ext;
        final String defBase = safeOrig.isEmpty() ? "asset" : safeOrig;

        g_dialog = true;
        runOnUiThread(() -> {
            final EditText et = new EditText(MainActivity.this);
            et.setInputType(InputType.TYPE_CLASS_TEXT);
            et.setText(defBase);
            et.selectAll();
            et.setTextColor(Color.WHITE);
            new AlertDialog.Builder(MainActivity.this)
                .setTitle("Name for " + category)
                .setView(et)
                .setPositiveButton("OK", (d, w) -> {
                    String base = et.getText().toString().replaceAll("[^a-zA-Z0-9._-]", "_");
                    if (base.isEmpty()) base = defBase;
                    String stored = copyToAssets(uri, category, base, finalExt, projRoot);
                    if (stored != null) {
                        importCatRes_ = category;
                        importNameRes_ = stored;
                        hasImportResult_ = true;
                        lastImportMsg_ = "copied " + stored;
                    } else {
                        lastImportMsg_ = "copy failed";
                    }
                    g_dialog = false;
                })
                .setNegativeButton("Cancel", (d, w) -> {
                    lastImportMsg_ = "import cancelled";
                    g_dialog = false;
                })
                .setOnCancelListener(d -> {
                    lastImportMsg_ = "import cancelled";
                    g_dialog = false;
                })
                .show();
        });
    }

    private String copyToAssets(Uri uri, String category, String base, String ext, String projRoot) {
        try {
            String rp = projRoot;
            if (rp == null || rp.isEmpty()) rp = getFilesDir().getAbsolutePath() + "/projects/" + GAME_DIR;
            String sub = category.equals("fonts") ? "assets/fonts/" : "assets/";
            File dir = new File(rp + "/" + sub);
            dir.mkdirs();
            String name = base + ext;
            File target = new File(dir, name);
            int k = 1;
            while (target.exists()) { name = base + "_" + k + ext; target = new File(dir, name); k++; }
            InputStream in = getContentResolver().openInputStream(uri);
            if (in == null) return null;
            FileOutputStream out = new FileOutputStream(target);
            byte[] buf = new byte[8192]; int n;
            while ((n = in.read(buf)) > 0) out.write(buf, 0, n);
            out.close(); in.close();
            return category.equals("fonts") ? ("fonts/" + name) : name;
        } catch (Throwable t) {
            return null;
        }
    }

    private String getFileName(Uri uri) {
        String result = null;
        if ("content".equals(uri.getScheme())) {
            Cursor cursor = getContentResolver().query(uri, null, null, null, null);
            try {
                if (cursor != null && cursor.moveToFirst()) {
                    int idx = cursor.getColumnIndex(OpenableColumns.DISPLAY_NAME);
                    if (idx >= 0) result = cursor.getString(idx);
                }
            } finally { if (cursor != null) cursor.close(); }
        }
        if (result == null) {
            result = uri.getPath();
            int cut = result == null ? -1 : result.lastIndexOf('/');
            if (cut >= 0) result = result.substring(cut + 1);
        }
        return result;
    }

    native boolean nativeInit(String root, String gameDir);
    native String nativeStep();
    native void nativeTouch(int action, float x, float y);
    native void nativeMultiTouch(int phase, float x0, float y0, float x1, float y1);
    native void nativeSetText(String text);
    native void nativeSetName(String text);
    native void nativeSetAction(String text);   // оверлей X/DBG шлёт "ov:...", surface шлёт "sys:ratio|..."
    native void nativeSetNumber(String text);
    native void nativeScriptText(String text);
    native void nativeScriptCompose(String text);
    native void nativeScriptFinish();
    native void nativeScriptKey(int key);
    native void nativeImportFile(String category, String relativePath);

    class GameView extends SurfaceView implements SurfaceHolder.Callback, Runnable {
        private Thread thread;
        private volatile boolean running_ = false;
        private volatile boolean editorFrame_ = false;
        private final Paint paint = new Paint();
        private final Paint measurePaint = new Paint(Paint.ANTI_ALIAS_FLAG);
        private final Map<String, Bitmap> bitmaps = new HashMap<>();
        private Typeface typeface;
        private final String root;
        private final InputMethodManager imm;

        private volatile float logicW = DEFAULT_LOGIC_W;
        private volatile float logicH = DEFAULT_LOGIC_H;
        private volatile int renderMode = MODE_HUB;
        private volatile int requestedOrient = ActivityInfo.SCREEN_ORIENTATION_LANDSCAPE;
        private volatile int appliedOrient = ActivityInfo.SCREEN_ORIENTATION_LANDSCAPE;
        // Запомненная ориентация ИГРЫ: обновляется тегом ORIENT| и держится между кадрами.
        private volatile int gameOrient = ActivityInfo.SCREEN_ORIENTATION_LANDSCAPE;

        // Транзишен (из тега TRANS|type|phase|progress).
        private volatile int transType = 0;
        private volatile int transPhase = 0;
        private volatile float transProgress = 0f;

        // Цвет фона сцены: им заливается ВЕСЬ экран в игре, чтобы чёрных полос
        // не было даже на тот кадр, пока пропорции экрана ещё не доехали до C++.
        private volatile int gameBg = Color.BLACK;
        private volatile boolean haveGameBg = false;

        // Оверлейные данные (экранные координаты, рисуются поверх вписанного кадра).
        private volatile String ovStat = "";
        private final ArrayList<String> ovLog = new ArrayList<>();

        private final ArrayList<Integer> clIdx = new ArrayList<>();
        private final ArrayList<String> clText = new ArrayList<>();
        private final ArrayList<Float> clY = new ArrayList<>();

        private int ratioTick = 0;

        GameView(Context c, String r) {
            super(c); root = r; paint.setAntiAlias(true); getHolder().addCallback(this);
            imm = (InputMethodManager) getContext().getSystemService(Context.INPUT_METHOD_SERVICE);
            setFocusable(true); setFocusableInTouchMode(true);
            File f = new File(root + "/assets/fonts/Ubuntu-Regular.ttf");
            if (f.exists()) typeface = Typeface.createFromFile(f);
            measurePaint.setTextSize(CODE_FONT);
            measurePaint.setTextAlign(Paint.Align.LEFT);
            measurePaint.setTypeface(typeface != null ? typeface : Typeface.DEFAULT);
        }

        @Override public InputConnection onCreateInputConnection(EditorInfo outAttrs) {
            outAttrs.inputType = InputType.TYPE_CLASS_TEXT | InputType.TYPE_TEXT_FLAG_MULTI_LINE;
            outAttrs.imeOptions |= EditorInfo.IME_FLAG_NO_EXTRACT_UI;
            return new BaseInputConnection(this, false) {
                @Override public boolean commitText(CharSequence text, int newCursorPosition) { nativeScriptText(text.toString()); return true; }
                @Override public boolean setComposingText(CharSequence text, int newCursorPosition) { nativeScriptCompose(text.toString()); return true; }
                @Override public boolean finishComposingText() { nativeScriptFinish(); return true; }
                @Override public boolean deleteSurroundingText(int before, int after) { for (int i = 0; i < before; ++i) nativeScriptKey(67); return true; }
                @Override public boolean sendKeyEvent(KeyEvent event) {
                    if (event.getAction() == KeyEvent.ACTION_DOWN) {
                        int c = event.getKeyCode();
                        if (c == KeyEvent.KEYCODE_DEL)        { nativeScriptKey(67); return true; }
                        if (c == KeyEvent.KEYCODE_ENTER)      { nativeScriptKey(66); return true; }
                        if (c == KeyEvent.KEYCODE_DPAD_LEFT)  { nativeScriptKey(21); return true; }
                        if (c == KeyEvent.KEYCODE_DPAD_RIGHT) { nativeScriptKey(22); return true; }
                    }
                    return super.sendKeyEvent(event);
                }
            };
        }

        private void requestIme(final boolean on) {
            post(() -> {
                if (on) { requestFocus(); imm.showSoftInput(GameView.this, InputMethodManager.SHOW_IMPLICIT); }
                else imm.hideSoftInputFromWindow(getWindowToken(), 0);
            });
        }

        private void applyOrientation(final int orient) {
            if (orient == appliedOrient) return;
            appliedOrient = orient;
            runOnUiThread(() -> { try { setRequestedOrientation(orient); } catch (Throwable t) { } });
        }

        // Сообщаем движку реальное отношение сторон экрана (ширина/высота).
        // Движок подгоняет логический кадр игры под него РАВНОМЕРНО, поэтому
        // игра занимает весь экран без чёрных полос и без расплющивания.
        private void sendScreenRatio() {
            int rw = getWidth(), rh = getHeight();
            if (rw > 0 && rh > 0) {
                float r = (float) rw / (float) rh;
                nativeSetAction("sys:ratio|" + r);
            }
        }

        @Override public void surfaceCreated(SurfaceHolder h) {
            running_ = true;
            thread = new Thread(this);
            thread.start();
            sendScreenRatio();
        }
        @Override public void surfaceChanged(SurfaceHolder h, int f, int w, int ht) {
            sendScreenRatio();
        }
        @Override public void surfaceDestroyed(SurfaceHolder h) {
            running_ = false;
            try { if (thread != null) { thread.interrupt(); thread.join(500); } } catch (Exception e) { }
            thread = null;
        }

        // Компактные кнопки в правом верхнем углу экрана. [0..3]=DBG, [4..7]=X.
        private float[] ovBtnRects(int rw, int rh) {
            float shortSide = Math.min(rw, rh);
            float bw = Math.min(Math.max(shortSide * 0.078f, 56f), 130f);
            float bh = bw * 0.60f;
            float m  = Math.max(6f, bh * 0.16f);
            float gap = m;
            float xRight = rw - m;
            float yTop = m;
            float xDbg0 = xRight - bw - gap - bw;
            float xDbg1 = xRight - bw - gap;
            float xX0 = xRight - bw;
            float xX1 = xRight;
            float yBot = yTop + bh;
            return new float[]{ xDbg0, yTop, xDbg1, yBot,
                                xX0,   yTop, xX1,   yBot };
        }

        @Override public void run() {
            while (running_) {
              try {
                if (g_dialog) { Thread.sleep(33); continue; }

                if (hasImportResult_) {
                    hasImportResult_ = false;
                    String ic = importCatRes_, inm = importNameRes_;
                    importCatRes_ = ""; importNameRes_ = "";
                    try { nativeImportFile(ic, inm); lastImportMsg_ = "ok: " + inm; }
                    catch (Throwable t) { lastImportMsg_ = "jni err: " + t; }
                }

                // Периодически повторяем пропорции экрана: даже если первое сообщение
                // потерялось, игра подстроится в течение полусекунды.
                if (++ratioTick >= 30) { ratioTick = 0; sendScreenRatio(); }

                String frame = nativeStep(); if (frame == null) frame = "";
                g_stepLen = frame.length();
                g_stepHead = frame.replace("\n", "|");
                if (g_stepHead.length() > 70) g_stepHead = g_stepHead.substring(0, 70);

                editorFrame_ = frame.contains("Inspector") || frame.contains("FileSystem") || frame.contains("SCRIPTS");

                int newMode = renderMode;
                String newStat = "";
                ArrayList<String> newLog = new ArrayList<>();
                for (String line : frame.split("\n")) {
                    if (line.startsWith("MODE|")) {
                        String v = line.substring(5).trim();
                        if (v.equals("hub")) newMode = MODE_HUB;
                        else if (v.equals("editor")) newMode = MODE_EDITOR;
                        else if (v.equals("game")) newMode = MODE_GAME;
                    } else if (line.startsWith("RES|")) {
                        String[] rp = line.split("\\|", 3);
                        if (rp.length >= 3) {
                            try {
                                float nw = Float.parseFloat(rp[1]);
                                float nh = Float.parseFloat(rp[2]);
                                if (nw >= 160f && nw <= 2160f && nh >= 160f && nh <= 2160f) { logicW = nw; logicH = nh; }
                            } catch (Throwable t) { }
                        }
                    } else if (line.startsWith("ORIENT|")) {
                        String[] op = line.split("\\|", 2);
                        if (op.length >= 2) {
                            String v = op[1].trim();
                            gameOrient = (v.equals("portrait") || v.equals("vertical") || v.equals("p") || v.equals("v"))
                                ? ActivityInfo.SCREEN_ORIENTATION_PORTRAIT
                                : ActivityInfo.SCREEN_ORIENTATION_LANDSCAPE;
                        }
                    } else if (line.startsWith("TRANS|")) {
                        String[] tp = line.split("\\|", 4);
                        if (tp.length >= 4) {
                            try {
                                transType = Integer.parseInt(tp[1]);
                                transPhase = Integer.parseInt(tp[2]);
                                transProgress = Float.parseFloat(tp[3]);
                            } catch (Throwable t) { }
                        }
                    } else if (line.startsWith("DRAW bg|")) {
                        try { gameBg = Color.parseColor(line.substring(8).trim()); haveGameBg = true; } catch (Throwable t) { }
                    } else if (line.startsWith("OVSTAT|")) {
                        newStat = line.substring(7);
                    } else if (line.startsWith("OVLOG|")) {
                        newLog.add(line.substring(6));
                    }
                }
                renderMode = newMode;
                ovStat = newStat;
                ovLog.clear(); ovLog.addAll(newLog);

                // Ориентация: в игре держим ЗАПОМНЕННУЮ gameOrient, хаб/редактор — ландшафт.
                if (renderMode == MODE_GAME) requestedOrient = gameOrient;
                else requestedOrient = ActivityInfo.SCREEN_ORIENTATION_LANDSCAPE;
                if (requestedOrient != appliedOrient) applyOrientation(requestedOrient);

                if (frame.contains("IME_ON"))  requestIme(true);
                if (frame.contains("IME_OFF")) requestIme(false);

                int ri = frame.indexOf("REQ_IMPORT|");
                if (!g_dialog && ri >= 0) {
                    String rest = frame.substring(ri);
                    int nl = rest.indexOf('\n');
                    if (nl >= 0) rest = rest.substring(0, nl);
                    String[] parts = rest.split("\\|", 3);
                    String cat = parts.length > 1 ? parts[1] : "";
                    String proot = parts.length > 2 ? parts[2] : "";
                    startImport(cat, proot);
                    continue;
                }

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
                if (c == null) { Thread.sleep(8); continue; }
                clIdx.clear(); clText.clear(); clY.clear();
                int rw = getWidth(), rh = getHeight();
                // В игре весь экран сразу заливается фоном сцены: чёрных полос не бывает
                // даже на кадре, пока пропорции ещё не применились. Хаб/редактор — чёрный фон.
                if (renderMode == MODE_GAME && haveGameBg) c.drawColor(gameBg);
                else c.drawColor(Color.BLACK);

                // Слайд-сдвиг в логических координатах contain-кадра.
                float slideDx = 0f;
                if (renderMode == MODE_GAME && (transType == 1 || transType == 2)) {
                    float p = transProgress;
                    if (p < 0f) p = 0f; if (p > 1f) p = 1f;
                    if (transPhase == 0) slideDx = (transType == 1 ? -logicW : logicW) * p;
                    else slideDx = (transType == 1 ? logicW : -logicW) * (1f - p);
                }

                if (rw > 0 && rh > 0 && logicW > 1.0f && logicH > 1.0f) {
                    c.save();
                    if (renderMode == MODE_GAME) {
                        // CONTAIN: единый масштаб по обеим осям + центрирование.
                        float s  = Math.min(rw / logicW, rh / logicH);
                        float ox = (rw - logicW * s) * 0.5f;
                        float oy = (rh - logicH * s) * 0.5f;
                        c.translate(ox, oy);
                        c.scale(s, s);
                        if (slideDx != 0f) c.translate(slideDx, 0f);
                    } else {
                        // STRETCH: хаб/редактор на весь экран.
                        c.scale(rw / logicW, rh / logicH);
                    }
                    for (String line : frame.split("\n")) drawLine(c, line);
                    c.restore();
                } else {
                    for (String line : frame.split("\n")) drawLine(c, line);
                }

                boolean editor = editorFrame_;
                boolean hubOrMenu = renderMode != MODE_GAME;
                if (editor || hubOrMenu) drawTitle(c, rw, rh, editor);
                if (renderMode == MODE_HUB || frame.isEmpty()) drawDiag(c, rw, rh);
                if (renderMode == MODE_GAME) drawOverlay(c, rw, rh);

                // Fade ПОСЛЕДНИМ и в ЭКРАННЫХ координатах: покрывает весь экран целиком.
                if (renderMode == MODE_GAME && transType == 0) {
                    float p = transProgress;
                    if (p < 0f) p = 0f; if (p > 1f) p = 1f;
                    float a = (transPhase == 0) ? p : (1f - p);
                    if (a > 0.001f) {
                        int ai = (int)(a * 255f + 0.5f);
                        if (ai > 255) ai = 255;
                        paint.setColor(Color.argb(ai, 0, 0, 0));
                        c.drawRect(0, 0, rw, rh, paint);
                    }
                }

                getHolder().unlockCanvasAndPost(c);
                Thread.sleep(16);
              } catch (Throwable t) {
                lastImportMsg_ = "loop err: " + t;
                if (!running_) return;
                try { Thread.sleep(33); } catch (Exception e) { return; }
              }
            }
        }

        private void drawOverlay(Canvas c, int rw, int rh) {
            if (typeface != null) paint.setTypeface(typeface);
            else paint.setTypeface(Typeface.DEFAULT);
            float fs = Math.min(Math.max(rh * 0.022f, 16f), 30f);
            paint.setTextSize(fs); paint.setTextAlign(Paint.Align.LEFT);
            float lx = fs * 0.6f, ly = fs * 0.6f;
            if (!ovStat.isEmpty()) { paint.setColor(Color.rgb(255, 215, 0)); c.drawText(ovStat, lx, ly + fs, paint); ly += fs * 1.35f; }
            paint.setColor(Color.rgb(135, 206, 235));
            for (String s : ovLog) { c.drawText(s, lx, ly + fs, paint); ly += fs * 1.25f; }
            float[] R = ovBtnRects(rw, rh);
            drawOvBtn(c, R[0], R[1], R[2], R[3], "DBG", Color.rgb(128, 128, 128));
            drawOvBtn(c, R[4], R[5], R[6], R[7], "X",   Color.rgb(214, 40, 40));
            paint.setTextAlign(Paint.Align.LEFT);
        }

        private void drawOvBtn(Canvas c, float x0, float y0, float x1, float y1, String label, int fill) {
            paint.setColor(fill);
            c.drawRoundRect(new RectF(x0, y0, x1, y1), 12f, 12f, paint);
            double lum = 0.299 * ((fill >> 16) & 255) + 0.587 * ((fill >> 8) & 255) + 0.114 * (fill & 255);
            paint.setColor(lum > 140 ? Color.rgb(26, 26, 46) : Color.WHITE);
            paint.setTextSize((y1 - y0) * 0.46f);
            paint.setTextAlign(Paint.Align.CENTER);
            Paint.FontMetrics fm = paint.getFontMetrics();
            float ty = (y0 + y1) / 2f - (fm.ascent + fm.descent) / 2f;
            c.drawText(label, (x0 + x1) / 2f, ty, paint);
            paint.setTextAlign(Paint.Align.LEFT);
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
            if (lastImportMsg_ != null && !lastImportMsg_.isEmpty()) {
                c.drawText("import: " + lastImportMsg_, x, y + sz * 2 + 12, paint);
            }
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

        private int utf8CpLen(int cp) {
            if (cp < 0x80) return 1;
            if (cp < 0x800) return 2;
            if (cp < 0x10000) return 3;
            return 4;
        }
        private int utf8Len(String s) {
            int n = 0; int i = 0; int len = s.length();
            while (i < len) { int cp = s.codePointAt(i); n += utf8CpLen(cp); i += Character.charCount(cp); }
            return n;
        }

        private void drawLine(Canvas c, String line) {
            if (!line.startsWith("DRAW ")) return;
            String[] p = line.substring(5).split("\\|", -1);
            try {
                if (p[0].equals("clipon")) { c.save(); c.clipRect(EDV_X0, EDV_Y0, EDV_X1, EDV_Y1); return; }
                if (p[0].equals("clipoff")) { c.restore(); return; }
                if (p[0].equals("bg")) {
                    // Фон сцены рисуем ВНУТРИ игрового прямоугольника (логические координаты).
                    paint.setColor(Color.parseColor(p[1]));
                    c.drawRect(0, 0, logicW, logicH, paint);
                    return;
                }

                if (p[0].equals("codeline")) {
                    if (p.length < 6) return;
                    int idx = Integer.parseInt(p[1]);
                    String t = p[2];
                    float yy = Float.parseFloat(p[3]);
                    float fs = Float.parseFloat(p[4]);
                    clIdx.add(idx); clText.add(t); clY.add(yy);
                    paint.setColor(Color.parseColor(p[5]));
                    paint.setTextSize(fs); paint.setTextAlign(Paint.Align.LEFT);
                    if (typeface != null) paint.setTypeface(typeface);
                    c.drawText(t, CODE_TEXT_X, yy + fs, paint);
                    return;
                }
                else if (p[0].equals("caret")) {
                    if (p.length < 6) return;
                    String pref = p[1];
                    float bx = Float.parseFloat(p[2]);
                    float by = Float.parseFloat(p[3]);
                    float hh = Float.parseFloat(p[4]);
                    paint.setColor(Color.parseColor(p[5]));
                    paint.setTextSize(CODE_FONT);
                    paint.setTextAlign(Paint.Align.LEFT);
                    if (typeface != null) paint.setTypeface(typeface);
                    float w = paint.measureText(pref);
                    c.drawRect(bx + w, by + 1, bx + w + 2f, by + hh, paint);
                }
                else if (p[0].equals("mtext")) {
                    paint.setColor(Color.parseColor(p[5]));
                    float fs = Float.parseFloat(p[4]);
                    float cell = p.length > 6 ? Float.parseFloat(p[6]) : fs * 0.6f;
                    paint.setTextSize(fs); paint.setTextAlign(Paint.Align.LEFT);
                    if (typeface != null) paint.setTypeface(typeface);
                    float x = Float.parseFloat(p[2]), y = Float.parseFloat(p[3]);
                    String t = p[1];
                    float cx = x;
                    int idx = 0;
                    while (idx < t.length()) {
                        int cp = t.codePointAt(idx);
                        c.drawText(new String(Character.toChars(cp)), cx, y + fs, paint);
                        cx += cell;
                        idx += Character.charCount(cp);
                    }
                }
                else if (p[0].equals("text")) {
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
                    int fa = Color.alpha(fill);
                    float ang = p.length > 7 ? Float.parseFloat(p[7]) : 0f;
                    String tex = p.length > 8 ? p[8] : "";
                    c.save(); c.translate(x + w/2, y + h/2); c.rotate(ang);
                    if (!tex.isEmpty()) {
                        Bitmap bm = loadBitmap(tex);
                        if (bm != null) { paint.setAlpha(fa); c.drawBitmap(bm, null, new RectF(-w/2, -h/2, w/2, h/2), paint); c.restore(); paint.setAlpha(255); return; }
                    }
                    paint.setColor(fill);
                    c.drawRoundRect(new RectF(-w/2, -h/2, w/2, h/2), 12f, 12f, paint);
                    double lum = 0.299 * ((fill >> 16) & 255) + 0.587 * ((fill >> 8) & 255) + 0.114 * (fill & 255);
                    paint.setColor(lum > 140 ? Color.rgb(26, 26, 46) : Color.WHITE);
                    paint.setAlpha(fa);
                    paint.setTextSize(Math.min(30f, h * 0.45f));
                    paint.setTextAlign(Paint.Align.CENTER);
                    if (typeface != null) paint.setTypeface(typeface);
                    c.drawText(p[1], 0, paint.getTextSize() * 0.35f, paint);
                    paint.setTextAlign(Paint.Align.LEFT);
                    c.restore();
                    paint.setAlpha(255);
                }
                else if (p[0].equals("tex")) {
                    Bitmap bm = loadBitmap(p[1]);
                    if (bm != null) {
                        float x = Float.parseFloat(p[2]), y = Float.parseFloat(p[3]);
                        float w = Float.parseFloat(p[4]), h = Float.parseFloat(p[5]);
                        float ang = p.length > 6 ? Float.parseFloat(p[6]) : 0f;
                        if (editorFrame_) { c.save(); c.clipRect(EDV_X0, EDV_Y0, EDV_X1, EDV_Y1); }
                        if (ang != 0f) { c.save(); c.translate(x + w/2, y + h/2); c.rotate(ang); c.drawBitmap(bm, null, new RectF(-w/2, -h/2, w/2, h/2), paint); c.restore(); }
                        else c.drawBitmap(bm, null, new RectF(x, y, x + w, y + h), paint);
                        if (editorFrame_) { c.restore(); }
                    }
                }
            } catch (Exception e) { }
        }

        @Override public boolean onTouchEvent(MotionEvent e) {
            int a = e.getActionMasked();
            int count = e.getPointerCount();
            int rw = getWidth(), rh = getHeight();

            // 1) Оверлейные кнопки (только в игре) — первыми, в экранных px.
            if (renderMode == MODE_GAME && count == 1 && a == MotionEvent.ACTION_DOWN) {
                float[] R = ovBtnRects(rw, rh);
                float ex = e.getX(), ey = e.getY();
                if (ex >= R[0] && ex <= R[2] && ey >= R[1] && ey <= R[3]) { nativeSetAction("ov:dbg:"); return true; }
                if (ex >= R[4] && ex <= R[6] && ey >= R[5] && ey <= R[7]) { nativeSetAction("ov:close:"); return true; }
            }

            // 2) Маппинг тапа в координаты холста (совпадает с трансформацией рисунка).
            float lx, ly;
            if (rw > 0 && rh > 0 && logicW > 1.0f && logicH > 1.0f) {
                if (renderMode == MODE_GAME) {
                    float s  = Math.min(rw / logicW, rh / logicH);
                    float ox = (rw - logicW * s) * 0.5f;
                    float oy = (rh - logicH * s) * 0.5f;
                    lx = (e.getX() - ox) / s;
                    ly = (e.getY() - oy) / s;
                } else {
                    lx = e.getX() * logicW / rw;
                    ly = e.getY() * logicH / rh;
                }
            } else {
                lx = e.getX(); ly = e.getY();
            }

            if (a == MotionEvent.ACTION_DOWN && count == 1 && !clIdx.isEmpty()
                    && lx >= CODE_X0 && lx <= CODE_X1 && ly >= CODE_Y0 && ly <= CODE_Y1) {
                int bi = 0; float bd = Float.MAX_VALUE;
                for (int k = 0; k < clY.size(); ++k) {
                    float mid = clY.get(k) + 9.5f;
                    float d = Math.abs(ly - mid);
                    if (d < bd) { bd = d; bi = k; }
                }
                int gline = clIdx.get(bi);
                String t = clText.get(bi);
                float target = lx - CODE_TEXT_X;
                int totalBytes = utf8Len(t);
                int col = 0;
                if (target > 0f) {
                    measurePaint.setTextSize(CODE_FONT);
                    measurePaint.setTextAlign(Paint.Align.LEFT);
                    measurePaint.setTypeface(typeface != null ? typeface : Typeface.DEFAULT);
                    int i = 0; int bo = 0; float prevW = 0f; boolean done = false;
                    int len = t.length();
                    while (i < len) {
                        int cp = t.codePointAt(i);
                        int nc = i + Character.charCount(cp);
                        int cplen = utf8CpLen(cp);
                        float w = measurePaint.measureText(t.substring(0, nc));
                        if (w >= target) {
                            col = (Math.abs(prevW - target) <= Math.abs(w - target)) ? bo : bo + cplen;
                            done = true; break;
                        }
                        prevW = w; bo += cplen; i = nc;
                    }
                    if (!done) col = totalBytes;
                }
                nativeTouch(9, (float) gline, (float) col);
                return true;
            }

            if (count >= 2) {
                float x0, y0, x1, y1;
                if (rw > 0 && rh > 0 && logicW > 1.0f && logicH > 1.0f) {
                    if (renderMode == MODE_GAME) {
                        float s  = Math.min(rw / logicW, rh / logicH);
                        float ox = (rw - logicW * s) * 0.5f;
                        float oy = (rh - logicH * s) * 0.5f;
                        x0 = (e.getX(0) - ox) / s; y0 = (e.getY(0) - oy) / s;
                        x1 = (e.getX(1) - ox) / s; y1 = (e.getY(1) - oy) / s;
                    } else {
                        x0 = e.getX(0) * logicW / rw; y0 = e.getY(0) * logicH / rh;
                        x1 = e.getX(1) * logicW / rw; y1 = e.getY(1) * logicH / rh;
                    }
                } else {
                    x0 = e.getX(0); y0 = e.getY(0); x1 = e.getX(1); y1 = e.getY(1);
                }
                int ph = (a == MotionEvent.ACTION_POINTER_DOWN || a == MotionEvent.ACTION_DOWN) ? 1
                       : (a == MotionEvent.ACTION_POINTER_UP   || a == MotionEvent.ACTION_UP)   ? 3 : 2;
                nativeMultiTouch(ph, x0, y0, x1, y1);
                return true;
            }
            if (a == MotionEvent.ACTION_DOWN || a == MotionEvent.ACTION_MOVE || a == MotionEvent.ACTION_UP) {
                nativeTouch(a, lx, ly);
            }
            return true;
        }
    }
                        }
