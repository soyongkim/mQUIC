#!/bin/bash
scp -P $2 -r quic_server/* ../out/Default/*.so ../out/Default/epoll_quic_server $1