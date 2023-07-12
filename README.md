![build](https://github.com/SimulPiscator/AirSane/workflows/build/badge.svg)

# AirSane

AirSane is a [SANE](http://www.sane-project.org/) frontend, and a scanner server
that supports Apple's AirScan protocol.
Scanners are detected automatically, and published through mDNS.
Acquired images may be transferred 
in JPEG, PNG, and [PDF/raster](https://www.pdfraster.org/) format.

AirSane's intended purpose is to be used with AirScan/eSCL clients such as
Apple's Image Capture, [sane-airscan](https://github.com/alexpevzner/sane-airscan) on Linux,
and the eSCL client built into Windows 10.

In addition to the AirScan/eSCL server functionality, a simple web interface is provided.

Images are encoded on-the-fly during acquisition, keeping memory/storage
demands low. Thus, AirSane will run fine on a Raspberry Pi or similar device.

Authentication and secure communication are supported in conjunction with a
proxy server such as nginx (see the [https readme file](README.https.md)).

If you are looking for a powerful SANE web frontend, AirSane may not be for you.
You may be interested in [scanservjs](https://github.com/sbs20/scanservjs) instead.

AirSane has been developed by reverse-engineering the communication protocol
implemented in Apple's AirScanScanner client
(macos 10.12.6, `/System/Library/Image Capture/Devices/AirScanScanner.app`).

Regarding the mDNS announcement, and the basic working of the eSCL protocol,
[David Poole's blog](http://testcluster.blogspot.com/2014/03) was very helpful.

In the meantime, the eSCL protocol has been officially published
[here](https://mopria.org/mopria-escl-specification).

## Usage
### Web interface
Open `http://machine-name:8090/` in a web browser, and follow a scanner 
link from the main page.

### macOS
When opening 'Image Capture', 'Preview', or other applications using the
ImageKit framework, scanners exported by AirSane should be immediately available.

In the 'Printers and Scanners' control panel, exported scanners will be listed with 
a type of 'Bonjour Scanner'.

A macOS compatible scanner plugin for the "GIMP" image editing software is provided
[here](https://github.com/SimulPiscator/GimpScan).

If you define a custom icon for your scanner (see below), note that you will
have to use the scanner through 'Image Capture' once before it will be
shown with this icon in 'Printers and Scanners'. This seems to be a bug in macOS
at least up to Catalina.

### Linux
Install the [sane-airscan backend](https://github.com/alexpevzner/sane-airscan) with
```
sudo apt-get install sane-airscan
```
or whatever the packet manager of your distribution requires to install it.
Using `sudo nano /etc/sane.d/dll.conf`, add a line "airscan", and prepend a # character
before the "escl" entry if present. (There are two airscan backends, called 
"escl" and "airscan", but only "airscan" is compatible with AirSane.)
When done, `scanimage -L` should list your AirSane devices, and SANE clients such
as XSane or simple-scan should be able to scan from them.

### Windows
eSCL support in Windows has been introduced in Windows 11 first, but is now available
in Windows 10 as well: <https://support.microsoft.com/en-us/topic/june-28-2022-kb5014666-os-builds-19042-1806-19043-1806-and-19044-1806-preview-4bd911df-f290-4753-bdec-a83bc8709eb6>

#### Connecting to an AirSane scanner
Go to "Settings"->"Bluetooth & devices"->"Printers and Scanners."
There, click "Add Device".
AirSane devices will appear as devices to add, click "Add".
Wait until the device appears in the list of devices below, click the device,
and choose "Install app" or "Open scanner" in order to install the Microsoft
scanner app, or open it if has been installed before.
Note that Windows does not allow more than 4 AirSane scanners in total.

### Mopria client on Android
As of version 1.4.10, the Mopria Scan App will detect all AirSane scanners and
display them with name and icon. After choosing scan options, you will be able
to scan to your android device.

## Installation

### Build and install from source for OpenWRT
Build files and instructions for OpenWRT have been published here:
<https://github.com/tbaela/AirSane-openwrt>,
and its up-to-date revision is published here:
<https://github.com/cmangla/AirSane-openwrt>

### Build and install from source on macOS
AirSane may be run on a macOS installation in order to serve locally attached
scanners to eSCL clients such as Apple Image Capture. For instructions, see 
[the macOS README file](README.macOS.md).

### Build and install from source on Debian/Ubuntu/Raspbian
#### Build
```
sudo apt-get install libsane-dev libjpeg-dev libpng-dev
sudo apt-get install libavahi-client-dev libusb-1.*-dev
sudo apt-get install git cmake g++
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
sudo apt-get install sane-utils
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
ENV{libsane_matched}=="yes", RUN+="/usr/bin/setfacl -m g:scanner:rw $env{DEVNAME}"
```
Double-check the location of the `setfacl` binary using `which setfacl`, adapt the line if necessary.
Unplug and re-plug all scanners. ```sudo -u saned scanimage -L``` should now list all
of them.

To install AirSane:
```
sudo apt-get install avahi-daemon
make && sudo make install
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
By default, the server listens on all local addresses, and on port 8090.

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
Name of a png file that should be used as the scanner's icon.
This may be an absolute path, or a relative path.
If relative (e.g., just a file name without a path), it is relative to the 
location of the options file.

The image should have a size of 512x512, 256x256 or 128x128 pixels and an alpha channel for transparency.
If pixel dimensions are not powers of two, the image will not be accepted by macOS.
#### location
A string that appears in the `note` field of the mDNS announcement. This should be an indication where the scanner is located,
such as "Living Room" or "Office." If no location is given in the options file, this defaults to the host name of the machine
that runs airsaned.

### Example
```
# Example options.conf file for airsane
# Set SANE brightness to 10 for all scanners
brightness 10
# Set a default icon for all scanners
icon Gnome-scanner.png

# Compensate for OS-side gamma correction with gamma = 1.8 = 1/0.555555
gray-gamma 0.555555
color-gamma 0.555555

# Set options for all scanners using the genesys backend
device genesys:.*
synthesize-gray yes

# Set icon and calibration file option for a scanner "Canon LiDE 60"
device Canon LiDE 60
icon CanonLiDE60.png
location Living Room
calibration-file /home/simul/some path with spaces/canon-lide-60.cal
```

## Color Management (Gamma Correction)

When receiving scan data from an AirScan scanner, macOS seems to ignore all color space related information from the
transmitted image files, and interprets color and gray levels according to standard scanner color profiles.
Using ColorSync Utility, one can see that these color profiles are called `Scanner RGB Profile.icc` and 
`Scanner Gray Profile.icc`, located at `/System/Library/Frameworks/ICADevices.framework/Resources`.
Unfortunately, it is not possible to permanently assign a different color profile to an AirScan scanner using 
ColorSync Utility: the specified color profile is not used, and the profile setting is reverted to the original standard profile.

The SANE standard does not prescribe a certain gamma of backend output.

The macOS standard profiles assume a gamma value of 1.8, which does not necessarily match the data coming from the SANE backend.
As a result, scanned images may appear darker than the original, with fewer details in darker areas, or brighter, with fewer
details in brighter areas.

Using the gamma options of AirSane, you will be able to neutralize the gamma value of 1.8 in the macOS scanner profile.
Apply the inverse of 1.8 as a `gray-gamma` and `color-gamma` value in your AirSane configuration file, as shown in the example 
above. By multiplying with another factor between 0.45 and 2.2, you can correct for the gamma value returned from the SANE backend.

## Ignore List

If a file exists at the location for the ignore list (by default, `/etc/airsane/ignorelist.conf`), AirSane will read that file line
by line, treat each line as a regular expression to be matched against a device's SANE name, and will ignore any device that
matches.

The original purpose of the ignore list is to avoid loops with backends that auto-detect eSCL devices, but it may be used to suppress
any device from AirSane's list of published devices.

## Troubleshoot

* Compiling fails with error: "‘png_const_bytep’ does not name a type".
You have libpng installed in an old version. Some distributions provide libpng12 and libpng16 for you to select.
Installing libpng16-dev should fix the issue:
```
   sudo apt install libpng16-dev
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

* Scan appears **too dark** or **too bright**. See notes about color management (gamma correction)
above. Start out with the suggested factor of 0.55. Try settings between 0.45 and 2.2 until scan
quality appears good.

* A **dark vertical stripe** appears in the middle of the scan when using a Canon scanner ("genesys" backend).
This is a known [bug in the genesys backend](https://bugs.launchpad.net/ubuntu/+source/sane-backends/+bug/1731459),
present in libsane versions 1.0.26 and 1.0.27. The solution is to remove the libsane package, and install
SANE from source.
   
* Apple Image Capture fails to connect to the scanner (shows an **"error 21345"**).
Enable IPv6 in your local network, and on the machine running AirSane.
After rebooting the machine running AirSane, you will be able to scan from Apple Image Capture.

* Scanners are not advertised, and in the debug log, you seen an avahi error **"Bad State (-2)"**.
Most likely, the avahi-daemon package is not installed, or avahi-daemon is not running/enabled:
```
  sudo install avahi-daemon
  sudo systemctl enable avahi-daemon
  sudo systemctl start avahi-damon

```

* You can see no scanners, and you’re using `scanbd`.
`scanbd` works by proxying local scanners as network scanners, so you need to tell AirSane to include network scanners in its list. In `/etc/default/airsane`, set `LOCAL_SCANNERS_ONLY=false`. Also, at least on Debian systems, the `scanbm` service needs 2 network connections to be able to talk to AirSane, so:
```
  sudo systemctl edit scanbm.socket
  # Change MaxConnections=1 to MaxConnections=2 and save
  sudo systemctl restart scanbm.socket
```
