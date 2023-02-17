#!/bin/bash

if [ -z "$6" ]
    then
        echo "usage: ./quic_nc.sh [time | psn] [msec | EA] [number of handover] [start1 | start2 | start3 | start4] [number of requests] [number of testcases]"
        echo "example: ./quic_nc.sh time 300 1 start1 1 1"
        exit
fi

if [ $1 != "time" ] && [ $1 != "psn" ]
    then
    echo "usage: ./quic_nc.sh [[time | psn]] [msec | EA] [number of handover] [start1 | start2 | start3 | start4] [number of requests] [number of testcases]"
    exit
fi

if [ $4 != "start1" ] && [ $4 != "start2" ] && [ $4 != "start3" ] && [ $4 != "start4" ]
    then
    echo "usage: ./quic_nc.sh [time | psn] [msec | EA] [number of handover] [[start1 | start2 | start3 | start4]] [number of requests] [number of testcases]"
    exit
fi


YAML=settings.yaml ENV=default source parse_yaml.sh
echo $iface1_name
echo $iface1_host
echo $iface1_gateway
echo $iface2_name
echo $iface2_host
echo $iface2_gateway
echo $server_name
echo $server_host

echo "New Connection"
cd ../..
for((var=1;var<=$6;var++))
do
    sudo iptables -D INPUT -i $iface1_name -j DROP &> /dev/null
    sudo iptables -D INPUT -i $iface2_name -j DROP &> /dev/null
    if [ $4 == "start1" ]
    then
        # iface1 시작 설정
        echo "$iface1_name($iface1_host) Start"
        sudo ip addr add $iface1_host/24 dev $iface1_name
        sudo ip addr del $iface2_host/24 dev $iface2_name
        sudo route del default dev $iface1_name &> /dev/null
        sudo route del default dev $iface1_name &> /dev/null
        sudo route del default dev $iface2_name &> /dev/null
        sudo route del default dev $iface2_name &> /dev/null
        sudo route add default gw $iface1_gateway dev $iface1_name metric 101
        sleep 1
        # iface1에서 iface2로 바뀜
        ./out/Debug/epoll_quic_client --disable_certificate_verification --host=$server_host --port=6121 --multi_packet_chlo --start_iface=1 --ho_num=$3 --ho_case=$1 --ho_interval=$2 --num_requests=$5 --disable_port_changes --quiet $server_name
    elif [ $4 == "start2" ]
    then
        echo "$iface2_name($iface2_host) Start"
        sudo ip addr add $iface2_host/24 dev $iface2_name
        sudo ip addr del $iface1_host/24 dev $iface1_name
        sudo route del default dev $iface1_name &> /dev/null
        sudo route del default dev $iface1_name &> /dev/null
        sudo route del default dev $iface2_name &> /dev/null
        sudo route del default dev $iface2_name &> /dev/null
        sudo route add default gw $iface2_gateway dev $iface2_name metric 101
        sleep 1   
        ./out/Debug/epoll_quic_client --disable_certificate_verification --host=$server_host --port=6121 --multi_packet_chlo --start_iface=2 --ho_num=$3 --ho_interval=$2 --num_requests=$5 --disable_port_changes --ho_case=$1 --quiet $server_name
    elif [ $4 == "start3" ]
    then
        echo "$horizon_ssid1 Start"
        #bash changeHorison.sh 1
        sleep 1   
        ./out/Debug/epoll_quic_client --disable_certificate_verification --host=$server_host --port=6121 --multi_packet_chlo --start_iface=4 --ho_num=$3 --ho_interval=$2 --num_requests=$5 --disable_port_changes --ho_case=$1 --quiet $server_name
    else
        echo "$horizon_ssid2 Start"
        #bash changeHorison.sh 2
        sleep 1   
        ./out/Debug/epoll_quic_client --disable_certificate_verification --host=$server_host --port=6121 --multi_packet_chlo --start_iface=3 --ho_num=$3 --ho_interval=$2 --num_requests=$5 --disable_port_changes --ho_case=$1 --quiet $server_name
    fi
    if [ $var != $6 ]
    then
        sleep 2
    fi
    sudo iptables -D INPUT -i $iface1_name -j DROP &> /dev/null
    sudo iptables -D INPUT -i $iface2_name -j DROP &> /dev/null
done