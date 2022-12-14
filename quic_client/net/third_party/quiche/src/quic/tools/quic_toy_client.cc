// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// A binary wrapper for QuicClient.
// Connects to a host using QUIC, sends a request to the provided URL, and
// displays the response.
//
// Some usage examples:
//
// Standard request/response:
//   quic_client www.google.com
//   quic_client www.google.com --quiet
//   quic_client www.google.com --port=443
//
// Use a specific version:
//   quic_client www.google.com --quic_version=23
//
// Send a POST instead of a GET:
//   quic_client www.google.com --body="this is a POST body"
//
// Append additional headers to the request:
//   quic_client www.google.com --headers="Header-A: 1234; Header-B: 5678"
//
// Connect to a host different to the URL being requested:
//   quic_client mail.google.com --host=www.google.com
//
// Connect to a specific IP:
//   IP=`dig www.google.com +short | head -1`
//   quic_client www.google.com --host=${IP}
//
// Send repeated requests and change ephemeral port between requests
//   quic_client www.google.com --num_requests=10
//
// Try to connect to a host which does not speak QUIC:
//   quic_client www.example.com
//
// This tool is available as a built binary at:
// /google/data/ro/teams/quic/tools/quic_client
// After submitting changes to this file, you will need to follow the
// instructions at go/quic_client_binary_update

#include "quic/tools/quic_toy_client.h"

#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <utility>
#include <vector>


// [SD] for address check
#include <arpa/inet.h>
#include <sys/socket.h>
#include <ifaddrs.h>
#include <thread>
#include <stdlib.h>
#include <chrono>
#include <net/route.h>
#include <sys/types.h>
#include <sys/ioctl.h>

#include "absl/strings/escaping.h"
#include "absl/strings/str_split.h"
#include "absl/strings/string_view.h"
#include "quic/core/crypto/quic_client_session_cache.h"
#include "quic/core/quic_packets.h"
#include "quic/core/quic_server_id.h"
#include "quic/core/quic_utils.h"
#include "quic/core/quic_versions.h"
#include "quic/platform/api/quic_default_proof_providers.h"
#include "quic/platform/api/quic_ip_address.h"
#include "quic/platform/api/quic_socket_address.h"
#include "quic/platform/api/quic_system_event_loop.h"
#include "quic/platform/api/quic_logging.h"
#include "quic/tools/fake_proof_verifier.h"
#include "quic/tools/quic_url.h"
#include "common/quiche_text_utils.h"

namespace {

using quic::QuicUrl;
using quiche::QuicheTextUtils;

}  // namespace

DEFINE_QUIC_COMMAND_LINE_FLAG(
    std::string,
    host,
    "",
    "The IP or hostname to connect to. If not provided, the host "
    "will be derived from the provided URL.");

DEFINE_QUIC_COMMAND_LINE_FLAG(int32_t, port, 0, "The port to connect to.");

DEFINE_QUIC_COMMAND_LINE_FLAG(std::string,
                              ip_version_for_host_lookup,
                              "",
                              "Only used if host address lookup is needed. "
                              "4=ipv4; 6=ipv6; otherwise=don't care.");

DEFINE_QUIC_COMMAND_LINE_FLAG(std::string,
                              body,
                              "",
                              "If set, send a POST with this body.");

DEFINE_QUIC_COMMAND_LINE_FLAG(
    std::string,
    body_hex,
    "",
    "If set, contents are converted from hex to ascii, before "
    "sending as body of a POST. e.g. --body_hex=\"68656c6c6f\"");

DEFINE_QUIC_COMMAND_LINE_FLAG(
    std::string,
    headers,
    "",
    "A semicolon separated list of key:value pairs to "
    "add to request headers.");

DEFINE_QUIC_COMMAND_LINE_FLAG(bool,
                              quiet,
                              false,
                              "Set to true for a quieter output experience.");

DEFINE_QUIC_COMMAND_LINE_FLAG(
    std::string,
    quic_version,
    "",
    "QUIC version to speak, e.g. 21. If not set, then all available "
    "versions are offered in the handshake. Also supports wire versions "
    "such as Q043 or T099.");

DEFINE_QUIC_COMMAND_LINE_FLAG(
    std::string,
    connection_options,
    "",
    "Connection options as ASCII tags separated by commas, "
    "e.g. \"ABCD,EFGH\"");

DEFINE_QUIC_COMMAND_LINE_FLAG(
    std::string,
    client_connection_options,
    "",
    "Client connection options as ASCII tags separated by commas, "
    "e.g. \"ABCD,EFGH\"");

DEFINE_QUIC_COMMAND_LINE_FLAG(bool,
                              quic_ietf_draft,
                              false,
                              "Use the IETF draft version. This also enables "
                              "required internal QUIC flags.");

DEFINE_QUIC_COMMAND_LINE_FLAG(
    bool,
    version_mismatch_ok,
    false,
    "If true, a version mismatch in the handshake is not considered a "
    "failure. Useful for probing a server to determine if it speaks "
    "any version of QUIC.");

DEFINE_QUIC_COMMAND_LINE_FLAG(
    bool,
    force_version_negotiation,
    false,
    "If true, start by proposing a version that is reserved for version "
    "negotiation.");

DEFINE_QUIC_COMMAND_LINE_FLAG(
    bool,
    multi_packet_chlo,
    false,
    "If true, add a transport parameter to make the ClientHello span two "
    "packets. Only works with QUIC+TLS.");

DEFINE_QUIC_COMMAND_LINE_FLAG(
    bool,
    redirect_is_success,
    true,
    "If true, an HTTP response code of 3xx is considered to be a "
    "successful response, otherwise a failure.");

DEFINE_QUIC_COMMAND_LINE_FLAG(int32_t,
                              initial_mtu,
                              0,
                              "Initial MTU of the connection.");

DEFINE_QUIC_COMMAND_LINE_FLAG(
    int32_t,
    num_requests,
    1,
    "How many sequential requests to make on a single connection.");

DEFINE_QUIC_COMMAND_LINE_FLAG(bool,
                              disable_certificate_verification,
                              false,
                              "If true, don't verify the server certificate.");

DEFINE_QUIC_COMMAND_LINE_FLAG(
    std::string, default_client_cert, "",
    "The path to the file containing PEM-encoded client default certificate to "
    "be sent to the server, if server requested client certs.");

DEFINE_QUIC_COMMAND_LINE_FLAG(
    std::string, default_client_cert_key, "",
    "The path to the file containing PEM-encoded private key of the client's "
    "default certificate for signing, if server requested client certs.");

DEFINE_QUIC_COMMAND_LINE_FLAG(
    bool,
    drop_response_body,
    false,
    "If true, drop response body immediately after it is received.");

DEFINE_QUIC_COMMAND_LINE_FLAG(
    bool,
    disable_port_changes,
    false,
    "If true, do not change local port after each request.");

// [SD] Custom Options
DEFINE_QUIC_COMMAND_LINE_FLAG(
    bool,
    enable_watcher,
    false,
    "If true, watcher enable to change the address.");

DEFINE_QUIC_COMMAND_LINE_FLAG(
    bool,
    enable_tracker,
    false,
    "If true, PSN tracker enable to change the address.");

DEFINE_QUIC_COMMAND_LINE_FLAG(
    bool,
    enable_cm,
    false,
    "If true, handover is processed using connection migration.");

DEFINE_QUIC_COMMAND_LINE_FLAG(std::string,
                              ho_case,
                              "time",
                              "Set handover timing to psn.");

DEFINE_QUIC_COMMAND_LINE_FLAG(
    bool,
    enable_zerortt,
    false,
    "If true, handover is processed using zero-rtt.");

DEFINE_QUIC_COMMAND_LINE_FLAG(
    int32_t,
    ho_num,
    0,
    "How many sequential requests to change networks.");

DEFINE_QUIC_COMMAND_LINE_FLAG(
    int32_t,
    ho_interval,
    500,
    "How many sequential requests to change networks.");
    
DEFINE_QUIC_COMMAND_LINE_FLAG(
    int32_t,
    local_port,
    0,
    "bind local port");

DEFINE_QUIC_COMMAND_LINE_FLAG(
    int32_t,
    start_iface,
    1,
    "bind local port");

DEFINE_QUIC_COMMAND_LINE_FLAG(bool,
                              one_connection_per_request,
                              false,
                              "If true, close the connection after each "
                              "request. This allows testing 0-RTT.");

DEFINE_QUIC_COMMAND_LINE_FLAG(int32_t,
                              server_connection_id_length,
                              -1,
                              "Length of the server connection ID used.");

DEFINE_QUIC_COMMAND_LINE_FLAG(int32_t,
                              client_connection_id_length,
                              -1,
                              "Length of the client connection ID used.");

DEFINE_QUIC_COMMAND_LINE_FLAG(int32_t, max_time_before_crypto_handshake_ms,
                              10000,
                              "Max time to wait before handshake completes.");

DEFINE_QUIC_COMMAND_LINE_FLAG(int32_t, max_inbound_header_list_size, 128 * 1024,
                              "Max inbound header list size. 0 means default.");

namespace quic {
namespace {

// Creates a ClientProofSource which only contains a default client certificate.
// Return nullptr for failure.
std::unique_ptr<ClientProofSource> CreateTestClientProofSource(
    absl::string_view default_client_cert_file,
    absl::string_view default_client_cert_key_file) {
  std::ifstream cert_stream(std::string{default_client_cert_file},
                            std::ios::binary);
  std::vector<std::string> certs =
      CertificateView::LoadPemFromStream(&cert_stream);
  if (certs.empty()) {
    std::cerr << "Failed to load client certs." << std::endl;
    return nullptr;
  }

  std::ifstream key_stream(std::string{default_client_cert_key_file},
                           std::ios::binary);
  std::unique_ptr<CertificatePrivateKey> private_key =
      CertificatePrivateKey::LoadPemFromStream(&key_stream);
  if (private_key == nullptr) {
    std::cerr << "Failed to load client cert key." << std::endl;
    return nullptr;
  }

  auto proof_source = std::make_unique<DefaultClientProofSource>();
  proof_source->AddCertAndKey(
      {"*"},
      QuicReferenceCountedPointer<ClientProofSource::Chain>(
          new ClientProofSource::Chain(certs)),
      std::move(*private_key));

  return proof_source;
}

}  // namespace

QuicToyClient::QuicToyClient(ClientFactory* client_factory)
    : client_factory_(client_factory) {}

uint64_t start_, end_;

void Tracker(std::shared_ptr<QuicClientBase> client, int* thread_kill) {
  while(!client->session()->connection()->IsHandshakeComplete());
  std::cout << "[quic_toy_client] tracker operating..." << std::endl;

  std::fstream writer;
  writer.open("ho_track.txt", std::ios::app);

  uint64_t start = client->timeStamp();
  while(*thread_kill == 0) {
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    if(client->connected())
      writer << client->timeStamp() - start << '\t' << client->session()->connection()->GetLargestReceivedPacket() << std::endl;
  }

  writer.close();
  std::cout << "[quic_toy_client] tracker terminates.." << std::endl;
}

void NetworkChange(std::shared_ptr<QuicClientBase> client, NetworkChangeConfig conf) {
  // [SD] wait for established connection
  while(!client->session()->connection()->IsHandshakeConfirmed());

  uint64_t cur_psn;
  for(int i=0; i<conf.ho_num; i++) {
    if(conf.ho_case == "psn") {
      cur_psn = client->session()->connection()->ack_frame().largest_acked.ToUint64();
      if(cur_psn < client->last_psn) {
        cur_psn += client->last_psn;
      }
      // [SD] handover occur based on received packet number
      while(cur_psn < (uint64_t)conf.ho_interval*(i+1)) {
        cur_psn = client->session()->connection()->ack_frame().largest_acked.ToUint64() + client->last_psn;
      }

      client->last_psn = cur_psn;
    } else {
      // [SD] handover occur based on time (msec)
      // srand((unsigned int)time(NULL));
      // int rand_interval = (conf.ho_interval-50) + (int)rand()%101;
      // std::this_thread::sleep_for(std::chrono::milliseconds(rand_interval));
       std::this_thread::sleep_for(std::chrono::milliseconds(conf.ho_interval));
    }

    if(conf.ho_case == "psn") {
      client->session()->connection()->SetStartPsn(cur_psn);
    } else {
      cur_psn = client->session()->connection()->ack_frame().largest_acked.ToUint64();
    }

    std::cout << "[quic_toy_client] Start Network Change(" << cur_psn << ") : " << client->timeStamp() - start_ << " msec" << std::endl;
    client->nc_start_ = client->timeStamp();

    std::string cmd;
    switch (conf.start_iface) {
    case 1:
      std::cout << "[quic_toy_client] iface1 -> iface2" << std::endl;
      cmd.append("bash mQUIC/quic_client/change1to2.sh ");
      cmd.append(std::to_string(150+(i*10)));
      std::cout << cmd << std::endl;
      system(cmd.c_str());
      break;
    case 2:
      std::cout << "[quic_toy_client] iface2 -> iface1" << std::endl;
      cmd.append("bash mQUIC/quic_client/change2to1.sh ");
      cmd.append(std::to_string(150+(i*10)));
      std::cout << cmd << std::endl;
      system(cmd.c_str());
      break;
    case 3:
      system("bash mQUIC/quic_client/changeHorison.sh 1");
      break;
    case 4:
      system("bash mQUIC/quic_client/changeHorison.sh 2");
      break;
    }

    if(conf.ho_num > 1) {
      if(conf.start_iface == 1)
        conf.start_iface = 2;
      else
        conf.start_iface = 1;
    }
  }
  std::cout << "[quic_toy_client] network changer terminate... - " << client->timeStamp() - client->nc_start_ << " msec" << std::endl;
}

int QuicToyClient::SendRequestsAndPrintResponses(
    std::vector<std::string> urls) {
  QuicUrl url(urls[0], "https");
  std::string host = GetQuicFlag(FLAGS_host);
  if (host.empty()) {
    host = url.host();
  }
  int port = GetQuicFlag(FLAGS_port);
  if (port == 0) {
    port = url.port();
  }

  quic::ParsedQuicVersionVector versions = quic::CurrentSupportedVersions();

  if (GetQuicFlag(FLAGS_quic_ietf_draft)) {
    quic::QuicVersionInitializeSupportForIetfDraft();
    versions = {};
    for (const ParsedQuicVersion& version : AllSupportedVersions()) {
      if (version.HasIetfQuicFrames() &&
          version.handshake_protocol == quic::PROTOCOL_TLS1_3) {
        versions.push_back(version);
      }
    }
  }

  std::string quic_version_string = GetQuicFlag(FLAGS_quic_version);
  if (!quic_version_string.empty()) {
    versions = quic::ParseQuicVersionVectorString(quic_version_string);
  }

  if (versions.empty()) {
    std::cerr << "No known version selected." << std::endl;
    return 1;
  }

  for (const quic::ParsedQuicVersion& version : versions) {
    quic::QuicEnableVersion(version);
  }

  if (GetQuicFlag(FLAGS_force_version_negotiation)) {
    versions.insert(versions.begin(),
                    quic::QuicVersionReservedForNegotiation());
  }

  // [SD] Default: const int32_t num_requests
  int32_t num_requests(GetQuicFlag(FLAGS_num_requests));
  std::unique_ptr<quic::ProofVerifier> proof_verifier;
  if (GetQuicFlag(FLAGS_disable_certificate_verification)) {
    proof_verifier = std::make_unique<FakeProofVerifier>();
  } else {
    proof_verifier = quic::CreateDefaultProofVerifier(url.host());
  }

  // [SD] zeroRTT ????????? ??????
  std::unique_ptr<quic::SessionCache> session_cache;
  // if (num_requests > 1 && GetQuicFlag(FLAGS_one_connection_per_request)) {
  //   session_cache = std::make_unique<QuicClientSessionCache>();
  // }
  
  // zeroRTT??? ???????????? ??? ??? ????????? ??????
  if(GetQuicFlag(FLAGS_enable_zerortt)) {
    session_cache = std::make_unique<QuicClientSessionCache>();
  }

  QuicConfig config;
  std::string connection_options_string = GetQuicFlag(FLAGS_connection_options);
  if (!connection_options_string.empty()) {
    config.SetConnectionOptionsToSend(
        ParseQuicTagVector(connection_options_string));
  }
  std::string client_connection_options_string =
      GetQuicFlag(FLAGS_client_connection_options);
  if (!client_connection_options_string.empty()) {
    config.SetClientConnectionOptions(
        ParseQuicTagVector(client_connection_options_string));
  }
  if (GetQuicFlag(FLAGS_multi_packet_chlo)) {
    // Make the ClientHello span multiple packets by adding a custom transport
    // parameter.
    constexpr auto kCustomParameter =
        static_cast<TransportParameters::TransportParameterId>(0x173E);
    std::string custom_value(2000, '?');
    config.custom_transport_parameters_to_send()[kCustomParameter] =
        custom_value;
  }
  config.set_max_time_before_crypto_handshake(QuicTime::Delta::FromMilliseconds(
      GetQuicFlag(FLAGS_max_time_before_crypto_handshake_ms)));

  int address_family_for_lookup = AF_UNSPEC;
  if (GetQuicFlag(FLAGS_ip_version_for_host_lookup) == "4") {
    address_family_for_lookup = AF_INET;
  } else if (GetQuicFlag(FLAGS_ip_version_for_host_lookup) == "6") {
    address_family_for_lookup = AF_INET6;
  }

  // Build the client, and try to connect.
  // [SD] QuicSpdyClientBase??? ????????????????????? quic_client_base??? ????????? quic_spdy_client_base??? ????????????
  std::shared_ptr<QuicSpdyClientBase> client = client_factory_->CreateClient(
      url.host(), host, address_family_for_lookup, port, versions, config,
      std::move(proof_verifier), std::move(session_cache));

  if (client == nullptr) {
    std::cerr << "Failed to create client." << std::endl;
    return 1;
  }

  client->set_local_port(GetQuicFlag(FLAGS_local_port));

  if (!GetQuicFlag(FLAGS_default_client_cert).empty() &&
      !GetQuicFlag(FLAGS_default_client_cert_key).empty()) {
    std::unique_ptr<ClientProofSource> proof_source =
        CreateTestClientProofSource(GetQuicFlag(FLAGS_default_client_cert),
                                    GetQuicFlag(FLAGS_default_client_cert_key));
    if (proof_source == nullptr) {
      std::cerr << "Failed to create client proof source." << std::endl;
      return 1;
    }
    client->crypto_config()->set_proof_source(std::move(proof_source));
  }

  int32_t initial_mtu = GetQuicFlag(FLAGS_initial_mtu);
  client->set_initial_max_packet_length(
      initial_mtu != 0 ? initial_mtu : quic::kDefaultMaxPacketSize);
  client->set_drop_response_body(GetQuicFlag(FLAGS_drop_response_body));
  const int32_t server_connection_id_length =
      GetQuicFlag(FLAGS_server_connection_id_length);
  if (server_connection_id_length >= 0) {
    client->set_server_connection_id_length(server_connection_id_length);
  }
  const int32_t client_connection_id_length =
      GetQuicFlag(FLAGS_client_connection_id_length);
  if (client_connection_id_length >= 0) {
    client->set_client_connection_id_length(client_connection_id_length);
  }
  const size_t max_inbound_header_list_size =
      GetQuicFlag(FLAGS_max_inbound_header_list_size);
  if (max_inbound_header_list_size > 0) {
    client->set_max_inbound_header_list_size(max_inbound_header_list_size);
  }
  if (!client->Initialize()) {
    std::cerr << "Failed to initialize client." << std::endl;
    return 1;
  }
  if (!client->Connect()) {
    quic::QuicErrorCode error = client->session()->error();
    if (error == quic::QUIC_INVALID_VERSION) {
      std::cerr << "Failed to negotiate version with " << host << ":" << port
                << ". " << client->session()->error_details() << std::endl;
      // 0: No error.
      // 20: Failed to connect due to QUIC_INVALID_VERSION.
      return GetQuicFlag(FLAGS_version_mismatch_ok) ? 0 : 20;
    }
    std::cerr << "Failed to connect to " << host << ":" << port << ". "
              << quic::QuicErrorCodeToString(error) << " "
              << client->session()->error_details() << std::endl;


    std::fstream writer;
    writer.open("measure_delay.txt", std::ios::app);
    writer << 0 << std::endl;
    writer.close();              
    return 1;
  }
  std::cerr << "Connected to " << host << ":" << port << std::endl;;

  // Construct the string body from flags, if provided.
  std::string body = GetQuicFlag(FLAGS_body);
  if (!GetQuicFlag(FLAGS_body_hex).empty()) {
    QUICHE_DCHECK(GetQuicFlag(FLAGS_body).empty())
        << "Only set one of --body and --body_hex.";
    body = absl::HexStringToBytes(GetQuicFlag(FLAGS_body_hex));
  }

  // Construct a GET or POST request for supplied URL.
  spdy::Http2HeaderBlock header_block;
  header_block[":method"] = body.empty() ? "GET" : "POST";
  header_block[":scheme"] = url.scheme();
  header_block[":authority"] = url.HostPort();
  header_block[":path"] = url.PathParamsQuery();

  // Append any additional headers supplied on the command line.
  const std::string headers = GetQuicFlag(FLAGS_headers);
  for (absl::string_view sp : absl::StrSplit(headers, ';')) {
    QuicheTextUtils::RemoveLeadingAndTrailingWhitespace(&sp);
    if (sp.empty()) {
      continue;
    }
    std::vector<absl::string_view> kv =
        absl::StrSplit(sp, absl::MaxSplits(':', 1));
    QuicheTextUtils::RemoveLeadingAndTrailingWhitespace(&kv[0]);
    QuicheTextUtils::RemoveLeadingAndTrailingWhitespace(&kv[1]);
    header_block[kv[0]] = kv[1];
  }

  // Make sure to store the response, for later output.
  client->set_store_response(true);

  client->session()->connection()->InitHandoverValue();

  // [SD] use connection migration if handover occured
  client->session()->connection()->SetActiveCM(GetQuicFlag(FLAGS_enable_cm));

  // Send the request.
  int32_t ho_num(GetQuicFlag(FLAGS_ho_num));
  uint64_t ho_start, ho_delay, total_success;

  // [SD] network change
  std::thread networkChangeThread;
  if(ho_num > 0) {
    int32_t start_iface(GetQuicFlag(FLAGS_start_iface));
    int32_t ho_interval(GetQuicFlag(FLAGS_ho_interval));
    networkChangeThread = std::thread(NetworkChange, client, NetworkChangeConfig(ho_num, ho_interval, start_iface, GetQuicFlag(FLAGS_ho_case)));
  }

  total_success = 0;
  start_ = client->timeStamp();
  client->session()->connection()->SetStartTimeFramer(start_);

  if(GetQuicFlag(FLAGS_enable_tracker)) {
    tracker_thread_kill_ = 0;
    std::thread trackerThread(Tracker, client, &tracker_thread_kill_);
    trackerThread.detach();
  }

  for (int i = 0; i < num_requests; ++i) {
    std::cout << "[quic_toy_client] Start to send a Request (" << i << ")" << std::endl;
    client->SendRequestAndWaitForResponse(header_block, body, /*fin=*/true);

    // Print request and response details.
    if (!GetQuicFlag(FLAGS_quiet)) {
      std::cout << "Request:" << std::endl;
      std::cout << "headers:" << header_block.DebugString();
      if (!GetQuicFlag(FLAGS_body_hex).empty()) {
        // Print the user provided hex, rather than binary body.
        std::cout << "body:\n"
                  << QuicheTextUtils::HexDump(
                         absl::HexStringToBytes(GetQuicFlag(FLAGS_body_hex)))
                  << std::endl;
      } else {
        std::cout << "body: " << body << std::endl;
      }
      std::cout << std::endl;

      if (!client->preliminary_response_headers().empty()) {
        std::cout << "Preliminary response headers: "
                  << client->preliminary_response_headers() << std::endl;
        std::cout << std::endl;
      }

      std::cout << "Response:" << std::endl;
      std::cout << "headers: " << client->latest_response_headers()
                << std::endl;
      std::string response_body = client->latest_response_body();
      if (!GetQuicFlag(FLAGS_body_hex).empty()) {
        // Assume response is binary data.
        std::cout << "body:\n"
                  << QuicheTextUtils::HexDump(response_body) << std::endl;
      } else {
        //std::cout << "body: " << response_body << std::endl;
        std::cout << "body: skip" << std::endl;
      }
      std::cout << "trailers: " << client->latest_response_trailers()
                << std::endl;
    }

    if (!client->connected()) {
      std::cerr << "Request caused connection failure. Error: "
                << quic::QuicErrorCodeToString(client->session()->error())
                << std::endl;
      thread_kill_ = 1;

      std::cout << "[quic_toy_client] error code : " << client->session()->error() << std::endl;

      if(!GetQuicFlag(FLAGS_enable_cm)) {
        ho_start = client->session()->connection()->GetHandoverStart();
        preLa = client->session()->connection()->GetLargestReceivedPacket().ToUint64();
        // ?????? new connection ???????????? ???????????? ??????
        if(client->session()->error() == 27 || client->session()->error() == 16) {
          client->Disconnect();
          std::cout << "[quic_toy_client] network unreachable, so request again" << std::endl;
          num_requests++;
          if (!client->Initialize()) {
            std::cerr << "Failed to reinitialize client between requests."
                      << std::endl;
            return 1;
          }

          if (!client->Connect()) {
            std::cerr << "Failed to reconnect client between requests."
                                << std::endl;
            return 1;
          }
          continue;
        }
        // if, other error, that case will skip
        std::fstream writer;
        writer.open("measure_delay.txt", std::ios::app);
        writer << std::endl;
        writer.close();
      }

      if(ho_num > 0) {
        networkChangeThread.join();
      }
      return 1;
    }

    int response_code = client->latest_response_code();
    if (response_code >= 200 && response_code < 300) {
      std::cout << "Request succeeded (" << response_code << ")." << std::endl;
      total_success++;
    } else if (response_code >= 300 && response_code < 400) {
      if (GetQuicFlag(FLAGS_redirect_is_success)) {
        std::cout << "Request succeeded (redirect " << response_code << ")."
                  << std::endl;
      } else {
        std::cout << "Request failed (redirect " << response_code << ")."
                  << std::endl;
        return 1;
      }
    } else {
      std::cout << "Request failed (" << response_code << ")." << std::endl;
      return 1;
    }

    if (i + 1 < num_requests) {  // There are more requests to perform.
      if (GetQuicFlag(FLAGS_one_connection_per_request)) {
        std::cout << "Disconnecting client between requests." << std::endl;
        client->Disconnect();
        tracker_thread_kill_ = 1;

        if (!client->Initialize()) {
          std::cerr << "Failed to reinitialize client between requests."
                    << std::endl;
          return 1;
        }

        if (!client->Connect()) {
          std::cerr << "Failed to reconnect client between requests."
                    << std::endl;
          return 1;
        }
      } else if (!GetQuicFlag(FLAGS_disable_port_changes)) {
        // Change the ephemeral port.
        if (!client->ChangeEphemeralPort()) {
          std::cerr << "Failed to change ephemeral port." << std::endl;
          return 1;
        }
      }
    }
  }
  
  end_ = client->timeStamp();
  std::fstream writer;
  if(ho_start == 0)
    ho_start = client->session()->connection()->GetHandoverStart();
  if(GetQuicFlag(FLAGS_enable_cm))
    ho_delay = client->session()->connection()->GetHandoverDelay();
  else
    ho_delay = client->session()->connection()->GetHandoverZeroRTT() - ho_start;

  if(ho_delay > 100000) {
    ho_delay = 0;
  }

  //curBW = client->session()->connection()->sent_packet_manager().BandwidthEstimate().ToBitsPerSecond();
  //SaveBandwidth(preBW, curBW);

  uint64_t total_received_packets = client->session()->connection()->GetLargestReceivedPacket().ToUint64();
  total_received_packets += preLa;

  std::cout << "[quic_toy_client] Total Delay : " << end_ - start_ <<  " msec / HO Delay : " << ho_delay << " msec / Total Received Packets: " << total_received_packets << " / Total Result: " << total_success << std::endl;
  writer.open("measure_delay.txt", std::ios::app);
  writer << ho_delay << '\t' << end_ - start_ << std::endl;
  writer.close();


  writer.open("ho_count.txt", std::ios::app);
  writer << client->ho_count << std::endl;
  client->ho_count = 0;
  writer.close();


  // QUIC_LOG_FIRST_N(WARNING, 1) << "Test man";
  // QUIC_DLOG(INFO) << "Hey";

  if(GetQuicFlag(FLAGS_enable_tracker)) {
    tracker_thread_kill_ = 1;
  }

  client->Disconnect();
  thread_kill_ = 1;
  if(ho_num > 0) {
    networkChangeThread.join();
  }
  return 0;
}

}  // namespace quic
