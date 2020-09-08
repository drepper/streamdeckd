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
entry specifies what happens when the key is pressed.  Currently two types are defined:

* `execute` which requires an additional dicionary item `command`. The string value of `command` is
  passed to the `system` function to be executed when the respective button is pressed.

* `keylight` which introduces an action to control a KeyLight device or several.  Each dictionary for
  this type must also define a `function` value which specifies the type of action that is performed.
  Supported so far are `toggle`, `brightness-`, `brightness+`, `color-`, and `color+`.  The `toggle`
  function turns on a light that is turned off and turns it off otherwise.  If a `serial` value is
  given only a device with the given serial number is affected.  Otherwise all found devices are
  controlled.


Notes
-----

The KeyLight devices are located using mDNS.  This does not always work 100% reliably, the devices
seem not to send out the required signs of life frequently enough.  In the moment this can only be
remedied by restarting the daemon.



Author: Ulrich Drepper <drepper@gmail.com>
