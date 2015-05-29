Introduction
============


Authors
-------
luksipc was written by Johannes Bauer <JohannesBauer@gmx.de>. Please report all
bugs directly to his email address or file a issue at GitHub (URL is below). If
you do not wish to be named in the ChangeLog or this README file, please tell
me and I'll omit your name. Inversely, if I forgot to include you in this list
and you would like to appear, please drop me a note and I'll fix it.

There are several contributors to the project:

  - Eric Murray (cryptsetup status issue)
  - Christian Pulvermacher (cryptsetup status issue)
  - John Morrissey (large header issue)

The current version is maintained at GitHub at the moment:
    https://github.com/johndoe31415/luksipc

The project documentation can be found at:
    https://johndoe31415.github.io/luksipc

The projects main page is hosted at:
    http://johannes-bauer.com/linux/luksipc/

Please send issues and pull requests to GitHub if you would like to contribute.
I do have a horrible latency sometimes but I'll try to do my best, promise.



Disclaimer
----------
If you use luksipc and it bricks your disk and destroys all your data then
that's your fault, not mine. luksips comes without any warranty (neither
expressed nor implied). Please have a backup for really, really important data.


Compiling
---------
luksipc has no external dependencies, it should compile just fine if you have a
recent Linux distribution with GNU make and gcc installed. Just type::

    $ make

That's it. At runtime, it needs access to the cryptsetup and dmsetup tools in
the PATH.
