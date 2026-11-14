package openglyph.dictionary;

import java.sql.Connection;
import java.sql.DriverManager;
import java.sql.PreparedStatement;
import java.sql.ResultSet;

import java.util.ArrayList;
import java.util.HashMap;

import javax.swing.JOptionPane;



/**
 * @author jagsanchez
 *
 * TODO To change the template for this generated type comment go to
 * Window - Preferences - Java - Code Style - Code Templates
 */
public class DictionaryDataBase {
    
    public static final int _EXACT      = 0;
    public static final int _BEGIN      = 1;
    public static final int _EVERYWHERE = 2;
    public static final int _END        = 3;
    
    Connection db = null;

    /**
     * Constructor
     */
    public DictionaryDataBase() {
        super();
    }
    
    public ArrayList doSearchSign( String _sign ) throws Exception {
        return doSearch( null, _sign, null, null, 0 );
    }
    
    public ArrayList doSearchGardiner( String _gardiner, int _tipoBusqueda ) throws Exception {
        return doSearch( null, null, _gardiner, null, _tipoBusqueda );
    }
    
    public ArrayList doSearchTransliteration( String _transliteration, int _tipoBusqueda ) throws Exception {
        return doSearch( _transliteration, null, null, null, _tipoBusqueda );
    }
    
    public ArrayList doSearchEnglish( String _english, int _tipoBusqueda ) throws Exception {
        return doSearch( null, null, null, _english, _tipoBusqueda );
    }
    
    public ArrayList doSearch( String _transliteration, String _sign, String _gardiner, String _english, int _tipoBusqueda ) throws Exception {
        
        ArrayList _result = new ArrayList();
                
        if (db==null){
            Class.forName("org.hsqldb.jdbcDriver");
            db = DriverManager.getConnection("jdbc:hsqldb:file:./openglyph", "sa", "");
        }
        
        // sql para realizar búsquedas por transliteración
        String sql = "select '-' || GARDINER || '+t' || TRANSLITERATION || '+s +l' || TRANSLATION || '+s -! ' from ";
        
        String _cadSearch = "";        
        
        if (_tipoBusqueda != _BEGIN && _tipoBusqueda != _EXACT) {
            _cadSearch = "%";
        }
        
        if ( _transliteration != null) {
            //System.out.println("Buscamos transliteración: "+_transliteration);
            _cadSearch += _transliteration;
            sql += "DICTIONARY where transliteration like ?";
        } else if ( _sign != null ) {
            //System.out.println("Buscamos signos: "+_sign);
            _cadSearch = _sign;
            sql = "select * from SIGNS where gardiner=?";
        } else if ( _gardiner != null ) {
            //System.out.println("Buscamos gardiner: "+_gardiner);
            _cadSearch += _gardiner;
            sql += "DICTIONARY where gardiner like ?";
        } else if ( _english != null ){
            //System.out.println("Buscamos English: "+_english);
            _cadSearch += _english;
            sql += "DICTIONARY where translation like ?";
        }
        
        if (_tipoBusqueda==_BEGIN || _tipoBusqueda==_EVERYWHERE){
            _cadSearch += "%";
        }

		PreparedStatement st = db.prepareStatement(sql);
        
        st.setString(1, _cadSearch);
		
		ResultSet rs = st.executeQuery();
		
		while ( rs.next() ) {
            if (_sign!=null) {
                HashMap _hm = new HashMap();
                _hm.put("GARDINER", rs.getString( "GARDINER" ));
                _hm.put("TRANSLITERATION", rs.getString( "TRANSLITERATION" ));
                _hm.put("TRANSLATION", rs.getString( "TRANSLATION" ));
                _result.add( _hm );
                _hm = null;
            } else {
                _result.add( rs.getString( 1 ) );
            }
		}
		
		st.close();

        return _result;
    }
    
    
    public void close() {
		try {
			db.close();
			db= null;
		} catch (Exception e) {
			// TODO Auto-generated catch block
			e.printStackTrace(System.out);
            JOptionPane.showMessageDialog(null, e.toString(), "Error al cerrar la base de datos",
                    JOptionPane.ERROR_MESSAGE);
		}
	}
	
	/* (non-Javadoc)
	 * @see java.lang.Object#finalize()
	 */
	protected void finalize() throws Throwable {
		if (db != null)
		{
			db.close();
			db= null;
		}
	}

}
