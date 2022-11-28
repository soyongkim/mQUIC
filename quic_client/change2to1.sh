#!/bin/bash
YAML=chromium_quic_handover/quic_client/settings.yaml ENV=default source chromium_quic_handover/quic_client/parse_yaml.sh
# iface1 : wlan0 / iface2: usb0
# echo $iface1_name
# echo $iface1_host
# echo $iface1_gateway
# echo $iface2_name
# echo $iface2_host
# echo $iface2_gateway

echo "$iface2_name($iface2_host) -> $iface1_name($iface1_host)"
sudo ip addr del $iface2_host/24 dev $iface2_name
echo "Del $iface2_name($iface2_host)"

sleep 6

sudo ip addr add $iface1_host/24 dev $iface1_name
sudo route add default gw $iface1_gateway dev $iface1_name metric 101
echo "Add IP $iface1_name($iface1_host)"



# sudo ip addr add $iface1_host/24 dev $iface1_name
# sudo route add default gw $iface1_gateway dev $iface1_name metric 150
# echo "Add $iface1_name($iface1_host)"

# # sleep 0.1

# # sleep 0.3
# sudo ip addr del $iface2_host/24 dev $iface2_name
# echo "Del IP $iface2_name($iface2_host)"