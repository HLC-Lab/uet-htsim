// -*- c-basic-offset: 4; indent-tabs-mode: nil -*-
#ifndef UEC_INC_SRC_H
#define UEC_INC_SRC_H

#include "inc_uec.h"

class UecIncNIC : public UecNIC {
public:
    UecIncNIC(id_t src_num, EventList& eventList, linkspeed_bps linkspeed, uint32_t ports);

    // Override of the end-of-transmission event to inject INC priority
    void doNextEvent() override;
};

class UecIncSrc : public UecSrc {
public:
    UecIncSrc(TrafficLogger* trafficLogger, 
              EventList& eventList, 
              unique_ptr<UecMultipath> mp,
              UecNIC& nic, 
              uint32_t no_of_ports, 
              bool rts = false);

    // Override of this method to have INC logic
    virtual void receivePacket(Packet& pkt, uint32_t portnum) override;
    virtual mem_b sendNewPacket(const Route& route) override;
    virtual mem_b sendRtxPacket(const Route& route) override;

    // Inizialization method for INC
    void initIncNscc(mem_b cwnd, simtime_picosec peer_rtt);
};

#endif
