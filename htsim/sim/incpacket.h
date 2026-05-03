#ifndef INCPACKET_H
#define INCPACKET_H

#include "network.h"

class IncPacket : public Packet {
public:
    static PacketDB<IncPacket> _packetdb;
    static PacketFlow _inc_flow;

    inline static IncPacket* newpkt(const Route& route, 
                                    uint32_t job_id, uint32_t block_id, 
                                    uint32_t int_data,
                                    packet_type p_type,
                                    uint32_t pkt_size) {
        
        IncPacket* p = _packetdb.allocPacket();
        
        // Base parameter initialization
        p->_type = p_type;
        p->_is_inc = true;
        p->_inc_job_id = job_id;
        p->_inc_block_id = block_id;
        p->_inc_int_data = int_data;
        p->set_size(pkt_size);
        p->set_route(route);
        p->_flow = &_inc_flow;
        p->_inc_last_switch_id = -1;
        p->_is_header = false;
        p->_bounced = false;

        return p;
    }

    void free() override {
        _packetdb.freePacket(this);
    }

    PktPriority priority() const override {
        return PRIO_MID; // Medium priority to avoid blocking control traffic while staying above standard data
    }

    void make_result() {
        _type = INC_RESULT;
        _direction = DOWN;
    }
};

#endif
