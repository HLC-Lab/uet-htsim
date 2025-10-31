// -*- c-basic-offset: 4; indent-tabs-mode: nil -*-
#include "dragonfly_plus_topology.h"
#include <stdexcept>
#include <vector>
#include <algorithm>
#include "compositequeue.h"
#include "connection_matrix.h"
#include "dragonfly_plus_switch.h"
#include "main.h"
#include "string.h"
#include "switch.h"

// Use tokenize from connection matrix
extern void tokenize(std::string const& str, const char delim, std::vector<std::string>& out);

// Default: ECN disabled
bool DragonflyPlusTopology::_enable_ecn = false;
mem_b DragonflyPlusTopology::_ecn_low = 0;
mem_b DragonflyPlusTopology::_ecn_high = 0;

// Default: all links 200 Gbps
linkspeed_bps DragonflyPlusTopology::_link_speed_global = 200ULL * 1000000000ULL;
linkspeed_bps DragonflyPlusTopology::_link_speed_local = 200ULL * 1000000000ULL;
linkspeed_bps DragonflyPlusTopology::_link_speed_host = 200ULL * 1000000000ULL;

// Default: global links 1000 ns, local links 500 ns, host links 200 ns
simtime_picosec DragonflyPlusTopology::_link_latency_global = 1000 * 1000;
simtime_picosec DragonflyPlusTopology::_link_latency_local = 500 * 1000;
simtime_picosec DragonflyPlusTopology::_link_latency_host = 200 * 1000;

// Default: all switches 0 ns
simtime_picosec DragonflyPlusTopology::_switch_latency = 0 * 1000;

// Default: number of link parallel is 1
uint32_t DragonflyPlusTopology::_no_parallel_link = 1;

std::string ntoa(double n);
std::string itoa(uint64_t n);

DragonflyPlusTopology::DragonflyPlusTopology(uint32_t k, uint32_t s, uint32_t l, uint32_t h,uint32_t p, uint32_t no_of_hosts, queue_type queue_type, 
    mem_b queue_size, QueueLoggerFactory* logger_factory, EventList* event_list, topology_type topo_type, uint64_t t, uint32_t no_par_link, linkspeed_bps linkspeed, simtime_picosec latency, simtime_picosec switch_latency) {
    _queue_type = queue_type;
    _queue_size = queue_size;
    _logger_factory = logger_factory;
    _event_list = event_list;
    _topology_type = topo_type;

    _h = h;
    _p = p;
    _s = s;
    _l = l;
    _t = t;
    _no_hosts = no_of_hosts;
    _no_parallel_link = no_par_link;

    if (_t == 0){
        // default queue size threshold
        _t = _queue_size/2;
    }

    if (linkspeed != 0){
        _link_speed_global = linkspeed;
        _link_speed_local = linkspeed;
        _link_speed_host = linkspeed;
    }

    if (latency != 0){
        _link_latency_global = latency;
        _link_latency_local = latency;
        _link_latency_host = latency;
    }

    if (switch_latency != 0){
        _switch_latency = switch_latency;
    }

    // if we want a integer number of nodes, k must be an even number
    uint32_t k2 = k*k;
    uint32_t k4 = k2*k2;
    uint32_t possible_nodes = (k4/16) + (k2/4);
    while (possible_nodes < no_of_hosts) {
        k = k+2;
        k2 = k*k;
        k4 = k2*k2;
        possible_nodes = (k4/16) + (k2/4);
    }

    _k = k;
    _no_hosts = possible_nodes;

    assert(_k > 0);

    if (_p != 0){
        if (_s == 0){
            _s = _k - _p;
        } else {
            if (_s + _p != _k){
                cerr << "Must have k = s + p, requested k: " << _k << " s: " << _s << " p: " << _p << endl;
                exit(1);
            }
        }
    } else {
        if (_s == 0){
            // default p = s = k/2
            _p = _k/2;
            _s = _k/2;
        } else {
            _p = _k - _s;
        }
    }

    if (_l != 0){
        if (_h == 0){
            _h = _k - _l;
        } else {
            if (_h + _l != _k){
                cerr << "Must have k = h + l, requested k: " << _k << " h: " << _h << " l: " << _l << endl;
                exit(1);
            }
        }
    } else {
        if (_h == 0){
            // default h = l = k/2
            _l = _k/2;
            _h = _k/2;
        } else {
            _l = _k - _h;
        }
    }

    _a = _s + _l;

    if (topo_type == LARGE){
        _no_groups = _s * _h + 1;
        cout << "Dragonfly+ with large topology type" << endl;
    } else if (topo_type == MEDIUM){
        _no_groups = _h + 1;
        cout << "Dragonfly+ with medium topology type" << endl;
    } else {
        // h must be a multiple of _no_parallel_link
        if (_h % _no_parallel_link != 0){
            cerr << "Must have h as a multiple of  the number of parallel link, requested h: " << _h << " parallel link: " << _no_parallel_link << endl;
            exit(1);
        }
        cout << "Dragonfly+ with small topology type with "<< _no_parallel_link << " number of parallel link" << endl;
        _no_groups = (_h / _no_parallel_link) + 1;
    }

    cout << "Link latencies: " << timeAsUs(_link_latency_global) << " and switch latencies: " << timeAsUs(_switch_latency) <<endl;

    _no_switches = _a * _no_groups;

    init_link_latencies();
    init_pipes_queues();
    init_network();

    cout << "DragonFly+ constructor done, " << _no_hosts << " nodes created\n";
}

void DragonflyPlusTopology::init_link_latencies() {
    link_latencies_.resize(_no_switches, std::unordered_map<uint32_t, simtime_picosec>());

    // i = each switch
    for (uint32_t i = 0; i < _no_switches; i++) {
        uint32_t group_id = i / _a;
        uint32_t local_id = i % _a;

        if (local_id < (_l)){
            // i is a leaf switch
            // j = each spine switch within same group
            for (uint32_t j = ((group_id * _a) + _s); j < (group_id + 1) * _a; j++) {
                set_link_latency(i, j, _link_latency_local);
            }
        } else {
            // i is a spine switch
            // Between groups
            // j = each switch to form cross-group connection with
            if (_topology_type == LARGE){
                uint32_t position = local_id - _l + 1;
                uint32_t previous_link = group_id;
                if (previous_link < (_h * position)){
                    uint32_t start = 0;
                    if (previous_link <= (_h*(position-1))){
                        start = (_h*(position-1))-previous_link;
                        previous_link = 0;
                    } else {
                        previous_link = previous_link - (_h*(position-1));
                    }
                    uint32_t possible_link = _h - previous_link;
                    for (uint32_t k = 1 + start; k <= possible_link + start; k++){
                        uint32_t new_position = (group_id/_h);
                        uint32_t j = ((group_id + k)*_a) + _s + new_position;
                        set_link_latency(i, j, _link_latency_global);
                    }
                }
            } else {
                // for medium and small topology because every parallel link between the same two switch has the same link_latency
                uint32_t position = local_id - _l;
                for (uint32_t k = group_id + 1; k < _no_groups; k++){
                    uint32_t j = (k *_a) + _l + position;
                    set_link_latency(i, j, _link_latency_global);
                }
            }
        }
    }
}

void DragonflyPlusTopology::init_pipes_queues() {
    uint32_t _no_switches_lf = _no_groups * _l;
    uint32_t _no_switches_sp = _no_groups * _s;

    switches_lf.resize(_no_switches_lf, NULL);
    switches_sp.resize(_no_switches_sp, NULL);

    queues_host_leaf.resize(_no_hosts, std::vector<Queue*>(_no_switches_lf));
    queues_leaf_spine.resize(_no_switches_lf, std::vector<Queue*>(_no_switches_sp));
    queues_spine_leaf.resize(_no_switches_sp, std::vector<Queue*>(_no_switches_lf));
    queues_leaf_host.resize(_no_switches_lf, std::vector<Queue*>(_no_hosts));

    pipes_host_leaf.resize(_no_hosts, std::vector<Pipe*>(_no_switches_lf));
    pipes_leaf_spine.resize(_no_switches_lf, std::vector<Pipe*>(_no_switches_sp));
    pipes_spine_leaf.resize(_no_switches_sp, std::vector<Pipe*>(_no_switches_lf));
    pipes_leaf_host.resize(_no_switches_lf, std::vector<Pipe*>(_no_hosts));

    queues_spine_spine.resize(_no_switches_sp, vector< vector<Queue*> >(_no_switches_sp, vector<Queue*>(_no_parallel_link)));
    pipes_spine_spine.resize(_no_switches_sp, vector< vector<Pipe*> >(_no_switches_sp, vector<Pipe*>(_no_parallel_link)));

    // Initializes all queues and pipes to NULL
    for (uint32_t i = 0; i < _no_switches_lf; i++) {
        for (uint32_t j = 0; j < _no_switches_sp; j++) {
            queues_leaf_spine[i][j] = NULL;
            queues_spine_leaf[j][i] = NULL;
            pipes_leaf_spine[i][j] = NULL;
            pipes_spine_leaf[j][i] = NULL;
        }

        for (uint32_t j = 0; j < _no_hosts; j++) {
            queues_host_leaf[j][i] = NULL;
            queues_leaf_host[i][j] = NULL;
            pipes_host_leaf[j][i] = NULL;
            pipes_leaf_host[i][j] = NULL;
        }
    }

    for (uint32_t i = 0; i < _no_switches_sp; i++) {
        for (uint32_t j = 0; j < _no_switches_sp; j++) {
            for (uint32_t b = 0; b < _no_parallel_link; b++) {
                queues_spine_spine[i][j][b] = NULL;
                pipes_spine_spine[i][j][b] = NULL;
            }
        }
    }
}

void DragonflyPlusTopology::init_network() {
    QueueLogger* queue_logger;
    uint32_t _no_switches_lf = _no_groups * _l;
    uint32_t _no_switches_sp = _no_groups * _s;

    // Create the leaf switches
    for (uint32_t i = 0; i < (_no_switches_lf); i++)
        switches_lf[i] = new DragonflyPlusSwitch(*_event_list, "Switch_Leaf_" + ntoa(i),
                                          DragonflyPlusSwitch::LEAF, i, _switch_latency, this);

    // Create the spine switches
    for (uint32_t i = 0; i < (_no_switches_sp); i++)
        switches_sp[i] = new DragonflyPlusSwitch(*_event_list, "Switch_Spine_" + ntoa(i),
                                          DragonflyPlusSwitch::SPINE, i, _switch_latency, this);

    // Create all links between Leaf Switches and Hosts
    // i = each leaf switch
    // j = each host per switch
    for (uint32_t i = 0; i < _no_switches_lf; i++) {
        for (uint32_t ip = 0; ip < _p; ip++) {
            uint32_t j = i * _p + ip;

            // Leaf Switch to Host
            
            queue_logger = _logger_factory == NULL ? NULL : _logger_factory->createQueueLogger();

            queues_leaf_host[i][j] =
                alloc_switch_queue(queue_logger, _link_speed_host, _queue_size);
            queues_leaf_host[i][j]->setName("LF" + ntoa(i) + "->DST" + ntoa(j));

            pipes_leaf_host[i][j] = new Pipe(_link_latency_host, *_event_list);
            pipes_leaf_host[i][j]->setName("Pipe-LF" + ntoa(i) + "->DST" + ntoa(j));

            // Host to Leaf Switch
            queue_logger = _logger_factory == NULL ? NULL : _logger_factory->createQueueLogger();


            queues_host_leaf[j][i] = alloc_host_queue(queue_logger, _link_speed_host);
            queues_host_leaf[j][i]->setName("SRC" + ntoa(j) + "->LF" + ntoa(i));

            pipes_host_leaf[j][i] = new Pipe(_link_latency_host, *_event_list);
            pipes_host_leaf[j][i]->setName("Pipe-SRC" + ntoa(j) + "->LF" + ntoa(i));

            // Add ports and set remote endpoints
            switches_lf[i]->addPort(queues_leaf_host[i][j]);
            switches_lf[i]->addPort(queues_host_leaf[j][i]);
            queues_host_leaf[j][i]->setRemoteEndpoint(switches_lf[i]);

            if (_queue_type==LOSSLESS_INPUT || _queue_type == LOSSLESS_INPUT_ECN){
                    //no virtual queue needed at server
                    new LosslessInputQueue(*_event_list, queues_host_leaf[j][i], switches_lf[i], _link_latency_host);
                }
        }
    }


    // Create all links between Leaf Switches and Spine Switches
    // i = each leaf switch
    for (uint32_t i = 0; i < _no_switches_lf; i++) {
        uint32_t group_id = i / _l;
        for (uint32_t ip = 0; ip < _s; ip++){
            uint32_t j = group_id * _l + ip;

            // global position of switches for _link_latencies_
            uint32_t global_leaf = group_id * _a + ( i % _l);
            uint32_t global_spine = group_id * _a + _l + ( j % _s);

            // i → j
            queue_logger = _logger_factory == NULL ? NULL : _logger_factory->createQueueLogger();

            queues_leaf_spine[i][j] =
                alloc_switch_queue(queue_logger, _link_speed_local, _queue_size);
            queues_leaf_spine[i][j]->setName("LF" + ntoa(i) + "->SP" + ntoa(j));

            pipes_leaf_spine[i][j] = new Pipe(get_link_latency(global_leaf, global_spine), *_event_list);
            pipes_leaf_spine[i][j]->setName("Pipe-LF" + ntoa(i) + "->SP" + ntoa(j));

            // j → i
            queue_logger = _logger_factory == NULL ? NULL : _logger_factory->createQueueLogger();

            queues_spine_leaf[j][i] =
                alloc_switch_queue(queue_logger, _link_speed_local, _queue_size);
            queues_spine_leaf[j][i]->setName("SP" + ntoa(j) + "->LF" + ntoa(i));

            pipes_spine_leaf[j][i] = new Pipe(get_link_latency(global_leaf, global_spine), *_event_list);
            pipes_spine_leaf[j][i]->setName("Pipe-SP" + ntoa(j) + "->LF" + ntoa(i));

            // Add ports and set remote endpoints

            switches_lf[i]->addPort(queues_leaf_spine[i][j]);
            switches_sp[j]->addPort(queues_spine_leaf[j][i]);
            queues_leaf_spine[i][j]->setRemoteEndpoint(switches_sp[j]);
            queues_spine_leaf[j][i]->setRemoteEndpoint(switches_lf[i]);

            if (_queue_type == LOSSLESS_INPUT || _queue_type == LOSSLESS_INPUT_ECN){
                        new LosslessInputQueue(*_event_list, queues_leaf_spine[i][j], switches_sp[j], get_link_latency(global_leaf, global_spine));
                        new LosslessInputQueue(*_event_list, queues_spine_leaf[j][i], switches_lf[i], get_link_latency(global_leaf, global_spine));
            }
        }
    }

    // Between groups
    // Create all links between Spine Switches and Spine Switches of other groups
    // i = each leaf switch
    for (uint32_t i = 0; i < _no_switches_sp; i++) {
        uint32_t group_id = i / _s;

        if (_topology_type == LARGE){
            uint32_t position = ( i % _s ) + 1;
            uint32_t previous_link = group_id;

            if (previous_link < (_h * position)){
                uint32_t start = 0;
                if (previous_link <= (_h*(position-1))){
                    start = (_h*(position-1))-previous_link;
                    previous_link = 0;
                } else {
                    previous_link = previous_link - (_h*(position-1));
                }

                uint32_t possible_link = _h - previous_link;

                for (uint32_t k = 1 + start; k <= possible_link + start; k++){
                    uint32_t new_position = (group_id/_h);
                    uint32_t j = ((group_id + k)*_s) + new_position;

                    uint32_t group_id_j = j / _s;
                        
                    uint32_t global_i = group_id * _a + _l + ( i % _s);
                    uint32_t global_j = group_id_j * _a + _l + ( j % _s);

                    for (uint32_t b = 0; b < _no_parallel_link; b++){
                        // i → j
                        queue_logger = _logger_factory == NULL ? NULL : _logger_factory->createQueueLogger();

                        queues_spine_spine[i][j][b] =
                            alloc_switch_queue(queue_logger, _link_speed_global, _queue_size);
                        queues_spine_spine[i][j][b]->setName("SP" + ntoa(i) + "->SP" + ntoa(j) + "(" + ntoa(b) + ")");

                        pipes_spine_spine[i][j][b] = new Pipe(get_link_latency(global_i, global_j), *_event_list);
                        pipes_spine_spine[i][j][b]->setName("Pipe-SP" + ntoa(i) + "->SP" + ntoa(j) + "(" + ntoa(b) + ")");

                        // j → i
                        queue_logger = _logger_factory == NULL ? NULL : _logger_factory->createQueueLogger();

                        queues_spine_spine[j][i][b] =
                            alloc_switch_queue(queue_logger, _link_speed_local, _queue_size);
                        queues_spine_spine[j][i][b]->setName("SP" + ntoa(j) + "->SP" + ntoa(i) + "(" + ntoa(b) + ")");

                        pipes_spine_spine[j][i][b] = new Pipe(get_link_latency(global_i, global_j), *_event_list);
                        pipes_spine_spine[j][i][b]->setName("Pipe-SP" + ntoa(j) + "->SP" + ntoa(i) + "(" + ntoa(b) + ")");

                        // Add ports and set remote endpoints
                        switches_sp[i]->addPort(queues_spine_spine[i][j][b]);
                        switches_sp[j]->addPort(queues_spine_spine[j][i][b]);
                        queues_spine_spine[i][j][b]->setRemoteEndpoint(switches_sp[j]);
                        queues_spine_spine[j][i][b]->setRemoteEndpoint(switches_sp[i]);

                        if (_queue_type == LOSSLESS_INPUT || _queue_type == LOSSLESS_INPUT_ECN){
                            new LosslessInputQueue(*_event_list, queues_spine_spine[i][j][b], switches_sp[j], get_link_latency(global_i, global_j));
                            new LosslessInputQueue(*_event_list, queues_spine_spine[j][i][b], switches_sp[i], get_link_latency(global_i, global_j));
                        }
                    }
                }
            }
        } else {
            uint32_t position = i % _s;
            for (uint32_t k = group_id + 1; k < _no_groups; k++){
                uint32_t j = (k *_s) + position;
                for (uint32_t b = 0; b < _no_parallel_link; b++){
                    // i → j
                    queue_logger = _logger_factory == NULL ? NULL : _logger_factory->createQueueLogger();

                    uint32_t group_id_j = j / _s;
                        
                    uint32_t global_i = group_id * _a + _l + ( i % _s);
                    uint32_t global_j = group_id_j * _a + _l + ( j % _s);

                    queues_spine_spine[i][j][b] =
                        alloc_switch_queue(queue_logger, _link_speed_global, _queue_size);
                    queues_spine_spine[i][j][b]->setName("SP" + ntoa(i) + "->SP" + ntoa(j) + "(" + ntoa(b) + ")");

                    pipes_spine_spine[i][j][b] = new Pipe(get_link_latency(global_i, global_j), *_event_list);
                    pipes_spine_spine[i][j][b]->setName("Pipe-SP" + ntoa(i) + "->SP" + ntoa(j) + "(" + ntoa(b) + ")");

                    // j → i
                    queue_logger = _logger_factory == NULL ? NULL : _logger_factory->createQueueLogger();

                    queues_spine_spine[j][i][b] =
                        alloc_switch_queue(queue_logger, _link_speed_local, _queue_size);
                    queues_spine_spine[j][i][b]->setName("SP" + ntoa(j) + "->SP" + ntoa(i) + "(" + ntoa(b) + ")");

                    pipes_spine_spine[j][i][b] = new Pipe(get_link_latency(global_i, global_j), *_event_list);
                    pipes_spine_spine[j][i][b]->setName("Pipe-SP" + ntoa(j) + "->SP" + ntoa(i) + "(" + ntoa(b) + ")");

                    // Add ports and set remote endpoints
                    switches_sp[i]->addPort(queues_spine_spine[i][j][b]);
                    switches_sp[j]->addPort(queues_spine_spine[j][i][b]);
                    queues_spine_spine[i][j][b]->setRemoteEndpoint(switches_sp[j]);
                    queues_spine_spine[j][i][b]->setRemoteEndpoint(switches_sp[i]);

                    if (_queue_type == LOSSLESS_INPUT || _queue_type == LOSSLESS_INPUT_ECN){
                        new LosslessInputQueue(*_event_list, queues_spine_spine[i][j][b], switches_sp[j], get_link_latency(global_i, global_j));
                        new LosslessInputQueue(*_event_list, queues_spine_spine[j][i][b], switches_sp[i], get_link_latency(global_i, global_j));
                    }
                }
            }
        }
    }

    if (_queue_type==LOSSLESS) {
        for (uint32_t j=0;j<_no_switches_lf;j++){
            switches_lf[j]->configureLossless();
        }
        for (uint32_t j=0;j<_no_switches_sp;j++){
            switches_sp[j]->configureLossless();
        }
    }
}

std::vector<const Route*>* DragonflyPlusTopology::get_bidir_paths(uint32_t src,
                                                              uint32_t dest,
                                                              bool reverse) {
    // TODO: implement (if needed)
    throw std::logic_error("Not implemented");
}

uint32_t DragonflyPlusTopology::get_group_switch(uint32_t src_group, uint32_t dst_group, Packet& pkt, uint32_t hash) {
    if (_topology_type == SMALL || _topology_type == MEDIUM){
        // scelgo randomicamente a quale spine mandarlo
        uint32_t choise = freeBSDHash(pkt.flow_id(),pkt.pathid(),hash) % _s;
        return src_group*_a + _l + choise;
    } else {
        uint32_t steps = dst_group / _h;
        if (src_group < dst_group){
            if (dst_group % _h == 0){
                steps = (dst_group / _h) - 1;
            }
        }
        return src_group*_a + _l + steps;
    }
}

uint32_t DragonflyPlusTopology::get_target_switch(uint32_t src_switch, uint32_t global_link) {
    uint32_t src_group = src_switch / _a;
    uint32_t src_switch_index = src_switch - (src_group * _a);
    uint32_t right_steps = (src_switch_index * _h) + global_link + 1;
    uint32_t dst_group = (src_group + right_steps) % _no_groups;
    return (dst_group * _a) + (_a - 1) - src_switch_index;
}

linkspeed_bps DragonflyPlusTopology::get_linkspeed() {
    // WARNING: same linkspeed assumed for now for CC
    assert(_link_speed_global == _link_speed_local);
    assert(_link_speed_local == _link_speed_host);
    return _link_speed_global;
}

std::string DragonflyPlusTopology::get_link_latencies() {
    return std::to_string(_link_latency_global) + ":" + std::to_string(_link_latency_local) + ":" +
           std::to_string(_link_latency_host);
}

inline void DragonflyPlusTopology::set_link_latency(uint32_t src_switch,
                                                uint32_t dst_switch,
                                                simtime_picosec latency) {
    link_latencies_[min(src_switch, dst_switch)][max(src_switch, dst_switch)] = latency;
}

inline simtime_picosec DragonflyPlusTopology::get_link_latency(uint32_t src_switch,
                                                           uint32_t dst_switch) {
    return link_latencies_[min(src_switch, dst_switch)][max(src_switch, dst_switch)];
}

Queue* DragonflyPlusTopology::alloc_host_queue(QueueLogger* queue_logger, linkspeed_bps linkspeed) {
    mem_b max_size = memFromPkt(FEEDER_BUFFER);
    return new FairPriorityQueue(linkspeed, max_size, *_event_list, queue_logger);
}

Queue* DragonflyPlusTopology::alloc_switch_queue(QueueLogger* queue_logger, linkspeed_bps linkspeed, mem_b queue_size){
                             //link_direction dir, int switch_tier, bool tor
    switch (_queue_type) {
    case RANDOM:{
        mem_b max_size = _queue_size;
        mem_b drop = memFromPkt(RANDOM_BUFFER);
        return new RandomQueue(linkspeed, max_size, *_event_list, queue_logger, drop);
    }
    case COMPOSITE:{
         throw std::logic_error("COMPOSITE queue not implemented for Dragonfly+");
        //return new CompositeQueue(linkspeed, queue_size, *_event_list, queue_logger);
    }
    case CTRL_PRIO:{
        return new CtrlPrioQueue(linkspeed, queue_size, *_event_list, queue_logger);
    }
    case LOSSLESS:{
        return new LosslessQueue(linkspeed, queue_size, *_event_list, queue_logger, NULL);
    }
    case LOSSLESS_INPUT:{
        return new LosslessOutputQueue(linkspeed, queue_size, *_event_list, queue_logger);
    }
    default:{
        throw std::logic_error("Not implemented");
    }
    }
}

void DragonflyPlusTopology::add_switch_loggers(Logfile& log, simtime_picosec sample_period) {
    uint32_t _no_switches_lf = _no_groups * _l;
    uint32_t _no_switches_sp = _no_groups * _s;
    for (uint32_t i = 0; i < _no_switches_lf; i++) {
        switches_lf[i]->add_logger(log, sample_period);
    }
    for (uint32_t i = 0; i < _no_switches_sp; i++) {
        switches_sp[i]->add_logger(log, sample_period);
    }
}

DragonflyPlusTopology* DragonflyPlusTopology::load(const char * filename, QueueLoggerFactory* logger_factory, EventList& eventlist, mem_b queuesize, queue_type q_type){
    std::ifstream file(filename);
    if (file.is_open()) {
        DragonflyPlusTopology* ft = load(file, logger_factory, eventlist, queuesize, q_type);
        file.close();
	return ft;
    } else {
        cerr << "Failed to open DragonFly+ config file " << filename << endl;
        exit(1);
    }
}

void to_low(string& s) {
    string::iterator i;
    for (i = s.begin(); i != s.end(); i++) {
        *i = std::tolower(*i);
    }
}

DragonflyPlusTopology* DragonflyPlusTopology::load(istream& file, QueueLoggerFactory* logger_factory, EventList& eventlist, mem_b queuesize, queue_type q_type){
    int no_of_hosts = 0;
    int k = 0;
    int l = 0;
    int s = 0;
    int h = 0;
    int p = 0;
    int t = 0;
    std::string line;
    std::string _size;
    
    while (std::getline(file, line)) {
        std::vector<std::string> tokens;
        tokenize(line, ' ', tokens);

        if (tokens.size() == 0 || tokens[0][0] == '#')
            continue;
        to_low(tokens[0]);

        if (tokens[0] == "k")
            k = stoi(tokens[1]);
        else if (tokens[0] == "l")
            l = stoi(tokens[1]);
        else if (tokens[0] == "s")
            s = stoi(tokens[1]);
        else if (tokens[0] == "h")
            h = stoi(tokens[1]);
        else if (tokens[0] == "p")
            p = stoi(tokens[1]);
        else if (tokens[0] == "t")
            t = stoi(tokens[1]);
        else if (tokens[0] == "nodes")
            no_of_hosts = stoi(tokens[1]);
        else if (tokens[0] == "size_topology")
            _size = tokens[1];
        else if (tokens[0] == "number_parallel_link")
            _no_parallel_link = stoi(tokens[1]);
        else if (tokens[0] == "switch_latency_ns")
            _switch_latency = timeFromNs(stoi(tokens[1]));
        else if (tokens[0] == "link_speed_global_gbps")
            _link_speed_global = speedFromGbps(stoi(tokens[1]));
        else if (tokens[0] == "link_speed_local_gbps")
            _link_speed_local = speedFromGbps(stoi(tokens[1]));
        else if (tokens[0] == "link_speed_host_gbps")
            _link_speed_host = speedFromGbps(stoi(tokens[1]));
        else if (tokens[0] == "link_latency_global_ns")
            _link_latency_global = timeFromNs(stoi(tokens[1]));
        else if (tokens[0] == "link_latency_local_ns")
            _link_latency_local = timeFromNs(stoi(tokens[1]));
        else if (tokens[0] == "link_latency_host_ns")
            _link_latency_host = timeFromNs(stoi(tokens[1]));
        else
            throw std::logic_error("Unexpected case");
    }

    if (k == 0) {
        cerr << "Missing router radix in file" << endl;
        exit(1);
    }

    topology_type topo_type;
    // if size is small, check if there is number of parallel link, potrei fare di defalut 2 link paralleli, ma da vedere

    if (_size == "large"){
        topo_type = LARGE;
        if (_no_parallel_link != 1){
            cerr << "Parallel link can't be more than 1 in large topology" << endl;
            exit(1);
        }
    } else if (_size == "medium"){
        topo_type = MEDIUM;
        if (_no_parallel_link != 1){
            cerr << "Parallel link can't be more than 1 in medium topology" << endl;
            exit(1);
        }
    } else if (_size == "small"){
        topo_type = SMALL;
    } else {
        //default topology
        topo_type = LARGE;
    }

    cout << "Topology load done\n";
    DragonflyPlusTopology* ft = new DragonflyPlusTopology(k, s, l, h, p, no_of_hosts, q_type, queuesize, logger_factory, &eventlist, topo_type, t, _no_parallel_link, 0, 0, 0);
    cout << "DragonFly+ constructor done, " << ft->get_no_hosts() << " nodes created\n";

    return ft;
}
