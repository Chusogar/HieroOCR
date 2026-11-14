package openglyph.ui;

import java.awt.BorderLayout;
import java.awt.Color;
import java.awt.Container;
import java.awt.Dimension;
import java.io.File;
import java.util.Collections;
import java.util.ResourceBundle;
import java.util.Vector;

import javax.swing.BoxLayout;
import javax.swing.JFrame;
import javax.swing.JPanel;
import javax.swing.JScrollPane;
import javax.swing.JTree;
import javax.swing.event.TreeSelectionEvent;
import javax.swing.event.TreeSelectionListener;
import javax.swing.tree.DefaultMutableTreeNode;
import javax.swing.tree.TreeNode;

public class OpenGlyphTree extends JPanel 
{ 
    // Panel donde se cargan los textos
    private OpenGlyphEditor _panelTextos = null;
    private String _rutaBase = "";
    private ResourceBundle _captions = null;
    
    /** Construct a FileTree */
    public OpenGlyphTree(File dir, OpenGlyphEditor panelTextos, ResourceBundle _res) {
        
        _panelTextos = panelTextos;
        _rutaBase = dir.getAbsolutePath();
        _captions = _res;
        
        setLayout(new BorderLayout());

        // Make a tree list with all the nodes, and make it a JTree
        JTree tree = new JTree(addNodes(null, dir));

        // Add a listener
        tree.addTreeSelectionListener(new TreeSelectionListener() {
            public void valueChanged(TreeSelectionEvent e) {
                DefaultMutableTreeNode node = (DefaultMutableTreeNode)e.getPath().getLastPathComponent();
                TreeNode[] _rutas = node.getPath();
                String _cadenaRuta = "";
                int _numNiveles = _rutas.length;
                
                for ( int i=0 ; i<_numNiveles ; i++) {
                    _cadenaRuta += "/" + _rutas[i].toString();
                }
                //System.out.println("Fichero seleccionado " + _cadenaRuta);
                
                /*_panEd = new OpenGlyphEditor(parent, title, transliteration, gardiner, _myDB, _captions, _numRegistros);
                _panEd.setSize(400,300);
                JScrollPane _scroll = new JScrollPane( _panEd );*/
                
                try {
                    //System.out.println("Cargamos "+"." + _cadenaRuta + " " + (_panelTextos==null));
                    _panelTextos.loadFile( "." + _cadenaRuta, _captions );
                } catch (Exception ex) {
                    System.out.println("Error al cargar fichero");
                    ex.printStackTrace(System.out);
                }
            }
        });

        // Lastly, put the JTree into a JScrollPane.
        JScrollPane scrollpane = new JScrollPane();
        scrollpane.getViewport().add(tree);
        add(BorderLayout.CENTER, scrollpane);
    }

    /** Add nodes from under "dir" into curTop. Highly recursive. */
    DefaultMutableTreeNode addNodes(DefaultMutableTreeNode curTop, File dir) {

        String curPath = dir.getName();
        DefaultMutableTreeNode curDir = new DefaultMutableTreeNode(curPath);
        if (curTop != null) {   // should only be null at root
            curTop.add(curDir);
        }

        File f;
        File[] _ficheros = dir.listFiles();
        int _numFicheros = _ficheros.length;

        for (int i=0; i<_numFicheros; i++) {

            f = _ficheros[i];
            if (f.isDirectory()){
                addNodes(curDir,  f);
            }else{
                curDir.add(new DefaultMutableTreeNode(f.getName()));
            }
        }

        return curDir;
    }

    public Dimension getMinimumSize() {
        return new Dimension(200, 400);
    }
    

    public Dimension getPreferredSize() {
        return new Dimension(200, 400);
    }

    /** Main: make a Frame, add a FileTree */
    public static void main(String[] av) {

        JFrame frame = new JFrame("FileTree");
        frame.setForeground(Color.black);
        frame.setBackground(Color.lightGray);
        Container cp = frame.getContentPane();

        /*if (av.length == 0) {
            cp.add(new OpenGlyphTree(new File("."), null));
        } else {
            cp.setLayout(new BoxLayout(cp, BoxLayout.X_AXIS));
            for (int i=0; i<av.length; i++)
                cp.add(new OpenGlyphTree(new File(av[i])));
        }*/

        frame.pack();
        frame.setVisible(true);
        frame.setDefaultCloseOperation(JFrame.EXIT_ON_CLOSE);
    }
}
