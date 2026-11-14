package openglyph.ui;

import java.awt.Component;
import java.io.BufferedReader;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileReader;
import java.io.IOException;
import java.io.Reader;
import java.io.StringReader;
import java.util.ArrayList;
import java.util.Locale;
import java.util.ResourceBundle;

import javax.swing.JLabel;
import javax.swing.JOptionPane;
import javax.swing.JPanel;

import jsesh.mdc.utils.MDCSyntaxError;
import jsesh.mdcDisplayer.swing.editor.HieroglyphicTextModel;
import jsesh.mdcDisplayer.swing.editor.JMDCEditor;
import openglyph.dictionary.DictionaryDataBase;

public class OpenGlyphEditor extends JPanel {
    
    private ArrayList _resultadosDiccionario = null;
    private DictionaryDataBase _myDB = null;
    
    //  idioma seleccionado
    private ResourceBundle  _captions   = null;
    private Locale          _local      = null;
    
    private JLabel _numRegistros;

    HieroglyphicTextModel _modelo;
    
    JMDCEditor _panDicResul;
    
    public void searchTransliteracion( String transliteration, int _tipoBusqueda, ResourceBundle _res ) throws Exception {
        _resultadosDiccionario = _myDB.doSearchTransliteration( transliteration, _tipoBusqueda );           
        actualizaPanelDiccionario(_res);
        _resultadosDiccionario = null;
    }
    
    public void searchEnglish( String english, int _tipoBusqueda, ResourceBundle _res ) throws Exception {
        _resultadosDiccionario = _myDB.doSearchEnglish( english, _tipoBusqueda );          
        actualizaPanelDiccionario(_res);
        _resultadosDiccionario = null;
    }
    
    public void searchGardiner( String gardiner, int _tipoBusqueda, ResourceBundle _res ) throws Exception {
        _resultadosDiccionario = _myDB.doSearchGardiner( gardiner, _tipoBusqueda );         
        actualizaPanelDiccionario(_res);
        _resultadosDiccionario = null;
    }
    
    private void actualizaPanelDiccionario(ResourceBundle _res) throws Exception {
        _captions = _res;
        if (_panDicResul == null){
            _modelo = new HieroglyphicTextModel();
        } else {
            _modelo = _panDicResul.getWorkflow().getHieroglyphicTextModel();
        }
        
        int _num = _resultadosDiccionario.size();
        
        if (_panDicResul!=null && _panDicResul.getWorkflow() != null){
            _panDicResul.getWorkflow().setMDCCode("");
        }
        
        String _cadSalida = "";
        
        String _cadResultado = _captions.getString("Resultado.1");
        
        int _iPosiResul = _cadResultado.indexOf("%");
        
        if (_iPosiResul!=-1){
            _cadResultado = _captions.getString("Resultado.1").substring(0,(_iPosiResul)) + Integer.toString(_num) + _captions.getString("Resultado.1").substring(_iPosiResul+1); 
        }
        
        _numRegistros.setText( _cadResultado );
        
        _cadResultado = null;
        
        for ( int i = 0 ; i < _num ; i++ ) {
            _cadSalida += (String)(_resultadosDiccionario.get(i));
        }
        
        // actualizamos el panel
        _modelo.readModel(new StringReader(_cadSalida));
        
        _cadSalida = null;
    }
    

    
    public OpenGlyphEditor(Component parent, String paneltitle, String transliteration, String gardiner, DictionaryDataBase myDB, ResourceBundle  captions, JLabel numRegistros) {
        super();
        
        _myDB = myDB;
        _captions = captions;
        _numRegistros = numRegistros;
        
        _panDicResul = null;
        
        
        try {
            //_myDB = new DictionaryDataBase();
            searchTransliteracion( transliteration, DictionaryDataBase._BEGIN, _captions );
            _panDicResul = new JMDCEditor( _modelo );
        } catch (Exception e){
            e.printStackTrace( System.out );
            JOptionPane.showMessageDialog(null, e.toString(), _captions.getString("Error.Busqueda"),
                    JOptionPane.ERROR_MESSAGE);
        }           

        add(_panDicResul);
        
    }

    public void loadFile(String _fichero, ResourceBundle _res) throws Exception {
        /*System.out.println("1");
        _resultadosDiccionario = new ArrayList();
        System.out.println("2");
        FileReader _reader = new FileReader( new File( _fichero ) );
        System.out.println("3");
        BufferedReader _buf = new BufferedReader(_reader);
        System.out.println("4");
        String _linea = null;
        System.out.println("5");
        while ( ( _linea = _buf.readLine() ) != null ) {
            System.out.println(_linea);
            _resultadosDiccionario.add( _linea );
        }
        System.out.println("6");
        _reader.close();
        System.out.println("7");
        _buf.close();
        System.out.println("8");
        actualizaPanelDiccionario(_res);
        System.out.println("9");
        _resultadosDiccionario = null;
        System.out.println("10");*/
        StringBuffer buffer = new StringBuffer();
        try {
            // We read the file in memory,
            // because we need to suppress non manuel de codage headers, if
            // any.
            File file = new File(_fichero);
            
            if (file.isDirectory()) return;
            
            Reader r = new FileReader(file);
            int c;
            while ((c = r.read()) != -1) {
                buffer.append((char) c);
            }
            r.close();

            if (buffer.length() != 0 && buffer.charAt(0) == '@') {
                int first = buffer.indexOf("\n");
                if (first == -1)
                    first = buffer.length();
                buffer.replace(0, first, "");
            }
            // handle the difference between winglyph and tksesh about
            // philology.
            _panDicResul.getWorkflow().getHieroglyphicTextModel().setPhilologyIsSign(!file.getName().endsWith(".hie"));

            _modelo.readModel(new StringReader(buffer.toString()));
            //currentFile = file;
        } catch (MDCSyntaxError e) {
            String msg = "error at line " + e.getLine();
            int infPos, supPos;
            if ((e.getCharPos() - 10) > 0)
                infPos = (e.getCharPos() - 10);
            else
                infPos = 0;
            if (e.getCharPos() + 10 > buffer.length())
                supPos = buffer.length();
            else
                supPos = e.getCharPos() + 10;
            msg += ". Wrong text : " + buffer.substring(infPos, supPos);
            JOptionPane.showMessageDialog(null, msg, "Syntax Error",
                    JOptionPane.ERROR_MESSAGE);
            e.getCharPos();
            e.printStackTrace();
        } catch (Exception e) {
            //e.printStackTrace();
            JOptionPane.showMessageDialog(null, e.toString(), "Syntax Error",
                    JOptionPane.ERROR_MESSAGE);
        }
    }
    
    /* (non-Javadoc)
     * @see java.awt.event.ActionListener#actionPerformed(java.awt.event.ActionEvent)
     */
    /*public void actionPerformed(ActionEvent e) {
    }*/


}
