// Copyright (c) 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quic/tools/quic_client_base.h"

#include <algorithm>
#include <memory>
#include <iostream>
#include <chrono>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <ifaddrs.h>
#include <stdlib.h>
#include <net/route.h>
#include <sys/types.h>
#include <sys/ioctl.h>

#include "quic/core/crypto/quic_random.h"
#include "quic/core/http/spdy_utils.h"
#include "quic/core/quic_packet_writer.h"
#include "quic/core/quic_path_validator.h"
#include "quic/core/quic_server_id.h"
#include "quic/core/quic_utils.h"
#include "quic/platform/api/quic_flags.h"
#include "quic/platform/api/quic_logging.h"

namespace quic {
// [SD] Address Change Alarm
// class AddressChangeDelegate : public QuicAlarm::Delegate {
//   public:
//     explicit AddressChangeDelegate(QuicClientBase* client_base)
//       : client_base_(client_base) {}
//     AddressChangeDelegate(const AddressChangeDelegate&) = delete;
//     AddressChangeDelegate& operator=(const AddressChangeDelegate&) = delete;

//   void OnAlarm() override {
//     client_base_->ValidateAndMigrateSocket(client_base_->ToIp());
//   }

//   QuicConnectionContext* GetConnectionContext() override {
//     return (client_base_->session()->connection() == nullptr) ? nullptr : client_base_->session()->connection()->context(); 
//   }

//   private:
//     QuicClientBase* client_base_;
// };

// A path context which owns the writer.
class QUIC_EXPORT_PRIVATE PathMigrationContext
    : public QuicPathValidationContext {
 public:
  PathMigrationContext(std::unique_ptr<QuicPacketWriter> writer,
                       const QuicSocketAddress& self_address,
                       const QuicSocketAddress& peer_address)
      : QuicPathValidationContext(self_address, peer_address),
        alternative_writer_(std::move(writer)) {}

  QuicPacketWriter* WriterToUse() override { return alternative_writer_.get(); }

  QuicPacketWriter* ReleaseWriter() { return alternative_writer_.release(); }

 private:
  std::unique_ptr<QuicPacketWriter> alternative_writer_;
};

// Implements the basic feature of a result delegate for path validation for
// connection migration. If the validation succeeds, migrate to the alternative
// path. Otherwise, stay on the current path.
class QuicClientSocketMigrationValidationResultDelegate
    : public QuicPathValidator::ResultDelegate {
 public:
  QuicClientSocketMigrationValidationResultDelegate(QuicClientBase* client)
      : QuicPathValidator::ResultDelegate(), client_(client) {}

  // QuicPathValidator::ResultDelegate
  // Overridden to start migration and takes the ownership of the writer in the
  // context.
  void OnPathValidationSuccess(
      std::unique_ptr<QuicPathValidationContext> context) override {
    QUIC_DLOG(INFO) << "Successfully validated path from " << *context
                    << ". Migrate to it now.";
    auto migration_context = std::unique_ptr<PathMigrationContext>(
        static_cast<PathMigrationContext*>(context.release()));
    //std::cout << "[quic_client_base] check self_address : " << migration_context->self_address() << std::endl;
    client_->session()->MigratePath(
        migration_context->self_address(), migration_context->peer_address(),
        migration_context->WriterToUse(), /*owns_writer=*/false);
    QUICHE_DCHECK(migration_context->WriterToUse() != nullptr);
    // Hand the ownership of the alternative writer to the client.
    client_->set_writer(migration_context->ReleaseWriter());
  }

  void OnPathValidationFailure(
      std::unique_ptr<QuicPathValidationContext> context) override {
    QUIC_LOG(WARNING) << "Fail to validate path " << *context
                      << ", stop migrating.";
    client_->session()->connection()->OnPathValidationFailureAtClient();
  }

 private:
  QuicClientBase* client_;
};

QuicClientBase::NetworkHelper::~NetworkHelper() = default;

QuicClientBase::QuicClientBase(
    const QuicServerId& server_id,
    const ParsedQuicVersionVector& supported_versions,
    const QuicConfig& config,
    QuicConnectionHelperInterface* helper,
    QuicAlarmFactory* alarm_factory,
    std::unique_ptr<NetworkHelper> network_helper,
    std::unique_ptr<ProofVerifier> proof_verifier,
    std::unique_ptr<SessionCache> session_cache)
    : server_id_(server_id),
      initialized_(false),
      local_port_(0),
      config_(config),
      crypto_config_(std::move(proof_verifier), std::move(session_cache)),
      helper_(helper),
      alarm_factory_(alarm_factory),
      supported_versions_(supported_versions),
      initial_max_packet_length_(0),
      num_sent_client_hellos_(0),
      connection_error_(QUIC_NO_ERROR),
      connected_or_attempting_connect_(false),
      network_helper_(std::move(network_helper)),
      connection_debug_visitor_(nullptr),
      server_connection_id_length_(kQuicDefaultConnectionIdLength),
      client_connection_id_length_(0) {
        //address_change_alarm_ = absl::WrapUnique<QuicAlarm>(alarm_factory_->CreateAlarm(new AddressChangeDelegate(this)));
      }

QuicClientBase::~QuicClientBase() = default;

// void QuicClientBase::SetAddressChangeAlarm() {
//   if(!connected()) {
//     std::cout << "Not yet connected.." << std::endl;
//     if(address_change_alarm_->IsSet()) {
//       address_change_alarm_->Cancel();
//     }
//     return;
//   }

//   // address_change_alarm_->Update(session_->connection()->clock()->ApproximateNow() + 
//   //                                 QuicTime::Delta::FromMilliseconds(50),
//   //                                 QuicTime::Delta::FromMilliseconds(5000));
//   std::cout << "[quic_client_base] alarm - " << std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count() - session_->connection()->GetHandoverStart() << " msec" << std::endl;

//   address_change_alarm_->Set(session_->connection()->clock()->ApproximateNow());
// }

QuicIpAddress IfaceToIp(std::string iface) {
  struct ifaddrs *ifap, *ifa;
  struct sockaddr_in *sa;
  QuicIpAddress ip = QuicIpAddress::Any4();

  getifaddrs(&ifap);
  for(ifa = ifap; ifa; ifa = ifa->ifa_next) {
    if(ifa->ifa_addr && ifa->ifa_addr->sa_family == AF_INET) {
      if(strcmp(ifa->ifa_name, iface.c_str()) == 0) {
        sa = (struct sockaddr_in *) ifa->ifa_addr;
        ip = QuicIpAddress(sa->sin_addr);
        break;
      }
    }
  }
  freeifaddrs(ifap);
  return ip;
}

// routing table search
int QuicClientBase::OnNetworkUnreachable() {
  // Find my new path address
  FILE* rt_fp;
  in_addr gway;
  in_addr_t dest, mask;
  int flags, refcnt, use, metric, mtu, win, irtt;
  char new_iface[64];
  QuicIpAddress newIP, newGateway;

  newIP = QuicIpAddress();
  newGateway = QuicIpAddress();

  //std::cout << "[quic_toy_client] Searching... - " << timeStamp() - session()->connection()->GetHandoverStart() << " msec" << std::endl;
  uint64_t search_start = timeStamp();
  while(timeStamp() - search_start < 10) {
    if((rt_fp = fopen("/proc/net/route", "r")) == NULL) {
        perror("file open error");
        return false;
      }

      if (fscanf(rt_fp, "%*[^\n]\n") < 0) {
          fclose(rt_fp);
          return false;
      }

      int nread = fscanf(rt_fp, "%63s%X%X%X%d%d%d%X%d%d%d\n",
                        new_iface, &dest, &gway.s_addr, &flags, &refcnt, &use, &metric, &mask,
                        &mtu, &win, &irtt);
      if (nread != 11) {
          break;
      }
      fclose(rt_fp);

      if ((flags & (RTF_UP|RTF_GATEWAY)) == (RTF_UP|RTF_GATEWAY) && dest == 0) {
        // found new path address
        newIP = QuicIpAddress(IfaceToIp(std::string(new_iface)));
        newGateway = QuicIpAddress(gway);
        if(newGateway != current_path_gateway_) {
          break;
        }
      }
  }

  if(newGateway.ToString() == "") {
    std::cout << "[quic_client_base] There is no avaliable path" << std::endl;
    return 0;
  }

  if(!current_path_gateway_.IsInitialized()) {
    std::cout << "[quic_client_base] set current path gateway with ip" << std::endl;
    current_path_gateway_ = newGateway;
    current_path_ip_ = newIP;
    return 0;
  }

  if(newGateway == current_path_gateway_) {
    std::cout << "[quic_client_base] Same Gateway, so check IP. new: " << newGateway << " / cur: " << current_path_gateway_ << std::endl;

    if(newIP == current_path_ip_) {
      std::cout << "[quic_client_base] Not Changed IP new: " << newGateway << " / cur: " << current_path_gateway_ << std::endl;
      return 1;
    }
  }

  if(!connected()) {
    return 0;
  }

  std::cout << "[quic_client_base] Detected network change and Start connection migration to " << new_iface << " - " << timeStamp() - session()->connection()->GetHandoverStart() << " msec" << std::endl;
  // [SD] migration start
  current_path_gateway_ = newGateway;
  current_path_ip_ = newIP;
  std::cout << "[quic_client_base] validate and migration - " << 
      timeStamp() - session()->connection()->GetHandoverStart() << " msec" << std::endl;
    //ValidateAndMigrateSocket(QuicIpAddress::Any4());
  ValidateAndMigrateSocket(newIP);
  return 2;
}

// bool QuicClientBase::OnNetworkUnreachable() {
//   if(!connected()) {
//     return false;
//   }

//   // [SD] migration start
//   std::cout << "[quic_client_base] validate and migration - " << 
//     timeStamp() - session()->connection()->GetHandoverStart() << " msec" << std::endl;
//   return ValidateAndMigrateSocket(QuicIpAddress::Any4());
// }

bool QuicClientBase::Initialize() {
  num_sent_client_hellos_ = 0;
  connection_error_ = QUIC_NO_ERROR;
  connected_or_attempting_connect_ = false;

  // If an initial flow control window has not explicitly been set, then use the
  // same values that Chrome uses.
  const uint32_t kSessionMaxRecvWindowSize = 15 * 1024 * 1024;  // 15 MB
  const uint32_t kStreamMaxRecvWindowSize = 6 * 1024 * 1024;    //  6 MB
  if (config()->GetInitialStreamFlowControlWindowToSend() ==
      kDefaultFlowControlSendWindow) {
    config()->SetInitialStreamFlowControlWindowToSend(kStreamMaxRecvWindowSize);
  }
  if (config()->GetInitialSessionFlowControlWindowToSend() ==
      kDefaultFlowControlSendWindow) {
    config()->SetInitialSessionFlowControlWindowToSend(
        kSessionMaxRecvWindowSize);
  }

  if (!network_helper_->CreateUDPSocketAndBind(server_address_,
                                               bind_to_address_, local_port_)) {
    return false;
  }

  initialized_ = true;
  return true;
}

bool QuicClientBase::Connect() {
  // Attempt multiple connects until the maximum number of client hellos have
  // been sent.
  int num_attempts = 0;
  while (!connected() &&
         num_attempts <= QuicCryptoClientStream::kMaxClientHellos) {
    StartConnect();
    while (EncryptionBeingEstablished()) {
      WaitForEvents();
    }
    ParsedQuicVersion version = UnsupportedQuicVersion();
    if (session() != nullptr && !CanReconnectWithDifferentVersion(&version)) {
      // We've successfully created a session but we're not connected, and we
      // cannot reconnect with a different version.  Give up trying.
      break;
    }
    num_attempts++;
  }
  if (session() == nullptr) {
    QUIC_BUG(quic_bug_10906_1) << "Missing session after Connect";
    return false;
  }
  return session()->connection()->connected();
}

void QuicClientBase::StartConnect() {
  QUICHE_DCHECK(initialized_);
  QUICHE_DCHECK(!connected());
  QuicPacketWriter* writer = network_helper_->CreateQuicPacketWriter();
  ParsedQuicVersion mutual_version = UnsupportedQuicVersion();
  const bool can_reconnect_with_different_version =
      CanReconnectWithDifferentVersion(&mutual_version);
  if (connected_or_attempting_connect()) {
    // Clear queued up data if client can not try to connect with a different
    // version.
    if (!can_reconnect_with_different_version) {
      ClearDataToResend();
    }
    // Before we destroy the last session and create a new one, gather its stats
    // and update the stats for the overall connection.
    UpdateStats();
  }

  const quic::ParsedQuicVersionVector client_supported_versions =
      can_reconnect_with_different_version
          ? ParsedQuicVersionVector{mutual_version}
          : supported_versions();

  session_ = CreateQuicClientSession(
      client_supported_versions,
      new QuicConnection(GetNextConnectionId(), QuicSocketAddress(),
                         server_address(), helper(), alarm_factory(), writer,
                         /* owns_writer= */ false, Perspective::IS_CLIENT,
                         client_supported_versions));
  if (can_reconnect_with_different_version) {
    session()->set_client_original_supported_versions(supported_versions());
  }
  if (connection_debug_visitor_ != nullptr) {
    session()->connection()->set_debug_visitor(connection_debug_visitor_);
  }
  session()->connection()->set_client_connection_id(GetClientConnectionId());
  if (initial_max_packet_length_ != 0) {
    session()->connection()->SetMaxPacketLength(initial_max_packet_length_);
  }
  // [SD] register client base visitor
  session()->connection()->set_client_base_visitor(this);
  // [SD] set current default gateway
  OnNetworkUnreachable();
  // Reset |writer()| after |session()| so that the old writer outlives the old
  // session.
  set_writer(writer);
  std::cout << "[quic_client_base] Start Connect" << std::endl;
  InitializeSession();
  if (can_reconnect_with_different_version) {
    // This is a reconnect using server supported |mutual_version|.
    session()->connection()->SetVersionNegotiated();
  }
  set_connected_or_attempting_connect(true);
}

void QuicClientBase::InitializeSession() {
  // [SD} quic_spdy_client_base에서 오버라이딩을 해버림. 그래서 얘가 호출이 안됨
  session()->Initialize();
}

void QuicClientBase::Disconnect() {
  QUICHE_DCHECK(initialized_);

  initialized_ = false;
  if (connected()) {
    session()->connection()->CloseConnection(
        QUIC_PEER_GOING_AWAY, "Client disconnecting",
        ConnectionCloseBehavior::SEND_CONNECTION_CLOSE_PACKET);
  }

  ClearDataToResend();
  current_path_gateway_ = QuicIpAddress();

  network_helper_->CleanUpAllUDPSockets();
}

ProofVerifier* QuicClientBase::proof_verifier() const {
  return crypto_config_.proof_verifier();
}

bool QuicClientBase::EncryptionBeingEstablished() {
  return !session_->IsEncryptionEstablished() &&
         session_->connection()->connected();
}

bool QuicClientBase::WaitForEvents() {
  if (!connected()) {
    QUIC_BUG(quic_bug_10906_2)
        << "Cannot call WaitForEvents on non-connected client";
    std::cout << "[quic_client_base] connection false!" << std::endl;
    return false;
  }

  network_helper_->RunEventLoop();

  QUICHE_DCHECK(session() != nullptr);
  ParsedQuicVersion version = UnsupportedQuicVersion();
  if (!connected() && CanReconnectWithDifferentVersion(&version)) {
    QUIC_DLOG(INFO) << "Can reconnect with version: " << version
                    << ", attempting to reconnect.";

    Connect();
  }
  return HasActiveRequests();
}

bool QuicClientBase::MigrateSocket(const QuicIpAddress& new_host) {
  return MigrateSocketWithSpecifiedPort(new_host, local_port_);
}

bool QuicClientBase::MigrateSocketWithSpecifiedPort(
    const QuicIpAddress& new_host,
    int port) {
  if (!connected()) {
    QUICHE_DVLOG(1)
        << "MigrateSocketWithSpecifiedPort failed as connection has closed";
    return false;
  }

  network_helper_->CleanUpAllUDPSockets();
  std::unique_ptr<QuicPacketWriter> writer =
      CreateWriterForNewNetwork(new_host, port);
  if (writer == nullptr) {
    QUICHE_DVLOG(1)
        << "MigrateSocketWithSpecifiedPort failed from writer creation";
    return false;
  }
  if (!session()->MigratePath(network_helper_->GetLatestClientAddress(),
                              session()->connection()->peer_address(),
                              writer.get(), false)) {
    QUICHE_DVLOG(1)
        << "MigrateSocketWithSpecifiedPort failed from session()->MigratePath";
    return false;
  }
  set_writer(writer.release());
  //std::cout << "[quic_client_base] migrating..." << std::endl;
  return true;
}

bool QuicClientBase::ValidateAndMigrateSocket(const QuicIpAddress& new_host) {
  QUICHE_DCHECK(VersionHasIetfQuicFrames(
                    session_->connection()->version().transport_version) &&
                session_->connection()->use_path_validator());
  if (!connected()) {
    return false;
  }

  std::unique_ptr<QuicPacketWriter> writer =
      CreateWriterForNewNetwork(new_host, local_port_);

  if (writer == nullptr) {
    return false;
  }
  // Asynchronously start migration.
  session_->ValidatePath(
      std::make_unique<PathMigrationContext>(
          std::move(writer), network_helper_->GetLatestClientAddress(),
          session_->peer_address()),
      std::make_unique<QuicClientSocketMigrationValidationResultDelegate>(
          this));
  return true;
}

std::unique_ptr<QuicPacketWriter> QuicClientBase::CreateWriterForNewNetwork(
    const QuicIpAddress& new_host,
    int port) {
  // [SD] Test bind to address
  set_bind_to_address(new_host);
  set_local_port(port);
  if (!network_helper_->CreateUDPSocketAndBind(server_address_,
                                               bind_to_address_, port)) {
    return nullptr;
  }

  QuicPacketWriter* writer = network_helper_->CreateQuicPacketWriter();
  QUIC_LOG_IF(WARNING, writer == writer_.get())
      << "The new writer is wrapped in the same wrapper as the old one, thus "
         "appearing to have the same address as the old one.";
  return std::unique_ptr<QuicPacketWriter>(writer);
}

bool QuicClientBase::ChangeEphemeralPort() {
  auto current_host = network_helper_->GetLatestClientAddress().host();
  return MigrateSocketWithSpecifiedPort(current_host, 0 /*any ephemeral port*/);
}

QuicSession* QuicClientBase::session() {
  return session_.get();
}

const QuicSession* QuicClientBase::session() const {
  return session_.get();
}

QuicClientBase::NetworkHelper* QuicClientBase::network_helper() {
  return network_helper_.get();
}

const QuicClientBase::NetworkHelper* QuicClientBase::network_helper() const {
  return network_helper_.get();
}

void QuicClientBase::WaitForStreamToClose(QuicStreamId id) {
  if (!connected()) {
    QUIC_BUG(quic_bug_10906_3)
        << "Cannot WaitForStreamToClose on non-connected client";
    return;
  }

  while (connected() && !session_->IsClosedStream(id)) {
    WaitForEvents();
  }
}

bool QuicClientBase::WaitForOneRttKeysAvailable() {
  if (!connected()) {
    QUIC_BUG(quic_bug_10906_4)
        << "Cannot WaitForOneRttKeysAvailable on non-connected client";
    return false;
  }

  while (connected() && !session_->OneRttKeysAvailable()) {
    WaitForEvents();
  }

  // If the handshake fails due to a timeout, the connection will be closed.
  QUIC_LOG_IF(ERROR, !connected()) << "Handshake with server failed.";
  return connected();
}

bool QuicClientBase::WaitForHandshakeConfirmed() {
  if (!session_->connection()->version().UsesTls()) {
    return WaitForOneRttKeysAvailable();
  }
  // Otherwise, wait for receipt of HANDSHAKE_DONE frame.
  while (connected() && session_->GetHandshakeState() < HANDSHAKE_CONFIRMED) {
    WaitForEvents();
  }

  // If the handshake fails due to a timeout, the connection will be closed.
  QUIC_LOG_IF(ERROR, !connected()) << "Handshake with server failed.";
  return connected();
}

bool QuicClientBase::connected() const {
  return session_.get() && session_->connection() &&
         session_->connection()->connected();
}

bool QuicClientBase::goaway_received() const {
  return session_ != nullptr && session_->transport_goaway_received();
}

int QuicClientBase::GetNumSentClientHellos() {
  // If we are not actively attempting to connect, the session object
  // corresponds to the previous connection and should not be used.
  const int current_session_hellos = !connected_or_attempting_connect_
                                         ? 0
                                         : GetNumSentClientHellosFromSession();
  return num_sent_client_hellos_ + current_session_hellos;
}

void QuicClientBase::UpdateStats() {
  num_sent_client_hellos_ += GetNumSentClientHellosFromSession();
}

int QuicClientBase::GetNumReceivedServerConfigUpdates() {
  // If we are not actively attempting to connect, the session object
  // corresponds to the previous connection and should not be used.
  return !connected_or_attempting_connect_
             ? 0
             : GetNumReceivedServerConfigUpdatesFromSession();
}

QuicErrorCode QuicClientBase::connection_error() const {
  // Return the high-level error if there was one.  Otherwise, return the
  // connection error from the last session.
  if (connection_error_ != QUIC_NO_ERROR) {
    return connection_error_;
  }
  if (session_ == nullptr) {
    return QUIC_NO_ERROR;
  }
  return session_->error();
}

QuicConnectionId QuicClientBase::GetNextConnectionId() {
  return GenerateNewConnectionId();
}

QuicConnectionId QuicClientBase::GenerateNewConnectionId() {
  return QuicUtils::CreateRandomConnectionId(server_connection_id_length_);
}

QuicConnectionId QuicClientBase::GetClientConnectionId() {
  return QuicUtils::CreateRandomConnectionId(client_connection_id_length_);
}

bool QuicClientBase::CanReconnectWithDifferentVersion(
    ParsedQuicVersion* version) const {
  if (session_ == nullptr || session_->connection() == nullptr ||
      session_->error() != QUIC_INVALID_VERSION) {
    return false;
  }

  const auto& server_supported_versions =
      session_->connection()->server_supported_versions();
  if (server_supported_versions.empty()) {
    return false;
  }

  for (const auto& client_version : supported_versions_) {
    if (std::find(server_supported_versions.begin(),
                  server_supported_versions.end(),
                  client_version) != server_supported_versions.end()) {
      *version = client_version;
      return true;
    }
  }
  return false;
}

bool QuicClientBase::HasPendingPathValidation() {
  return session()->HasPendingPathValidation();
}

class ValidationResultDelegate : public QuicPathValidator::ResultDelegate {
 public:
  ValidationResultDelegate(QuicClientBase* client)
      : QuicPathValidator::ResultDelegate(), client_(client) {}

  void OnPathValidationSuccess(
      std::unique_ptr<QuicPathValidationContext> context) override {
    QUIC_DLOG(INFO) << "Successfully validated path from " << *context;
    client_->AddValidatedPath(std::move(context));
  }
  void OnPathValidationFailure(
      std::unique_ptr<QuicPathValidationContext> context) override {
    QUIC_LOG(WARNING) << "Fail to validate path " << *context
                      << ", stop migrating.";
    client_->session()->connection()->OnPathValidationFailureAtClient();
  }

 private:
  QuicClientBase* client_;
};

void QuicClientBase::ValidateNewNetwork(const QuicIpAddress& host) {
  std::unique_ptr<QuicPacketWriter> writer =
      CreateWriterForNewNetwork(host, local_port_);
  auto result_delegate = std::make_unique<ValidationResultDelegate>(this);
  if (writer == nullptr) {
    result_delegate->OnPathValidationFailure(
        std::make_unique<PathMigrationContext>(
            nullptr, network_helper_->GetLatestClientAddress(),
            session_->peer_address()));
    return;
  }
  session()->ValidatePath(
      std::make_unique<PathMigrationContext>(
          std::move(writer), network_helper_->GetLatestClientAddress(),
          session_->peer_address()),
      std::move(result_delegate));
}

}  // namespace quic
