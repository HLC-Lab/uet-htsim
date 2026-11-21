// -*- c-basic-offset: 4; indent-tabs-mode: nil -*-
#ifndef TWO_DIMENSIONAL_TORUS_H
#define TWO_DIMENSIONAL_TORUS_H
#include <cmath>
#include <cstdint>
#include <unordered_map>
#include <vector>
#include "2d_torus_switch.h"
#include "config.h"
#include "eventlist.h"
#include "logfile.h"
#include "loggers.h"
#include "loggertypes.h"
#include "pipe.h"
#include "route.h"
#include "switch.h"
#include "topology.h"

#ifndef QT
#define QT
typedef enum {
    UNDEFINED,
    RANDOM,
    ECN,
    COMPOSITE,
    PRIORITY,
    CTRL_PRIO,
    FAIR_PRIO,
    LOSSLESS,
    LOSSLESS_INPUT,
    LOSSLESS_INPUT_ECN,
    COMPOSITE_ECN,
    COMPOSITE_ECN_LB,
    SWIFT_SCHEDULER,
    ECN_PRIO,
    AEOLUS,
    AEOLUS_ECN
} queue_type;
#endif

typedef struct {
    bool disable_trim;
    uint16_t trim_size;

    // ECN
    mem_b ecn_min_threshold;
    mem_b ecn_max_threshold;
} composite_q_cfg;

/**
 * In a 2D torus topology, we may visualise switches/nodes as being on an n*m grid.
 * Every switch is connected to its four neighbours to the left, right, up and down. Switches at the
 * extremities of the grid must also have four neighbours. So, for instance: a switch at coordinate
 * (x=0, y) for a fixed y will connect with the switch at (x=n, y), thus creating a "wrap-around"
 * link.
 * This class aims to implement one such topology, with switches capable of adaptive routing.
 *
 * Furthermore, this class also aims to allow simulating a theoretical satellite constellation,
 * where the satellites are capable of computing and communicating, effectively forming a
 * space-borne computer cluster; indeed, a 2D torus is a natural topology choice for satellite
 * constellations.
 */
class TwoDimensionalTorusTopology : public Topology {
public:
    EventList* _eventlist;
    QueueLoggerFactory* _q_logger_factory;

    //! n*m 2D vector of switches
    vector<vector<Switch*>> switches;
    //! Each switch is assigned one or more servers/hosts
    //! So we model those hosts' egress queues and pipes
    //! 3D vector: first 2D correspond to switch, 3rd is the associated hosts
    vector<vector<vector<Queue*>>> host_to_switch_queues;
    vector<vector<vector<Queue*>>> switch_to_host_queues;
    vector<vector<vector<Pipe*>>> host_to_switch_pipes;
    vector<vector<vector<Pipe*>>> switch_to_host_pipes;
    //! Ingress queue associated to host_to_switch_queues
    vector<vector<vector<Queue*>>> switch_internal_ingress_queue;
    // NOTE: switch_to_host_queues aren't associated an ingress queue from the host as the host is
    // the packets' destination. Not sure why that's a good reason to not model buffers but alright.
    // TODO: Assiging an individual queue to each pipe isn't equivalent to the switch having a
    // single queue (buffer) for all hosts. If there isn't a good reason to have one queue per pipe,
    // change this.

    //! Switch-to-switch network
    //! 3D vector for Pipe: first 2D correspond to associated switch, 3rd dimension represents the
    //! virtual channels
    vector<vector<vector<Pipe*>>> northbound_pipes;
    vector<vector<vector<Pipe*>>> eastbound_pipes;
    vector<vector<vector<Pipe*>>> southbound_pipes;
    vector<vector<vector<Pipe*>>> westbound_pipes;
    //! Each pipe is preceded by its own egress queue
    vector<vector<vector<Queue*>>> north_egress_queues;
    vector<vector<vector<Queue*>>> east_egress_queues;
    vector<vector<vector<Queue*>>> south_egress_queues;
    vector<vector<vector<Queue*>>> west_egress_queues;
    //! To each egress queue we associate the ingress queue opposite to it (although ingress queues
    //! are only used for lossless input queues)
    vector<vector<vector<Queue*>>> north_ingress_queues;
    vector<vector<vector<Queue*>>> east_ingress_queues;
    vector<vector<vector<Queue*>>> south_ingress_queues;
    vector<vector<vector<Queue*>>> west_ingress_queues;

    //! Map switch ids to their network coordinate
    unordered_map<uint32_t, vector<uint32_t>> get_coords_of_switch;
    //! Map host ids to their coordinate (switch coords + host's position)
    unordered_map<uint32_t, vector<uint32_t>> get_coords_of_host;

    //! Minimal constructor to set defaults, used to not bloat full constructor
    TwoDimensionalTorusTopology(QueueLoggerFactory* q_log_factory,
                                uint32_t n,
                                uint32_t m,
                                bool is_constellation);

    TwoDimensionalTorusTopology(QueueLoggerFactory* q_log_factory,
                                uint32_t n,
                                uint32_t m,
                                uint32_t hosts_per_switch,
                                queue_type switch_queue_type,
                                queue_type host_queue_type,
                                mem_b queue_size,
                                linkspeed_bps linkspeed,
                                bool disable_trim,
                                uint16_t trim_size,
                                mem_b ecn_high,
                                mem_b ecn_low,
                                torus_routing_strategy routing_strategy,
                                bool is_constellation,
                                simtime_picosec latency,
                                simtime_picosec host_switch_latency,
                                uint32_t altitude,
                                float_t inclination);

    static TwoDimensionalTorusTopology* load(string filename,
                                             EventList& eventlist,
                                             QueueLoggerFactory* q_log_factory,
                                             torus_routing_strategy routing_strategy,
                                             queue_type switch_q_type,
                                             queue_type host_q_type);

    ~TwoDimensionalTorusTopology();

    virtual vector<const Route*>* get_paths(uint32_t src, uint32_t dest) {
        return get_bidir_paths(src, dest, true);
    }
    virtual vector<const Route*>* get_bidir_paths(uint32_t src, uint32_t dest, bool reverse);
    virtual vector<uint32_t>* get_neighbours(uint32_t src);
    virtual uint32_t no_of_nodes() const { return _n * _m * _hosts_per_switch; }

    inline uint32_t get_n() const { return _n; }
    inline uint32_t get_m() const { return _m; }
    inline uint32_t get_n_virtual_channels() const { return _n_virtual_channels; }

    virtual void add_switch_loggers(Logfile& log, simtime_picosec sample_period);
    // NOTE: While switches and queues log events themselves, pipes are logged by transport protocol

    void set_n_virtual_channels();
    void set_hosts_per_switch(uint32_t hosts_per_switch);
    void set_queue_params(queue_type switch_queue_type,
                          queue_type host_queue_type,
                          mem_b queue_size,
                          bool disable_trim,
                          uint16_t trim_size,
                          mem_b ecn_high,
                          mem_b ecn_low);
    // TODO: create different linkspeeds for different components?
    void set_linkspeed(linkspeed_bps linkspeed);
    void set_routing_strategy(torus_routing_strategy routing_strategy);
    void set_constellation_params(uint32_t altitude, float_t inclination);
    void set_switch_latency(simtime_picosec latency);
    void set_host_switch_latency(simtime_picosec host_switch_latency);

    //! Hosts (servers) may have multiple packet flows, so need to be allocated specific queue types
    Queue* alloc_host_queue(QueueLogger* queue_logger, linkspeed_bps linkspeed_bps);
    //! By contrast, switch's queues only care about receiving packets and forwarding them
    Queue* alloc_queue(QueueLogger* queue_logger,
                       linkspeed_bps linkspeed_bps,
                       mem_b queue_size,
                       Switch* sw);

    simtime_picosec compute_average_interplane_latency_ps();

private:
    //! In satellite constellation, N is the number of orbital planes and M is the number of
    //! satellite per plane
    uint32_t _n;
    uint32_t _m;
    uint32_t _hosts_per_switch;
    uint32_t _n_virtual_channels;

    queue_type _switch_queue_type;
    queue_type _host_queue_type;
    mem_b _queue_size_b;
    composite_q_cfg _composite_q_cfg;

    linkspeed_bps _linkspeed_bps;
    torus_routing_strategy _routing_strategy;

    bool _is_constellation;
    //! Latency is really only used when *not* simulating a constellation, and thus can be
    //! considered constant
    simtime_picosec _latency;
    simtime_picosec _host_switch_latency;
    //! If simulating a constellation, shell altitude and orbital inclination allows calculating
    //! latency between nodes/satellites
    uint32_t _altitude_in_m;
    float_t _inclination_in_deg;

    void _add_eastward_route_to(Route* route,
                                const vector<uint32_t>& src_coords,
                                const vector<uint32_t>& dest_coords,
                                bool reverse);
    void _add_westward_route_to(Route* route,
                                const vector<uint32_t>& src_coords,
                                const vector<uint32_t>& dest_coords,
                                bool reverse);
    void _add_x_routes_to(Route* route1,
                          Route* route2,
                          const vector<uint32_t>& src_coords,
                          const vector<uint32_t>& dest_coords,
                          bool reverse);
    void _add_northward_route_to(Route* route,
                                 const vector<uint32_t>& src_coords,
                                 const vector<uint32_t>& dest_coords,
                                 bool);
    void _add_southward_route_to(Route* route,
                                 const vector<uint32_t>& src_coords,
                                 const vector<uint32_t>& dest_coords,
                                 bool);
    void _add_y_routes_to(Route* route1,
                          Route* route2,
                          const vector<uint32_t>& src_coords,
                          const vector<uint32_t>& dest_coords,
                          bool);
};

#endif  //! TWO_DIMENSIONAL_TORUS_H
