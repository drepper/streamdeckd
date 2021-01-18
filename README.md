StreamDeck Control Daemon
=========================

The daemon should run in the background.  It is controlled by the configuration in

    ~/.config/streamdeckd.conf

which is using the syntax of libconfig.  It could look as follows:

    serial= "CL...";
    
    keys: {
      r1c1: {
      	type: "keylight";
      	serial: "BW...";
      	function: "on/off";
      	icon: "bulb_on.png";
      };
      r1c2: {
      	type: "keylight";
      	serial: "BW...";
      	function: "brightness-";
      	icon: "brightness-.png";
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
      }
    };

The first `serial` definition is the serial number of the StreamDeck which is meant to be controlled.
The support is limited to those devices supported by the `streamdeckpp` package which should be,
as of the time of this writing, be all of them.  The library can be used to determine the serial
number if it is not known.

The second top-level definition is the `keys` dictionary which should contain an entry for every
key that is used.  The individual keys are specified as `rXcY` which `X` is the row number and `Y` the column number of the key, from the top/left starting counting at one.

The definition for the individual keys is a dictionary again.  It must always contain entries `type`
and `icon`.  The latter is the name of the file to use as the icon on the respective key.  The `type`
entry specifies what happens when the key is pressed.  Currently three types are defined:

* `execute` which requires an additional dicionary item `command`. The string value of `command` is
  passed to the `system` function to be executed when the respective button is pressed.

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
  more than one key to be sent.

Notes
-----

The KeyLight devices are located using mDNS.  This does not always work 100% reliably, the devices
seem not to send out the required signs of life frequently enough.  In the moment this can only be
remedied by restarting the daemon.



Author: Ulrich Drepper <drepper@gmail.com>
