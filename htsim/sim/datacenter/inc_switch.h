// -*- c-basic-offset: 4; indent-tabs-mode: nil -*-
#ifndef _INCSWITCH_H
#define _INCSWITCH_H

#include "fat_tree_switch.h"
#include "incpacket.h"
#include <map>
#include <set>
#include <vector>

// Struttura per tracciare lo stato dell'aggregazione
struct AggregationEntry {
    std::set<uint32_t> received_flows; 
    simtime_picosec first_arrival;
    uint32_t aggregated_data = 0;
};

class IncSwitch : public FatTreeSwitch {
public:
    IncSwitch(EventList& eventlist, string s, switch_type t, uint32_t id, simtime_picosec delay, FatTreeTopology* ft);

    // Override del metodo principale di ricezione
    void receivePacket(Packet& pkt) override;

    // Metodi specifici per INC
    bool hasUpLinks();
    void handle_inc_packet(Packet* p);
    void send_aggregated_packet(uint32_t job_id, uint32_t block_id, uint32_t aggregated_data, uint32_t pkt_size);
    void send_multicast_down(Packet* p, uint32_t aggregated_data, uint32_t pkt_size);
    
    static void add_job_participant(uint32_t host_id);
    static vector<uint32_t> _job_participants;
    vector<int> get_multicast_ports(const vector<uint32_t>& participants);

private:
    // Tabelle di stato specifiche per questa istanza di switch
    std::map<std::pair<uint32_t, uint32_t>, AggregationEntry> _aggregation_table;
    std::set<std::pair<uint32_t, uint32_t>> _completed_blocks;

    // Helper interni
    int calculate_expected_children();
    bool is_destination_downstream(uint32_t dest_id);
    vector<int> get_multicast_ports();
    int select_best_port_towards_spine();
    Route* build_route_core_to_host(uint32_t dest_id);
};

#endif