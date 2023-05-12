#!/bin/bash
YAML=mQUIC/quic_client/settings.yaml ENV=default source mQUIC/quic_client/parse_yaml.sh
# iface1 : wlan0 / iface2: usb0
# echo $iface1_name
# echo $iface1_host
# echo $iface1_gateway
# echo $iface2_name
# echo $iface2_host
# echo $iface2_gateway


# Wi-Fi -> cellular Handover
# start=`date +%s.%N`
# echo "[handover] $iface2_name($iface2_host) -> $iface1_name($iface1_host)"

# sudo iptables -A INPUT -i $iface2_name -j DROP &> /dev/null
# sudo ip addr del $iface2_host/24 dev $iface2_name
# echo "[handover] Release the IP used before handover : $iface2_name($iface2_host)"

# range=0
# random_delay=`echo "scale=3; ($(($RANDOM%31))+$range*100)/1000" | bc`
# # echo $random_delay
# sleep $random_delay

# sudo iptables -D INPUT -i $iface1_name -j DROP &> /dev/null
# sudo ip addr add $iface1_host/24 dev $iface1_name
# sudo route add default gw $iface1_gateway dev $iface1_name metric $1
# echo "[handover] Add new IP to use after handover $iface1_name($iface1_host)"

# end=`date +%s.%N`
# diff=$( echo "($end - $start)*1000" | bc -l )
# int=${diff%.*}
# echo "[handover] L2~L3 Handover complete - $int msec"
# echo $int >> ac_delay.txt



# --------------------------------------------------------------------------------------------------

# cellular -> Wi-Fi Handover
sudo iptables -D INPUT -i $iface1_name -j DROP &> /dev/null
sudo ip addr add $iface1_host/24 dev $iface1_name
sudo route add default gw $iface1_gateway dev $iface1_name metric $1
echo "[handover] Add new IP to use after handover $iface1_name($iface1_host)"

sudo iptables -A INPUT -i $iface2_name -j DROP &> /dev/null
sudo ip addr del $iface2_host/24 dev $iface2_name
echo "[handover] Release the IP used before handover $iface2_name($iface2_host)"