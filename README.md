#AirSane

A SANE WebScan frontend that supports Apple's AirScan protocol.
Scanners are detected automatically, and published through mDNS.
Though images may be acquired and transferred
in JPEG, PNG, and PDF/raster format through a simple web interface,
its intended purpose is to be used with AirScan/eSCL clients such as
Apple's Image Capture.

Images are encoded on-the-fly during acquisition, keeping memory/storage
demands low. Should work fine on a Raspberry Pi or similar device.
Authentication and secure communication are not unsupported.

AirSane has been developed by reverse-engineering the communication protocol
implemented in Apple's AirScanScanner client
(macos 10.12.6, `/System/Library/Image Capture/Devices/AirScanScanner.app`).

If you are looking for a powerful SANE web frontend, AirSane may not be for you.
You may be interested in [phpSANE] (https://sourceforge.net/projects/phpsane) instead.

##Usage
###Web interface
Open `http://machine-name:8090/` in a web browser, and follow a scanner 
link from the main page.
###macos
When opening Apple Image Capture or similar applications, scanners exported
by AirSane should be immediately available.
In 'Printers and Scanners', exported scanners will be listed with a type of 
'Bonjour Scanner'.

##Build

 sudo apt install libsane-dev libjpeg-dev libpng-dev
 sudo apt install libavahi-client-dev libusb-1.*-dev
 sudo apt install git cmake g++
 git clone https://github.com/SimulPiscator/audiocast.git
 mkdir airsaned-build && cd airsaned-build
 cmake ../airsaned
 make

##Install

The provided systemd service file will assumes that user and group
'saned' exist and have permission to access scanners.
Installing the saned package is a convenient way to set up a user 'saned'
with proper permissions:
 sudo apt install saned

 sudo make install
 sudo systemctl enable airsaned
 sudo systemctl start airsaned
 sudo systemctl status airsaned
Disable saned if you are not using it:
 sudo systemctl disable saned
Disable unused scanner backends to speed up device searching:
 sudo nano /etc/sane.d/dll.conf
The server's listening port, and other configuration details, may be changed
by editing '/etc/default/airsane'. For options, and their meaning, run
 airsaned --help

By default, the server listens on all local addresses, and port 8090.
To verify http access, open `http://localhost:8090/` in a web browser.
From there, follow a link to a scanner page, and click the 'update preview'
button for a preview scan.

##Troubleshoot

If you are able to open the server's web page locally, but not from a remote
machine, you may have to allow access to port 8090 in your iptables
configuration.

Enabling the 'test' backend in `/etc/sane.d/dll.conf` may be helpful 
to separate software from hardware issues.

To troubleshoot permission issues, compare debug output when running
airsaned as user saned vs running as root:
 sudo systemctl stop airsaned
 sudo su - saned -s /bin/sh -c 'airsaned --debug=true --access-log=-'
 sudo airsaned --debug=true --access-log=-
