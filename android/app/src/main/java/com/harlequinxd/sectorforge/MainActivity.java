package com.harlequinxd.sectorforge;

import android.app.AlertDialog;
import android.app.NativeActivity;
import android.content.ActivityNotFoundException;
import android.content.Intent;
import android.content.pm.ActivityInfo;
import android.database.Cursor;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.provider.OpenableColumns;
import android.text.InputType;
import android.view.HapticFeedbackConstants;
import android.view.View;
import android.view.WindowInsets;
import android.view.WindowInsetsController;
import android.view.WindowManager;
import android.view.inputmethod.EditorInfo;
import android.view.inputmethod.InputMethodManager;
import android.widget.EditText;
import android.widget.FrameLayout;

import java.io.File;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.io.OutputStream;

/**
 * The editor runs in native code (libsectorforge.so via NativeActivity). This class only provides the
 * pieces that need the Java framework: orientation, immersive full screen, safe-area insets,
 * haptics, the text-entry dialog and the gallery picker.
 */
public class MainActivity extends NativeActivity {
    static { System.loadLibrary("sectorforge"); }

    private static final int REQ_IMAGE = 4201;

    static native void nativeInsets(int left, int top, int right, int bottom);
    static native void nativeTextResult(int request, String text);
    static native void nativeImagePicked(String path);

    @Override
    protected void onCreate(Bundle state) {
        super.onCreate(state);
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.P) {
            WindowManager.LayoutParams lp = getWindow().getAttributes();
            lp.layoutInDisplayCutoutMode = WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_SHORT_EDGES;
            getWindow().setAttributes(lp);
        }
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        View decor = getWindow().getDecorView();
        decor.setOnApplyWindowInsetsListener((v, insets) -> {
            reportInsets(insets);
            return v.onApplyWindowInsets(insets);
        });
        immersive();
    }

    private void reportInsets(WindowInsets wi) {
        int l = 0, t = 0, r = 0, b = 0;
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
            android.graphics.Insets c = wi.getInsets(WindowInsets.Type.displayCutout());
            android.graphics.Insets g = wi.getInsets(WindowInsets.Type.mandatorySystemGestures());
            l = c.left; t = c.top; r = c.right; b = Math.max(c.bottom, g.bottom / 2);
        } else if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.P && wi.getDisplayCutout() != null) {
            l = wi.getDisplayCutout().getSafeInsetLeft();
            t = wi.getDisplayCutout().getSafeInsetTop();
            r = wi.getDisplayCutout().getSafeInsetRight();
            b = wi.getDisplayCutout().getSafeInsetBottom();
        }
        nativeInsets(l, t, r, b);
    }

    @SuppressWarnings("deprecation")
    private void immersive() {
        View decor = getWindow().getDecorView();
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
            getWindow().setDecorFitsSystemWindows(false);
            WindowInsetsController c = decor.getWindowInsetsController();
            if (c != null) {
                c.hide(WindowInsets.Type.statusBars() | WindowInsets.Type.navigationBars());
                c.setSystemBarsBehavior(WindowInsetsController.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE);
            }
        } else {
            decor.setSystemUiVisibility(View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY | View.SYSTEM_UI_FLAG_FULLSCREEN
                    | View.SYSTEM_UI_FLAG_HIDE_NAVIGATION | View.SYSTEM_UI_FLAG_LAYOUT_STABLE
                    | View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN | View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION);
        }
    }

    @Override
    public void onWindowFocusChanged(boolean hasFocus) {
        super.onWindowFocusChanged(hasFocus);
        if (hasFocus) immersive();
    }

    /* ---- called from native code (any thread) ---- */

    public void setOrientation(final int o) {
        runOnUiThread(() -> setRequestedOrientation(o == 0 ? ActivityInfo.SCREEN_ORIENTATION_PORTRAIT
                : o == 1 ? ActivityInfo.SCREEN_ORIENTATION_SENSOR_LANDSCAPE : ActivityInfo.SCREEN_ORIENTATION_FULL_USER));
    }

    public void haptic(final int kind) {
        runOnUiThread(() -> {
            int c = HapticFeedbackConstants.KEYBOARD_TAP;
            if (kind == 1) c = Build.VERSION.SDK_INT >= Build.VERSION_CODES.R ? HapticFeedbackConstants.CONFIRM : HapticFeedbackConstants.VIRTUAL_KEY;
            if (kind == 2) c = HapticFeedbackConstants.LONG_PRESS;
            getWindow().getDecorView().performHapticFeedback(c);
        });
    }

    public void openUrl(final String url) {
        runOnUiThread(() -> {
            try { startActivity(new Intent(Intent.ACTION_VIEW, Uri.parse(url))); } catch (ActivityNotFoundException ignored) { }
        });
    }

    public void textInput(final int request, final String title, final String initial, final int kind) {
        runOnUiThread(() -> {
            final EditText edit = new EditText(this);
            edit.setSingleLine(true);
            edit.setText(initial);
            edit.setSelectAllOnFocus(true);
            edit.setInputType(kind == 1
                    ? InputType.TYPE_CLASS_NUMBER | InputType.TYPE_NUMBER_FLAG_SIGNED | InputType.TYPE_NUMBER_FLAG_DECIMAL
                    : InputType.TYPE_CLASS_TEXT | InputType.TYPE_TEXT_FLAG_CAP_WORDS);
            edit.setImeOptions(EditorInfo.IME_ACTION_DONE | EditorInfo.IME_FLAG_NO_EXTRACT_UI);
            FrameLayout box = new FrameLayout(this);
            int pad = (int) (22 * getResources().getDisplayMetrics().density);
            box.setPadding(pad, pad / 2, pad, 0);
            box.addView(edit);
            final AlertDialog dlg = new AlertDialog.Builder(this, android.R.style.Theme_DeviceDefault_Dialog_Alert)
                    .setTitle(title)
                    .setView(box)
                    .setPositiveButton(android.R.string.ok, (d, w) -> nativeTextResult(request, edit.getText().toString().trim()))
                    .setNegativeButton(android.R.string.cancel, null)
                    .create();
            edit.setOnEditorActionListener((v, actionId, ev) -> {
                if (actionId == EditorInfo.IME_ACTION_DONE) {
                    nativeTextResult(request, edit.getText().toString().trim());
                    dlg.dismiss();
                    return true;
                }
                return false;
            });
            dlg.setOnShowListener(d -> {
                edit.requestFocus();
                InputMethodManager imm = (InputMethodManager) getSystemService(INPUT_METHOD_SERVICE);
                if (imm != null) imm.showSoftInput(edit, InputMethodManager.SHOW_IMPLICIT);
            });
            dlg.setOnDismissListener(d -> immersive());
            dlg.show();
        });
    }

    public void pickImage() {
        runOnUiThread(() -> {
            Intent i = new Intent(Intent.ACTION_GET_CONTENT);
            i.setType("image/*");
            i.addCategory(Intent.CATEGORY_OPENABLE);
            try { startActivityForResult(Intent.createChooser(i, "Import texture"), REQ_IMAGE); }
            catch (ActivityNotFoundException e) { nativeImagePicked(""); }
        });
    }

    @Override
    protected void onActivityResult(int request, int result, Intent data) {
        super.onActivityResult(request, result, data);
        if (request != REQ_IMAGE || result != RESULT_OK || data == null || data.getData() == null) return;
        final Uri uri = data.getData();
        new Thread(() -> {
            String name = "IMPORTED";
            try (Cursor c = getContentResolver().query(uri, null, null, null, null)) {
                if (c != null && c.moveToFirst()) {
                    int col = c.getColumnIndex(OpenableColumns.DISPLAY_NAME);
                    if (col >= 0 && c.getString(col) != null) name = c.getString(col);
                }
            } catch (Exception ignored) { }
            String base = name.replaceAll("\\.[^.]*$", "").replaceAll("[^A-Za-z0-9_-]", "_");
            if (base.isEmpty()) base = "IMPORTED";
            String ext = name.toLowerCase().endsWith(".jpg") || name.toLowerCase().endsWith(".jpeg") ? ".jpg" : ".png";
            File dir = new File(getCacheDir(), "imports");
            dir.mkdirs();
            File out = new File(dir, base + ext);
            try (InputStream in = getContentResolver().openInputStream(uri); OutputStream os = new FileOutputStream(out)) {
                if (in == null) return;
                byte[] buf = new byte[65536];
                int n;
                while ((n = in.read(buf)) > 0) os.write(buf, 0, n);
            } catch (Exception e) {
                return;
            }
            nativeImagePicked(out.getAbsolutePath());
        }).start();
    }
}
