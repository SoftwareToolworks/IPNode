#### A 10 Meter 1200 Baud QPSK IP Node
Based on highly modified Dire Wolf repository, to create a Linux based QPSK Packet Radio Node designed for 10 meter band. This band has a maximum symbol rate of 1200 Baud in the US, so a maximum of 2400 bit/s, and uses an I/Q sampling rate of 9600 bit/s. A Root-Raised-Cosine (RRC) matched filter is used on transmit and receive, and a slightly modified Improved Level 2 Protocol (IL2P) encodes the data onto AX.25 packets. A Garner Timing Error Detection (TED). and a Costas Loop are used on receive. This is a point-point link, as no digipeaters are used.

Designed for Internet Protocol (IP) use, it must operate in the **28.120 - 28.189 MHz** band if automatically controlled. The center frequency of ```1000 Hz``` keeps the signal well inside the audio bandpass of most radios.   

IL2P only uses the maximum FEC and Header Type 1. It is used to transport Level 3 Internet Protocol (IP). There is some Broadcast functionality for ARP, Node Identification, and multicast UDP. The KISS is limited to sending data, and control commands are masked off.   

The modem uses the ALSA Linux Soundcard 16-bit 2-channel PCM, at a fixed 9600 bit/s sample rate. The network interface uses a Linux pseudo-terminal running the KISS protocol. This interfaces to the kernel AX.25 using the ```kissattach``` program, making the modem routable over IP.   
#### Status
The missing piece of the puzzle, is a method of determining Data Carrier Detect (DCD). A squelch type circuit monitoring the receive audio is needed.

Ubuntu desktop is used for development. The PTT code is currently commented out to prevent core dumps, as the desktop doesn't have the GPIO, but the idea is to run this on a Linux microcontroller when fully developed.   

The GPIO will need PTT, DCD, Connect, and Sync as interface lines.   
#### Notes
The IL2P description says the idle symbols are supposed to be alternating ```0101``` binary during ```txdelay``` and ```txtail``` but this doesn't work for PSK, so we send '00' and '11' as 1200 Baud BPSK.   

```
+----------------------+-------------+---------------------+
| txdelay idle symbols | IL2P packet | txtail idle symbols |
+----------------------+-------------+---------------------+
   ramp-up transmitter     payload    ramp-down transmitter
```
The following shows a series of pings, with no station answering, and the beginning is extranoues output from the chrome browser over UDP:   

<img src="docs/time-domain.png" alt="time-domain"/>   

[Link to a recorded audio WAV file](docs/ipdata.wav)  

This has an exagerated ```txdelay``` and ```txtail``` which is the beeps you hear.

What the spectrum looks like, and you would use upper-sideband SSB mode:   

<img src="docs/actual-packet-data-spectrum.png" alt="actual spectrum" width="400"/>   

This is 1 kHz +/- 800 Hz or 1600 Hz bandwidth, so the emission symbol would be **1K60J2D**.
#### Startup
The ```ipnode``` program runs in a loop with three threads (tx, rx, and kiss). It will read the config file ```ipnode.conf``` if available, and begin running.
```
$ ipnode >>ipnode.log &
```
Using ax25-tools and ```kissattach``` command seems to work well as the pseudo-terminal kiss interface.

Example:

Create the ```/etc/ax25/axports``` file:
```
#
# The format of this file is:
# name callsign speed paclen window description
#
ip172 W1AW-10 0	256 7	ip172 port (1200 Baud QPSK)
```
To use the AX.25 pseudo-terminal:
```
$ sudo kissattach $(ls -l /tmp/kisstnc | awk '{print $NF}') ip172
$ sudo ip addr add 172.30.10.1/24 dev ax0
```
You should see a new interface called ```ax0``` which should look something like this:
```
$ ifconfig ax0
ax0: flags=67<UP,BROADCAST,RUNNING>  mtu 255
        inet 172.30.10.1  netmask 255.255.255.0  broadcast 0.0.0.0
        ax25 W1AW-10  txqueuelen 10  (AMPR AX.25)
        RX packets 0  bytes 0 (0.0 B)
        RX errors 0  dropped 0  overruns 0  frame 0
        TX packets 97  bytes 11112 (11.1 KB)
        TX errors 0  dropped 0 overruns 0  carrier 0  collisions 0
```
A routing table might look like this:
```
$ route -4n
Kernel IP routing table
Destination     Gateway         Genmask         Flags Metric Ref    Use Iface
0.0.0.0         192.168.1.1     0.0.0.0         UG    100    0        0 enp1s0
169.254.0.0     0.0.0.0         255.255.0.0     U     1000   0        0 enp1s0
172.30.10.0     0.0.0.0         255.255.255.0   U     0      0        0 ax0
192.168.1.0     0.0.0.0         255.255.255.0   U     100    0        0 enp1s0
```
When you are done, or you want to restart, you can delete the kissattach using:
```
sudo pkill -f "kissattach"
```
You can use ```wireshark``` to view the ```ax0``` interface.
```
Frame 1: 216 bytes on wire (1728 bits), 216 bytes captured (1728 bits)
KISS: Data frame, Port 0
    .... 0000 = Cmd: Data frame (0)
    0000 .... = Port: 0
AX.25, Src: W1AW-10, Dst: QST, Ver: V?.?
    Destination: QST
    Source: W1AW-10
    Control field: U, func=UI (0x03)
    Protocol ID: IP (0xcc)
Internet Protocol Version 4, Src: 172.30.10.1, Dst: 239.255.255.250
User Datagram Protocol, Src Port: 34392, Dst Port: 1900
Simple Service Discovery Protocol
```
#### FCC Enabling USA Regulations
##### § 97.115 Third party communications.
(c) No station may transmit third party communications while being automatically controlled ***except a station transmitting a data emission***.   
(d) At the end of an exchange of international third party communications, the station must also transmit in the station identification procedure the call sign of the station with which a third party message was exchanged.   
##### § 97.221 Automatically controlled digital station.   
(b) A station may be automatically controlled while transmitting a data emission on the ***28.120-28.189 MHz*** segment.   
##### § 97.307 Emission standards.
(f)(4) Only a data emission using a ```specified``` digital code may be transmitted. The symbol rate must not exceed ***1200 bauds***, or for ```frequency-shift keying```, the frequency shift between mark and space must not exceed 1 kHz. (not applicable for PSK)   
##### § 97.309 RTTY and data emission codes.
(b) A station may also transmit a data emission using an ***unspecified*** digital code, except to a station in a country with which the United States does not have an agreement permitting the code to be used.   
