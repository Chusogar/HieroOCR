package openglyph.dictionary;

import java.awt.BorderLayout;
import java.awt.Color;
import java.awt.Component;
import java.awt.Dimension;
import java.awt.GridBagLayout;
import java.awt.GridLayout;
import java.awt.event.ActionEvent;
import java.awt.event.ActionListener;
import java.awt.event.KeyEvent;
import java.awt.event.WindowAdapter;
import java.awt.event.WindowEvent;
import java.io.File;
import java.io.StringReader;
import java.text.DateFormat;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.Locale;
import java.util.ResourceBundle;

import javax.swing.BorderFactory;
import javax.swing.ButtonGroup;
import javax.swing.JButton;
import javax.swing.JFrame;
import javax.swing.JLabel;
import javax.swing.JMenu;
import javax.swing.JMenuBar;
import javax.swing.JMenuItem;
import javax.swing.JOptionPane;
import javax.swing.JPanel;
import javax.swing.JPopupMenu;
import javax.swing.JRadioButton;
import javax.swing.JScrollPane;
import javax.swing.JSplitPane;
import javax.swing.JTextField;
import javax.swing.JTree;
import javax.swing.UIManager;
import javax.swing.tree.DefaultMutableTreeNode;

import openglyph.splash.SplashScreen;
import openglyph.ui.OpenGlyphTree;
import openglyph.ui.OpenGlyphEditor;

import jsesh.mdcDisplayer.swing.application.MDCIconAction;
import jsesh.mdcDisplayer.swing.editor.HieroglyphicTextModel;
import jsesh.mdcDisplayer.swing.editor.JMDCEditor;
import jsesh.mdcDisplayer.swing.hieroglyphicMenu.HieroglyphicMenu;
import jsesh.mdcDisplayer.swing.hieroglyphicMenu.HieroglyphicMenuListener;

/**
 * @author jagsanchez
 *
 * TODO To change the template for this generated type comment go to
 * Window - Preferences - Java - Code Style - Code Templates
 */
public class DictionaryPanel extends JFrame implements ActionListener{
    
    /**
	 * 
	 */
	private static final long serialVersionUID = 275968290539679276L;
	
    // idioma seleccionado
    private ResourceBundle  _captions   = null;
    private Locale          _local      = null;
    
    // separador para los paneles
    private JSplitPane  _jSeparador     = null;
    private JPanel      _panOpenGlyph   = null;
    private JPanel      _panDiccionario   = null;
    private JSplitPane  _jSeparadorTextos     = null;
    
    private JPanel      _pan1   = null;
    
    private JMenu _dictionary;
    private JMenu hieroglyphs;
    private JMenu _idiomas;
    
    // tipo de búsqueda
    private boolean _buscoTransliteracion = true;
    private boolean _buscoGardiner = false;
    private boolean _buscoEnglish = false;
    
    // tipo de cadena a buscar
    private boolean _buscoBegin = true;
    private boolean _buscoEvery = false;
    private boolean _buscoEnd = false;
    private boolean _buscoExacto = false;
    
    // RadioButtons
    JRadioButton _rTranslit = null;
    JRadioButton _rGardiner = null;
    JRadioButton _rEnglish  = null;
    
    JRadioButton _rInicio = null;
    JRadioButton _rMedio = null;
    JRadioButton _rFinal = null;
    JRadioButton _rExacto = null;
    
    private OpenGlyphEditor _panEd = null;
    private OpenGlyphEditor _panText = null;
    
    private JPanel _panOptions = null;
    private JTextField _cajaTranslit;
    
    private JLabel _numRegistros = new JLabel();
    private JPanel _statePan;
    
    private JButton _botonTransliteracion;
    
    private JMenuItem _menuSpanish = null;
    private JMenuItem _menuEnglish = null;
    
    private static class SplashCloser extends WindowAdapter
    {

        public void windowOpened(WindowEvent e)
        {
            splash.setVisible(false);
            splash.dispose();
        }

        private SplashScreen splash;

        public SplashCloser(SplashScreen splash)
        {
            this.splash = splash;
        }
    }
    
    class DictionaryAction extends MDCIconAction {
		/**
		 * 
		 */
		private static final long serialVersionUID = 3084392352183198626L;
		private String tTranslit;

		/**
		 * @param mdcText
		 */
		public DictionaryAction(String mdcText, String trCode) {
			super(mdcText);
			this.tTranslit=trCode;
		}
		
		public String getTranslit(){
		    return tTranslit;
		}
		
		
		/*
		 * (non-Javadoc)
		 * 
		 * @see java.awt.event.ActionListener#actionPerformed(java.awt.event.ActionEvent)
		 */
		public void actionPerformed(ActionEvent e) {
		    
		    try {
		        setTransliteration(tTranslit);
		        _panEd.searchTransliteracion(tTranslit, DictionaryDataBase._BEGIN, _captions);
		        
		    } catch (Exception ex){
		        ex.printStackTrace(System.out);
		    }
		}

	}

    
    /**
     * 
     */
    
    private DictionaryDataBase _myDB = null;
    
    private DictionaryAction[] dictionaryActions;
    
	private String transliteration;
	private String gardiner;
	
	private JMenuBar _menu = new JMenuBar();

	
	public DictionaryPanel() {
	    super();
	    
		setDefaults();
		this.setTitle(_captions.getString("Titulo"));
	}

	public void setDefaults() {
        
        _myDB = new DictionaryDataBase();
        
        // idioma por defecto
        _local = Locale.getDefault();
        _captions = ResourceBundle.getBundle("languages/Messages", _local);
        Locale list[] = DateFormat.getAvailableLocales();

        /*for (int i = 0; i < list.length; i++) {
            System.out.println
               (list[i].getLanguage() + " " + list[i].getCountry());
        }

        System.out.println("Idioma seleccionado: " + _local.getLanguage()+"_"+_local.getCountry());
        */
        
        _menu = new JMenuBar();

        buildDictionaryActions();
        buildDictionaryMenu();
        buildHieroglyphicMenus();
        buildLanguagesMenus();
        this.setJMenuBar( _menu );
        getContentPane().setLayout(new BorderLayout());
        
	    addWindowListener(new WindowAdapter() {
			
			/* (non-Javadoc)
			 * @see java.awt.event.WindowAdapter#windowClosed(java.awt.event.WindowEvent)
			 */
			public void windowClosing(WindowEvent e) {
			    _myDB.close();
			    System.exit(0);
			}
		
		});
	}
	
	private void regeneraPanel() {
        
        // título de la ventana
        this.setTitle(_captions.getString("Titulo"));
        
        // cambiamos el literal del botón de búsqueda 
	    _botonTransliteracion.setText(_captions.getString("Boton.busqueda"));
        
        // cambiamos los literales de las opciones
        _rInicio.setText(_captions.getString("Opcion.1"));
        _rMedio.setText(_captions.getString("Opcion.2"));
        _rFinal.setText(_captions.getString("Opcion.3"));
        _rExacto.setText(_captions.getString("Opcion.4"));
        
        // cambiamos los literales de las criterios
        _rEnglish.setText(_captions.getString("Criterio.1"));
        _rGardiner.setText(_captions.getString("Criterio.2"));
        _rTranslit.setText(_captions.getString("Criterio.3"));

        // literales de los menús
        _menu.remove(_dictionary);
        _menu.remove(hieroglyphs);
        _menu.remove(_idiomas);
        
        // regeneramos los literales del menú de jeroglíficos
        buildDictionaryMenu();
        buildHieroglyphicMenus();
        buildLanguagesMenus();
        
        this.setJMenuBar(_menu);
    }

    private void buildLanguagesMenus() {
        _idiomas = new JMenu(_captions.getString("Menu.3"));
        _menuSpanish = new JMenuItem("Español");
        _menuSpanish.addActionListener(this);
        _menuEnglish = new JMenuItem("English");
        _menuEnglish.addActionListener(this);
        
        _idiomas.add(_menuSpanish);
        _idiomas.add(_menuEnglish);
        
        _menu.add(_idiomas);
        
    }

    private void buildDictionaryMenu() {
		_dictionary = new JMenu(_captions.getString("Menu.1"));
		_dictionary.setMnemonic(KeyEvent.VK_D);
		JPopupMenu pm = _dictionary.getPopupMenu();
		pm.setLayout(new GridLayout(0, 5));
		
		int _numClaves = dictionaryActions.length;
		
		for (int i= 0; i < _numClaves ; i++) {			
			_dictionary.add(dictionaryActions[i]);
		}
		_menu.add(_dictionary);
	}
	
	private void buildHieroglyphicMenus() {
		String families[]= {"A", "B", "C", "D", "E", "F", "G", "H", "I", "K", "L", "M", "N", "O", "P", "Q", "R", "S", "T", "U",
				"V","W","X","Y","Z","Aa", "Ff"};
		String titles[]= {
                _captions.getString("Jero.A"),
                _captions.getString("Jero.B"),
                _captions.getString("Jero.C"),
                _captions.getString("Jero.D"),
                _captions.getString("Jero.E"),
                _captions.getString("Jero.F"),
                _captions.getString("Jero.G"),
                _captions.getString("Jero.H"),
                _captions.getString("Jero.I"),
                _captions.getString("Jero.K"),
                _captions.getString("Jero.L"),
                _captions.getString("Jero.M"),
                _captions.getString("Jero.N"),
                _captions.getString("Jero.O"),
                _captions.getString("Jero.P"),
                _captions.getString("Jero.Q"),
                _captions.getString("Jero.R"),
                _captions.getString("Jero.S"),
                _captions.getString("Jero.T"),
                _captions.getString("Jero.U"),
                _captions.getString("Jero.V"),
                _captions.getString("Jero.W"),
                _captions.getString("Jero.X"),
                _captions.getString("Jero.Y"),
                _captions.getString("Jero.Z"),
                _captions.getString("Jero.Aa"),	
                _captions.getString("Jero.Ff"),
		};
		
		hieroglyphs= new JMenu(_captions.getString("Menu.2"));
		for (int i=0; i< families.length; i++) {
			HieroglyphicMenu fmenu = new HieroglyphicMenu(titles[i], families[i], 6);
            fmenu.setHieroglyphicMenuListener(new HieroglyphicMenuMediator());
            if (i < 25)
                fmenu.setMnemonic(families[i].toUpperCase().charAt(0));
            else if (i == 25) {
                fmenu.setMnemonic(KeyEvent.VK_J);
            } else if (i == 26)
                fmenu.setMnemonic(KeyEvent.VK_AMPERSAND);
            hieroglyphs.add(fmenu);
		}
		hieroglyphs.setMnemonic(KeyEvent.VK_H);
		_menu.add(hieroglyphs);
		
	}
	
	private class HieroglyphicMenuMediator implements HieroglyphicMenuListener {

		/* (non-Javadoc)
		 * @see jsesh.mdcDisplayer.swing.hieroglyphicMenu.HieroglyphicMenuListener#codeSelected(java.lang.String)
		 */
		public void codeSelected(String code) {
			
			ArrayList _arr;
			String _translitSign = "";
			String _translateSign = "";
            String _gardiner = "";
			
			try {
			    _arr = _myDB.doSearchSign( code );
			    if ( _arr !=null && _arr.size()>0 ) {
			        HashMap _hm = (HashMap) _arr.get(0);
			        _translitSign = (String) _hm.get("TRANSLITERATION");
			        _translateSign = (String) _hm.get("TRANSLATION");
                    _gardiner = (String) _hm.get("GARDINER");
			        _hm = null;
			    }
			} catch (Exception e) {
			    
			    e.printStackTrace(System.out);
			    JOptionPane.showMessageDialog(null, e.toString(), _captions.getString("Error.Signo"),
                        JOptionPane.ERROR_MESSAGE);
			}
						
			if ( _cajaTranslit.getText().length() > 0 ){
                if (_rTranslit.isSelected())
                    _cajaTranslit.setText( _cajaTranslit.getText() + " " + _translitSign );
                else if (_rGardiner.isSelected())
                    _cajaTranslit.setText( _cajaTranslit.getText() + "-" + _gardiner );
                else
                    _cajaTranslit.setText( _cajaTranslit.getText() + " " + _translateSign );
            }else{
                if (_rTranslit.isSelected())
                    _cajaTranslit.setText( _translitSign );
                else if (_rGardiner.isSelected())
                    _cajaTranslit.setText( _gardiner );
                else
                    _cajaTranslit.setText( _translateSign );
            }
		}

		/* (non-Javadoc)
		 * @see jsesh.mdcDisplayer.swing.hieroglyphicMenu.HieroglyphicMenuListener#enter(java.lang.String)
		 */
		public void enter(String code) {
			// TODO : display information about the sign, instead of just its code.
			//workflow.setDisplayedCode(code);
		    //System.out.println("Enter: "+code);
		}

		/* (non-Javadoc)
		 * @see jsesh.mdcDisplayer.swing.hieroglyphicMenu.HieroglyphicMenuListener#exit(java.lang.String)
		 */
		public void exit(String code) {
			//workflow.setDisplayedCode("");
		    //System.out.println("Exit: "+code);
		}

	}
	
	/* (non-Javadoc)
	 * @see java.awt.event.ActionListener#actionPerformed(java.awt.event.ActionEvent)
	 */
	public void actionPerformed(ActionEvent e) {
        
		if (e.getSource() == _botonTransliteracion) {
            
            int _tipoBusqueda = 0;
            
            // inicializamos la búsqueda
            _buscoBegin             = false;
            _buscoEnd               = false;
            _buscoEnglish           = false;
            _buscoEvery             = false;
            _buscoGardiner          = false;
            _buscoTransliteracion   = false;
            _buscoExacto            = false;
            
            // definimos tipo de búsqueda
            if (_rInicio.isSelected()){
                _buscoBegin = true;
            } else if (_rMedio.isSelected()){
                _buscoEvery = true;
            } else if (_rFinal.isSelected()){
                _buscoEnd = true;
            } else if (_rExacto.isSelected()){
                _buscoExacto = true;
            }
            
            if (_rTranslit.isSelected()){
                _buscoTransliteracion = true;
            } else if (_rGardiner.isSelected()){
                _buscoGardiner = true;
            } else if (_rEnglish.isSelected()){
                _buscoEnglish = true;
            }
            
            if (_buscoBegin) _tipoBusqueda = DictionaryDataBase._BEGIN;
            else if (_buscoEvery) _tipoBusqueda = DictionaryDataBase._EVERYWHERE;
            else if (_buscoEnd) _tipoBusqueda = DictionaryDataBase._END;
            else if (_buscoExacto) _tipoBusqueda = DictionaryDataBase._EXACT;
            
		    try {
                if ( _buscoTransliteracion ) {
                    _panEd.searchTransliteracion( _cajaTranslit.getText(), _tipoBusqueda, _captions );
                } else if ( _buscoGardiner ) {
                    _panEd.searchGardiner( _cajaTranslit.getText(), _tipoBusqueda, _captions );
                } else if ( _buscoEnglish ) {
                    _panEd.searchEnglish( _cajaTranslit.getText(), _tipoBusqueda, _captions );
                }
		    } catch ( Exception eT ) {
		        eT.printStackTrace(System.out);
		        JOptionPane.showMessageDialog(this, eT.toString(), _captions.getString("Error.Diccionario"),
                        JOptionPane.ERROR_MESSAGE);
		    }
		} else if (e.getSource() == _menuSpanish) {
            //System.out.println("Idioma Seleccionado Español");
            _local = new Locale("es", "ES");
            _captions = ResourceBundle.getBundle("languages/Messages", _local);
            regeneraPanel();
        } else if (e.getSource() == _menuEnglish) {
            //System.out.println("Idioma Seleccionado Inglés");
            _local = new Locale("en", "US");
            _captions = ResourceBundle.getBundle("languages/Messages", _local);
            regeneraPanel();
        }
	}

	
	public void generateSearchCriteriaPanel(){
	    _panOptions = new JPanel(new BorderLayout());
        
	    JPanel _panOptionsA = new JPanel(new BorderLayout());
	    _panOptionsA.setBorder(BorderFactory.createLineBorder(Color.BLACK));
		_cajaTranslit = new JTextField("");
        		
		_botonTransliteracion = new JButton(_captions.getString("Boton.busqueda"));
		_botonTransliteracion.addActionListener(this);
		
		_panOptionsA.add( _cajaTranslit, BorderLayout.CENTER );
		_panOptionsA.add( _botonTransliteracion, BorderLayout.EAST );
		
		// panel B
		JPanel _panOptionsB = new JPanel();
		_panOptionsB.setLayout(new GridLayout(1, 0));
		_panOptionsB.setBorder(BorderFactory.createLineBorder(Color.BLACK));
		_rInicio = new JRadioButton(_captions.getString("Opcion.1"));
        _rInicio.setSelected(true);
		_rMedio = new JRadioButton(_captions.getString("Opcion.2"));
		_rFinal = new JRadioButton(_captions.getString("Opcion.3"));
        _rExacto = new JRadioButton(_captions.getString("Opcion.4"));

		ButtonGroup _grupo = new ButtonGroup();
		_grupo.add( _rInicio );
		_grupo.add( _rMedio );
		_grupo.add( _rFinal );
        _grupo.add( _rExacto );
		
		_panOptionsB.add( _rInicio );
		_panOptionsB.add( _rMedio );
		_panOptionsB.add( _rFinal );
        _panOptionsB.add( _rExacto );
				
		// panel C
		JPanel _panOptionsC = new JPanel();
		_panOptionsC.setLayout(new GridBagLayout());
		_panOptionsC.setBorder(BorderFactory.createLineBorder(Color.BLACK));
		
		_rTranslit = new JRadioButton(_captions.getString("Criterio.1"));
        _rTranslit.setSelected(true);
		_rGardiner = new JRadioButton(_captions.getString("Criterio.2"));
		_rEnglish = new JRadioButton(_captions.getString("Criterio.3"));
        
		ButtonGroup _grupo2 = new ButtonGroup();
		_grupo2.add( _rTranslit);
		_grupo2.add( _rGardiner );
		_grupo2.add( _rEnglish );
		
		_panOptionsC.add( _rTranslit );
		_panOptionsC.add( _rGardiner );
		_panOptionsC.add( _rEnglish );
		
		// inserto en el panel de criterios
		_panOptions.add( _panOptionsA, BorderLayout.NORTH);
		_panOptions.add( _panOptionsC, BorderLayout.EAST);
		_panOptions.add( _panOptionsB, BorderLayout.WEST);
        
        _panOpenGlyph = new JPanel(new BorderLayout());
        _panDiccionario = new JPanel(new BorderLayout());
        
        // creamos el separador entre el árbol y el diccionario
        _jSeparador = new JSplitPane(JSplitPane.HORIZONTAL_SPLIT);
        _jSeparador.setRightComponent(_panDiccionario);
        _jSeparador.setLeftComponent(_panOpenGlyph);
        _jSeparador.setOneTouchExpandable(true);

        //Provide minimum sizes for the two components in the split pane
        Dimension minimumSize = new Dimension(200, 150);
        _panDiccionario.setMinimumSize(minimumSize);
        _panOpenGlyph.setMinimumSize(minimumSize);
        
        //Set the initial location and size of the divider
        _jSeparador.setDividerLocation(200);
        _jSeparador.setDividerSize(10);
        _jSeparador.setPreferredSize(new Dimension(800, 520));
        
        // AQUIIIIIIIIIIIIIIIIIIIIII
//      creamos el separador entre el árbol y el diccionario
        _pan1 = new JPanel(new BorderLayout());
        _jSeparadorTextos = new JSplitPane(JSplitPane.VERTICAL_SPLIT);
        _jSeparadorTextos.setRightComponent(_pan1);
        _jSeparadorTextos.setLeftComponent(_jSeparador);
        _jSeparadorTextos.setOneTouchExpandable(true);

        // PANELES
        _pan1.setMinimumSize(minimumSize);
        
        //Set the initial location and size of the divider
        _jSeparadorTextos.setDividerLocation(200);
        _jSeparadorTextos.setDividerSize(10);
        _jSeparadorTextos.setPreferredSize(new Dimension(800, 520));


        
        //getContentPane().add( _panOptions, BorderLayout.NORTH );
        _panDiccionario.add( _panOptions, BorderLayout.NORTH );
        //getContentPane().add( _jSeparador, BorderLayout.NORTH );
        getContentPane().add( _jSeparadorTextos, BorderLayout.CENTER);
		
	}

    /**
	 * gets a panel suitable for option editing.
	 * @return a panel
	 */
	public void generateOptionPanel(Component parent, String title) {
        _panEd = new OpenGlyphEditor(parent, title, transliteration, gardiner, _myDB, _captions, _numRegistros);
	    _panEd.setSize(400,300);
	    JScrollPane _scroll = new JScrollPane( _panEd );
        _panDiccionario.add( _scroll, BorderLayout.CENTER );
        
        _panText = new OpenGlyphEditor(parent, title, transliteration, gardiner, _myDB, _captions, _numRegistros);
        _panText.setSize(400,300);
        JScrollPane _scrollTextos = new JScrollPane( _panText );
        _pan1.add( _scrollTextos, BorderLayout.CENTER );
        
//      creamos el árbol con los directorios
        _panOpenGlyph.add(new OpenGlyphTree(new File("./textos"), _panText, _captions), BorderLayout.CENTER);
	}
	
	public void generateStatePanel() {
	    _statePan = new JPanel();
	    _numRegistros.setText("");
	    _statePan.add( _numRegistros );

        getContentPane().add( _statePan, BorderLayout.SOUTH );
	}

	private void buildDictionaryActions() {
		dictionaryActions = new DictionaryAction[23];
		int i = 0;
		dictionaryActions[i++] = new DictionaryAction("G1+tA","A");
		//System.out.println("--------------"+dictionaryActions[0].getKeys());
		dictionaryActions[i++] = new DictionaryAction("M17+ti","i");
		dictionaryActions[i++] = new DictionaryAction("D36+ta","a");
		dictionaryActions[i++] = new DictionaryAction("G43-V1+tw","w");
		dictionaryActions[i++] = new DictionaryAction("D58+tb","b");
		dictionaryActions[i++] = new DictionaryAction("Q3+tp","p");
		dictionaryActions[i++] = new DictionaryAction("I9+tf","f");
		dictionaryActions[i++] = new DictionaryAction("G17+tm","m");
		dictionaryActions[i++] = new DictionaryAction("N35+tn","n");
		dictionaryActions[i++] = new DictionaryAction("D21+tr","r");						
		dictionaryActions[i++] = new DictionaryAction("O4+th","h");
		dictionaryActions[i++] = new DictionaryAction("V28+tH","H");
		dictionaryActions[i++] = new DictionaryAction("Aa1+tx","x");
		dictionaryActions[i++] = new DictionaryAction("O29+tX","X");
		dictionaryActions[i++] = new DictionaryAction("S29-O34+ts","s");						
		dictionaryActions[i++] = new DictionaryAction("O39+tS","S");
		dictionaryActions[i++] = new DictionaryAction("X7+tq","q");
		dictionaryActions[i++] = new DictionaryAction("V31+tk","k");
		dictionaryActions[i++] = new DictionaryAction("W11+tg","g");
		dictionaryActions[i++] = new DictionaryAction("X1+tt","t");						
		dictionaryActions[i++] = new DictionaryAction("V13+tT","T");
		dictionaryActions[i++] = new DictionaryAction("D46+td","d");
		dictionaryActions[i++] = new DictionaryAction("I10+tD","D");								
											
	}

	
	/**
	 * @param string
	 */
	public void setTransliteration(String string) {
		transliteration = string;
	}
	
	/**
	 * @param string
	 */
	public void setGardiner(String string) {
		gardiner = string;
	}

	/*class OptionPanel extends JPanel {
		
		private static final long serialVersionUID = -5196578994058417940L;

			}*/

	public static void main(String[] args){
        
        // splash screen
        SplashScreen splash = new SplashScreen();
        splash.display();
        UIManager.put("Button.defaultButtonFollowsFocus", Boolean.TRUE);

	    DictionaryPanel dictionaryPanel = new DictionaryPanel();
	    dictionaryPanel.setTransliteration( "Htp" );
        dictionaryPanel.setGardiner( null );
        dictionaryPanel.generateSearchCriteriaPanel();
        dictionaryPanel.generateOptionPanel(null, "Diccionario");
        dictionaryPanel.generateStatePanel();
        dictionaryPanel.setSize(800, 600);
        dictionaryPanel.show();
        
        dictionaryPanel.addWindowListener(new SplashCloser(splash));
        /*StartMe run = new StartMe(dictionaryPanel.frame);
        SwingUtilities.invokeLater(run);*/
        
	}

}
