#!/bin/bash

# back up original files
DIR="./net_backup"
if [ ! -d "$DIR" ] 
then
    echo "back up original files"
    mkdir -p net_backup/third_party/quiche/src/quic/core/crypto net_backup/third_party/quiche/src/quic/tools
    rsync ../../net/third_party/quiche/src/quic/core/quic_connection.* ../../net/third_party/quiche/src/quic/core/quic_one_block_arena.h ../../net/third_party/quiche/src/quic/core/quic_framer.* ../../net/third_party/quiche/src/quic/core/quic_path_validator.* ../../net/third_party/quiche/src/quic/core/quic_session.* ../../net/third_party/quiche/src/quic/core/quic_udp_socket_posix.cc net_backup/third_party/quiche/src/quic/core
    rsync ../../net/third_party/quiche/src/quic/core/crypto/tls_connection.* net_backup/third_party/quiche/src/quic/core/crypto
    rsync ../../net/third_party/quiche/src/quic/tools/quic_client_base.* ../../net/third_party/quiche/src/quic/tools/quic_toy_client.* ../../net/third_party/quiche/src/quic/tools/quic_client_epoll_network_helper.* net_backup/third_party/quiche/src/quic/tools
fi


# overwrite quic_handover_module
# echo "port quic_handover_module"
rsync ./net/third_party/quiche/src/quic/core/crypto/tls_connection.* ../../net/third_party/quiche/src/quic/core/crypto
rsync ./net/third_party/quiche/src/quic/core/quic_connection.* ./net/third_party/quiche/src/quic/core/quic_one_block_arena.h ./net/third_party/quiche/src/quic/core/quic_framer.* ./net/third_party/quiche/src/quic/core/quic_path_validator.* ./net/third_party/quiche/src/quic/core/quic_session.* ./net/third_party/quiche/src/quic/core/quic_udp_socket_posix.cc ../../net/third_party/quiche/src/quic/core
rsync ./net/third_party/quiche/src/quic/tools/quic_client_base.* ./net/third_party/quiche/src/quic/tools/quic_toy_client.* ./net/third_party/quiche/src/quic/tools/quic_client_epoll_network_helper.* ../../net/third_party/quiche/src/quic/tools
