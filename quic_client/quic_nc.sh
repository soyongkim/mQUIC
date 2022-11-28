#sudo tc qdisc del dev wlan0 root netem
#sleep 0.3
#sudo tc qdisc add dev wlan0 root netem delay $5ms loss $6%

# #!/bin/bash
YAML=settings.yaml ENV=default source parse_yaml.sh
echo $iface1_name
echo $iface1_host
echo $iface1_gateway
echo $iface2_name
echo $iface2_host
echo $iface2_gateway
echo $server_name
echo $server_host

for((var=1;var<=$5;var++))
do
    if [ $3 == "start1" ]
    then
        # iface1 시작 설정
        echo "$iface1_name($iface1_host) Start"
        sudo ip addr add $iface1_host/24 dev $iface1_name
        sudo ip addr del $iface2_host/24 dev $iface2_name
        sudo route del default dev $iface1_name
        sudo route del default dev $iface1_name
        sudo route del default dev $iface2_name
        sudo route del default dev $iface2_name
        sudo route add default gw $iface1_gateway dev $iface1_name metric 101
        sleep 1
        # iface1에서 iface2로 바뀜
        echo "Connection Migration"
        ./out/Debug/epoll_quic_client --disable_certificate_verification --host=$server_host --port=6121 --multi_packet_chlo --enable_cm --ho_mode=client_pc --start_iface=1 --ho_num=$2 --ho_interval=$1 --num_requests=$4 --disable_port_changes --enable_tracker --quiet $server_name
    elif [ $3 == "start2" ]
    then
        echo "$iface2_name($iface2_host) Start"
        sudo ip addr add $iface2_host/24 dev $iface2_name
        sudo ip addr del $iface1_host/24 dev $iface1_name
        sudo route del default dev $iface1_name
        sudo route del default dev $iface1_name
        sudo route del default dev $iface2_name
        sudo route del default dev $iface2_name
        sudo route add default gw $iface2_gateway dev $iface2_name metric 101
        sleep 1   
        echo "Connection Migration"
        ./out/Debug/epoll_quic_client --disable_certificate_verification --host=$server_host --port=6121 --multi_packet_chlo --enable_cm --ho_mode=client_pc --start_iface=2 --ho_num=$2 --ho_interval=$1 --num_requests=$4 --disable_port_changes --enable_tracker --quiet $server_name
    elif [ $3 == "start3" ]
    then
        echo "$horizon_ssid1 Start"
        #bash changeHorison.sh 1
        sleep 1   
        echo "Connection Migration"
        ./out/Debug/epoll_quic_client --disable_certificate_verification --host=$server_host --port=6121 --multi_packet_chlo --enable_cm --ho_mode=client_pc --start_iface=4 --ho_num=$2 --ho_interval=$1 --num_requests=$4 --disable_port_changes --quiet $server_name
    else
        echo "$horizon_ssid2 Start"
        #bash changeHorison.sh 2
        sleep 1   
        echo "Connection Migration"
        ./out/Debug/epoll_quic_client --disable_certificate_verification --host=$server_host --port=6121 --multi_packet_chlo --enable_cm --ho_mode=client_pc --start_iface=3 --ho_num=$2 --ho_interval=$1 --num_requests=$4 --disable_port_changes --quiet $server_name
    fi
    if [ $var != $5 ]
    then
        sleep 2
    fi
done