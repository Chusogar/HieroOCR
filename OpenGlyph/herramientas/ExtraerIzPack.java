import com.izforge.izpack.PackFile;
import java.io.*;
public class ExtraerIzPack {
    public static void main(String[] a) throws Exception {
        File out = new File(a[0]);
        for (int i = 1; i < a.length; i++) {
            ObjectInputStream in = new ObjectInputStream(new BufferedInputStream(new FileInputStream(a[i])));
            int n = in.readInt();
            System.err.println(a[i] + ": " + n + " archivos");
            for (int k = 0; k < n; k++) {
                PackFile pf = (PackFile) in.readObject();
                String t = pf.getTargetPath().replace("$INSTALL_PATH", "").replace("\\", "/");
                File f = new File(out, t);
                if (pf.isDirectory()) { f.mkdirs(); continue; }
                if (pf.isBackReference()) { System.err.println("ref " + t); continue; }
                f.getParentFile().mkdirs();
                try (OutputStream o = new FileOutputStream(f)) {
                    byte[] b = new byte[65536]; long left = pf.length();
                    while (left > 0) { int r = in.read(b, 0, (int) Math.min(b.length, left)); if (r < 0) throw new EOFException(); o.write(b, 0, r); left -= r; }
                }
            }
        }
    }
}
