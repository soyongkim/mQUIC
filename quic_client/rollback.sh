#!/bin/bash

DIR="./net_backup"
if [ -d "$DIR" ] 
then
    echo "Roll back original files"
    rsync ./net_backup/third_party/quiche/src/quic/core/crypto/tls_connection.* ../net/third_party/quiche/src/quic/core/crypto
    rsync ./net_backup/third_party/quiche/src/quic/core/quic_connection.* ./net_backup/third_party/quiche/src/quic/core/quic_one_block_arena.h ./net_backup/third_party/quiche/src/quic/core/quic_framer.* ./net_backup/third_party/quiche/src/quic/core/quic_path_validator.* ./net_backup/third_party/quiche/src/quic/core/quic_session.* ./net_backup/third_party/quiche/src/quic/core/quic_udp_socket_posix.cc ../net/third_party/quiche/src/quic/core
    rsync ./net_backup/third_party/quiche/src/quic/tools/quic_client_base.* ./net_backup/third_party/quiche/src/quic/tools/quic_toy_client.* ./net_backup/third_party/quiche/src/quic/tools/quic_client_epoll_network_helper.* ../net/third_party/quiche/src/quic/tools
fi