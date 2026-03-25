// -*- c-basic-offset: 4; indent-tabs-mode: nil -*-
#include "inc_switch.h"
#include "network.h"
#include "fat_tree_topology.h"
#include "uec.h"


vector<uint32_t> IncSwitch::_job_participants;

IncSwitch::IncSwitch(EventList& eventlist, string s, switch_type t, uint32_t id, simtime_picosec delay, FatTreeTopology* ft)
    : FatTreeSwitch(eventlist, s, t, id, delay, ft) {
}

void IncSwitch::add_job_participant(uint32_t host_id) {
    vector<uint32_t>& list = _job_participants;
    for (uint32_t existing : list) {
        if (existing == host_id) return;
    }
    list.push_back(host_id);
}

void IncSwitch::receivePacket(Packet& pkt) {
    if(pkt.type() == INC_RESULT && pkt.get_direction() == DOWN) {
        IncPacket* p = (IncPacket*)&pkt;
        send_multicast_down(&pkt, p->_inc_int_data, p->size());
        pkt.free(); 
        return;
    }

    UecBasePacket* uec_pkt = dynamic_cast<UecBasePacket*>(&pkt);
    if (uec_pkt && uec_pkt->dst() == UINT32_MAX) {
        pkt._is_inc = true;
    }

    if (pkt._is_inc) {
        handle_inc_packet(&pkt); 
        return; 
    }


    FatTreeSwitch::receivePacket(pkt);
}

bool IncSwitch::hasUpLinks(){  
    if (_type == CORE) {
        return false;
    }
    if (_type == TOR) {
        return true;
    }
    if (_type == AGG) {
        return (_ft->cfg().get_tiers() > 2);
    }
    return false;
}

void IncSwitch::handle_inc_packet(Packet* p) {
    uint32_t job_id = p->_inc_job_id;
    uint32_t block_id = p->_inc_block_id;
    uint32_t contributor_id = (p->_inc_last_switch_id != -1) ? (uint32_t)p->_inc_last_switch_id : p->flow_id();

    auto key = std::make_pair(job_id, block_id);
    
    if (_completed_blocks.find(key) != _completed_blocks.end()) {
        p->free();
        return;
    }

    if (_aggregation_table.find(key) == _aggregation_table.end()) {
        AggregationEntry entry;
        entry.first_arrival = eventlist().now();
        entry.aggregated_data = 0;
        _aggregation_table[key] = entry;
    }

    _aggregation_table[key].aggregated_data += p->_inc_int_data;
    _aggregation_table[key].received_flows.insert(contributor_id);
    
    int expected_children = calculate_expected_children();
    
    if (expected_children > 0 && _aggregation_table[key].received_flows.size() >= (uint32_t)expected_children) {
        uint32_t final_sum = _aggregation_table[key].aggregated_data;
        
        _completed_blocks.insert(key);
        _aggregation_table.erase(key);

        bool i_cover_all_participants = true;
        for (uint32_t participant_id : _job_participants) {
            if (!is_destination_downstream(participant_id)) {
                i_cover_all_participants = false;
                break;
            }
        }

        if (i_cover_all_participants) {
            send_multicast_down(p, final_sum, p->size());
        } 
        else if (hasUpLinks()) {
            send_aggregated_packet(job_id, block_id, final_sum, p->size());
        }
    } 
    
    p->free();
}

void IncSwitch::send_aggregated_packet(uint32_t job_id, uint32_t block_id, uint32_t aggregated_data, uint32_t pkt_size) {
    int best_port = select_best_port_towards_spine();
    if (best_port == -1) return;

    BaseQueue* q = _ports.at(best_port);
    PacketSink* next_hop_sink = q->getRemoteEndpoint();
    if (!next_hop_sink) return;

    Route* route = new Route();
    route->push_back(next_hop_sink);

    IncPacket* p = IncPacket::newpkt(*route, job_id, block_id, aggregated_data, INC_DATA, pkt_size);

    // FIRMA FONDAMENTALE PER NON FARLO INTERCETTARE DALLA CODA IN USCITA
    p->_inc_last_switch_id = getID(); 
    p->set_next_hop(next_hop_sink);
    p->set_direction(UP);

    // Inserisce nella coda! Subirà il ritardo di rete.
    q->receivePacket(*p); 
}

void IncSwitch::send_multicast_down(Packet* p, uint32_t aggregated_data, uint32_t pkt_size) { 
    vector<int> target_ports = get_multicast_ports(_job_participants);

    for (int port_index : target_ports) {
        BaseQueue* q = _ports.at(port_index);
        PacketSink* next_hop_sink = q->getRemoteEndpoint();

        Route* hop_route = new Route();
        hop_route->push_back(next_hop_sink);

        IncPacket* copy = IncPacket::newpkt(*hop_route, p->_inc_job_id, p->_inc_block_id, aggregated_data, INC_DATA, pkt_size);
        copy->make_result(); 
        copy->set_direction(DOWN);
        copy->set_next_hop(next_hop_sink);
        
        // FIRMA FONDAMENTALE
        copy->_inc_last_switch_id = getID();

        q->receivePacket(*copy);
    }
}

int IncSwitch::calculate_expected_children() {
    if (_job_participants.empty()) return 0;
    uint32_t type = getType();

    if (type == TOR) {
        int expected = 0;
        for (uint32_t host_id : _job_participants) {
            if (_ft->cfg().HOST_POD_SWITCH(host_id) == getID()) expected++;
        }
        return expected;
    } 
    else if (type == AGG) {
        std::set<uint32_t> active_tors;
        uint32_t my_pod = _ft->cfg().AGG_SWITCH_POD_ID(getID());
        for (uint32_t host_id : _job_participants) {
            if (_ft->cfg().HOST_POD(host_id) == my_pod) 
                active_tors.insert(_ft->cfg().HOST_POD_SWITCH(host_id));
        }
        return active_tors.size();
    } 
    else if (type == CORE) {
        std::set<uint32_t> active_pods;
        for (uint32_t host_id : _job_participants) 
            active_pods.insert(_ft->cfg().HOST_POD(host_id));
        return active_pods.size();
    }
    return 0;
}

bool IncSwitch::is_destination_downstream(uint32_t dest_id) {
    uint32_t type = getType();
    if (type == TOR) return _ft->cfg().HOST_POD_SWITCH(dest_id) == getID();
    if (type == AGG) return _ft->cfg().HOST_POD(dest_id) == _ft->cfg().AGG_SWITCH_POD_ID(getID());
    if (type == CORE) return true;
    return false;
}

vector<int> IncSwitch::get_multicast_ports(const vector<uint32_t>& participants) {
    set<int> unique_ports;
    uint32_t type = getType();

    for (uint32_t dest_id : participants) {
        if (!is_destination_downstream(dest_id)) continue;

        int port_index = -1;
        if (type == TOR) {
            BaseQueue* q = _ft->queues_nlp_ns[getID()][dest_id][0];
            for(size_t i = 0; i < _ports.size(); ++i) {
                if (_ports[i] == q) { port_index = i; break; }
            }
        } 
        else if (type == AGG) {
            uint32_t target_tor = _ft->cfg().HOST_POD_SWITCH(dest_id);
            BaseQueue* q = _ft->queues_nup_nlp[getID()][target_tor][0];
            for(size_t i = 0; i < _ports.size(); ++i) {
                if (_ports[i] == q) { port_index = i; break; }
            }
        }
        else if (type == CORE) {
            uint32_t pod_id = _ft->cfg().HOST_POD(dest_id);
            uint32_t agg_target = _ft->cfg().MIN_POD_AGG_SWITCH(pod_id) + (getID() % _ft->cfg().agg_switches_per_pod());
            BaseQueue* q = _ft->queues_nc_nup[getID()][agg_target][0];
            for(size_t i = 0; i < _ports.size(); ++i) {
                if (_ports[i] == q) { port_index = i; break; }
            }
        }

        if (port_index != -1) unique_ports.insert(port_index);
    }
    return vector<int>(unique_ports.begin(), unique_ports.end());
}

int IncSwitch::select_best_port_towards_spine() {
    int best_port = -1;
    uint64_t min_size = UINT64_MAX;
    size_t start_up_port = 0;
    uint32_t type = getType();

    if (type == TOR) start_up_port = _ft->cfg().radix_down(TOR_TIER);
    else if (type == AGG) start_up_port = _ft->cfg().radix_down(AGG_TIER);
    else return -1;

    for (size_t i = start_up_port; i < _ports.size(); i++) {
        BaseQueue* q = _ports.at(i);
        if (q->getRemoteEndpoint() != NULL && q->queuesize() < min_size) {
            min_size = q->queuesize();
            best_port = i;
        }
    }
    return best_port;
}