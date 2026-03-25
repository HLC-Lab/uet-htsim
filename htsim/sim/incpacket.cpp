// -*- c-basic-offset: 4; indent-tabs-mode: nil -*-
#include "incpacket.h"

// Inizializzazione del database statico per il riciclo dei pacchetti
PacketDB<IncPacket> IncPacket::_packetdb;
PacketFlow IncPacket::_inc_flow(nullptr);