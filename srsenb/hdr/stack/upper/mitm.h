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

#ifndef SRSENB_MITM_H
#define SRSENB_MITM_H

#include "srsran/interfaces/enb_mitm_interfaces.h"
#include "srsran/interfaces/enb_pdcp_interfaces.h"
#include "srsran/interfaces/enb_rrc_interfaces.h"

#include "srsenb/hdr/stack/enb_stack_base.h"

namespace srsenb {

typedef struct {
  uint32_t lcid;
  uint32_t length;
} mitm_header_t;

class mitm : public mitm_interface_pdcp, public mitm_interface_rrc, public srsran::thread
{
public:
  mitm();
  ~mitm();

  int  init(const mitm_args_t& args, pdcp_interface_mitm* pdcp, rrc_interface_mitm* rrc);
  void stop();

  void write_sdu(uint16_t rnti, uint32_t lcid, srsran::unique_byte_buffer_t sdu);

private:
  srslog::basic_logger& logger = srslog::fetch_basic_logger("MITM");

  mitm_args_t          args;
  pdcp_interface_mitm* pdcp;
  rrc_interface_mitm*  rrc;

  bool running;
  int  sockfd;

  struct sockaddr_in local_addr;
  struct sockaddr_in remote_addr;

  uint16_t rnti;

  void run_thread();

  void        write_pdu(srsran::unique_byte_buffer_t& pdu);
  const char* get_rb_name(uint32_t lcid);
};

} // namespace srsenb

#endif // SRSENB_SIM_H
