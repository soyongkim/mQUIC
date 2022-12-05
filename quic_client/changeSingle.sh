#!/bin/bash
YAML=mQUIC/quic_client/settings.yaml ENV=default source mQUIC/quic_client/parse_yaml.sh
# iface1 : wlan0 / iface2: usb0
echo "$single_ssid1/$single_pass1 : $single_ssid2/$single_pass2" 
start=`date +%s.%N`

if [ $1 == 1 ]
then
    echo "Change to $single_ssid1"
    sudo nmcli dev wifi connect $single_ssid1 password $single_pass1
else
    echo "Change to $single_ssid2"
    sudo nmcli dev wifi connect $single_ssid2 password $single_pass2
fi

end=`date +%s.%N`
diff=$( echo "$end - $start" | bc -l )

echo $diff