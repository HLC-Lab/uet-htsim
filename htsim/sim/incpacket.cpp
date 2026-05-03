// -*- c-basic-offset: 4; indent-tabs-mode: nil -*-
#include "incpacket.h"

// Static database initialization for packet recycling
PacketDB<IncPacket> IncPacket::_packetdb;
PacketFlow IncPacket::_inc_flow(nullptr);
