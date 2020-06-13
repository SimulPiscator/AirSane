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

Authentication and secure communication are not supported.

If you are looking for a powerful SANE web frontend, AirSane may not be for you.
You may be interested in [phpSANE](https://sourceforge.net/projects/phpsane) instead.

AirSane has been developed by reverse-engineering the communication protocol
implemented in Apple's AirScanScanner client
(macos 10.12.6, `/System/Library/Image Capture/Devices/AirScanScanner.app`), using a 
[Disassembler](https://www.hopperapp.com/) able to reconstruct much of the original 
Objective-C source code.

Regarding the mdns announcement, and the basic working of the eSCL protocol, [David Poole's blog](http://testcluster.blogspot.com/2014/03) was very helpful.

## Usage
### Web interface
Open `http://machine-name:8090/` in a web browser, and follow a scanner 
link from the main page.

### macOS
When opening 'Image Capture', 'Preview', or other applications using the
ImageKit framework, scanners exported by AirSane should be immediately available.
In 'Printers and Scanners', exported scanners will be listed with a type of 
'Bonjour Scanner'.

If you defined a custom icon for your scanner (see below), note that you will
have to use the scanner through 'Image Capture' once before it will be
shown with this icon in 'Printers and Scanners'. This seems to be a bug in macOS.

### Mopria client on Android
As of version 1.3.7, the Mopria Scan App will display all AirSane scanners and
display them with name and icon. After choosing scan options, you will be able
to scan to your android device.

## Installation
### Packages for Synology NAS
Pre-built packages for Synology are available here: 
<https://search.synopackage.com/sources/pcloadletter>

### Build for OpenWRT
Build files and instructions for OpenWRT have been published here:
<https://github.com/tbaela/AirSane-openwrt>

### Build and install from source on Debian/Ubuntu/Raspbian
#### Build
```
sudo apt install libsane-dev libjpeg-dev libpng-dev
sudo apt install libavahi-client-dev libusb-1.*-dev
sudo apt install git cmake g++
git clone https://github.com/SimulPiscator/AirSane.git
mkdir AirSane-build && cd AirSane-build
cmake ../AirSane
make
```
#### Install
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
options to be used when scanning from a certain device.
To specify such options, create a file `/etc/airsane/options.conf`, readable by user `saned`.  
This file may contain the following kinds of lines:
* Empty lines, and comment lines starting with #, will be ignored.
* Lines beginning with the word `device`, followed with a regular expression,   
  will begin a device section that applies to all devices with SANE device name or make-and-model string matching 
  the regular expression.
* Lines beginning with an option name, and an option value separated by white space,
  will define an option.  
  Options at the top of the file are applied to all scanners. If a scanner matches multiple `device` lines,  
  options from all of these `device` sections will be applied.  
  Options may be SANE backend options, or AirSane device options (see below).

### SANE backend options
To display the SANE options supported by a device, use `sudo -u saned scanimage -L` to get its SANE name, and
then `sudo -u saned scanimage -d <device name> -A` to get a list of options.  
In `options.conf`, SANE options must be given without leading minus signs, and with white space between the 
option's name and its value. White space is removed from the beginnning and the end of the value.

### AirSane device options
#### gray-gamma
A gamma value that is applied to grayscale image data before transmission. The gamma value is given as a floating-point value.
#### color-gamma
A gamma value that is applied to color image data before transmission, using identical gamma values for all components.
#### synthesize-gray
A value of `yes` or `no`. If set to `yes`, AirSane will always request color data from the SANE backend, even if the user
requests a grayscale scan. In this case, grayscale values will be computed from RGB component data after gamma correction, 
using weights as suited for sRGB data:  
`Y = 0.2126 * R + 0.7152 * G + 0.0722 * B`  
This is useful for backends that do not allow true grayscale scanning or incorrectly return a single color component even if
true gray is requested ([observed](https://gitlab.com/sane-project/backends/-/issues/308) with the SANE genesys backend).
#### icon
Full path to a png file that should be used as the scanner's icon. The image should have a size of 512x512, 256x256 or 
128x128 pixels and an alpha channel for transparency. If pixel dimensions are not powers of two, the image will not be
accepted by macOS.

### Example
```
# Example options.conf file for airsane
# Set SANE brightness to 10 for all scanners
brightness 10

# Compensate for OS-side gamma correction with gamma = 1.8 = 1/0.555555
gray-gamma 0.555555
color-gamma 0.555555

# Set options for all scanners using the genesys backend
device genesys:.*
synthesize-gray yes

# Set icon and calibration file option for a scanner "Canon LiDE 60"
device Canon LiDE 60
icon /etc/airsane/CanonLiDE60.png
calibration-file /home/simul/some path with spaces/canon-lide-60.cal
```

## Color Management (Gamma Correction)

Although not stated explicitly, it seems that SANE backends try to perform color and gamma correction such as
to return image data in a linear color space.

When receiving scan data from an AirScan scanner, macOS seems to ignore all color space related information from the
transmitted image files, and interprets color and gray levels according to standard scanner color profiles.
Using ColorSync Utility, one can see that these color profiles are called `Scanner RGB Profile.icc` and 
`Scanner Gray Profile.icc`, located at `/System/Library/Frameworks/ICADevices.framework/Resources`.
Unfortunately, it is not possible to permanently assign a different color profile to an AirScan scanner using 
ColorSync Utility: the specified color profile is not used, and the profile setting is reverted to the original standard profile.

The macOS standard profiles assume a gamma value of 1.8, which does not match the linear data coming from SANE.
As a result, scanned images appear darker than the original, with fewer details in darker areas.

Using the gamma options of AirSane, you will be able to neutralize the gamma value of 1.8 in the macOS scanner profile.
Apply the inverse of 1.8 as a `gray-gamma` and `color-gamma` value in your AirSane configuration file, as shown in the example 
above.

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
