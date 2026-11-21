// -*- c-basic-offset: 4; indent-tabs-mode: nil -*-

#include "2d_torus_topology.h"
#include <cassert>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
#include "2d_torus_switch.h"
#include "compositequeue.h"
#include "config.h"
#include "ecnqueue.h"
#include "eventlist.h"
#include "inter_satellite_link.h"
#include "logfile.h"
#include "loggers.h"
#include "loggertypes.h"
#include "main.h"
#include "network.h"
#include "pipe.h"
#include "prioqueue.h"
#include "queue.h"
#include "queue_lossless.h"
#include "queue_lossless_input.h"
#include "queue_lossless_output.h"
#include "randomqueue.h"
#include "route.h"
#include "switch.h"

// Define virtual channels 0 and 1
// NOTE: constructor ensures those two must exist, although there may be more
#define VC0 0
#define VC1 1

#define PI 3.14159265358979323846
// Earth's radius in m, needed to compute latencies
#define R_EARTH_M 6378000
// Speed of light in mps
#define C_MPS 299792458

// Use tokenize from connection matrix
extern void tokenize(std::string const& str, const char delim, std::vector<std::string>& out);

//! Minimal constructor used to set default parameters
TwoDimensionalTorusTopology::TwoDimensionalTorusTopology(QueueLoggerFactory* q_log_factory,
                                                         uint32_t n,
                                                         uint32_t m,
                                                         bool is_constellation)
    : _eventlist(&EventList::getTheEventList()),
      _q_logger_factory(q_log_factory),
      _n(n),
      _m(m),
      _hosts_per_switch(0),
      _n_virtual_channels(0),
      _switch_queue_type(UNDEFINED),
      _host_queue_type(UNDEFINED),
      _queue_size_b(0),
      _composite_q_cfg(),
      _linkspeed_bps(0),
      _routing_strategy(DIMENSION_ORDER),
      _is_constellation(is_constellation),
      _latency(0),
      _host_switch_latency(0),
      _altitude_in_m(0),
      _inclination_in_deg(0) {};

//! Full constructor
TwoDimensionalTorusTopology::TwoDimensionalTorusTopology(QueueLoggerFactory* q_log_factory,
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
                                                         float_t inclination)
    : TwoDimensionalTorusTopology(q_log_factory, n, m, is_constellation) {
    // Set parameters
    set_hosts_per_switch(hosts_per_switch);
    set_routing_strategy(routing_strategy);
    set_n_virtual_channels();
    set_queue_params(switch_queue_type, host_queue_type, queue_size, disable_trim, trim_size,
                     ecn_high, ecn_low);
    set_linkspeed(linkspeed);
    if (is_constellation) {
        set_constellation_params(altitude, inclination);
    } else {
        set_switch_latency(latency);
    }
    set_host_switch_latency(host_switch_latency);

    // Allocating switches, queues, and pipes' vectors
    // NOTE: resize() initialises vectors with values' default type, in this case nullptr
    // If things don't go right, explicitly set values to nullptr with loops here
    switches.resize(_n, vector<Switch*>(_m));

    // Switch-host networking
    host_to_switch_queues.resize(_n, vector<vector<Queue*>>(_m, vector<Queue*>(_hosts_per_switch)));
    switch_to_host_queues.resize(_n, vector<vector<Queue*>>(_m, vector<Queue*>(_hosts_per_switch)));
    host_to_switch_pipes.resize(_n, vector<vector<Pipe*>>(_m, vector<Pipe*>(_hosts_per_switch)));
    switch_to_host_pipes.resize(_n, vector<vector<Pipe*>>(_m, vector<Pipe*>(_hosts_per_switch)));
    switch_internal_ingress_queue.resize(
        _n, vector<vector<Queue*>>(_m, vector<Queue*>(_hosts_per_switch)));

    // Switch-switch networking
    north_egress_queues.resize(_n, vector<vector<Queue*>>(_m, vector<Queue*>(_n_virtual_channels)));
    east_egress_queues.resize(_n, vector<vector<Queue*>>(_m, vector<Queue*>(_n_virtual_channels)));
    south_egress_queues.resize(_n, vector<vector<Queue*>>(_m, vector<Queue*>(_n_virtual_channels)));
    west_egress_queues.resize(_n, vector<vector<Queue*>>(_m, vector<Queue*>(_n_virtual_channels)));

    north_ingress_queues.resize(_n,
                                vector<vector<Queue*>>(_m, vector<Queue*>(_n_virtual_channels)));
    east_ingress_queues.resize(_n, vector<vector<Queue*>>(_m, vector<Queue*>(_n_virtual_channels)));
    south_ingress_queues.resize(_n,
                                vector<vector<Queue*>>(_m, vector<Queue*>(_n_virtual_channels)));
    west_ingress_queues.resize(_n, vector<vector<Queue*>>(_m, vector<Queue*>(_n_virtual_channels)));

    northbound_pipes.resize(_n, vector<vector<Pipe*>>(_m, vector<Pipe*>(_n_virtual_channels)));
    eastbound_pipes.resize(_n, vector<vector<Pipe*>>(_m, vector<Pipe*>(_n_virtual_channels)));
    southbound_pipes.resize(_n, vector<vector<Pipe*>>(_m, vector<Pipe*>(_n_virtual_channels)));
    westbound_pipes.resize(_n, vector<vector<Pipe*>>(_m, vector<Pipe*>(_n_virtual_channels)));

    // Create all switches first to avoid referencing un-instantiated switches in next step
    uint32_t id = 0;
    for (uint32_t i = 0; i < _n; i++) {
        for (uint32_t j = 0; j < _m; j++) {
            // Save id mapping
            get_coords_of_switch[id] = {i, j};
            string name = "Switch_(" + ntoa(j) + ", " + ntoa(i) + ")";
            switches[i][j] =
                new TwoDimensionalTorusSwitch(_eventlist, this, id++, name, _routing_strategy);
        }
    }

    // Create queues and pipes
    uint32_t host_id = 0;
    QueueLogger* hst_swi_q_logger;
    QueueLogger* swi_hst_q_logger;

    QueueLogger* n_queue_logger;
    QueueLogger* e_queue_logger;
    QueueLogger* s_queue_logger;
    QueueLogger* w_queue_logger;
    for (uint32_t i = 0; i < _n; i++) {
        for (uint32_t j = 0; j < _m; j++) {
            // Switch-host networking
            for (uint32_t host = 0; host < _hosts_per_switch; host++) {
                // Map id to position
                get_coords_of_host[host_id++] = {i, j, host};
                // Create Pipes
                host_to_switch_pipes[i][j][host] = new Pipe(_host_switch_latency, *_eventlist);
                switch_to_host_pipes[i][j][host] = new Pipe(_host_switch_latency, *_eventlist);
                host_to_switch_pipes[i][j][host]->forceName(
                    "Pipe_host(" + ntoa(host) + ")->switch(" + ntoa(j) + ", " + ntoa(i) + ")");
                switch_to_host_pipes[i][j][host]->forceName(
                    "Pipe_switch(" + ntoa(j) + ", " + ntoa(i) + ")->host(" + ntoa(host) + ")");

                // Create queues
                hst_swi_q_logger =
                    (_q_logger_factory) ? _q_logger_factory->createQueueLogger() : nullptr;
                swi_hst_q_logger =
                    (_q_logger_factory) ? _q_logger_factory->createQueueLogger() : nullptr;

                host_to_switch_queues[i][j][host] =
                    alloc_host_queue(hst_swi_q_logger, _linkspeed_bps);
                switch_to_host_queues[i][j][host] =
                    alloc_queue(swi_hst_q_logger, _linkspeed_bps, _queue_size_b, switches[i][j]);

                assert(switches[i][j]);
                // Create switches' ingress queues dedicated to hosts
                if (switch_queue_type == LOSSLESS_INPUT ||
                    switch_queue_type == LOSSLESS_INPUT_ECN) {
                    switch_internal_ingress_queue[i][j][host] =
                        new LosslessInputQueue(*_eventlist, host_to_switch_queues[i][j][host],
                                               switches[i][j], host_switch_latency);
                } else {
                    host_to_switch_queues[i][j][host]->setRemoteEndpoint(switches[i][j]);
                }
                host_to_switch_queues[i][j][host]->setName(
                    "_Queue_host(" + ntoa(host) + ")->switch(" + ntoa(j) + ", " + ntoa(i) + ")");
                switch_to_host_queues[i][j][host]->setName("_Queue_switch(" + ntoa(j) + ", " +
                                                           ntoa(i) + ")->host(" + ntoa(host) + ")");

                // LosslessQueue will call addPort on switch upon creation
                if (switch_queue_type != LOSSLESS)
                    switches[i][j]->addPort(switch_to_host_queues[i][j][host]);
            }

            // Switch-switch networking
            for (uint32_t vc = 0; vc < _n_virtual_channels; vc++) {
                // --- Create Pipes ---
                if (is_constellation) {
                    northbound_pipes[i][j][vc] =
                        new InterSatelliteLink(_n, _m, InterSatelliteLink::ISL_Type::INTERPLANE,
                                               _inclination_in_deg, _altitude_in_m);
                    eastbound_pipes[i][j][vc] =
                        new InterSatelliteLink(_n, _m, InterSatelliteLink::ISL_Type::INTRAPLANE,
                                               _inclination_in_deg, _altitude_in_m);
                    southbound_pipes[i][j][vc] =
                        new InterSatelliteLink(_n, _m, InterSatelliteLink::ISL_Type::INTERPLANE,
                                               _inclination_in_deg, _altitude_in_m);
                    westbound_pipes[i][j][vc] =
                        new InterSatelliteLink(_n, _m, InterSatelliteLink::ISL_Type::INTRAPLANE,
                                               _inclination_in_deg, _altitude_in_m);
                } else {
                    northbound_pipes[i][j][vc] = new Pipe(_latency, *_eventlist);
                    eastbound_pipes[i][j][vc] = new Pipe(_latency, *_eventlist);
                    southbound_pipes[i][j][vc] = new Pipe(_latency, *_eventlist);
                    westbound_pipes[i][j][vc] = new Pipe(_latency, *_eventlist);
                }
                // Set names
                northbound_pipes[i][j][vc]->forceName("NorthboundPipe_(" + ntoa(j) + ", " +
                                                      ntoa(i) + ", VC" + ntoa(vc) + ")");
                eastbound_pipes[i][j][vc]->forceName("EastboundPipe_(" + ntoa(j) + ", " + ntoa(i) +
                                                     ", VC" + ntoa(vc) + ")");
                southbound_pipes[i][j][vc]->forceName("SouthboundPipe_(" + ntoa(j) + ", " +
                                                      ntoa(i) + ", VC" + ntoa(vc) + ")");
                westbound_pipes[i][j][vc]->forceName("WestboundPipe_(" + ntoa(j) + ", " + ntoa(i) +
                                                     ", VC" + ntoa(vc) + ")");
                // --------------------

                // --- Create Queues ---
                // Start with egress queues

                // Init queue loggers
                n_queue_logger =
                    (_q_logger_factory) ? _q_logger_factory->createQueueLogger() : nullptr;
                e_queue_logger =
                    (_q_logger_factory) ? _q_logger_factory->createQueueLogger() : nullptr;
                s_queue_logger =
                    (_q_logger_factory) ? _q_logger_factory->createQueueLogger() : nullptr;
                w_queue_logger =
                    (_q_logger_factory) ? _q_logger_factory->createQueueLogger() : nullptr;

                // We allocate a queue of given type to _egress_queues with alloc_queue,
                // and then populate _ingress_queues with LosslessInputQueue only
                // if such queue types are used. Otherwise, ingress queues are left as nullptr.
                north_egress_queues[i][j][vc] =
                    alloc_queue(n_queue_logger, _linkspeed_bps / _n_virtual_channels,
                                _queue_size_b / _n_virtual_channels, switches[i][j]);
                east_egress_queues[i][j][vc] =
                    alloc_queue(e_queue_logger, _linkspeed_bps / _n_virtual_channels,
                                _queue_size_b / _n_virtual_channels, switches[i][j]);
                south_egress_queues[i][j][vc] =
                    alloc_queue(s_queue_logger, _linkspeed_bps / _n_virtual_channels,
                                _queue_size_b / _n_virtual_channels, switches[i][j]);
                west_egress_queues[i][j][vc] =
                    alloc_queue(w_queue_logger, _linkspeed_bps / _n_virtual_channels,
                                _queue_size_b / _n_virtual_channels, switches[i][j]);

                north_egress_queues[i][j][vc]->setName("_NorthEgressQueue_(" + ntoa(j) + ", " +
                                                       ntoa(i) + ", VC" + ntoa(vc) + ")");
                east_egress_queues[i][j][vc]->setName("_EastEgressQueue_(" + ntoa(j) + ", " +
                                                      ntoa(i) + ", VC" + ntoa(vc) + ")");
                south_egress_queues[i][j][vc]->setName("_SouthEgressQueue_(" + ntoa(j) + ", " +
                                                       ntoa(i) + ", VC" + ntoa(vc) + ")");
                west_egress_queues[i][j][vc]->setName("_WestEgressQueue_(" + ntoa(j) + ", " +
                                                      ntoa(i) + ", VC" + ntoa(vc) + ")");
                // Connect switch to these queues
                if (switch_queue_type != LOSSLESS) {
                    // LosslessQueue will call addPort on switch upon creation
                    switches[i][j]->addPort(north_egress_queues[i][j][vc]);
                    switches[i][j]->addPort(east_egress_queues[i][j][vc]);
                    switches[i][j]->addPort(south_egress_queues[i][j][vc]);
                    switches[i][j]->addPort(west_egress_queues[i][j][vc]);
                }
                // Assign remote endpoint to egress queues:

                // For instance, current switch's north egress queue will be paired with south
                // ingress queue of the opposite switch, so determine coords of opposite switch
                uint32_t n_i, n_j, e_i, e_j, s_i, s_j, w_i, w_j;
                // Simulate wrap-around when network edge is reached
                n_i = (i == 0) ? _n - 1 : i - 1;
                n_j = j;

                e_i = i;
                e_j = (j == _m - 1) ? 0 : j + 1;

                s_i = (i == _n - 1) ? 0 : i + 1;
                s_j = j;

                w_i = i;
                w_j = (j == 0) ? _m - 1 : j - 1;

                if (_switch_queue_type == LOSSLESS_INPUT ||
                    _switch_queue_type == LOSSLESS_INPUT_ECN) {
                    // Ingress queue vectors exist only to keep track of lossless input queues, so
                    // that they may be deleted in the destructor. Otherwise, in its constructor a
                    // LosslessInputQueue and its peer queue mutually set each other as remote
                    // endpoints, so future reference to the  ingress queues will be through
                    // _egress_queues->getRemoteEndpoint()
                    south_ingress_queues[n_i][n_j][vc] = new LosslessInputQueue(
                        *_eventlist, north_egress_queues[i][j][vc], switches[n_i][n_j], _latency);
                    south_ingress_queues[n_i][n_j][vc]->setName("_SouthIngressQueue_(" + ntoa(n_i) +
                                                                ", " + ntoa(n_j) + ", VC" +
                                                                ntoa(vc) + ")");

                    west_ingress_queues[e_i][e_j][vc] = new LosslessInputQueue(
                        *_eventlist, east_egress_queues[i][j][vc], switches[e_i][e_j], _latency);
                    west_ingress_queues[e_i][e_j][vc]->setName("_WestIngressQueue_(" + ntoa(e_i) +
                                                               ", " + ntoa(e_j) + ", VC" +
                                                               ntoa(vc) + ")");

                    north_ingress_queues[s_i][s_j][vc] = new LosslessInputQueue(
                        *_eventlist, south_egress_queues[i][j][vc], switches[s_i][s_j], _latency);
                    north_ingress_queues[s_i][s_j][vc]->setName("_NorthIngressQueue_(" + ntoa(s_i) +
                                                                ", " + ntoa(s_j) + ", VC" +
                                                                ntoa(vc) + ")");

                    east_ingress_queues[w_i][w_j][vc] = new LosslessInputQueue(
                        *_eventlist, west_egress_queues[i][j][vc], switches[w_i][w_j], _latency);
                    east_ingress_queues[w_i][w_j][vc]->setName("_EastIngressQueue_(" + ntoa(w_i) +
                                                               ", " + ntoa(w_j) + ", VC" +
                                                               ntoa(vc) + ")");
                } else {
                    // It is still necessary to set the egress queues' endpoints as their opposite
                    // switch, this will be needed by switches to receive packets when using
                    // lossless queues and to route packets properly in adaptive routing.
                    north_egress_queues[i][j][vc]->setRemoteEndpoint(switches[n_i][n_j]);
                    east_egress_queues[i][j][vc]->setRemoteEndpoint(switches[e_i][e_j]);
                    south_egress_queues[i][j][vc]->setRemoteEndpoint(switches[s_i][s_j]);
                    west_egress_queues[i][j][vc]->setRemoteEndpoint(switches[w_i][w_j]);
                }

                // --------------------
            }

            // NOTE: Pipes are "connected" upon route creation, see get_bidir_paths
        }
    }
    // Initialise lossless queues' thresholds to send PAUSE and UNPAUSE frames
    if (switch_queue_type == LOSSLESS) {
        for (uint32_t i = 0; i < _n; i++) {
            for (uint32_t j = 0; j < _m; j++) {
                for (size_t p = 0; p < switches[i][j]->portCount(); p++) {
                    LosslessQueue* q = (LosslessQueue*)switches[i][j]->getPort(p);
                    q->initThresholds();
                }
            }
        }
    }
}

/*
 * Load config from file and return topology
 */
TwoDimensionalTorusTopology* TwoDimensionalTorusTopology::load(
    string filename,
    EventList& eventlist,
    QueueLoggerFactory* q_log_factory,
    torus_routing_strategy routing_strategy,
    queue_type switch_q_type,
    queue_type host_q_type) {
    uint32_t n, m, host_p_switch, altitude_m;
    uint16_t trim_size;
    float_t inclination_deg;
    mem_b q_size, ecn_high, ecn_low;
    linkspeed_bps linkspeed_bps;
    bool disable_trim, is_constellation;
    simtime_picosec latency, host_switch_latency;

    string line;

    ifstream file(filename);
    cout << "Reading torus cfg from: " << filename << endl;
    if (file.is_open()) {
        TwoDimensionalTorusTopology* topo = nullptr;

        while (getline(file, line)) {
            vector<string> tokens;
            tokenize(line, ' ', tokens);

            if (tokens.size() == 0 || tokens[0][0] == '#')
                continue;

            if (tokens[0] == "n")
                n = stoi(tokens[1]);
            else if (tokens[0] == "m")
                m = stoi(tokens[1]);
            else if (tokens[0] == "host_per_switch")
                host_p_switch = stoi(tokens[1]);
            else if (tokens[0] == "altitude_m")
                altitude_m = stoi(tokens[1]);
            else if (tokens[0] == "trim_size_b")
                trim_size = (mem_b)stoi(tokens[1]);
            else if (tokens[0] == "inclination_deg")
                inclination_deg = stof(tokens[1]);
            else if (tokens[0] == "queue_size_b" || tokens[0] == "queue_size_from_npackets")
                q_size = (tokens[0] == "queue_size_b") ? (mem_b)stoi(tokens[1])
                                                       : memFromPkt(stoi(tokens[1]));
            else if (tokens[0] == "ecn_high_b")
                ecn_high = (mem_b)stoi(tokens[1]);
            else if (tokens[0] == "ecn_low_b")
                ecn_low = (mem_b)stoi(tokens[1]);
            else if (tokens[0] == "linkspeed_Gbps")
                linkspeed_bps = speedFromGbps(stoi(tokens[1]));
            else if (tokens[0] == "disable_trim")
                disable_trim = (tokens[1] == "true") ? true : false;
            else if (tokens[0] == "is_constellation")
                is_constellation = (tokens[1] == "true") ? true : false;
            else if (tokens[0] == "latency_ns")
                latency = timeFromNs((uint32_t)stoi(tokens[1]));
            else if (tokens[0] == "host_switch_latency_ns")
                host_switch_latency = timeFromNs((uint32_t)stoi(tokens[1]));
        }

        topo = new TwoDimensionalTorusTopology(
            q_log_factory, n, m, host_p_switch, switch_q_type, host_q_type, q_size, linkspeed_bps,
            disable_trim, trim_size, ecn_high, ecn_low, routing_strategy, is_constellation, latency,
            host_switch_latency, altitude_m, inclination_deg);

        cout << "Torus topology created" << endl;
        cout << "n: " << n << endl;
        cout << "m: " << m << endl;
        cout << "p: " << host_p_switch << endl;

        file.close();
        return topo;
    } else {
        cerr << "Failed to open 2D torus config file " << filename << endl;
        exit(1);
    }
}

template <class P>
void delete_3d_vector(vector<vector<vector<P*>>>& vec3d) {
    for (auto& vec1 : vec3d) {
        for (auto& vec2 : vec1) {
            for (auto* pipe : vec2) {
                delete pipe;
            }
        }
    }
    vec3d.clear();
}

TwoDimensionalTorusTopology::~TwoDimensionalTorusTopology() {
    for (auto& switch_vec : switches) {
        for (auto* sw : switch_vec) {
            delete sw;
        }
    }
    switches.clear();

    delete_3d_vector(host_to_switch_queues);
    delete_3d_vector(host_to_switch_pipes);
    delete_3d_vector(switch_to_host_queues);
    delete_3d_vector(switch_to_host_pipes);
    delete_3d_vector(switch_internal_ingress_queue);

    delete_3d_vector(north_egress_queues);
    delete_3d_vector(east_egress_queues);
    delete_3d_vector(south_egress_queues);
    delete_3d_vector(west_egress_queues);

    delete_3d_vector(north_ingress_queues);
    delete_3d_vector(east_ingress_queues);
    delete_3d_vector(south_ingress_queues);
    delete_3d_vector(west_ingress_queues);

    delete_3d_vector(northbound_pipes);
    delete_3d_vector(westbound_pipes);
    delete_3d_vector(southbound_pipes);
    delete_3d_vector(eastbound_pipes);
}

void TwoDimensionalTorusTopology::set_hosts_per_switch(uint32_t hosts_per_switch) {
    _hosts_per_switch = (hosts_per_switch == 0) ? 1 : hosts_per_switch;
}
void TwoDimensionalTorusTopology::set_routing_strategy(torus_routing_strategy rs) {
    _routing_strategy = rs;
}

void TwoDimensionalTorusTopology::set_n_virtual_channels() {
    // Two VCs for dimension-ordered, 3 for minimum adaptive
    _n_virtual_channels = (_routing_strategy == DIMENSION_ORDER) ? 2 : 3;
}

void TwoDimensionalTorusTopology::set_queue_params(queue_type switch_queue_type,
                                                   queue_type host_queue_type,
                                                   mem_b queue_size,
                                                   bool disable_trim,
                                                   uint16_t trim_size,
                                                   mem_b ecn_high,
                                                   mem_b ecn_low) {
    _switch_queue_type = (switch_queue_type == UNDEFINED) ? LOSSLESS : switch_queue_type;
    _host_queue_type = (host_queue_type == UNDEFINED) ? FAIR_PRIO : host_queue_type;
    // Default to accomodating 10 packet per virtual channel
    _queue_size_b = (queue_size == 0) ? memFromPkt(10 * _n_virtual_channels) : queue_size;

    if (_switch_queue_type == COMPOSITE || _switch_queue_type == COMPOSITE_ECN) {
        ecn_low = (ecn_low == 0) ? ecn_high : ecn_low;
        _composite_q_cfg = composite_q_cfg{disable_trim, trim_size, ecn_high, ecn_low};
    }
}

void TwoDimensionalTorusTopology::set_linkspeed(linkspeed_bps ls) {
    //! Default to 100Gbps
    _linkspeed_bps = (ls == 0) ? speedFromGbps(100) : ls;
}

void TwoDimensionalTorusTopology::set_constellation_params(uint32_t altitude, float_t inclination) {
    if (altitude <= 160000 || altitude > 2000000) {
        cout << "Set altitude is invalid, using default of 800km" << endl;
        _altitude_in_m = 800000;
    } else {
        _altitude_in_m = altitude;
    }

    if (inclination <= 0 || inclination >= 360) {
        cout << "Set orbital inclination is invalid, using default of 45 degrees" << endl;
        _inclination_in_deg = 45;
    } else {
        _inclination_in_deg = inclination;
    }

    // NOTE: since latency will still be used by lossless ingress queues' callback pipes, we cannot
    // leave it at 0. Instead, compute the fuxed intraplane latency and use that
    simtime_picosec latency = compute_average_interplane_latency_ps();
    set_switch_latency(latency);
}

void TwoDimensionalTorusTopology::set_switch_latency(simtime_picosec latency) {
    _latency = latency;
}

void TwoDimensionalTorusTopology::set_host_switch_latency(simtime_picosec latency) {
    _host_switch_latency = latency;
}

double square(double x) {
    return x * x;
}

/*
 * The distance formula used can be found in the paper "QoS-Aware Resource Placement For LEO
 * Satellite Edge Computing" by T. Pfandzelter and D. Bermbach.
 * Since it computes an average, it doesn't need additional input parameters.
 * It makes use of the complete elliptic integral of the second kind.
 */
simtime_picosec TwoDimensionalTorusTopology::compute_average_interplane_latency_ps() {
    double factor1 = sqrt(2 * (1 - cos(2 * PI / _n)));
    double factor2 = ellint_2l((1 - square(cos(_inclination_in_deg * PI / 180))), PI / 2);

    double avg_dist = (2 / PI) * (_altitude_in_m + R_EARTH_M) * factor1 * factor2;
    double latency_s = avg_dist / C_MPS;
    return timeFromSec(latency_s);
}

Queue* TwoDimensionalTorusTopology::alloc_host_queue(QueueLogger* queue_logger,
                                                     linkspeed_bps linkspeed_bps) {
    // NOTE: since these queues need to handle multiple senders at once, their size is considerably
    // larger than switch queues (at the time of writing, FEEDER_BUFFER=1000)
    switch (_host_queue_type) {
        case PRIORITY:
            // Priority queue with 3 levels, low, mid, and high. Heavily favours high priority
            // packets.
            return new PriorityQueue(linkspeed_bps, memFromPkt(FEEDER_BUFFER), *_eventlist,
                                     queue_logger);
        case FAIR_PRIO:
            // Similar to PriorityQueue, but handles packets more fairly
            return new FairPriorityQueue(linkspeed_bps, memFromPkt(FEEDER_BUFFER), *_eventlist,
                                         queue_logger);
        default:
            // Uncaught exception, will eventually lead to abort() but hopefully with error message
            // first
            throw runtime_error(
                "Error allocating host queue: queue type is invalid or hasn't been implemented");
    }
}

Queue* TwoDimensionalTorusTopology::alloc_queue(QueueLogger* queue_logger,
                                                linkspeed_bps linkspeed_bps,
                                                mem_b queue_size,
                                                Switch* sw) {
    switch (_switch_queue_type) {
        case RANDOM:
            // Simple queue that randomly decides whether to drop packet when above a given
            // threshold (set through RANDOM_BUFFER), and drops incoming packet when full
            return new RandomQueue(linkspeed_bps, queue_size, *_eventlist, queue_logger,
                                   memFromPkt(RANDOM_BUFFER));
        case ECN:
            // Explicit Congestion Notification, sends pause frames to signal when buffer is full,
            // but drops incoming packet
            return new ECNQueue(linkspeed_bps, queue_size, *_eventlist, queue_logger,
                                memFromPkt(15));
        case LOSSLESS:
            // Sets a threshold below max size, and when reached sends a pause frame back to sender
            // until ready to receive again. If it receives a pause frame, it waits until sender is
            // ready to receive again
            // NOTE: queue type uses thresholds which are fixed, but also too small for large packet
            // flows (+500 000 pkts in flight), and which will cause LOSSLESS-ness to fail for torus
            // Always prefer LOSSLESS_INPUT, where thresholds can be set statically/per run
            return new LosslessQueue(linkspeed_bps, queue_size, *_eventlist, queue_logger, sw);
        case LOSSLESS_INPUT:
            // We need to pair this queue to a LosslessInputQueue we'll create, so actually return
            // LosslessOutputQueue. Only input queues can send pause frame, output queues must wait
            return new LosslessOutputQueue(linkspeed_bps, queue_size, *_eventlist, queue_logger);
        case LOSSLESS_INPUT_ECN:
            // Emulate (actually lossless) ECN by having virtually inexhaustible buffer size
            // NOTE: Because of its queue size, it should be preferred for large simulations
            return new LosslessOutputQueue(linkspeed_bps, memFromPkt(10000), *_eventlist,
                                           queue_logger);
        case CTRL_PRIO:
            // Queue that sets packets as high or low priority and serves them accordingly
            return new CtrlPrioQueue(linkspeed_bps, queue_size, *_eventlist, queue_logger);
        case COMPOSITE:
            // Complex queue type: contains high and low priority buffers, with weighted round
            // robin that heavily prioritises high priority packets. Can use ECN on low priority
            // queue. When that queue is full, can trim packets down to header and put them in high
            // priority queue, or drop them entirely if not trimming.

            //! _trim_size is size packet is trimmed down to, i.e. header's size
            //! _ecn_threshold is size starting which ECN is triggered
            //! _ecn_low and _ecn_high are respectively soft and hard thresholds: if queue_size
            //! exceeds _ecn_high, ECN is triggered. Else, if only exceedes low, then ECN is
            //! triggered only with a given probability proportional to fullness of queue. Setting
            //! single threshold means _ecn_high == _ecn_low
            return new CompositeQueue(linkspeed_bps, queue_size, *_eventlist, queue_logger,
                                      _composite_q_cfg.trim_size, _composite_q_cfg.disable_trim);
        case COMPOSITE_ECN: {
            CompositeQueue* q =
                new CompositeQueue(linkspeed_bps, queue_size, *_eventlist, queue_logger,
                                   _composite_q_cfg.trim_size, _composite_q_cfg.disable_trim);
            q->set_ecn_thresholds(_composite_q_cfg.ecn_min_threshold,
                                  _composite_q_cfg.ecn_max_threshold);
            return q;
        }
        default:
            // Uncaught exception, will eventually lead to abort() but hopefully with error message
            // first
            throw runtime_error(
                "Error allocating switch queue: queue type is invalid or hasn't been implemented");
    }
}

/*
 * Since the minimal adaptive routing algorithm is based off dimension-ordered routing, we shall
 * output the DOR routes, going along X direction first and then on Y direction.
 * Furthermore, because the Pipes in the road represent virtual channels, the switches can choose
 * whether to use another virtual channel from the ones outputted here
 */
vector<const Route*>* TwoDimensionalTorusTopology::get_bidir_paths(uint32_t src,
                                                                   uint32_t dest,
                                                                   bool reverse) {
    vector<const Route*>* paths = new vector<const Route*>();
    Route *route_src_dest, *route_src_dest_2, *route_src_dest_3, *route_src_dest_4;
    // DOR admits one, two, or four minimal routes
    route_src_dest = new Route();
    route_src_dest_2 = nullptr;
    route_src_dest_3 = nullptr;
    route_src_dest_4 = nullptr;

    // cout << "--------------------------------------------------------------" << endl;
    // cout << "Creating route between src=" << src << " and dest=" << dest << endl;

    // Coords are saved in [i, j, host_position] == [y-dimension, x-dimension, host_position]
    vector<uint32_t> pkt_coords = get_coords_of_host[src];
    vector<uint32_t> dest_coords = get_coords_of_host[dest];
    // cout << "src_x=" << pkt_coords[1] << ", src_y=" << pkt_coords[0] << endl;
    // cout << "dst_x=" << dest_coords[1] << ", dst_y=" << dest_coords[0] << endl;

    // First, handle case where src and dest belong to the same switch
    if (pkt_coords[0] == dest_coords[0] && pkt_coords[1] == dest_coords[1]) {
        // Route up into switch
        route_src_dest->push_back(
            host_to_switch_queues[pkt_coords[0]][pkt_coords[1]][pkt_coords[2]]);
        route_src_dest->push_back(
            host_to_switch_pipes[pkt_coords[0]][pkt_coords[1]][pkt_coords[2]]);
        route_src_dest->push_back(host_to_switch_queues[pkt_coords[0]][pkt_coords[1]][pkt_coords[2]]
                                      ->getRemoteEndpoint());
        // Route down from switch to dest host
        route_src_dest->push_back(
            switch_to_host_queues[dest_coords[0]][dest_coords[1]][dest_coords[2]]);
        route_src_dest->push_back(
            switch_to_host_pipes[dest_coords[0]][dest_coords[1]][dest_coords[2]]);

        if (reverse) {
            Route* route_dest_src = new Route();
            route_dest_src->push_back(
                host_to_switch_queues[dest_coords[0]][dest_coords[1]][dest_coords[2]]);
            route_dest_src->push_back(
                host_to_switch_pipes[dest_coords[0]][dest_coords[1]][dest_coords[2]]);
            route_dest_src->push_back(
                host_to_switch_queues[dest_coords[0]][dest_coords[1]][dest_coords[2]]
                    ->getRemoteEndpoint());
            route_dest_src->push_back(
                switch_to_host_queues[pkt_coords[0]][pkt_coords[1]][pkt_coords[2]]);
            route_dest_src->push_back(
                switch_to_host_pipes[pkt_coords[0]][pkt_coords[1]][pkt_coords[2]]);

            route_src_dest->set_reverse(route_dest_src);
            route_dest_src->set_reverse(route_src_dest);
        }
    }

    // In dimension-ordered routing, we use two virtual channels per link.
    // Then, when routing a packet from (x,y) to (x', y'), we first route it over one dimension, in
    // our case over the X-dimension first, until the packet reach x', and only then is it routed
    // along the Y dimension, until the packet reaches y'.
    // By default, the packet starts on VC0. However, if for instance when travelling across the X
    // dimension, the packet takes a wrap around link (i.e. the packet went from (0, y) to (m-1, y)
    // or vice-versa), then after wrapping around the packet will use VC1. Doing so eliminates
    // possible loops on the X dimension, and subsequently deadlocks. The same applies on the Y
    // dimension. This ensures deadlock-free and minimal path routing.

    // X-dimension routing
    if (pkt_coords[1] != dest_coords[1]) {
        // Three possibilities:
        // 1. Packet needs to go east for min route (1 min route)
        // 2. Packet needs to go west for min route (1 min route)
        // 3. Packet can go east or west if routes are equidistant (2 min routes)

        // 1. Eastward case: (x < x' && wrapping around to the west is slower) || (x > x' &&
        // wrapping around to the east is faster)
        if (((pkt_coords[1] < dest_coords[1]) && ((dest_coords[1] - pkt_coords[1]) * 2 < _m)) ||
            ((pkt_coords[1] > dest_coords[1]) && ((pkt_coords[1] - dest_coords[1]) * 2 > _m))) {
            // Create single eastward route
            // cout << "Min path is eastward" << endl;
            _add_eastward_route_to(route_src_dest, pkt_coords, dest_coords, reverse);
        }

        // 2. Westward case: (x > x' && wrapping around to the east is slower) || (x < x' &&
        // wrapping around to the west is faster)
        if (((pkt_coords[1] > dest_coords[1]) && ((pkt_coords[1] - dest_coords[1]) * 2 < _m)) ||
            ((pkt_coords[1] < dest_coords[1]) && ((dest_coords[1] - pkt_coords[1]) * 2 > _m))) {
            // Create single westward route
            // cout << "Min path is westward" << endl;
            _add_westward_route_to(route_src_dest, pkt_coords, dest_coords, reverse);
        }

        // 3. Two min routes: (|x->x'| == |x'->x| == _m/2), can only happen when m is even
        if ((pkt_coords[1] - dest_coords[1] == _m / 2) ||
            (dest_coords[1] - pkt_coords[1] == _m / 2)) {
            // Add both east and west routes
            // cout << "East and west both valid min paths" << endl;
            route_src_dest_2 = new Route();
            _add_x_routes_to(route_src_dest, route_src_dest_2, pkt_coords, dest_coords, reverse);
        }
    }

    // If we got here, X dimension was corrected, so set
    pkt_coords[1] = dest_coords[1];  // (the above loop doesn't update it)

    // Y-dimension routing
    if (pkt_coords[0] != dest_coords[0]) {
        // Same logic as before, but with more subcases:
        // 1.1. Packet must go north, and we start with a single route (end with 1 min route)
        //   2. Packet must go north, and we start with two possible routes (end with 2 min routes)
        //
        // 2.1. Packet must go south, and we start with a single route (end with 1 min route)
        //   2. Packet must go south, and we start with two possible routes (end with 2 min routes)
        //
        // 3.1. Packet can go either way, and we start with a single route (end with 2 min routes)
        //   2. Packet can go either way, and we start with two possible routes (4 min routes)
        //
        // NOTE: we'll use route_src_dest_2's nullity as a proxy for if there's a second route

        // 1. Northward case: (y > y' && southward wrap-around is slower) || (y < y' &&
        // wrap-around north is faster)
        if (((pkt_coords[0] > dest_coords[0]) && ((pkt_coords[0] - dest_coords[0]) * 2 < _n)) ||
            ((pkt_coords[0] < dest_coords[0]) && ((dest_coords[0] - pkt_coords[0]) * 2 > _n))) {
            // 1. We start with a single route
            // cout << "Min path is northward" << endl;
            _add_northward_route_to(route_src_dest, pkt_coords, dest_coords, reverse);

            // 2. We start with two possible routes
            if (route_src_dest_2 != nullptr) {
                // cout << "Two min paths" << endl;
                _add_northward_route_to(route_src_dest_2, pkt_coords, dest_coords, reverse);
            }
        }

        // 2. Southward case: (y < y' && wrap-around to the north is slower) || (y > y' &&
        // wrap-around to the south is faster)
        if (((pkt_coords[0] < dest_coords[0]) && ((dest_coords[0] - pkt_coords[0]) * 2 < _n)) ||
            ((pkt_coords[0] > dest_coords[0]) && ((pkt_coords[0] - dest_coords[0]) * 2 > _n))) {
            // cout << "Min path is southward" << endl;
            // 1. Single route
            _add_southward_route_to(route_src_dest, pkt_coords, dest_coords, reverse);
            if (route_src_dest_2 != nullptr) {
                // cout << "Two min paths" << endl;
                // 2. Two routes
                _add_southward_route_to(route_src_dest_2, pkt_coords, dest_coords, reverse);
            }
        }

        // 3. Two min routes: (|y->y'| == |y-y'| == _n/2), can only happen when n is even
        if ((pkt_coords[0] - dest_coords[0] == _n / 2) ||
            (dest_coords[0] - pkt_coords[0] == _n / 2)) {
            // cout << "North and south both min paths" << endl;
            // 1. Single route: copy route_src_dest into route3, so as to create one variant that
            // goes north and another that goes south
            route_src_dest_3 = route_src_dest->clone();
            // NOTE: clone() only makes a shallow copy of the reverse, meaning route and route3
            // would share the same reverse, which we don't want as each will go in a different
            // direction and have different reverse routes
            if (route_src_dest->reverse() != nullptr) {
                Route* rev_clone = route_src_dest->reverse()->clone();
                route_src_dest_3->set_reverse(rev_clone);
                rev_clone->set_reverse(route_src_dest_3);
            }
            _add_y_routes_to(route_src_dest, route_src_dest_3, pkt_coords, dest_coords, reverse);

            if (route_src_dest_2 != nullptr) {
                // cout << "Four min paths" << endl;
                // 2. Two routes: copy route_src_dest_2 into route_src_dest_4 and do the same as
                // above
                route_src_dest_4 = route_src_dest_2->clone();
                if (route_src_dest_2->reverse() != nullptr) {
                    Route* rev_clone = route_src_dest_2->reverse()->clone();
                    route_src_dest_4->set_reverse(rev_clone);
                    rev_clone->set_reverse(route_src_dest_4);
                }
                _add_y_routes_to(route_src_dest_2, route_src_dest_4, pkt_coords, dest_coords,
                                 reverse);
            }
        }
    }

    paths->push_back(route_src_dest);
    if (route_src_dest_2 != nullptr) {
        paths->push_back(route_src_dest_2);
    }
    if (route_src_dest_3 != nullptr) {
        paths->push_back(route_src_dest_3);
        if (route_src_dest_4 != nullptr)
            paths->push_back(route_src_dest_4);
    }

    // cout << "Route created" << endl;

    return paths;
}

/*
 * Constructs eastward route on X-dimension from source's x to destination's x. This method
 * doesn't handle Y-dimension routing.
 * The construction follows dimension-ordered routing starting from the X-dimension, and so uses
 * VC0 until a possible wrap-around, after which it uses VC1.
 */
void TwoDimensionalTorusTopology::_add_eastward_route_to(Route* route,
                                                         const vector<uint32_t>& src_coords,
                                                         const vector<uint32_t>& dest_coords,
                                                         bool reverse) {
    vector<uint32_t> pkt_coords = src_coords;
    vector<uint32_t> reverse_pkt_coords = dest_coords;
    bool has_wrapped_around = false;
    bool rev_has_wrapped_around = false;

    // cout << "-----------------" << endl;
    // cout << "Printing eastward path" << endl;
    Route* reverse_route = (reverse) ? new Route() : nullptr;

    // First push the host's egress queues into route to allow packets into switch.
    route->push_back(host_to_switch_queues[pkt_coords[0]][pkt_coords[1]][pkt_coords[2]]);
    route->push_back(host_to_switch_pipes[pkt_coords[0]][pkt_coords[1]][pkt_coords[2]]);
    route->push_back(
        host_to_switch_queues[pkt_coords[0]][pkt_coords[1]][pkt_coords[2]]->getRemoteEndpoint());
    // cout << "Host to switch route added" << endl;

    if (reverse) {
        reverse_route->push_back(host_to_switch_queues[reverse_pkt_coords[0]][reverse_pkt_coords[1]]
                                                      [reverse_pkt_coords[2]]);
        reverse_route->push_back(host_to_switch_pipes[reverse_pkt_coords[0]][reverse_pkt_coords[1]]
                                                     [reverse_pkt_coords[2]]);
        reverse_route->push_back(host_to_switch_queues[reverse_pkt_coords[0]][reverse_pkt_coords[1]]
                                                      [reverse_pkt_coords[2]]
                                                          ->getRemoteEndpoint());
        // cout << "Reverse host to switch route added" << endl;
    }

    // Then move east
    while (pkt_coords[1] != dest_coords[1]) {
        bool this_hop_wraps = (pkt_coords[1] == _m - 1);
        uint32_t vc = (has_wrapped_around || this_hop_wraps) ? 1 : 0;

        // cout << "Using VC" << vc << endl;
        // cout << "Add eastward route: (" << pkt_coords[1] << ", " << pkt_coords[0] << ")-->";
        // Keep moving east, using wrap-around if necessary
        route->push_back(east_egress_queues[pkt_coords[0]][pkt_coords[1]][vc]);
        route->push_back(eastbound_pipes[pkt_coords[0]][pkt_coords[1]][vc]);
        route->push_back(east_egress_queues[pkt_coords[0]][pkt_coords[1]][vc]->getRemoteEndpoint());

        // Increment x-coord with possible wrap-around
        if (pkt_coords[1] == _m - 1) {
            pkt_coords[1] = 0;
            has_wrapped_around = true;
        } else {
            pkt_coords[1]++;
        }
        // cout << "(" << pkt_coords[1] << ", " << pkt_coords[0] << ")" << endl;

        if (reverse) {
            // Apply same logic but from dest to src, so reverse route
            bool this_hop_wraps = (reverse_pkt_coords[1] == 0);
            uint32_t vc = (rev_has_wrapped_around || this_hop_wraps) ? 1 : 0;

            // cout << "Using VC" << vc << endl;
            // cout << "Add reverse westward route: (" << reverse_pkt_coords[1] << ", "
            // << reverse_pkt_coords[0] << ")-->";
            reverse_route->push_back(
                west_egress_queues[reverse_pkt_coords[0]][reverse_pkt_coords[1]][vc]);
            reverse_route->push_back(
                westbound_pipes[reverse_pkt_coords[0]][reverse_pkt_coords[1]][vc]);
            reverse_route->push_back(
                west_egress_queues[reverse_pkt_coords[0]][reverse_pkt_coords[1]][vc]
                    ->getRemoteEndpoint());

            if (reverse_pkt_coords[1] == 0) {
                reverse_pkt_coords[1] = _m - 1;
                rev_has_wrapped_around = true;
            } else {
                reverse_pkt_coords[1]--;
            }
            // cout << "(" << pkt_coords[1] << ", " << pkt_coords[0] << ")" << endl;
        }
    }

    // If dest is reached, route packet from switch to host
    // if (pkt_coords[0] == dest_coords[0]) {
    //     route->push_back(switch_to_host_queues[dest_coords[0]][dest_coords[1]][dest_coords[2]]);
    //     route->push_back(switch_to_host_pipes[dest_coords[0]][dest_coords[1]][dest_coords[2]]);
    //     // cout << "Route to destination host added" << endl;
    //     if (reverse) {
    //         reverse_route->push_back(
    //             switch_to_host_queues[src_coords[0]][src_coords[1]][src_coords[2]]);
    //         reverse_route->push_back(
    //             switch_to_host_pipes[src_coords[0]][src_coords[1]][src_coords[2]]);
    //         // cout << "Reverse route to destination host added" << endl;
    //     }
    // }
    // NOTE: This last piece of routing is now left to TwoDimensionalTorusSwitch::receivePacket()

    if (reverse) {
        route->set_reverse(reverse_route);
        reverse_route->set_reverse(route);
    }
    // cout << "Route created" << endl;
}

/*
 * Constructs westward route on X-dimension from source's x to destination's x. This method
 * doesn't handle Y-dimension routing.
 * The construction follows dimension-ordered routing starting from the X-dimension, and so uses
 * VC0 until a possible wrap-around, after which it uses VC1.
 */
void TwoDimensionalTorusTopology::_add_westward_route_to(Route* route,
                                                         const vector<uint32_t>& src_coords,
                                                         const vector<uint32_t>& dest_coords,
                                                         bool reverse) {
    vector<uint32_t> pkt_coords = src_coords;
    vector<uint32_t> reverse_pkt_coords = dest_coords;
    bool has_wrapped_around = false;
    bool rev_has_wrapped_around = false;

    // cout << "-----------------" << endl;
    // cout << "Printing westward path" << endl;
    Route* reverse_route = (reverse) ? new Route() : nullptr;

    // First push the host's egress queues into route to allow packets into switch.
    route->push_back(host_to_switch_queues[pkt_coords[0]][pkt_coords[1]][pkt_coords[2]]);
    route->push_back(host_to_switch_pipes[pkt_coords[0]][pkt_coords[1]][pkt_coords[2]]);
    route->push_back(
        host_to_switch_queues[pkt_coords[0]][pkt_coords[1]][pkt_coords[2]]->getRemoteEndpoint());
    // cout << "Host to switch route added" << endl;

    if (reverse) {
        reverse_route->push_back(host_to_switch_queues[reverse_pkt_coords[0]][reverse_pkt_coords[1]]
                                                      [reverse_pkt_coords[2]]);
        reverse_route->push_back(host_to_switch_pipes[reverse_pkt_coords[0]][reverse_pkt_coords[1]]
                                                     [reverse_pkt_coords[2]]);
        reverse_route->push_back(host_to_switch_queues[reverse_pkt_coords[0]][reverse_pkt_coords[1]]
                                                      [reverse_pkt_coords[2]]
                                                          ->getRemoteEndpoint());
        // cout << "Reverse host to switch route added" << endl;
    }

    while (pkt_coords[1] != dest_coords[1]) {
        bool this_hop_wraps = (pkt_coords[1] == 0);
        uint32_t vc = (has_wrapped_around || this_hop_wraps) ? 1 : 0;

        // cout << "Using VC" << vc << endl;
        // cout << "Add westward route: (" << pkt_coords[1] << ", " << pkt_coords[0] << ")-->";
        // Keep moving west, using wrap-around if necessary
        route->push_back(west_egress_queues[pkt_coords[0]][pkt_coords[1]][vc]);
        route->push_back(westbound_pipes[pkt_coords[0]][pkt_coords[1]][vc]);
        route->push_back(west_egress_queues[pkt_coords[0]][pkt_coords[1]][vc]->getRemoteEndpoint());

        // Decrement x-coord with possible wrap-around
        if (pkt_coords[1] == 0) {
            pkt_coords[1] = _m - 1;
            has_wrapped_around = true;
        } else {
            pkt_coords[1]--;
        }
        // cout << "(" << pkt_coords[1] << ", " << pkt_coords[0] << ")" << endl;

        if (reverse) {
            // Apply same logic but from dest to src, so reverse route
            bool this_hop_wraps = (reverse_pkt_coords[1] == _m - 1);
            uint32_t vc = (rev_has_wrapped_around || this_hop_wraps) ? 1 : 0;

            // cout << "Using VC" << vc << endl;
            // cout << "Add reverse eastward route: (" << reverse_pkt_coords[1] << ", "
            //      << reverse_pkt_coords[0] << ")-->";

            reverse_route->push_back(
                east_egress_queues[reverse_pkt_coords[0]][reverse_pkt_coords[1]][vc]);
            reverse_route->push_back(
                eastbound_pipes[reverse_pkt_coords[0]][reverse_pkt_coords[1]][vc]);
            reverse_route->push_back(
                east_egress_queues[reverse_pkt_coords[0]][reverse_pkt_coords[1]][vc]
                    ->getRemoteEndpoint());

            if (reverse_pkt_coords[1] == _m - 1) {
                reverse_pkt_coords[1] = 0;
                rev_has_wrapped_around = true;
            } else {
                reverse_pkt_coords[1]++;
            }
            // cout << "(" << reverse_pkt_coords[1] << ", " << reverse_pkt_coords[0] << ")" << endl;
        }
    }

    // If dest is reached, route packet from switch to host
    // if (pkt_coords[0] == dest_coords[0]) {
    //     route->push_back(switch_to_host_queues[dest_coords[0]][dest_coords[1]][dest_coords[2]]);
    //     route->push_back(switch_to_host_pipes[dest_coords[0]][dest_coords[1]][dest_coords[2]]);
    //     // cout << "Route to destination host added" << endl;
    //     if (reverse) {
    //         reverse_route->push_back(
    //             switch_to_host_queues[src_coords[0]][src_coords[1]][src_coords[2]]);
    //         reverse_route->push_back(
    //             switch_to_host_pipes[src_coords[0]][src_coords[1]][src_coords[2]]);
    //         // cout << "Reverse route to destination host added" << endl;
    //     }
    // }
    // NOTE: This last piece of routing is now left to TwoDimensionalTorusSwitch::receivePacket()

    if (reverse) {
        route->set_reverse(reverse_route);
        reverse_route->set_reverse(route);
    }
}

/*
 * This method is only used when source-destination routes are of the same length going east as
 * going west, so along the x-axis.
 */
void TwoDimensionalTorusTopology::_add_x_routes_to(Route* route1,
                                                   Route* route2,
                                                   const vector<uint32_t>& src_coords,
                                                   const vector<uint32_t>& dest_coords,
                                                   bool reverse) {
    _add_eastward_route_to(route1, src_coords, dest_coords, reverse);
    _add_westward_route_to(route2, src_coords, dest_coords, reverse);
}

void TwoDimensionalTorusTopology::_add_northward_route_to(Route* route,
                                                          const vector<uint32_t>& src_coords,
                                                          const vector<uint32_t>& dest_coords,
                                                          bool reverse) {
    vector<uint32_t> pkt_coords = src_coords;
    vector<uint32_t> reverse_pkt_coords = dest_coords;
    // Update reverse pkt coords to reflect that reverse route has corrected X-dimension
    reverse_pkt_coords[1] = src_coords[1];

    bool has_wrapped_around = false;
    bool rev_has_wrapped_around = false;

    // cout << "-----------------" << endl;
    // cout << "Printing northward path" << endl;
    Route* reverse_route = nullptr;

    if (reverse) {
        // Reconstruct reverse route (can't use route->reverse() directly as it's a const)
        // NOTE: if src and dest have the same X coords, then route's reverse was never initialised
        reverse_route = (route->reverse() == nullptr) ? new Route() : route->reverse()->clone();
    }

    // If route wasn't initialised because x == x', do it here by pushing packets from host to
    // switch
    if (route->size() == 0) {
        route->push_back(host_to_switch_queues[pkt_coords[0]][pkt_coords[1]][pkt_coords[2]]);
        route->push_back(host_to_switch_pipes[pkt_coords[0]][pkt_coords[1]][pkt_coords[2]]);
        route->push_back(host_to_switch_queues[pkt_coords[0]][pkt_coords[1]][pkt_coords[2]]
                             ->getRemoteEndpoint());
        // cout << "Host to switch route added" << endl;
        if (reverse) {
            reverse_route->push_back(
                host_to_switch_queues[reverse_pkt_coords[0]][reverse_pkt_coords[1]]
                                     [reverse_pkt_coords[2]]);
            reverse_route->push_back(
                host_to_switch_pipes[reverse_pkt_coords[0]][reverse_pkt_coords[1]]
                                    [reverse_pkt_coords[2]]);
            reverse_route->push_back(
                host_to_switch_queues[reverse_pkt_coords[0]][reverse_pkt_coords[1]]
                                     [reverse_pkt_coords[2]]
                                         ->getRemoteEndpoint());
            // cout << "Reverse host to switch route added" << endl;
        }
    }

    while (pkt_coords[0] != dest_coords[0]) {
        bool this_hop_wraps = (pkt_coords[0] == 0);
        uint32_t vc = (has_wrapped_around || this_hop_wraps) ? 1 : 0;

        // cout << "Using VC" << vc << endl;
        // cout << "Add northward route: (" << pkt_coords[1] << ", " << pkt_coords[0] << ")-->";

        route->push_back(north_egress_queues[pkt_coords[0]][pkt_coords[1]][vc]);
        route->push_back(northbound_pipes[pkt_coords[0]][pkt_coords[1]][vc]);
        route->push_back(
            north_egress_queues[pkt_coords[0]][pkt_coords[1]][vc]->getRemoteEndpoint());

        if (pkt_coords[0] == 0) {
            pkt_coords[0] = _n - 1;
            has_wrapped_around = true;
        } else {
            pkt_coords[0]--;
        }
        // cout << "(" << pkt_coords[1] << ", " << pkt_coords[0] << ")" << endl;

        if (reverse) {
            bool this_hop_wraps = (reverse_pkt_coords[0] == _n - 1);
            uint32_t vc = (rev_has_wrapped_around || this_hop_wraps) ? 1 : 0;

            // cout << "Using VC" << vc << endl;
            // cout << "Add reverse southward route: (" << reverse_pkt_coords[1] << ", "
            //      << reverse_pkt_coords[0] << ")-->";

            reverse_route->push_back(
                south_egress_queues[reverse_pkt_coords[0]][reverse_pkt_coords[1]][vc]);
            reverse_route->push_back(
                southbound_pipes[reverse_pkt_coords[0]][reverse_pkt_coords[1]][vc]);
            reverse_route->push_back(
                south_egress_queues[reverse_pkt_coords[0]][reverse_pkt_coords[1]][vc]
                    ->getRemoteEndpoint());

            if (reverse_pkt_coords[0] == _n - 1) {
                reverse_pkt_coords[0] = 0;
                rev_has_wrapped_around = true;
            } else {
                reverse_pkt_coords[0]++;
            }
            // cout << "(" << pkt_coords[1] << ", " << pkt_coords[0] << ")" << endl;
        }
    }

    // Dest is reached, so route packet from switch to host
    // route->push_back(switch_to_host_queues[dest_coords[0]][dest_coords[1]][dest_coords[2]]);
    // route->push_back(switch_to_host_pipes[dest_coords[0]][dest_coords[1]][dest_coords[2]]);
    // // cout << "Route to destination host added" << endl;
    // if (reverse) {
    //     reverse_route->push_back(
    //         switch_to_host_queues[src_coords[0]][src_coords[1]][src_coords[2]]);
    //     reverse_route->push_back(switch_to_host_pipes[src_coords[0]][src_coords[1]][src_coords[2]]);
    //     // cout << "Reverse route to destination host added" << endl;
    // }
    // NOTE: This last piece of routing is now left to TwoDimensionalTorusSwitch::receivePacket()

    // Overwrite reverse route to update
    if (reverse) {
        // Avoid memory leak, as we dereference the old reverse route and won't reference it again
        delete route->reverse();
        route->set_reverse(reverse_route);
        reverse_route->set_reverse(route);
    }
}

void TwoDimensionalTorusTopology::_add_southward_route_to(Route* route,
                                                          const vector<uint32_t>& src_coords,
                                                          const vector<uint32_t>& dest_coords,
                                                          bool reverse) {
    vector<uint32_t> pkt_coords = src_coords;
    vector<uint32_t> reverse_pkt_coords = dest_coords;
    // Update reverse pkt coords to reflect that reverse route has corrected X-dimension
    reverse_pkt_coords[1] = src_coords[1];

    bool has_wrapped_around = false;
    bool rev_has_wrapped_around = false;

    // cout << "-----------------" << endl;
    // cout << "Printing southward path" << endl;
    Route* reverse_route = nullptr;

    if (reverse) {
        reverse_route = (route->reverse() == nullptr) ? new Route() : route->reverse()->clone();
    }

    // If route wasn't initialised because x == x', do it here by pushing packets from host to
    // switch
    if (route->size() == 0) {
        route->push_back(host_to_switch_queues[pkt_coords[0]][pkt_coords[1]][pkt_coords[2]]);
        route->push_back(host_to_switch_pipes[pkt_coords[0]][pkt_coords[1]][pkt_coords[2]]);
        route->push_back(host_to_switch_queues[pkt_coords[0]][pkt_coords[1]][pkt_coords[2]]
                             ->getRemoteEndpoint());
        // cout << "Host to switch route added" << endl;
        if (reverse) {
            reverse_route->push_back(
                host_to_switch_queues[reverse_pkt_coords[0]][reverse_pkt_coords[1]]
                                     [reverse_pkt_coords[2]]);
            reverse_route->push_back(
                host_to_switch_pipes[reverse_pkt_coords[0]][reverse_pkt_coords[1]]
                                    [reverse_pkt_coords[2]]);
            reverse_route->push_back(
                host_to_switch_queues[reverse_pkt_coords[0]][reverse_pkt_coords[1]]
                                     [reverse_pkt_coords[2]]
                                         ->getRemoteEndpoint());
            // cout << "Reverse host to switch route added" << endl;
        }
    }

    while (pkt_coords[0] != dest_coords[0]) {
        bool this_hop_wraps = (pkt_coords[0] == _n - 1);
        uint32_t vc = (has_wrapped_around || this_hop_wraps) ? 1 : 0;

        // cout << "Using VC" << vc << endl;
        // cout << "Add southward route: (" << pkt_coords[1] << ", " << pkt_coords[0] << ")-->";

        route->push_back(south_egress_queues[pkt_coords[0]][pkt_coords[1]][vc]);
        route->push_back(southbound_pipes[pkt_coords[0]][pkt_coords[1]][vc]);
        route->push_back(
            south_egress_queues[pkt_coords[0]][pkt_coords[1]][vc]->getRemoteEndpoint());

        if (pkt_coords[0] == _n - 1) {
            pkt_coords[0] = 0;
            has_wrapped_around = true;
        } else {
            pkt_coords[0]++;
        }
        // cout << "(" << pkt_coords[1] << ", " << pkt_coords[0] << ")" << endl;

        if (reverse) {
            bool this_hop_wraps = (reverse_pkt_coords[0] == 0);
            uint32_t vc = (rev_has_wrapped_around || this_hop_wraps) ? 1 : 0;

            // cout << "Using VC" << vc << endl;
            // cout << "Add reverse northward route: (" << reverse_pkt_coords[1] << ", "
            //      << reverse_pkt_coords[0] << ")-->";

            reverse_route->push_back(
                north_egress_queues[reverse_pkt_coords[0]][reverse_pkt_coords[1]][vc]);
            reverse_route->push_back(
                northbound_pipes[reverse_pkt_coords[0]][reverse_pkt_coords[1]][vc]);
            reverse_route->push_back(
                north_egress_queues[reverse_pkt_coords[0]][reverse_pkt_coords[1]][vc]
                    ->getRemoteEndpoint());

            if (reverse_pkt_coords[0] == 0) {
                reverse_pkt_coords[0] = _n - 1;
                rev_has_wrapped_around = true;
            } else {
                reverse_pkt_coords[0]--;
            }
            // cout << "(" << pkt_coords[1] << ", " << pkt_coords[0] << ")" << endl;
        }
    }

    // Dest is reached, so route packet from switch to host
    // route->push_back(switch_to_host_queues[dest_coords[0]][dest_coords[1]][dest_coords[2]]);
    // route->push_back(switch_to_host_pipes[dest_coords[0]][dest_coords[1]][dest_coords[2]]);
    // // cout << "Route to destination host added" << endl;
    // if (reverse) {
    //     reverse_route->push_back(
    //         switch_to_host_queues[src_coords[0]][src_coords[1]][src_coords[2]]);
    //     reverse_route->push_back(switch_to_host_pipes[src_coords[0]][src_coords[1]][src_coords[2]]);
    //     // cout << "Reverse route to destination host added" << endl;
    // }
    // NOTE: This last piece of routing is now left to TwoDimensionalTorusSwitch::receivePacket()

    if (reverse) {
        delete route->reverse();
        route->set_reverse(reverse_route);
        reverse_route->set_reverse(route);
    }
}

void TwoDimensionalTorusTopology::_add_y_routes_to(Route* route1,
                                                   Route* route2,
                                                   const vector<uint32_t>& src_coords,
                                                   const vector<uint32_t>& dest_coords,
                                                   bool reverse) {
    _add_northward_route_to(route1, src_coords, dest_coords, reverse);
    _add_southward_route_to(route2, src_coords, dest_coords, reverse);
}

void TwoDimensionalTorusTopology::add_switch_loggers(Logfile& logfile,
                                                     simtime_picosec sample_period) {
    for (uint32_t i = 0; i < _n; i++) {
        for (uint32_t j = 0; j < _m; j++) {
            switches[i][j]->add_logger(logfile, sample_period);
        }
    }
}

/*
 * Not implemented, as it is unclear what a host's neighbour is (linked to same switch, or hosts
 * linked to neighbouring switches?)
 */
vector<uint32_t>* TwoDimensionalTorusTopology::get_neighbours(uint32_t src) {
    return nullptr;
}
