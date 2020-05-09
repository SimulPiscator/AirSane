![build](https://github.com/SimulPiscator/AirSane/workflows/build/badge.svg)

# AirSane

A SANE WebScan frontend that supports Apple's AirScan protocol.
Scanners are detected automatically, and published through mDNS.
Though images may be acquired and transferred
in JPEG, PNG, and PDF/raster format through a simple web interface,
AirSane's intended purpose is to be used with AirScan/eSCL clients such as
Apple's Image Capture.

Images are encoded on-the-fly during acquisition, keeping memory/storage
demands low. Thus, AirSane will run fine on a Raspberry Pi or similar device.

AirSane has been developed by reverse-engineering the communication protocol
implemented in Apple's AirScanScanner client
(macos 10.12.6, `/System/Library/Image Capture/Devices/AirScanScanner.app`).

Authentication and secure communication are not supported.

If you are looking for a powerful SANE web frontend, AirSane may not be for you.
You may be interested in [phpSANE](https://sourceforge.net/projects/phpsane) instead.

## Usage
### Web interface
Open `http://machine-name:8090/` in a web browser, and follow a scanner 
link from the main page.
### macOS
When opening Apple Image Capture or similar applications, scanners exported
by AirSane should be immediately available.
In 'Printers and Scanners', exported scanners will be listed with a type of 
'Bonjour Scanner'.

## Build for OpenWRT

Build files and instructions have been published here:
https://github.com/tbaela/AirSane-openwrt

## Build on Debian/Ubuntu/Raspbian
```
sudo apt install libsane-dev libjpeg-dev libpng-dev
sudo apt install libavahi-client-dev libusb-1.*-dev
sudo apt install git cmake g++
git clone https://github.com/SimulPiscator/AirSane.git
mkdir AirSane-build && cd AirSane-build
cmake ../AirSane
make
```
## Install

The provided systemd service file assumes that user and group
'saned' exist and have permission to access scanners.
Installing the sane-utils package is a convenient way to set up a user 'saned'
with proper permissions:
```
sudo apt install sane-utils
```
Make sure that ```sudo scanimage -L``` lists all scanners attached to your machine.
Listing scanners as user 'saned' should show all scanners as well:
```
sudo -u saned scanimage -L
```
If all scanners are listed for 'root' but none for 'saned,' you might have hit
a [bug in libsane](https://bugs.debian.org/cgi-bin/bugreport.cgi?bug=918358).
As a workaround, create a file ```/etc/udev/rules.d/65-libsane.rules``` with this content:
```
ENV{libsane_matched}=="yes", RUN+="/bin/setfacl -m g:scanner:rw $env{DEVNAME}"
```
Unplug and re-plug all scanners. ```sudo -u saned scanimage -L``` should now list all
of them.

To install AirSane:
```
sudo apt install avahi-daemon
sudo make install
sudo systemctl enable airsaned
sudo systemctl start airsaned
sudo systemctl status airsaned
```
Disable saned if you are not using it:
```
sudo systemctl disable saned
```
Disable unused scanner backends to speed up device search:
```
sudo nano /etc/sane.d/dll.conf
```
The server's listening port, and other configuration details, may be changed
by editing '/etc/default/airsane'. For options, and their meanings, run
```
airsaned --help
```
By default, the server listens on all local addresses, and port 8090.
To verify http access, open `http://localhost:8090/` in a web browser.
From there, follow a link to a scanner page, and click the 'update preview'
button for a preview scan.

## Optional configuration

In addition to the options that may be configured through `/etc/default/airsane`, it is possible to configure 
SANE options to be used when scanning from a certain device. Create a file `/etc/airsane/options.conf`, readable
by user `saned`, and containing lines that either specify a SANE option and a value, or the word `device` followed
with a regular expression that will be matched against both a scanner's SANE device name, and its make-and-model string.
SANE options at the top of the file are applied to all scanners. If a scanner matches multiple `device` lines,
SANE options from all of these `device` sections will be applied.

To display the SANE options supported by a device, use `sudo -u saned scanimage -L` to get its SANE name, and then
`sudo -u saned scanimage -d <device name> -A` to get a list of options.

In `options.conf`, SANE options must be given without leading minus signs, and with white space between the option's name and its value. White space is removed from the beginnning and the end of the value.

```
# Example options.conf file for airsane
# Set brightness to 10 for all scanners
brightness 10

# Set options for all scanners using the genesys backend
device genesys:.*
contrast -10
swdeskew yes

# Set calibration file option for a scanner "Canon LiDE 60"
device Canon LiDE 60
calibration-file /home/simul/some path with spaces/canon-lide-60.cal
```


## Troubleshoot

* Compiling fails with error: "‘png_const_bytep’ does not name a type".
You have libpng installed in an old version. Some distributions provide libpng12 and libpng16 for you to select.
Installing libpng16-dev should fix the issue:
```
   sudo apt install libpng16-dev
```
* Compiling fails because of **`#include <libpng/png.h>`** not being found. 
On some distributions (e.g., Arch Linux), `libpng` may come in multiple flavors, with each having its
own `/usr/include` subdirectory. 
Creating a symlink will then fix the build:
```
  sudo ln -s /usr/include/libpng16/ /usr/include/libpng/
```
* If you are able to open the server's web page locally, but **not from a remote
machine,** you may have to allow access to port 8090 in your iptables
configuration.

* Enabling the **'test' backend** in `/etc/sane.d/dll.conf` may be helpful 
to separate software from hardware issues.

* To troubleshoot **permission issues,** compare debug output when running
airsaned as user saned vs running as root:
```
  sudo systemctl stop airsaned
  sudo -u saned airsaned --debug=true --access-log=-
  sudo airsaned --debug=true --access-log=-
```
