Stream Deck Control Daemon
==========================

The daemon should run in the background.  It is controlled by the configuration in

    ~/.config/streamdeckd.conf

which is using the syntax of libconfig.  An alternative file name can
be provided as the command line parameter.  The file content could look as
follows:

    serial= "CL...";
    
    keys: (
      {
        r1c1: {
          type: "keylight";
          serial: "BW...";
          function: "on/off";
        };
        r1c2: {
          type: "keylight";
          serial: "BW...";
          function: "brightness-";
        };
      
        r2c1: {
          type: "key";
          sequence: "alt+ctrl+shift+1"
          icon: "preview1.png";
        };
        r3c1: {
          type: "key";
          sequence: ("alt+ctrl+shift+1", "alt+ctrl+shift+quoteleft")
          icon: "switch1.png";
        };
        
        r4c1: {
          type: "execute";
          command: "chromium-browser --new-window https://example.com/path";
          icon: "example.png";
        };
        r1c8: {
          type: "nextpage";
        };
      },
      {
        r1c7: {
          type: "prevpage";
        };
      }
    );

The first `serial` definition is the serial number of the StreamDeck which is meant to be controlled.
The support is limited to those devices supported by the `streamdeckpp` package which should be,
as of the time of this writing, be all of them.  The library can be used to determine the serial
number if it is not known.

The second top-level definition is the `keys` list.  It contains one entry,
which must be a directory as explained below, per page.  A page consists
of the button which are visible together.  One or more buttons can be
specified to navigate between the pages.

The dictionaries defining each page should contain an entry for every
key that is used.  The individual keys are specified as `rXcY` which `X` is the row number and `Y` the column number of the key, from the top/left starting counting at one.

The definition for the individual keys is a dictionary again.  It must always contain an entry `type` which specifies the action associated
with the specific key.  The possible values are:


The `type`
entry specifies what happens when the key is pressed.  Currently three types are defined:

* `execute` which requires an additional dicionary item `command`. The string value of `command` is
  passed to the `system` function to be executed when the respective button is pressed.  An `icon` value is needed for the icon to display on the
  button.

* `keylight` which introduces an action to control a KeyLight device or several.  Each dictionary for
  this type must also define a `function` value which specifies the type of action that is performed.
  Supported so far are `toggle`, `brightness-`, `brightness+`, `color-`, and `color+`.  The `toggle`
  function turns on a light that is turned off and turns it off otherwise.  If a `serial` value is
  given only a device with the given serial number is affected.  Otherwise all found devices are
  controlled.

* `key` which is used to send the key sequence specified in the dictionary item `sequence` to the
  current window.  This by itself can be useful, a shortcut sequence for an editor or game can
  be issued.  Some programs listen for key sequences globally and if the specified key sequence
  is obscure enough this causes no problems.  In the example above pressing the first button in
  the second row send the key `1` with the modifiers Alt, Ctrl, and Shift.  Note the string notation
  to specify this.  The next entry is an example of how a list of strings can be used to specify
  more than one key to be sent.  An `icon` value is needed to specify
  the graphics shown on the button.

* `obs` which allows to control and monitor OBS through the websocket
  interface.  The `obs-websockets` plugin must be installed to use this.
  Various functions are supported as described in a separate section
  below.

* `nextpage` changes the currently displayed page of buttons to the next
  higher one (or back to the first one).  This is obviously only useful
  if more than one page of buttons is defined in the configuration.

* `prevpage` is similar to `nextpage`, just the opposite direction.

All actions except `execute` and `key` have default icons used for the
graphics associated with them.  If wanted they can be overwritten by
providing either an `icon` entry or `icon1` and `icon2` entries.  The
latter is needed if the button has two different displays, depending on
the state (e.g., KeyLight on or off).  If the icon file names are not
absolute path names the file is looked-up first in the internal
resources, then the `Pictures` subdirectory in the user's home directory,
and finally in the `/usr/share/pixmaps` directory.


OBS Control
-----------

TBD


Notes
-----

The KeyLight devices are located using mDNS.  This does not always work 100% reliably, the devices
seem not to send out the required signs of life frequently enough.  The
daemon tries to find the device a few times but if this fails the only
remedy is to restart the daemon.


To Do
-----

2.  When config file changed, reload
3.  GUI for creation of the config file
4.  Show controls for sources in scene
6.  FTB button: when pressed, blink red until pressed again
7.  Errors and updating when switching studio mode in OBS
8.  when selected transition is cut no duration is reported

Author: Ulrich Drepper <drepper@gmail.com>
