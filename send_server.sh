#!/bin/bash

scp -P $2 -r ../out/Default/*.so ../out/Default/epoll_quic_server ../out/Default/epoll_quic_client quic_server_data quic_server.sh $1