package org.hieroocr.app;

import android.app.AlertDialog;
import android.content.ActivityNotFoundException;
import android.content.ClipData;
import android.content.ClipboardManager;
import android.content.Context;
import android.content.Intent;
import android.content.pm.PackageInfo;
import android.database.Cursor;
import android.graphics.Bitmap;
import android.graphics.BitmapFactory;
import android.graphics.Color;
import android.graphics.Matrix;
import android.graphics.pdf.PdfRenderer;
import android.media.ExifInterface;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.os.ParcelFileDescriptor;
import android.provider.MediaStore;
import android.provider.OpenableColumns;
import android.text.InputType;
import android.util.Log;
import android.view.View;
import android.widget.EditText;
import android.widget.Toast;

import androidx.core.content.FileProvider;
import androidx.core.graphics.Insets;
import androidx.core.view.ViewCompat;
import androidx.core.view.WindowCompat;
import androidx.core.view.WindowInsetsCompat;

import org.libsdl.app.SDLActivity;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.util.ArrayList;
import java.util.Locale;
import java.util.TreeSet;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;

/**
 * Actividad de HieroOCR: la interfaz y el reconocimiento están en C (SDL2);
 * aquí sólo se hace lo que necesita la API de Android: elegir archivos, usar
 * la cámara, rasterizar PDF, enderezar fotos y compartir el resultado.
 */
public class HieroActivity extends SDLActivity {
    private static final String TAG = "hieroocr";
    private static final int REQ_OPEN = 4101, REQ_CAMERA = 4102;
    private static final int MAX_SIDE = 3600;   /* píxeles: suficiente para reconocer, sin agotar memoria */
    private static final int PDF_DPI = 300;

    /* Tipos de petición: deben coincidir con el enum de src/hieroocr.h. */
    static final int K_DOCUMENT = 0, K_PHOTO = 1, K_MODEL = 2, K_STATUS = 3;

    private static HieroActivity self;
    private final ExecutorService worker = Executors.newSingleThreadExecutor();
    private File photoFile;

    /** Entrega imágenes ya preparadas (o un aviso) a la interfaz SDL (src/android.c). */
    public static native void nativeRequest(int kind, String[] files, String msg);

    @Override
    protected String[] getLibraries() {
        return new String[] { "SDL2", "SDL2_image", "SDL2_ttf", "main" };
    }

    @Override
    protected void onCreate(Bundle state) {
        self = this;
        installData();   /* antes de que arranque el hilo nativo */
        if (state != null && state.getString("foto") != null)
            photoFile = new File(state.getString("foto"));
        super.onCreate(state);
        keepClearOfSystemBars();
        handleIntent(getIntent());
    }

    @Override
    protected void onSaveInstanceState(Bundle out) {
        super.onSaveInstanceState(out);
        if (photoFile != null)
            out.putString("foto", photoFile.getAbsolutePath());
    }

    @Override
    protected void onNewIntent(Intent intent) {
        super.onNewIntent(intent);
        handleIntent(intent);
    }

    /* Desde Android 15 (targetSdk 35+) la ventana ocupa toda la pantalla, también bajo
     * la barra de estado, la de navegación y la muesca. Se hace igual en todas las
     * versiones y se deja un margen alrededor de la superficie de SDL, para que la
     * barra de botones y el panel inferior no queden tapados. */
    private void keepClearOfSystemBars() {
        WindowCompat.setDecorFitsSystemWindows(getWindow(), false);
        getWindow().getDecorView().setBackgroundColor(Color.BLACK);
        View content = findViewById(android.R.id.content);
        if (content == null)
            return;
        ViewCompat.setOnApplyWindowInsetsListener(content, (v, insets) -> {
            Insets b = insets.getInsets(WindowInsetsCompat.Type.systemBars()
                                        | WindowInsetsCompat.Type.displayCutout());
            v.setPadding(b.left, b.top, b.right, b.bottom);
            return WindowInsetsCompat.CONSUMED;
        });
        ViewCompat.requestApplyInsets(content);
    }

    /* "Abrir con..." o "Compartir" desde otra aplicación. Para pruebas: --es archivo RUTA. */
    private void handleIntent(Intent intent) {
        if (intent == null)
            return;
        Uri uri = null;
        String action = intent.getAction();
        if (Intent.ACTION_VIEW.equals(action))
            uri = intent.getData();
        else if (Intent.ACTION_SEND.equals(action))
            uri = intent.getParcelableExtra(Intent.EXTRA_STREAM);
        String path = intent.getStringExtra("archivo");
        if (path != null)
            uri = Uri.fromFile(new File(path));
        if (uri != null) {
            final Uri u = uri;
            worker.execute(() -> importUri(u));
        }
    }

    /* ---------- datos de la aplicación ---------- */

    /** Copia los datos de assets/datos a la memoria interna (una vez por versión). */
    private void installData() {
        File dir = new File(getFilesDir(), "datos");
        File stamp = new File(dir, ".version");
        String version = "?";
        try {
            PackageInfo pi = getPackageManager().getPackageInfo(getPackageName(), 0);
            version = pi.versionName + "/" + pi.lastUpdateTime;
        } catch (Exception e) {
            Log.w(TAG, "versión desconocida", e);
        }
        if (stamp.exists() && version.equals(readSmall(stamp)))
            return;
        dir.mkdirs();
        try {
            for (String name : getAssets().list("datos")) {
                try (InputStream in = getAssets().open("datos/" + name)) {
                    copy(in, new File(dir, name));
                }
            }
            try (OutputStream o = new FileOutputStream(stamp)) {
                o.write(version.getBytes("UTF-8"));
            }
        } catch (IOException e) {
            Log.e(TAG, "no se pueden instalar los datos", e);
        }
    }

    private static String readSmall(File f) {
        try (InputStream in = new java.io.FileInputStream(f)) {
            byte[] b = new byte[256];
            int n = in.read(b);
            return n > 0 ? new String(b, 0, n, "UTF-8") : "";
        } catch (IOException e) {
            return "";
        }
    }

    private static void copy(InputStream in, File dst) throws IOException {
        File tmp = new File(dst.getPath() + ".tmp");
        try (OutputStream out = new FileOutputStream(tmp)) {
            byte[] buf = new byte[1 << 16];
            int n;
            while ((n = in.read(buf)) > 0)
                out.write(buf, 0, n);
        }
        if (!tmp.renameTo(dst))
            throw new IOException("no se puede renombrar " + tmp);
    }

    /* ---------- llamadas desde C (hilo de SDL) ---------- */

    public static void pickFile() {
        if (self == null)
            return;
        self.runOnUiThread(() -> {
            Intent i = new Intent(Intent.ACTION_OPEN_DOCUMENT);
            i.addCategory(Intent.CATEGORY_OPENABLE);
            i.setType("*/*");
            i.putExtra(Intent.EXTRA_MIME_TYPES,
                       new String[] { "image/*", "application/pdf", "application/octet-stream" });
            try {
                self.startActivityForResult(i, REQ_OPEN);
            } catch (ActivityNotFoundException e) {
                status("No hay ninguna aplicación para elegir archivos.");
            }
        });
    }

    public static void takePhoto() {
        if (self == null)
            return;
        self.runOnUiThread(() -> self.startCamera());
    }

    private void startCamera() {
        File dir = new File(getCacheDir(), "camara");
        dir.mkdirs();
        File[] old = dir.listFiles();
        if (old != null)
            for (File f : old)
                f.delete();
        photoFile = new File(dir, "foto-" + System.currentTimeMillis() + ".jpg");
        Uri uri = FileProvider.getUriForFile(this, getPackageName() + ".archivos", photoFile);
        Intent i = new Intent(MediaStore.ACTION_IMAGE_CAPTURE);
        i.putExtra(MediaStore.EXTRA_OUTPUT, uri);
        i.setClipData(ClipData.newRawUri("foto", uri));
        i.addFlags(Intent.FLAG_GRANT_WRITE_URI_PERMISSION | Intent.FLAG_GRANT_READ_URI_PERMISSION);
        try {
            startActivityForResult(i, REQ_CAMERA);
        } catch (ActivityNotFoundException e) {
            status("Este dispositivo no tiene aplicación de cámara.");
        }
    }

    public static void shareText(String path, String text) {
        if (self == null)
            return;
        self.runOnUiThread(() -> {
            ClipboardManager cb = (ClipboardManager) self.getSystemService(Context.CLIPBOARD_SERVICE);
            if (cb != null)
                cb.setPrimaryClip(ClipData.newPlainText("MdC", text));
            Toast.makeText(self, "MdC copiado al portapapeles", Toast.LENGTH_SHORT).show();
            Intent i = new Intent(Intent.ACTION_SEND);
            i.setType("text/plain");
            i.putExtra(Intent.EXTRA_SUBJECT, "Texto jeroglífico en MdC");
            if (text.length() < 100000)
                i.putExtra(Intent.EXTRA_TEXT, text);
            try {
                Uri uri = FileProvider.getUriForFile(self, self.getPackageName() + ".archivos", new File(path));
                i.putExtra(Intent.EXTRA_STREAM, uri);
                i.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION);
            } catch (IllegalArgumentException e) {
                Log.w(TAG, "no se puede adjuntar " + path, e);
            }
            try {
                self.startActivity(Intent.createChooser(i, "Compartir el MdC"));
            } catch (ActivityNotFoundException e) {
                status("MdC guardado en " + path);
            }
        });
    }

    private static void status(String msg) {
        nativeRequest(K_STATUS, null, msg);
    }

    /* ---------- resultados del selector y de la cámara ---------- */

    @Override
    protected void onActivityResult(int req, int res, Intent data) {
        super.onActivityResult(req, res, data);
        if (req == REQ_OPEN && res == RESULT_OK && data != null && data.getData() != null) {
            final Uri u = data.getData();
            worker.execute(() -> importUri(u));
        } else if (req == REQ_CAMERA && res == RESULT_OK && photoFile != null) {
            final File f = photoFile;
            worker.execute(() -> importImage(f, true));
        } else if (req == REQ_OPEN || req == REQ_CAMERA) {
            status("Cancelado.");
        }
    }

    private String displayName(Uri uri) {
        String name = null;
        if ("content".equals(uri.getScheme())) {
            try (Cursor c = getContentResolver().query(uri, new String[] { OpenableColumns.DISPLAY_NAME },
                                                       null, null, null)) {
                if (c != null && c.moveToFirst())
                    name = c.getString(0);
            } catch (Exception e) {
                Log.w(TAG, "sin nombre para " + uri, e);
            }
        }
        if (name == null)
            name = uri.getLastPathSegment();
        return name == null ? "" : name;
    }

    /** Copia lo elegido y decide qué es: PDF, modelo (.hmod) o imagen. (Hilo de trabajo.) */
    private void importUri(Uri uri) {
        String name = displayName(uri).toLowerCase(Locale.ROOT);
        String mime = getContentResolver().getType(uri);
        status("Leyendo " + displayName(uri) + "...");
        try {
            File dir = new File(getCacheDir(), "entrada");
            dir.mkdirs();
            if (name.endsWith(".hmod")) {
                File dst = new File(new File(getFilesDir(), "datos"), "modelo_fotos.hmod");
                try (InputStream in = getContentResolver().openInputStream(uri)) {
                    copy(in, dst);
                }
                nativeRequest(K_MODEL, null, "");
                return;
            }
            boolean pdf = "application/pdf".equals(mime) || name.endsWith(".pdf");
            File dst = new File(dir, pdf ? "documento.pdf" : "imagen");
            try (InputStream in = getContentResolver().openInputStream(uri)) {
                if (in == null)
                    throw new IOException("no se puede leer");
                copy(in, dst);
            }
            if (pdf)
                runOnUiThread(() -> askPages(dst));
            else
                importImage(dst, true);
        } catch (Exception e) {
            Log.e(TAG, "importación fallida", e);
            status("No se puede abrir el archivo: " + e.getMessage());
        }
    }

    /** Decodifica (JPEG, PNG, HEIC, WebP...), endereza según el EXIF, reduce y guarda en PNG. */
    private void importImage(File src, boolean photo) {
        try {
            BitmapFactory.Options bo = new BitmapFactory.Options();
            bo.inJustDecodeBounds = true;
            BitmapFactory.decodeFile(src.getPath(), bo);
            if (bo.outWidth <= 0 || bo.outHeight <= 0) {
                status("El archivo no es una imagen que se pueda leer.");
                return;
            }
            int sample = 1;
            while (Math.max(bo.outWidth, bo.outHeight) / (sample * 2) >= MAX_SIDE)
                sample *= 2;
            bo = new BitmapFactory.Options();
            bo.inSampleSize = sample;
            Bitmap bmp = BitmapFactory.decodeFile(src.getPath(), bo);
            if (bmp == null) {
                status("No se puede decodificar la imagen.");
                return;
            }
            Matrix m = new Matrix();
            float k = (float) MAX_SIDE / Math.max(bmp.getWidth(), bmp.getHeight());
            if (k < 1)
                m.postScale(k, k);
            try {
                int or = new ExifInterface(src.getPath()).getAttributeInt(ExifInterface.TAG_ORIENTATION,
                                                                           ExifInterface.ORIENTATION_NORMAL);
                switch (or) {
                case ExifInterface.ORIENTATION_ROTATE_90: m.postRotate(90); break;
                case ExifInterface.ORIENTATION_ROTATE_180: m.postRotate(180); break;
                case ExifInterface.ORIENTATION_ROTATE_270: m.postRotate(270); break;
                case ExifInterface.ORIENTATION_FLIP_HORIZONTAL: m.postScale(-1, 1); break;
                case ExifInterface.ORIENTATION_FLIP_VERTICAL: m.postScale(1, -1); break;
                default: break;
                }
            } catch (IOException e) {
                Log.w(TAG, "sin EXIF", e);
            }
            if (!m.isIdentity()) {
                Bitmap b2 = Bitmap.createBitmap(bmp, 0, 0, bmp.getWidth(), bmp.getHeight(), m, true);
                if (b2 != bmp)
                    bmp.recycle();
                bmp = b2;
            }
            File dir = freshDir("imagen");
            File out = new File(dir, "imagen.png");
            try (OutputStream o = new FileOutputStream(out)) {
                bmp.compress(Bitmap.CompressFormat.PNG, 100, o);
            }
            bmp.recycle();
            nativeRequest(photo ? K_PHOTO : K_DOCUMENT, new String[] { out.getPath() }, "");
        } catch (OutOfMemoryError e) {
            status("La imagen es demasiado grande.");
        } catch (Exception e) {
            Log.e(TAG, "imagen fallida", e);
            status("No se puede abrir la imagen: " + e.getMessage());
        }
    }

    /** Carpeta de caché vacía y con nombre nuevo (la interfaz puede tener abiertas las anteriores). */
    private File freshDir(String prefix) {
        File[] old = getCacheDir().listFiles();
        if (old != null)
            for (File d : old)
                if (d.isDirectory() && d.getName().startsWith(prefix + "-")) {
                    File[] fs = d.listFiles();
                    if (fs != null)
                        for (File f : fs)
                            f.delete();
                    d.delete();
                }
        File d = new File(getCacheDir(), prefix + "-" + System.currentTimeMillis());
        d.mkdirs();
        return d;
    }

    /* ---------- PDF ---------- */

    private void askPages(File pdf) {
        int n;
        try (ParcelFileDescriptor fd = ParcelFileDescriptor.open(pdf, ParcelFileDescriptor.MODE_READ_ONLY);
             PdfRenderer r = new PdfRenderer(fd)) {
            n = r.getPageCount();
        } catch (Exception e) {
            status("No se puede abrir el PDF: " + e.getMessage());
            return;
        }
        final int total = n;
        if (total == 1) {
            worker.execute(() -> renderPdf(pdf, "1", total));
            return;
        }
        EditText input = new EditText(this);
        input.setInputType(InputType.TYPE_CLASS_TEXT);
        input.setText(total <= 5 ? "1-" + total : "1");
        input.setSelectAllOnFocus(true);
        new AlertDialog.Builder(this)
            .setTitle("PDF de " + total + " páginas")
            .setMessage("¿Qué páginas quieres reconocer? Por ejemplo: 7, 71-92, 71- o 3,5,7-9")
            .setView(input)
            .setPositiveButton("Aceptar", (d, w) -> {
                String spec = input.getText().toString();
                worker.execute(() -> renderPdf(pdf, spec, total));
            })
            .setNegativeButton("Cancelar", (d, w) -> status("Cancelado."))
            .show();
    }

    /** "71", "71-92", "71-", "-10", "3,5,7-9" -> páginas (desde 1). Igual que -p en escritorio. */
    static TreeSet<Integer> parsePages(String spec, int total) {
        TreeSet<Integer> pages = new TreeSet<>();
        for (String part : spec.replace(" ", "").split(",")) {
            if (part.isEmpty())
                continue;
            int a, b;
            int dash = part.indexOf('-');
            if (dash < 0) {
                a = b = Integer.parseInt(part);
            } else {
                String l = part.substring(0, dash), r = part.substring(dash + 1);
                a = l.isEmpty() ? 1 : Integer.parseInt(l);
                b = r.isEmpty() ? total : Integer.parseInt(r);
            }
            for (int p = Math.max(1, a); p <= Math.min(b, total); p++)
                pages.add(p);
        }
        return pages;
    }

    private void renderPdf(File pdf, String spec, int total) {
        TreeSet<Integer> pages;
        try {
            pages = parsePages(spec, total);
        } catch (NumberFormatException e) {
            status("Rango de páginas no válido: " + spec);
            return;
        }
        if (pages.isEmpty()) {
            status("Ninguna de esas páginas está en el PDF (tiene " + total + ").");
            return;
        }
        File dir = freshDir("pdf");
        ArrayList<String> files = new ArrayList<>();
        try (ParcelFileDescriptor fd = ParcelFileDescriptor.open(pdf, ParcelFileDescriptor.MODE_READ_ONLY);
             PdfRenderer r = new PdfRenderer(fd)) {
            int k = 0;
            for (int p : pages) {
                status(String.format(Locale.ROOT, "Rasterizando la página %d (%d de %d)...", p, ++k, pages.size()));
                try (PdfRenderer.Page page = r.openPage(p - 1)) {
                    float s = PDF_DPI / 72f;
                    float big = Math.max(page.getWidth(), page.getHeight()) * s;
                    if (big > MAX_SIDE + 400)
                        s *= (MAX_SIDE + 400) / big;
                    int w = Math.max(1, Math.round(page.getWidth() * s));
                    int h = Math.max(1, Math.round(page.getHeight() * s));
                    Bitmap bmp = Bitmap.createBitmap(w, h, Bitmap.Config.ARGB_8888);
                    bmp.eraseColor(Color.WHITE);
                    page.render(bmp, null, null, PdfRenderer.Page.RENDER_MODE_FOR_PRINT);
                    File out = new File(dir, String.format(Locale.ROOT, "p-%03d.png", p));
                    try (OutputStream o = new FileOutputStream(out)) {
                        bmp.compress(Bitmap.CompressFormat.PNG, 100, o);
                    }
                    bmp.recycle();
                    files.add(out.getPath());
                }
            }
        } catch (OutOfMemoryError e) {
            status("Sin memoria al rasterizar el PDF.");
            return;
        } catch (Exception e) {
            Log.e(TAG, "PDF fallido", e);
            status("No se puede rasterizar el PDF: " + e.getMessage());
            return;
        }
        nativeRequest(K_DOCUMENT, files.toArray(new String[0]), "");
    }
}
