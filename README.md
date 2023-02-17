# mQUIC: Implementation

This is an implementation of the article titled 'mQUIC: Use of QUIC for Handover Support with Connection Migration in Wireless/Mobile Networks,' which explores the practical application of QUIC protocol for seamless handover support and connection migration in wireless and mobile networks.

The mQUIC(mobile QUIC) is designed with the following key requirements:

1) Ability to detect handover events within a reasonable time, without relying on underlying link-layer information;
2) Obtaining a new IP address through routing table lookup, instead of establishing a new connection;
3) Performing path validation and connection migration with the remote server using the new IP address;
4) Supporting both homogeneous handover within the same network and heterogeneous handover across different networks; and
5) Preserving the congestion window size of the existing path in the new network, if the newly visited network has better conditions, so as to improve throughput performance.

The mQUIC implementation was developed based on Chromium (quiche), and both the modified code from Chromium and a shell script file that can apply the code to the original Chromium (quiche) are being distributed together.

Moreover, the mQUIC implementation directly modifies the routing table to simulate handover scenarios. Therefore, to conduct a handover experiment, two NICs are required.

It has been confirmed that the mQUIC implementation runs on Ubuntu 20.04.4 LTS, and both the build and distribution are based on Linux.



## Building mQUIC

To build mQUIC, you need to first obtain the Chromium source code. You can download the Chromium source code through the link below. Due to its considerable size, it is recommended to make extra space in advance.

- https://chromium.googlesource.com/chromium/src/+/main/docs/linux/build_instructions.md

If you have completed the "Get the Code" step, depot_tools will be installed and the Chromium code will be downloaded. However, since the Chromium code is continually being modified, building the mQUIC implementation using the latest Chromium code may fail. Therefore, to return to the Chromium version used for mQUIC implementation, revert the code to the following commit point.

```bash
$ git checkout c91b87056
$ gclient sync
```

If you have successfully rolled back the Chromium code to the corresponding commit point, you can install mQUIC. Go to the "src" folder of the Chromium code and download the mQUIC code.

```bash
$ cd /path/to/chromium/src
$ git clone https://github.com/soyongkim/mQUIC.git
```

If you download mQUIC, a net folder will exist, which contains modified Chromium codes for mQUIC. To port this code to Chromium code, execute the script below.

```bash
$ cd mQUIC
$ bash port_module.sh
```

After porting the code, a net_backup folder is created, which contains copied original Chromium code. Now, you can build Chromium with mQUIC. The build is performed using the ninja build tool, and the out/Default folder is created through the -C option, under which the QUIC client and server will be built.

```bash
$ cd /path/to/chromium/src
$ ninja -C out/Default epoll_quic_client epoll_quic_server
```

If you want to revert back to the original Chromium code, you can execute the rollback.sh script file.

```bash
$ bash rollback.sh
```



## Run QUIC server

To conduct a handover experiment, the server needs to be transferred to a different Linux device. Transferring the server to a desired device can be easily done by running send_server.sh

send_server.sh moves the built mQUIC and necessary files for execution (cert, index files, etc.) and a script file to run the server. Note that if the desired folder name was changed using the -C option during ninja build, it must be changed to that folder name in send_server.sh.

send_server.sh was created based on scp tool. If you want to use a different tool, you can simply check the list of files in send_server.sh and use that tool.

```bash
$ cd mQUIC
$ bash send_server.sh "account@ip_address:/path/to/server" "Port"
```

If the file transfer is complete, run quic_server.sh on the Linux device. 

```bash
$ bash quic_server.sh 0
```

0 is an option that disables the function of maintaining the initial congestion window after the handover proposed by mQUIC. If changed to 1, this function can be activated

```bash
$ bash quic_server.sh 1
```



## Run QUIC client

Before running the client, prepare two NICs and first confirm that both NICs can use the network. Then record the information for the two NICs you want to use in the settings.yaml file and enter the IP information of the mQUIC server in "server"

```yaml
default:
  iface1:
    name: "wlanx"
    host: 0.0.0.0
    gateway: 0.0.0.0
  iface2:
    name: "wlany"
    host: 0.0.0.0
    gateway: 0.0.0.0
  server:
    name: "quic.smalldragon.net"
    host: 0.0.0.0
  single:
    ssid1: "ssid1"
    pass1: "password1"
    ssid2: "ssid2"
    pass2: "password2"
```

"single" is used to experiment with homogeneous handover using one NIC. With two routers prepared and SSID and password entered, the experiment can be started. Handover will be directly triggered by the client through a thread."

quic_cm.sh is a script that runs the client with the connection migration capability by applying mQUIC's handover detection technique. Execute it as follows.

```bash
$ bash quic_cm.sh [time | psn] [msec | EA] [number of handover] [start1 | start2 | start3 | start4] [number of requests] [number of testcases]
```

**[time | psn]** : time elapsed after requesting the handover occurrence time, or the amount of received packets, to determine the criteria for handover

**[msec | EA]** : If the previous option was set to 'time,' this value will trigger the handover in milliseconds at that point, and if it was set to 'psn,' the client will trigger the handover after receiving the specified number of packets.

**[number of handover]** : This indicates how many times handover will occur. If handover is triggered more than twice, and the option was set to 'time,' it will wait for the delay time after the first handover and then trigger it again. If the option was set to 'psn,' it will trigger handover when the same amount of packets have been received.

**[start1 | start2 | start3 | start4 ]** : The starting point of handover is determined on which interface to start. For 'start1,' it receives data on 'iface1' in 'settings.yaml' and triggers handover to 'iface2,' while for 'start2,' it receives data on 'iface2' and triggers handover to 'iface1.' 'Start3' and 'start4' trigger handover between 'SSID1' and 'SSID2' on a single interface.

**[number of requests]** : It determines how many times the client will perform data requests to the server.

**[number of testcases]** : It determines how many times to execute this script.

It can be used as an example like the following.

```bash
$ bash quic_cm.sh time 200 1 start1 1 1
```

The above command triggers handover from 'iface1' to 'iface2' once, 200ms after the data request. The script will be executed once for a total of one request. 

'quic_nc.sh' shows how handover is handled by establishing a new connection when triggered, without applying mQUIC techniques. Usage is the same as 'quic_cm.sh'



## Handover Simulate

In this testbed, the mQUIC client manipulates the routing table and iptables to simulate handover situations, and the client triggers the handover by executing the change1to2.sh and change2to1.sh files.

change1to2.sh file is used to trigger handover from iface1 to iface2 in settings.yaml, while change2to1.sh is used to trigger handover from iface2 to iface1. By using these files, it is possible to experiment with both heterogeneous and homogeneous handover situations

Heterogeneous handover can minimize handover delay because it occurs between two NICs. While continuing to receive data from the NIC that was being used before the handover, the new NIC can reduce the latency time by finding a new router and obtaining a new IP address. This situation is implemented using routing tables and iptables as follows.

```bash
# Heterogeneous Handover in change1to2.sh
sudo iptables -D INPUT -i $iface2_name -j DROP &> /dev/null
sudo ip addr add $iface2_host/24 dev $iface2_name
sudo route add default gw $iface2_gateway dev $iface2_name metric $1
echo "[handover] Add new IP to use after handover $iface2_name($iface2_host)"

sudo iptables -A INPUT -i $iface1_name -j DROP &> /dev/null
sudo ip addr del $iface1_host/24 dev $iface1_name
echo "[handover] Release the IP used before handover $iface1_name($iface1_host)"
```

Although there is a way to bring down the interface using ifconfig, it is not recommended as it causes significant delays due to the need to repeatedly activate and deactivate the interface in repeated experiments.

Homogeneous handover cannot apply the technique mentioned earlier because it uses only one NIC. Therefore, since the handover delay can vary, it is configured to test by handover delay range as shown below. By changing the range, the handover delay range can be changed in units of 100ms.

```bash
# Homogeneous Handover in change1to2.sh
start=`date +%s.%N`
echo "$[handover] iface1_name($iface1_host) -> $iface2_name($iface2_host)"

sudo iptables -A INPUT -i $iface1_name -j DROP &> /dev/null
sudo ip addr del $iface1_host/24 dev $iface1_name
echo "[handover] Release the IP used before handover $iface1_name($iface1_host)"

# Random handover delay
range=2
random_delay=`echo "scale=3; ($(($RANDOM%31))+$range*100)/1000" | bc`
sleep $random_delay

sudo iptables -D INPUT -i $iface2_name -j DROP &> /dev/null
sudo ip addr add $iface2_host/24 dev $iface2_name
sudo route add default gw $iface2_gateway dev $iface2_name metric $1
echo "[handover] Add new IP to use after handover $iface2_name($iface2_host)"

end=`date +%s.%N`
diff=$( echo "($end - $start)*1000" | bc -l )
int=${diff%.*}
echo "[handover] Handover complete - $int msec"
echo $int >> ac_delay.txt
```

If you want to perform a real homogeneous handover experiment using only one NIC, you can use the changeSingle.sh by specifying the SSID through the command line using the start3 or start4 option when running the client. changeSingle.sh can perform handover to a router with the specified SSID using the nmcli tool via the command line. However, it is not recommended for repeat experiments because, based on this testbed, it takes about 9-10 seconds to consume the handover delay.

## Execution

After performing the above steps, you can see that the system behaves as shown in the following figure.

![mquic_client](./.assets/mquic_client.gif)
