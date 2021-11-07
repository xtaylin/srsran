/**
 * Copyright 2013-2021 Software Radio Systems Limited
 *
 * This file is part of srsRAN.
 *
 * srsRAN is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as
 * published by the Free Software Foundation, either version 3 of
 * the License, or (at your option) any later version.
 *
 * srsRAN is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Affero General Public License for more details.
 *
 * A copy of the GNU Affero General Public License can be found in
 * the LICENSE file in the top-level directory of this distribution
 * and at http://www.gnu.org/licenses/.
 *
 */

#include "srsenb/hdr/stack/upper/mitm.h"

#include "srsran/common/common_lte.h"

namespace srsenb {

mitm::mitm() : thread("MITM")
{
  pdcp = nullptr;
  rrc  = nullptr;

  running = false;
  sockfd  = -1;

  rnti = 0;
}

mitm::~mitm() {}

int mitm::init(const mitm_args_t& args_, pdcp_interface_mitm* pdcp_, rrc_interface_mitm* rrc_)
{
  args = args_;
  pdcp = pdcp_;
  rrc  = rrc_;

  sockfd = socket(AF_INET, SOCK_DGRAM, 0);
  if (sockfd < 0) {
    logger.error("Failed to create socket.");
    return SRSRAN_ERROR;
  }

  int enable = 1;
#if defined(SO_REUSEADDR)
  if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(int)) < 0)
    logger.error("Failed to set socket option SO_REUSEADDR.");
#endif
#if defined(SO_REUSEPORT)
  if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEPORT, &enable, sizeof(int)) < 0)
    logger.error("Failed to set socket option SO_REUSEPORT.");
#endif

  bzero(&local_addr, sizeof(sockaddr_in));
  local_addr.sin_family      = AF_INET;
  local_addr.sin_addr.s_addr = inet_addr(args.local_addr.c_str());
  local_addr.sin_port        = htons(args.local_port);

  bzero(&remote_addr, sizeof(sockaddr_in));
  remote_addr.sin_family      = AF_INET;
  remote_addr.sin_addr.s_addr = inet_addr(args.remote_addr.c_str());
  remote_addr.sin_port        = htons(args.remote_port);

  if (bind(sockfd, (struct sockaddr*)&local_addr, sizeof(sockaddr_in)) < 0) {
    logger.error("Failed to bind on address %s:%d.", args.local_addr.c_str(), args.local_port);
    return SRSRAN_ERROR;
  }

  start();

  return SRSRAN_SUCCESS;
}

void mitm::stop()
{
  if (running) {
    running = false;
    thread_cancel();
    wait_thread_finish();
  }

  if (sockfd >= 0) {
    close(sockfd);
    sockfd = -1;
  }
}

void mitm::run_thread()
{
  srsran::unique_byte_buffer_t pdu = srsran::make_byte_buffer("mitm::run_thread");
  if (!pdu) {
    logger.error("Failed to allocate buffer.");
    return;
  }

  running = true;

  while (running) {
    pdu->clear();

    ssize_t n_recv = recv(sockfd, pdu->data(), pdu->get_tailroom(), 0);
    if (n_recv == -1 and errno != EAGAIN) {
      logger.error("Failed to recv from socket: %s.", strerror(errno));
      continue;
    }
    if (n_recv == -1 and errno == EAGAIN) {
      logger.info("Socket timeout reached.");
      continue;
    }

    pdu->resize(n_recv);

    while (pdu->size() > 0) {
      write_pdu(pdu);
    }
  }
}

void mitm::write_pdu(srsran::unique_byte_buffer_t& pdu)
{
  mitm_header_t header = {0};

  if (pdu->size() < sizeof(mitm_header_t)) {
    logger.error("PDU too small to extract header.");

    pdu->clear();
    return;
  }

  memcpy(&header, pdu->data(), sizeof(mitm_header_t));

  logger.info(pdu->data(), pdu->size(), "RX %s PDU (%d B).", get_rb_name(header.lcid), pdu->size());

  pdu->msg += sizeof(mitm_header_t);
  pdu->N_bytes -= sizeof(mitm_header_t);

  if (pdu->size() < header.length) {
    logger.error("PDU too small to extract content.");

    pdu->clear();
    return;
  }

  srsran::unique_byte_buffer_t sdu = srsran::make_byte_buffer("mitm::write_pdu");
  if (!sdu) {
    logger.error("Failed to allocate buffer.");

    pdu->clear();
    return;
  }

  sdu->append_bytes(pdu->data(), header.length);

  logger.info(sdu->data(), sdu->size(), "RX %s SDU (%d B).", get_rb_name(header.lcid), sdu->size());

  if (header.lcid < SRSRAN_N_SRB) {
    rrc->parse_dl_dcch(rnti, header.lcid, sdu);
  }

  if (sdu.get() && (sdu->N_bytes > 0)) {
    pdcp->write_sdu(rnti, header.lcid, std::move(sdu));
  }

  pdu->msg += header.length;
  pdu->N_bytes -= header.length;
}

void mitm::write_sdu(uint16_t rnti, uint32_t lcid, srsran::unique_byte_buffer_t sdu)
{
  mitm_header_t header = {0};

  this->rnti = rnti;

  logger.info(sdu->data(), sdu->size(), "TX %s SDU (%d B).", get_rb_name(lcid), sdu->size());

  bzero(&header, sizeof(mitm_header_t));
  header.lcid   = lcid;
  header.length = sdu->size();

  srsran::unique_byte_buffer_t pdu = srsran::make_byte_buffer("mitm::write_sdu");
  if (!pdu) {
    logger.error("Failed to allocate buffer.");
    return;
  }

  pdu->append_bytes(reinterpret_cast<uint8_t*>(&header), sizeof(mitm_header_t));

  pdu->append_bytes(sdu->data(), sdu->size());

  logger.info(pdu->data(), pdu->size(), "TX %s PDU (%d B).", get_rb_name(lcid), pdu->size());

  if (sendto(sockfd, pdu->data(), pdu->size(), 0, (struct sockaddr*)&remote_addr, sizeof(sockaddr_in)) != pdu->size()) {
    logger.error("Failed to send to socket.");
  }
}

const char* mitm::get_rb_name(uint32_t lcid)
{
  return (srsran::is_lte_srb(lcid)) ? srsran::get_srb_name(srsran::lte_lcid_to_srb(lcid))
                                    : srsran::get_drb_name(static_cast<srsran::lte_drb>(lcid - srsran::MAX_LTE_SRB_ID));
}

} // namespace srsenb
