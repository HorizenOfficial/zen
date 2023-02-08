*** Warning: Do not assume Tor support does the correct thing in Zen; better Tor support is a future feature goal. ***

### TOR SUPPORT IN Zen

It is possible to run Zen as a Tor hidden service, and connect to such services.

### BASIC Tor INSTALL AND CONFIGUATION


1. Install Tor from the official Repository (Debian based only)
First add the Tor repository to your package sources
```
sudo su -c "echo 'deb http://deb.torproject.org/torproject.org '$(lsb_release -c | cut -f2)' main' > /etc/apt/sources.list.d/torproject.list"
gpg --keyserver keys.gnupg.net --recv A3C4F0F979CAA22CDBA8F512EE8CBC9E886DDD89
gpg --export A3C4F0F979CAA22CDBA8F512EE8CBC9E886DDD89 | sudo apt-key add -
```
and install it
```
sudo apt-get update
sudo apt-get install tor deb.torproject.org-keyring
```
then add your current user to the `debian-tor` group to be able to use cookie authentication.
```
sudo usermod -a -G debian-tor $(whoami)
```
Now log out and back in, then edit `/etc/tor/torrc` to enable the control port, enable cookie authentication and make the auth file group readable.
```
sudo sed -i 's/#ControlPort 9051/ControlPort 9051/g' /etc/tor/torrc
sudo sed -i 's/#CookieAuthentication 1/CookieAuthentication 1/g' /etc/tor/torrc
sudo su -c "echo 'CookieAuthFileGroupReadable 1' >> /etc/tor/torrc"
```
Now restart the Tor service.
```
sudo systemctl restart tor.service
```

If you've configured Tor as previously described there is nothing more you need to do. When you start zend without any command line options e.g. `./zend` it will automatically create a HiddenService and be reachable via the Tor network and clearnet. The node should have created a file `~/.zen/onion_private_key`, this file stores the private key of your HiddenService address.

When you use `./zen-cli getnetworkinfo` you should see that your node is reachable as a HiddenService like so:
```
[...]
    {
      "name": "onion",
      "limited": false,
      "reachable": true,
      "proxy": "127.0.0.1:9050",
      "proxy_randomize_credentials": true
    }
  ],
  "relayfee": 0.00000100,
  "localaddresses": [
    {
      "address": "lbfuusf2bg5e2t3j.onion",
      "port": 9033,
      "score": 4
    }
[...]
```

Currently, there is no Zen Seed Node to provide Tor node addresses, edit your `~/.zen/zen.conf` and add:
```
addnode=d2y2vsq5rxkcpk6f.onion
addnode=eorrku3hauy53zup.onion
addnode=dr7rtjaszmuowjrt.onion
```


### ADVANCED Zend/Tor CONFIGURATION OPTIONS


The following directions assume you have a Tor proxy running on port 9050 (possibly by following the above). Many distributions default to having a SOCKS proxy listening on port 9050, but others may not. In particular, the Tor Browser Bundle defaults to listening on port 9150. See [Tor Project FAQ:TBBSocksPort](https://www.torproject.org/docs/faq.html.en#TBBSocksPort) for how to properly
configure Tor.


1. Run Zen behind a Tor proxy
-------------------------------

The first step is running Zen behind a Tor proxy. This will already make all
outgoing connections be anonymized, but more is possible.

	-proxy=ip:port  Set the proxy server. If SOCKS5 is selected (default), this proxy
	                server will be used to try to reach .onion addresses as well.

	-onion=ip:port  Set the proxy server to use for Tor hidden services. You do not
	                need to set this if it's the same as -proxy. You can use -noonion
	                to explicitly disable access to hidden service.

	-listen         When using -proxy, listening is disabled by default. If you want
	                to run a hidden service (see next section), you'll need to enable
	                it explicitly.

	-connect=X      When behind a Tor proxy, you can specify .onion addresses instead
	-addnode=X      of IP addresses or hostnames in these parameters. It requires
	-seednode=X     SOCKS5. In Tor mode, such addresses can also be exchanged with
	                other P2P nodes.

In a typical situation, this suffices to run behind a Tor proxy:

	./Zend -proxy=127.0.0.1:9050

Note: Running Zend without the proxy argument will work, however, you will only be able to connect to other nodes on Tor.  The proxy argument allows you to connect to both Tor nodes and Clearnet nodes.

2. Run a Zen hidden server
----------------------------

If you configure your Tor system accordingly, it is possible to make your node also
reachable from the Tor network. Add these lines to your /etc/tor/torrc (or equivalent
config file):

	HiddenServiceDir /var/lib/tor/zen-service/
	HiddenServicePort 9033 127.0.0.1:9033
	HiddenServicePort 20033 127.0.0.1:20033

The directory can be different of course, but (both) port numbers should be equal to
your Zend's P2P listen port (9033 by default).

	-externalip=X   You can tell zend about its publicly reachable address using
	                this option, and this can be a .onion address. Given the above
	                configuration, you can find your onion address in
	                /var/lib/tor/Zen-service/hostname. Onion addresses are given
	                preference for your node to advertize itself with, for connections
	                coming from unroutable addresses (such as 127.0.0.1, where the
	                Tor proxy typically runs).

	-listen         You'll need to enable listening for incoming connections, as this
	                is off by default behind a proxy.

	-discover       When -externalip is specified, no attempt is made to discover local
	                IPv4 or IPv6 addresses. If you want to run a dual stack, reachable
	                from both Tor and IPv4 (or IPv6), you'll need to either pass your
	                other addresses using -externalip, or explicitly enable -discover.
	                Note that both addresses of a dual-stack system may be easily
	                linkable using traffic analysis.

In a typical situation, where you're only reachable via Tor, this should suffice:

	./zend -proxy=127.0.0.1:9050 -externalip=d2y2vsq5rxkcpk6f.onion -listen

(Replace the Onion address with your own). It should be noted that you still
listen on all devices and another node could establish a clearnet connection, when knowing
your address. To mitigate this, additionally bind the address of your Tor proxy:

	./zend ... -bind=127.0.0.1

If you don't care too much about hiding your node, and want to be reachable on IPv4
as well, use `discover` instead:

	./zend ... -discover

and open port 9033 on your firewall (or use -upnp).

If you only want to use Tor to reach onion addresses, but not use it as a proxy
for normal IPv4/IPv6 communication, use:

	./zend -onion=127.0.0.1:9050 -externalip=d2y2vsq5rxkcpk6f.onion -discover


3. Automatically listen on Tor
--------------------------------

Starting with Tor version 0.2.7.1 it is possible, through Tor's control socket
API, to create and destroy 'ephemeral' hidden services programmatically.
Zen has been updated to make use of this.

This means that if Tor is running (and proper authentication has been configured),
Zend automatically creates a hidden service to listen on. Zend will also use Tor
automatically to connect to other .onion nodes if the control socket can be
successfully opened. This will positively affect the number of available .onion
nodes and their usage.

This new feature is enabled by default if Zen is listening (`-listen`), and
requires a Tor connection to work. It can be explicitly disabled with `-listenonion=0`
and, if not disabled, configured using the `-torcontrol` and `-torpassword` settings.
To show verbose debugging information, pass `-debug=tor`.

Connecting to Tor's control socket API requires one of two authentication methods to be 
configured. For cookie authentication the user running Zend must have write access 
to the `CookieAuthFile` specified in Tor configuration. In some cases this is 
preconfigured and the creation of a hidden service is automatic. If permission problems 
are seen with `-debug=tor` they can be resolved by adding both the user running tor and 
the user running Zend to the same group and setting permissions appropriately. On 
Debian-based systems the user running Zend can be added to the debian-tor group, 
which has the appropriate permissions. An alternative authentication method is the use 
of the `-torpassword` flag and a `hash-password` which can be enabled and specified in 
Tor configuration.


4. Connect to a Zen hidden server
-----------------------------------

To test your set-up, you might want to try connecting via Tor on a different computer to just a
a single Zen hidden server. Launch zend as follows:

	./zend -onion=127.0.0.1:9050 -connect=d2y2vsq5rxkcpk6f.onion

Now use Zen-cli to verify there is only a single peer connection.

	Zen-cli getpeerinfo

	[
	    {
	        "id" : 1,
	        "addr" : "d2y2vsq5rxkcpk6f.onion:18233",
	        ...
	        "version" : 170005,
	        "subver" : "/zen:1.0.0/",
	        ...
	    }
	]

To connect to multiple Tor nodes, use:

	./zend -onion=127.0.0.1:9050 -addnode=d2y2vsq5rxkcpk6f.onion -dnsseed=0 -onlynet=onion

### USING Tor PLUGGABLE TRANSPORTS

Pluggable Transports transform the Tor traffic flow between the client and the bridge. This way, censors who monitor traffic between the client and the bridge will see innocent-looking transformed traffic instead of the actual Tor traffic. External programs can talk to Tor clients and Tor bridges using the pluggable transport API, to make it easier to build interoperable programs.

(Currently, only Meek has been confirmed to work with Zend.)

--------------------------------
1. install `meek-client` (Ubuntu only):
```
sudo add-apt-repository ppa:hda-me/meek
sudo apt-get update
sudo apt-get install meek-client
```
If using Ubuntu, an AppArmor definition is also required to resolve a 'permission denied' error:
```
sudo su -c "echo '  /usr/bin/meek-client ix,' >> /etc/apparmor.d/abstractions/tor"
sudo systemctl resetart apparmor.service
```
2. Add these lines to your `/etc/tor/torrc` file:
```
UseBridges 1
ClientTransportPlugin meek exec  /usr/bin/meek-client
Bridge meek 0.0.2.0:1 B9E7141C594AF25699E0079C1F0146F409495296 url=https://d2cly7j4zqgua7.cloudfront.net/ front=a0.awsstatic.com
Bridge meek 0.0.2.0:2 97700DFE9F483596DDA6264C4D7DF7641E1E39CE url=https://meek.azureedge.net/ front=ajax.aspnetcdn.com
```
An updated list of Tor Meek bridges can be found here:
https://gitweb.torproject.org/builders/tor-browser-bundle.git/tree/Bundle-Data/PTConfigs/bridge_prefs.js
