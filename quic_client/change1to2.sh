#!/bin/bash
YAML=mQUIC/quic_client/settings.yaml ENV=default source mQUIC/quic_client/parse_yaml.sh
# iface1 : wlan0 / iface2: usb0
# echo $iface1_name
# echo $iface1_host
# echo $iface1_gateway
# echo $iface2_name
# echo $iface2_host
# echo $iface2_gateway

echo "$iface1_name($iface1_host) -> $iface2_name($iface2_host)"
# sudo ip addr del $iface1_host/24 dev $iface1_name
# sudo route del default dev $iface1_name

# # sleep 2

# sudo ip addr add $iface2_host/24 dev $iface2_name
# sudo route add default gw $iface2_gateway dev $iface2_name metric 101


sudo ip addr add $iface2_host/24 dev $iface2_name
sudo route add default gw $iface2_gateway dev $iface2_name metric $1
echo "Add $iface2_name($iface2_host)"

# sleep 0.1

# sleep 0.3
sudo ip addr del $iface1_host/24 dev $iface1_name
echo "Del IP $iface1_name($iface1_host)"





