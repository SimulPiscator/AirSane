# Configuring AirSane for secure communication 
By itself, AirSane does not support secure data transmission over HTTPS. 
Still, secure connections are possible by using the nginx webserver as a proxy.
Also, user authentication may be configured via nginx.
To set up secure traffic, follow these steps:
## Install nginx
```sudo apt install nginx```
## Create a location for a forwarded unix socket
```
sudo mkdir /var/run/airsaned
sudo chown saned:www-data /var/run/airsaned
sudo chmod g+s /var/run/airsaned
```
## Copy example configuration
```
sudo cp https/systemd/airsaned.default /etc/default/airsane
sudo cp https/nginx/airsaned-ssl /etc/nginx/sites-available
sudo ln -s /etc/nginx/sites-available/airsaned-ssl /etc/nginx/sites-enabled
```
## Restart services
```
sudo service airsaned restart
sudo service nginx restart
```
## Configure user authentication
Officially, the eSCL protocol supports all means of web authentication.
In our tests however, the macOS AirScan client supported neither basic nor digest authentication,
and the Mopria Scan app for Android only worked with basic authentication.

If you still want to use authentication for the web interface or the Mopria app, 
basic authentication will be fine, as no clear text will be transmitted during authentication over
a HTTPS connection.

### Create a htpasswd file
```
sudo su
printf "USER:$(openssl passwd -crypt PASSWORD)\n" >> /etc/nginx/.htpasswd
exit
```
### In the nginx site configuration, enable the two lines defining user authentication
```sudo nano /etc/nginx/sites-available/airsaned-ssl```
### Restart nginx
```sudo service nginx restart```

Secure communication have been tested and confirmed to work
using a Firefox web browser, macOS Image Capture, and Mopria Scan for Android as clients.
Authentication only works using a web browser, or Mopria Scan for Android.

When using a web browser to access the AirSane web interface, you will receive a warning
about an invalid certificate unless you configure nginx to use a real certificate rather
than the self-signed one coming with openssl.
If you have your own internet domain, you will be able to use a real certificate 
for free from [the Let's Encrypt initiative](https://letsencrypt.org).
