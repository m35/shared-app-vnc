//
//Copyright (C) 2004 UCHINO Satoshi.  All Rights Reserved.
//
//This is free software; you can redistribute it and/or modify
//it under the terms of the GNU General Public License as published by
//the Free Software Foundation; either version 2 of the License, or
//(at your option) any later version.
//
//This software is distributed in the hope that it will be useful,
//but WITHOUT ANY WARRANTY; without even the implied warranty of
//MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//GNU General Public License for more details.
//
//You should have received a copy of the GNU General Public License
//along with this software; if not, write to the Free Software
//Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307,
//USA.

import java.awt.*;
import java.beans.*;
import javax.swing.*;

public class PasswdDialog extends JDialog implements PropertyChangeListener {
  static private String defaultMessage = "Enter Password.";

  private String password;

  private JPasswordField passwdField;
  private JOptionPane optionPane;

  public PasswdDialog() {
    super((Frame)null, "Enter Password", true);
    password = null;
    passwdField = new JPasswordField(20);

    //Create the JOptionPane.
    optionPane = new JOptionPane(null,
				 JOptionPane.QUESTION_MESSAGE,
				 JOptionPane.OK_CANCEL_OPTION);

    setMessage(defaultMessage);

    //Make this dialog display it.
    setContentPane(optionPane);

    pack();

    //Register an event handler that reacts to option pane state changes.
    optionPane.addPropertyChangeListener(this);
  }

  void setMessage(String message) {
    //Create an array of the text and components to be displayed.
    Object[] array = {message, passwdField};
    optionPane.setMessage(array);
  }
  
  String getPasswd() {
    return password;
  }

  /** This method reacts to state changes in the option pane. */
  public void propertyChange(PropertyChangeEvent e) {
    String prop = e.getPropertyName();

    if (isVisible()
	&& (e.getSource() == optionPane)
	&& (JOptionPane.VALUE_PROPERTY.equals(prop) ||
	    JOptionPane.INPUT_VALUE_PROPERTY.equals(prop))) {
      Object value = optionPane.getValue();

      if (value == JOptionPane.UNINITIALIZED_VALUE) {
	//ignore reset
	return;
      }

      //Reset the JOptionPane's value.
      //If you don't do this, then if the user
      //presses the same button next time, no
      //property change event will be fired.
      optionPane.setValue(JOptionPane.UNINITIALIZED_VALUE);

      int ivalue = ((Integer)value).intValue();

      if (ivalue == JOptionPane.OK_OPTION) {
        password = new String(passwdField.getPassword());
        clearAndHide();
      } else { //user closed dialog or clicked cancel
        password = null;
        clearAndHide();
      }
    }
  }

  /** This method clears the dialog and hides it. */
  public void clearAndHide() {
    passwdField.setText(null);
    setMessage(defaultMessage);
    setVisible(false);
  }
}
