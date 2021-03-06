//
//  Copyright (C) 2005 Grant Wallace, Princeton University.  All Rights Reserved.
//
//  This is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License as published by
//  the Free Software Foundation; either version 2 of the License, or
//  (at your option) any later version.
//
//  This software is distributed in the hope that it will be useful,
//  but WITHOUT ANY WARRANTY; without even the implied warranty of
//  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//  GNU General Public License for more details.
//
//  You should have received a copy of the GNU General Public License
//  along with this software; if not, write to the Free Software
//  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307,
//  USA.
//

/*
 * javactrl.java
 */

import javax.swing.*;
import java.awt.*;
import javax.swing.border.*;
import java.awt.event.*;
import java.io.*;
import java.util.Properties;

public class javactrl extends JFrame {

  private JCheckBoxMenuItem cbIncludeDialogs;
    
    /** Creates new form javactrl */
    public javactrl() {
        initComponents();
    }
    
    private void initComponents() {

        TitleLabel = new JLabel();
        ShareStateButtonGroup = new ButtonGroup();
        NoSharingButton = new JRadioButton();
        ShareDesktopButton = new JRadioButton();
        ShareModePanel = new JPanel();
        ShareWindowsButton = new JRadioButton();
        AddRemovePanel = new JPanel(new GridBagLayout());
        c = new GridBagConstraints();
        AddButton = new JButton();
        RemoveButton = new JButton();
        RemoveAllButton = new JButton();

        getContentPane().setLayout(new BoxLayout(getContentPane(), BoxLayout.Y_AXIS));

        addWindowListener(new WindowAdapter() {
            public void windowClosing(WindowEvent evt) {
                exitForm(evt);
            }
        });

        setTitle("SharedAppVnc Sharing Controller");

        JMenuBar menuBar = new JMenuBar();
        JMenu menu = new JMenu("Client");
        JMenuItem menuItem = new JMenuItem("Connect...");
        menu.add(menuItem);
        menuItem.addActionListener(new ActionListener() {
            public void actionPerformed(ActionEvent evt) {
                ConnectActionPerformed(evt);
            }
        });
        menuBar.add(menu);

        menu = new JMenu("Options");
        cbIncludeDialogs = new JCheckBoxMenuItem("Include Dialog Windows");
        cbIncludeDialogs.setState(true);
        menu.add(cbIncludeDialogs);
        cbIncludeDialogs.addActionListener(new ActionListener() {
            public void actionPerformed(ActionEvent evt) {
                if (cbIncludeDialogs.getState() == true)
                {
                  execCommand("includeDialogs");
                } else {
                  execCommand("excludeDialogs");
                }
            }
        });
        menuBar.add(menu);

        setJMenuBar(menuBar);

        //TitleLabel.setFont(new Font("MS Sans Serif", 0, 14));
        //TitleLabel.setHorizontalAlignment(SwingConstants.CENTER);
        //TitleLabel.setText("Window Sharing Control Panel");
        //TitleLabel.setAlignmentX(Component.LEFT_ALIGNMENT);
        //getContentPane().add(TitleLabel);

        ShareModePanel.setLayout(new BoxLayout(ShareModePanel, BoxLayout.Y_AXIS));
        ShareModePanel.setBorder(new TitledBorder("Share Mode"));
        ShareModePanel.setMaximumSize(new Dimension(200, 190));
        ShareModePanel.setMinimumSize(new Dimension(200, 190));
        ShareModePanel.setAlignmentX(Component.LEFT_ALIGNMENT);
        ShareModePanel.setName("");
        ShareModePanel.setEnabled(false);

        //NoSharingButton.setSelected(true);
        NoSharingButton.setText("No Sharing");
        NoSharingButton.setAlignmentX(Component.LEFT_ALIGNMENT);
        ShareStateButtonGroup.add(NoSharingButton);
        NoSharingButton.addActionListener(new ActionListener() {
            public void actionPerformed(ActionEvent evt) {
                NoSharingButtonActionPerformed(evt);
            }
        });
        ShareModePanel.add(NoSharingButton);

        ShareDesktopButton.setText("Share Entire Desktop");
        ShareDesktopButton.setAlignmentX(Component.LEFT_ALIGNMENT);
        ShareStateButtonGroup.add(ShareDesktopButton);
        ShareDesktopButton.addActionListener(new ActionListener() {
            public void actionPerformed(ActionEvent evt) {
                ShareDesktopButtonActionPerformed(evt);
            }
        });
        ShareModePanel.add(ShareDesktopButton);

        ShareWindowsButton.setSelected(true);
        ShareWindowsButton.setText("Share Individual Windows");
        ShareWindowsButton.setAlignmentX(Component.LEFT_ALIGNMENT);
        ShareStateButtonGroup.add(ShareWindowsButton);
        ShareWindowsButton.addActionListener(new ActionListener() {
            public void actionPerformed(ActionEvent evt) {
                ShareWindowsButtonActionPerformed(evt);
            }
        });
        ShareModePanel.add(ShareWindowsButton);

        AddRemovePanel.setBorder(new TitledBorder("Add/Remove Shared Windows"));
        //AddRemovePanel.setMaximumSize(new Dimension(190, 90));
        //AddRemovePanel.setMinimumSize(new Dimension(190, 90));
        AddRemovePanel.setAlignmentX(Component.LEFT_ALIGNMENT);
	c.fill = GridBagConstraints.HORIZONTAL;
	c.insets = new Insets(2,5,2,5);

	c.gridx = 0;
	c.gridy = 0;
        AddButton.setText("Share");
        AddButton.setEnabled(true);
        AddButton.addActionListener(new ActionListener() {
            public void actionPerformed(ActionEvent evt) {
                AddButtonActionPerformed(evt);
            }
        });
        AddRemovePanel.add(AddButton, c);

	c.gridx = 1;
	c.gridy = 0;
        RemoveButton.setText("Hide");
        //RemoveButton.setMargin(new Insets(2, 5, 2, 5));
        RemoveButton.setEnabled(true);
        RemoveButton.addActionListener(new ActionListener() {
            public void actionPerformed(ActionEvent evt) {
                RemoveButtonActionPerformed(evt);
            }
        });
        AddRemovePanel.add(RemoveButton, c);

	c.gridx = 0;
	c.gridy = 1;
	c.gridwidth = 2;
        RemoveAllButton.setText("Hide All");
        //RemoveAllButton.setMargin(new Insets(2, 5, 2, 5));
        RemoveAllButton.setEnabled(true);
        RemoveAllButton.addActionListener(new ActionListener() {
            public void actionPerformed(ActionEvent evt) {
                RemoveAllButtonActionPerformed(evt);
            }
        });
        AddRemovePanel.add(RemoveAllButton, c);

        ShareModePanel.add(AddRemovePanel);

        getContentPane().add(ShareModePanel);

        pack();

        execCommand("hideall");
        System.out.println("Connected to server successfully");

        defaultValues = new Properties();
        try
        {
          FileInputStream in = new FileInputStream(defaultsFile);
          defaultValues.load(in);
          in.close();
        } catch (Exception e) {
          if (debug) System.out.println("INFO: User defaults file not found: " + defaultsFile);
        }
    }

    private void execCommand(String cmd) {
      String basecmd = "windowshare ";
      if (passwdFile != null)
      {
        basecmd = basecmd + "-passwd " + passwdFile + " ";
      } else {
        basecmd = basecmd + " ";
      }

      basecmd = basecmd + "-display " + displayName + " -command ";
      String str;

        try {
            if (debug) System.out.println(basecmd + cmd);
            Process p = Runtime.getRuntime().exec(basecmd + cmd);
            //DataInputStream in = new DataInputStream(p.getInputStream());
            //DataInputStream err = new DataInputStream(p.getErrorStream());
            BufferedReader in = new BufferedReader(new InputStreamReader(p.getInputStream()));
            BufferedReader err = new BufferedReader(new InputStreamReader(p.getErrorStream()));
            while ( (str = in.readLine()) != null) {
              System.out.println(str); 
            }
            while ( (str = err.readLine()) != null) {
              if (debug) System.out.println(str); 
            }
            p.waitFor();
            if (p.exitValue() != 0)
	    {
	      System.out.println("**Failed to connect to server**");
	      System.out.println("Verify server is running. Check password file and path. Restart controller with -debug option.");
              usage();
              System.exit(1);
	    }

        } catch (Exception e) {
            System.err.println(e);
            System.exit(1);
        }
    }
    
    private void enableAddRemoveButtons(boolean flag) {
        AddButton.setEnabled(flag);
        RemoveButton.setEnabled(flag);
        RemoveAllButton.setEnabled(flag);
    }
    
    private void ShareWindowsButtonActionPerformed(ActionEvent evt) {
        execCommand("on");
        enableAddRemoveButtons(true);
    }

    private void ShareDesktopButtonActionPerformed(ActionEvent evt) {
        execCommand("disable");
        enableAddRemoveButtons(false);
    }

    private void NoSharingButtonActionPerformed(ActionEvent evt) {
        execCommand("off");
        enableAddRemoveButtons(false);
    }

    private void RemoveButtonActionPerformed(ActionEvent evt) {
        
        execCommand("hide");
    }

    private void RemoveAllButtonActionPerformed(ActionEvent evt) {
        
        execCommand("hideall");
    }

    private void AddButtonActionPerformed(ActionEvent evt) {
        
        execCommand("share");
    }

    private void ConnectActionPerformed(ActionEvent evt) {
      String defaultClient = defaultValues.getProperty("defaultClient");

      String client = JOptionPane.showInputDialog(this, "Share To: <host:port>", defaultClient);
      if (client != null)
      {
        if ((defaultClient == null) || !defaultClient.equals(client))
        {
          defaultValues.put("defaultClient", client);
          try
          {
            //File defDir = new File(defaultsDir);
            File defFile = new File(defaultsFile);
            File defDir = defFile.getParentFile();
            if (!defFile.exists())
            {
              if (defDir != null && !defDir.exists())
              {
                defDir.mkdir();
              }
              defFile.createNewFile();
            }
            FileOutputStream out = new FileOutputStream(defaultsFile);
            defaultValues.store(out, "--Default Values for JavaCtrl--");
            out.close();
          } catch (Exception e) {
            System.out.println(e);
            System.out.println("Couldn't write user defaults at: " + defaultsFile);
          }
        }
        String[] clientParams = client.split(":");
        int port = Integer.parseInt(clientParams[1]); 
        if (port < 100)
        {
          port = port + 5500;
        }
        client = "connect " + clientParams[0] + ":" + port;
        if (debug) System.out.println("Connect to " + client);
        execCommand(client);
      }
    }
    
    /** Exit the Application */
    private void exitForm(WindowEvent evt) {
        System.exit(0);
    }
    
    /**
     * @param args the command line arguments
     */
    public static void main(String args[]) {

      for (int i = 0; i < args.length; i++) {
        if (args[i].startsWith("-")) {
          if (args[i].equals("-display")) {
            if (i + 1 > args.length) usage();
            displayName = args[++i];
          } else if (args[i].equals("-passwd")) {
            if (i + 1 > args.length) usage();
            passwdFile = args[++i];
          } else if (args[i].equals("-debug")) {
	    debug = true;
          }
        }
      }

      new javactrl().show();
    }

    private static void usage()
    {
      System.out.println("javactrl [-display <DPY>] [-passwd <pwdfile>] [-debug]");
    }
    
    // Variables declaration
    private JLabel TitleLabel;
    private JMenu menu;
    private ButtonGroup ShareStateButtonGroup;
    private JRadioButton NoSharingButton;
    private JRadioButton ShareDesktopButton;
    private JRadioButton ShareWindowsButton;
    private JButton AddButton;
    private JButton RemoveButton;
    private JButton RemoveAllButton;
    private JPanel ShareModePanel;
    private JPanel AddRemovePanel;
    private GridBagConstraints c;
    private Properties defaultValues;
    private static String displayName = ":0";
    private static String passwdFile = null; //"passwd";
    private static String defaultsFile = System.getProperty("user.home") + "/.collab/javactrl.cfg";
    private static boolean debug = false;
    // End of variables declaration
    
}
