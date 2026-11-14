import jsesh.hieroglyphs.HieroglyphsManager;
import java.util.*;
public class CodigosJSesh {
    public static void main(String[] a) {
        HieroglyphsManager m = HieroglyphsManager.getInstance();
        TreeSet<String> codes = new TreeSet<>();
        for (Object o : m.getCodesSet()) codes.add(o.toString());
        for (String c : codes) {
            System.out.print(c + "\t" + m.getCanonicalCode(c));
            List vs = m.getValuesFor(c); if (vs != null) for (Object v : vs) System.out.print("\t" + v);
            System.out.println();
        }
    }
}
