# macOS build instructions
Building AirSane on macOS is quite similar to building on Linux but
requires MacPorts to provide the necessary dependencies.

If you have not done so, install MacPorts as described here:
https://www.macports.org/install.php

Then, open a terminal and `cd` to a location that will hold your AirSane
source directory.

There, execute the following commands:
```
sudo port install sane-backends jpeg libpng
sudo port install cmake
git clone github.com/SimulPiscator/AirSane.git
mkdir AirSane-build && cd AirSane-build
cmake  ../AirSane
make
sudo make install
sudo launchctl load /Library/LaunchDaemons/org.simulpiscator.airsaned.plist 
```

If everything went well, a window will pop up, asking whether to allow
the program `airsaned` to listen on the network. Click "Allow".

Now, whenever you plug in an USB scanner that is recognized by `scanimage -L`, 
it will also be available in Apple Image Capture, Preview, and other programs
that rely on Apple's ImageCapture framework.

If your scanner is not recognized by `scanimage -L`, you might need to enable
its backend in `/opt/local/etc/sane.d/dll.conf`.

There, you may also disable unused backends by prepending with a `#` in order
to speed up device search.
