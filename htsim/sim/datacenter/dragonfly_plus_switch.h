// -*- c-basic-offset: 4; indent-tabs-mode: nil -*-
#ifndef _DRAGONFLY_PLUS_SWITCH_H
#define _DRAGONFLY_PLUS_SWITCH_H

#include <random>
#include <unordered_map>
#include <unordered_set>
#include <fstream>
#include "callback_pipe.h"
#include "switch.h"
#include "fat_tree_switch.h"

struct QueueInfo {
    uint64_t queue_size;
    int8_t compare_info;
};

struct QueueChoice {
    uint64_t queue_size;
    uint32_t queue_choice;
};

class DragonflyPlusTopology;

class DragonflyPlusSwitch : public Switch {
public:
    enum SwitchType { NONE = 0, LEAF = 1 , SPINE = 2};
    enum RoutingStrategy { MINIMAL = 0, FPAR = 1, REPS_DFP = 2 };

    DragonflyPlusSwitch(EventList& event_list,
                    std::string name,
                    SwitchType type,
                    uint32_t id,
                    simtime_picosec delay,
                    DragonflyPlusTopology* topo);

    virtual void receivePacket(Packet& pkt);
    virtual Route* getNextHop(Packet& pkt, BaseQueue* ingress_port);
    virtual void addHostPort(int addr, int flowid, PacketSink* transport_port); 
    virtual void permute_paths(vector<FibEntry*>* uproutes); 
    virtual uint32_t getType() { return _type; }

    
    static QueueInfo compare_queuesize(FibEntry* left, FibEntry* right);
    static void set_strategy(RoutingStrategy s) { _routing_strategy = s; }

    static void set_config(RoutingStrategy routing_strategy,
                           bool trim_disable,
                           uint16_t trim_size) {
        _routing_strategy = routing_strategy;
        _trim_disable = trim_disable;
        _trim_size = trim_size;
    }
    static uint16_t get_trim_disable() { return _trim_disable; }    
    static uint16_t get_trim_size() { return _trim_size; }
    static void set_entropy_partition(uint16_t threshold) { _entropy_partition_threshold = threshold; }
    static void set_low_share(uint16_t pct) { _low_share_pct = pct; } // to regulate the 7 hops paths choice (max length)
    static void set_minimal_share(uint16_t pct) { _minimal_share_pct = pct; }
    static void set_low_uniform(bool u)         { _low_share_uniform = u; }

    static QueueInfo (*fn)(FibEntry*,FibEntry*);

private:
    SwitchType _type; //
    DragonflyPlusTopology* _topo;//
    uint32_t _s;
    uint32_t _l;
    uint32_t _h;
    uint32_t _p;
    uint32_t _a;
    uint64_t _t;

    uint32_t _no_groups;

    Pipe* _pipe; //
    std::vector<std::pair<simtime_picosec, uint64_t>> _list_sent;
    std::unordered_map<Packet*, bool> _packets;
    std::vector<uint32_t> _neighbours;

    uint32_t _hash_salt;

    uint32_t get_next_switch_minimal(uint32_t this_switch, uint32_t dst_switch, Packet& pkt);
    QueueChoice fully_progressive_adaptive_route(vector<FibEntry*>* ecmp_set, QueueInfo (*cmp)(FibEntry*,FibEntry*));
    FibEntry* ecmp_select_combined(vector<FibEntry*>* minimal, vector<FibEntry*>* non_minimal, Packet& pkt, bool& from_primary);

    // vector<FibEntry*>* _routes; vedere se è possibile utilizzarlo
    
    static bool _trim_disable;
    static uint16_t _trim_size;
    static uint16_t _entropy_partition_threshold;
    static RoutingStrategy _routing_strategy;
    static uint16_t _low_share_pct; 

    static uint16_t _minimal_share_pct;   // default 10; ignored when partition on
    static bool     _low_share_uniform;   // false = off (default)
};

#endif