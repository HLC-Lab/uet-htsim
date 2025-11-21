// -*- c-basic-offset: 4; indent-tabs-mode: nil -*-
#ifndef DRAGONFLY_PLUS_TOPOLOGY_H
#define DRAGONFLY_PLUS_TOPOLOGY_H
#include <ostream>
#include <unordered_map>
#include "config.h"
#include "dragonfly_plus_switch.h"
#include "eventlist.h"
#include "logfile.h"
#include "loggers.h"
#include "main.h"
#include "network.h"
#include "pipe.h"
#include "compositequeue.h"
#include "prioqueue.h"
#include "queue_lossless.h"
#include "queue_lossless_output.h"
#include "queue_lossless_input.h"
#include "randomqueue.h"
#include "switch.h"
#include "topology.h"
#include "firstfit.h"

// Dragonfly+ parameters
//  k = radix router
//  p = number of hosts per leaf switch
//  a = number of switches per group
//  h = number of links used to connect spine switch to other groups
//  l = number of leaf switch connected to one spine switch
//  s = number of spine switch connected to one leaf switch
//
//  To balance channel load on load-balanced traffic: p = l = s = h = k/2;

#ifndef QT
#define QT
typedef enum {UNDEFINED, RANDOM, ECN, COMPOSITE, PRIORITY,
              CTRL_PRIO, FAIR_PRIO, LOSSLESS, LOSSLESS_INPUT, LOSSLESS_INPUT_ECN,
              COMPOSITE_ECN, COMPOSITE_ECN_LB, SWIFT_SCHEDULER, ECN_PRIO, AEOLUS, AEOLUS_ECN} queue_type;
#endif

#ifndef TT
#define TT
typedef enum {
    LARGE, MEDIUM, SMALL
} topology_type;
#endif

class DragonflyPlusTopology : public Topology {
public:
    // leaf and spine switch
    std::vector<Switch*> switches_lf;
    std::vector<Switch*> switches_sp;

    std::vector<std::vector<Pipe*>> pipes_host_leaf;
    std::vector<std::vector<Pipe*>> pipes_leaf_spine;
    std::vector<std::vector<Pipe*>> pipes_spine_leaf;
    std::vector<std::vector<Pipe*>> pipes_leaf_host;

    std::vector<std::vector<Queue*>> queues_host_leaf;
    std::vector<std::vector<Queue*>> queues_leaf_spine;
    std::vector<std::vector<Queue*>> queues_spine_leaf;
    std::vector<std::vector<Queue*>> queues_leaf_host;
    
    // 3rd index is link number in bundle because between group we can have parallel link
    std::vector<std::vector<std::vector<Pipe*>>> pipes_spine_spine;
    std::vector<std::vector<std::vector<Queue*>>> queues_spine_spine;

    DragonflyPlusTopology(uint32_t k, uint32_t s, uint32_t l, uint32_t h, uint32_t p, uint32_t no_of_host, queue_type queue_type, mem_b queue_size,
            QueueLoggerFactory* logger_factory, EventList* event_list, topology_type topo_type, uint64_t t, uint32_t no_par_link, linkspeed_bps linkspeed, simtime_picosec latency, simtime_picosec switch_latency);

    static DragonflyPlusTopology* load(const char * filename, QueueLoggerFactory* logger_factory, EventList& eventlist,
        mem_b queuesize, queue_type q_type);

    virtual std::vector<const Route*>* get_bidir_paths(uint32_t src, uint32_t dest, bool reverse);
    virtual std::vector<uint32_t>* get_neighbours(uint32_t src) { return NULL; };
    virtual void add_switch_loggers(Logfile& log, simtime_picosec sample_period); 

    void init_link_latencies();
    void init_pipes_queues();
    void init_network();

    void count_queue(Queue*); //da vedere se implementarlo o meno 

    uint32_t get_host_switch(uint32_t src) { return (src / _p); }
    uint32_t get_a() { return _a; }
    uint32_t get_h() { return _h; }
    uint32_t get_k() { return _k; }
    uint32_t get_l() { return _l; }
    uint32_t get_s() { return _s; }
    uint32_t get_p() { return _p; }
    uint64_t get_t() { return _t; }
    uint32_t get_no_groups() { return _no_groups; }
    uint32_t get_no_hosts() { return _no_hosts; }
    uint32_t get_no_switches() { return _no_switches; }
    uint32_t get_no_parallel_link() { return _no_parallel_link; }
    uint32_t get_topology_type() { return _topology_type; }

    uint32_t get_group_switch(uint32_t src_group, uint32_t dst_group, Packet& pkt, uint32_t hash);
    uint32_t get_target_switch(uint32_t src_switch, uint32_t global_link);
    linkspeed_bps get_linkspeed();
    std::string get_link_latencies();

    static void set_ecn(bool enable_ecn, mem_b ecn_low, mem_b ecn_high) {
        _enable_ecn = enable_ecn;
        _ecn_low = ecn_low;
        _ecn_high = ecn_high;
    }

private:
    uint32_t _p, _a, _h, _l, _s, _k;
    uint64_t _t; //queue length threshold
    mem_b _queue_size;
    QueueLoggerFactory* _logger_factory;
    EventList* _event_list;
    queue_type _queue_type;
    topology_type _topology_type;
    uint32_t _no_groups, _no_switches, _no_hosts;
    uint32_t failed_links;
    
    static DragonflyPlusTopology* load(istream& file, QueueLoggerFactory* logger_factory, EventList& eventlist,
        mem_b queuesize, queue_type q_type);

    inline void set_link_latency(uint32_t src_switch, uint32_t dst_switch, simtime_picosec latency);
    inline simtime_picosec get_link_latency(uint32_t src_switch, uint32_t dst_switch);

    Queue* alloc_host_queue(QueueLogger* queue_logger, linkspeed_bps linkspeed);
    Queue* alloc_switch_queue(QueueLogger* queue_logger, linkspeed_bps linkspeed, mem_b queue_size);

    std::vector<std::unordered_map<uint32_t, simtime_picosec>> link_latencies_;

    static uint32_t _no_parallel_link;

    static bool _enable_ecn;
    static mem_b _ecn_low;
    static mem_b _ecn_high;

    static linkspeed_bps _link_speed_global;
    static linkspeed_bps _link_speed_local;
    static linkspeed_bps _link_speed_host;

    static simtime_picosec _link_latency_global;
    static simtime_picosec _link_latency_local;
    static simtime_picosec _link_latency_host;

    static simtime_picosec _switch_latency;
};

#endif
