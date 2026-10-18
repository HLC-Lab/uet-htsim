// -*- c-basic-offset: 4; indent-tabs-mode: nil -*-
#include "dragonfly_plus_switch.h"
#include <random>
#include <stdexcept>
#include <cassert>
#include "dragonfly_plus_topology.h"

bool DragonflyPlusSwitch::_trim_disable = false;
uint16_t DragonflyPlusSwitch::_trim_size = 0;
uint16_t DragonflyPlusSwitch::_entropy_partition_threshold = 0;
DragonflyPlusSwitch::RoutingStrategy DragonflyPlusSwitch::_routing_strategy = DragonflyPlusSwitch::MINIMAL;
uint16_t DragonflyPlusSwitch::_minimal_share_pct = 10;  // Share of the entropy space pinned to the minimal global link at the source spine.
// Note for future upgrades this is NOT the resulting minimal fraction becauseMID, which still contains the minimal link as one of its g-1 members, so the
// real fraction is p + (1-p)/(g-1) -- 19% at the calibrated p=10 with 11 groups.
// Ignored while the entropy partition is active.
uint16_t DragonflyPlusSwitch::_low_share_pct = 10; // tuned
bool DragonflyPlusSwitch::_low_share_uniform = false;   // false = off

string ntoa(double n);
string itoa(uint64_t n);

DragonflyPlusSwitch::DragonflyPlusSwitch(EventList& event_list,
                                 string name,
                                 SwitchType type,
                                 uint32_t id,
                                 simtime_picosec delay,
                                 DragonflyPlusTopology* topo)
    : Switch(event_list, name) {
    _id = id;
    _fib = new RouteTable();

    _type = type;
    _topo = topo;
    _s = topo->get_s();
    _p = topo->get_p();
    _l = topo->get_l();
    _h = topo->get_h();
    _a = topo->get_a();
    _t = topo->get_t();
    _no_groups = topo->get_no_groups();

    _pipe = new CallbackPipe(delay, event_list, this);
    _hash_salt = (uint32_t)random();
      
    

    //trovo gli spine a cui è collegato ogni switch
    if (_type == LEAF){
        uint32_t this_group = _id / _l;
        for (uint32_t k=(this_group*_s); k<((this_group + 1)*_s);k++){
            _neighbours.push_back(k);
        }
    } else {
        uint32_t this_group = _id / _s;
        if (_topo->get_topology_type() == LARGE){
            uint32_t position = ( _id % _s ) + 1;
            uint32_t previous_link = this_group;

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
                    uint32_t new_position = (this_group/_h);
                    uint32_t j = ((this_group + k)*_s) + new_position;

                    _neighbours.push_back(j);  
                }

                for (uint32_t k = 1; k <= previous_link; k++){
                    uint32_t new_src = this_group - k;
                    uint32_t steps = this_group / _h;
                    if (new_src < this_group){
                        if (this_group % _h == 0){
                            steps = (this_group / _h) - 1;
                        }
                    }

                    uint32_t j = new_src*_s + steps;
                    _neighbours.push_back(j);
                }
            } else {
                for (uint32_t k = (position-1)*_h; k < position*_h; k++){
                    uint32_t steps = this_group / _h;
                    if (k < this_group){
                        if (this_group % _h == 0){
                            steps = (this_group / _h) - 1;
                        }
                    }

                    uint32_t j = k*_s + steps;
                    _neighbours.push_back(j);
                }
            }
        } else {
            uint32_t position = _id % _s;
            for (uint32_t k = 0; k < _no_groups; k++){
                if (k != this_group){
                    uint32_t j = (k *_s) + position;
                    _neighbours.push_back(j);
                }
            }
        }

    }
}

void DragonflyPlusSwitch::receivePacket(Packet& pkt) {
    if (_packets.find(&pkt) == _packets.end()) {
        _packets[&pkt] = true; 
        if (pkt.type() == UECDATA) {
            // Only count hops for forward data packets. Ack/Nack/Rts/Pull
            pkt.increment_hop_count();
        }
        const Route* next_hop = getNextHop(pkt, NULL);
        if (next_hop == NULL){
            cout << "è qui il problema" <<endl;
        }
        pkt.set_route(*next_hop);

        _pipe->receivePacket(pkt);
                //if (_type == LEAF && _id == _topo->get_host_switch(pkt.dst())) {
            //cout << "DELIVERED flow=" << pkt.flow_id()
              //   << " channel=" << pkt.get_channel()
                // << " hops=" << pkt.hop_count() << endl;
        //}
    } else {
        _packets.erase(&pkt);
        // TODO: check understanding (missing code from HTSIM - Packet signature changed)

        pkt.sendOn();
    }
}

void DragonflyPlusSwitch::addHostPort(int addr, int flowid, PacketSink* transport) {
    uint32_t i = _topo->get_host_switch(addr);
    uint32_t j = addr;
    Route* route = new Route();

    route->push_back(_topo->queues_leaf_host[i][j]);
    route->push_back(_topo->pipes_leaf_host[i][j]);

    route->push_back(transport);
    _fib->addHostRoute(addr, route, flowid);
}

void DragonflyPlusSwitch::permute_paths(vector<FibEntry*>* routes) {
    // TODO: check understanding
    // int len = routes->size();
    // for (int i = 0; i < len; i++) {
    //     int ix = random() % (len - i);
    //     FibEntry* path = (*routes)[ix];
    //     (*routes)[ix] = (*routes)[len - 1 - i];
    //     (*routes)[len - 1 - i] = path;
    // }
    throw std::logic_error("Not implemented");
}

uint32_t DragonflyPlusSwitch::get_next_switch_minimal(uint32_t this_switch, uint32_t dst_switch, Packet& pkt) {
    // this_switch e dst_switch sono locali e dst è sempre un leaf
    // quello che ritorno ha un valore globale
    uint32_t this_group;
    uint32_t dst_group = dst_switch / _l;

    if (_type == SPINE){
        this_group = this_switch / _s;

        // Switch within the same group as this switch
        if (this_group == dst_group){
            // lo mando al dst_switch globale
            uint32_t dst_s = dst_switch % _l;
            return dst_group*_a + dst_s;
        } else {
            if (_topo->get_topology_type() != LARGE) {                                
                return dst_group*_a + _l + (this_switch % _s);                        
            } 
            // lo mando allo spine giusto collegato al gruppo di destinazione
            uint32_t dst_group_switch = _topo->get_group_switch(dst_group, this_group, pkt, _hash_salt);
            return dst_group_switch;
        }
    } else {
        this_group = this_switch / _l;

        // Switch within the same group as this switch
        if (this_group == dst_group){
            // scelgo randomicamente a quale spine mandarlo
            uint32_t choise = freeBSDHash(pkt.flow_id(),pkt.pathid(),_hash_salt) % _s;
            return (this_group * _a + _l + choise);
        } else {
            // lo mando allo spine giusto collegato al gruppo di destinazione
              
            uint32_t group_switch = _topo->get_group_switch(this_group, dst_group, pkt, _hash_salt);
            return group_switch; 
        }
    }
}

QueueInfo DragonflyPlusSwitch::compare_queuesize(FibEntry* left, FibEntry* right){
    Route * r1= left->getEgressPort();
    assert(r1 && r1->size()>1);
    BaseQueue* q1 = dynamic_cast<BaseQueue*>(r1->at(0));
    Route * r2= right->getEgressPort();
    assert(r2 && r2->size()>1);
    BaseQueue* q2 = dynamic_cast<BaseQueue*>(r2->at(0));

    QueueInfo info;
    //cout <<"Ecco i valori delle code " << q1->queuesize() << " " << q2->queuesize() <<endl;

    if (q1->queuesize() < q2->queuesize()){
        info.compare_info = 1;
        info.queue_size = q1->queuesize();
    }
    else if (q1->queuesize() > q2->queuesize()){
        info.compare_info = -1;
        info.queue_size = q2->queuesize();
    }
    else {
        info.compare_info = 0;
        info.queue_size =q1->queuesize(); 
    }
    return info;
}

QueueChoice DragonflyPlusSwitch::fully_progressive_adaptive_route(vector<FibEntry*>* ecmp_set, QueueInfo (*cmp)(FibEntry*,FibEntry*)){
    //cout << "fully progressive adaptive routing" << endl;
    uint32_t choice = 0;

    uint32_t best_choices[256];
    uint32_t best_choices_count = 0;
  
    FibEntry* min = (*ecmp_set)[choice];
    best_choices[best_choices_count++] = choice;

    Route * r1= min->getEgressPort();
    assert(r1 && r1->size()>1);
    BaseQueue* q1 = dynamic_cast<BaseQueue*>(r1->at(0));
    uint64_t min_queue_size = q1->queuesize();

    for (uint32_t i = 1; i< ecmp_set->size(); i++){
        QueueInfo c = cmp(min,(*ecmp_set)[i]);

        if (c.compare_info < 0){
            choice = i;
            min = (*ecmp_set)[choice];
            min_queue_size = c.queue_size;
            best_choices_count = 0;
            best_choices[best_choices_count++] = choice;
        }
        else if (c.compare_info == 0){
            assert(best_choices_count<255);
            best_choices[best_choices_count++] = i;
        }        
    }

    assert (best_choices_count>=1);
    uint32_t choiceindex = random()%best_choices_count;
    choice = best_choices[choiceindex];
    //cout << "ECMP set choices " << ecmp_set->size() << " Choice count " << best_choices_count << " chosen entry " << choiceindex << " chosen path " << choice << " ";

    QueueChoice q_choice;
    q_choice.queue_choice = choice;
    q_choice.queue_size = min_queue_size;

    return q_choice;
}

FibEntry* DragonflyPlusSwitch::ecmp_select_combined(vector<FibEntry*>* minimal, vector<FibEntry*>* non_minimal, Packet& pkt, bool& from_primary) {

    uint32_t n_minimal = minimal ? minimal->size() : 0;
    uint32_t n_non_minimal = non_minimal ? non_minimal->size() : 0;
    uint32_t total = n_minimal + n_non_minimal;
    assert(total > 0);

    if (n_minimal > 0 && !_low_share_uniform) {
        uint32_t h = freeBSDHash(pkt.flow_id(), pkt.pathid(), _hash_salt);
        if (n_non_minimal > 0 && _low_share_pct > 0 && (h % 100) < _low_share_pct) {
            from_primary = false;
            // inverted salt so deflection choice does not correlate with h
            return (*non_minimal)[freeBSDHash(pkt.flow_id(), pkt.pathid(),
                                              ~_hash_salt) % n_non_minimal];
        }
        from_primary = true;
        return (*minimal)[h % n_minimal]; //reps share return
    }

    uint32_t idx = freeBSDHash(pkt.flow_id(), pkt.pathid(), _hash_salt) % total; // basically only for uniform now
    if (idx < n_minimal) {
        from_primary = true;
        return (*minimal)[idx];
    }
    from_primary = false;
    return (*non_minimal)[idx - n_minimal];
}

QueueInfo (*DragonflyPlusSwitch::fn)(FibEntry*,FibEntry*)= &DragonflyPlusSwitch::compare_queuesize;

Route* DragonflyPlusSwitch::getNextHop(Packet& pkt, BaseQueue* ingress_port) {
     uint32_t previous_channel = pkt.get_channel(); 

     assert(pkt.src() < _topo->get_no_hosts());     // <-- added
     uint32_t src_switch = _topo->get_host_switch(pkt.src());
     uint32_t src_group = src_switch / _l;
     
    vector<FibEntry*> * available_hops = _fib->getRoutes(pkt.dst());
    vector<FibEntry*> * available_hops_high = _fib->getRoutesHigh(pkt.dst());
    vector<FibEntry*> * available_hops_medium = _fib->getRoutesMedium(pkt.dst());
    vector<FibEntry*> * available_hops_low = _fib->getRoutesLow(pkt.dst());
    //uint32_t previous_channel = pkt.get_channel(); 
    //uint32_t src_switch = _topo->get_host_switch(pkt.src());
    //uint32_t src_group = src_switch / _l;

    uint32_t this_switch = _id;
    uint32_t dst_switch = _topo->get_host_switch(pkt.dst());
    uint32_t dst_group = dst_switch / _l;

    if (available_hops || available_hops_high || available_hops_medium || available_hops_low){
        // if available_hops is not null, i choose this switch
        uint32_t ecmp_choice = 0;

        if (available_hops){
            FibEntry* e = (*available_hops)[ecmp_choice];
            pkt.set_direction(e->getDirection());
            pkt.set_channel(e->getChannel());
            return e->getEgressPort();
        }

                if (pkt.type() != UECDATA) {
            vector<FibEntry*>* minimal = available_hops_high;
            if (!minimal)
                minimal = available_hops_medium ? available_hops_medium : available_hops_low;
            ecmp_choice = freeBSDHash(pkt.flow_id(), pkt.pathid(), _hash_salt) % minimal->size();
            FibEntry* e = (*minimal)[ecmp_choice];
            pkt.set_channel(1);
            pkt.set_direction(e->getDirection());
            return e->getEgressPort();
        }

        FibEntry* e;

        if (available_hops_high){
            ecmp_choice = freeBSDHash(pkt.flow_id(),pkt.pathid(),_hash_salt) % available_hops_high->size();
            e = (*available_hops_high)[ecmp_choice];
        }
        if (available_hops_medium){
            ecmp_choice = freeBSDHash(pkt.flow_id(),pkt.pathid(),_hash_salt) % available_hops_medium->size();
            e = (*available_hops_medium)[ecmp_choice];
        } else {
            ecmp_choice = freeBSDHash(pkt.flow_id(),pkt.pathid(),_hash_salt) % available_hops_low->size();
            e = (*available_hops_low)[ecmp_choice];
        }

        switch(_routing_strategy){
            case MINIMAL:
                //cout << " minimal routing" << endl;
                // need to choose between switch in avilable_hops_high
                //ecmp_choice = freeBSDHash(pkt.flow_id(),pkt.pathid(),_hash_salt) % available_hops_high->size();      OLD
                //e = (*available_hops_high)[ecmp_choice];   
                if (available_hops_high){
                    ecmp_choice = freeBSDHash(pkt.flow_id(),pkt.pathid(),_hash_salt) % available_hops_high->size();
                    e = (*available_hops_high)[ecmp_choice];
                } 
                else if (available_hops_medium){
                    ecmp_choice = freeBSDHash(pkt.flow_id(),pkt.pathid(),_hash_salt) % available_hops_medium->size();
                    e = (*available_hops_medium)[ecmp_choice];
                } 
                else {
                    ecmp_choice = freeBSDHash(pkt.flow_id(),pkt.pathid(),_hash_salt) % available_hops_low->size();
                    e = (*available_hops_low)[ecmp_choice];
                }                                                          
                break;
            case FPAR: {
                //cout << " fpar" << endl;
                int high = 0;
                int medium = 0;
                int low = 0; 
                QueueChoice high_choice, medium_choice, low_choice;
                if (_type == LEAF){
                    uint32_t this_group = this_switch / _l;
                    if (this_group == src_group){
                        if (available_hops_high){
                            high = 1;
                            high_choice = fully_progressive_adaptive_route(available_hops_high,fn); 
                        }
                        if (high && high_choice.queue_size <= _t){
                            ecmp_choice = high_choice.queue_choice;
                            e = (*available_hops_high)[ecmp_choice];
                        } else {
                            if (available_hops_medium){
                                medium = 1;
                                medium_choice = fully_progressive_adaptive_route(available_hops_medium,fn); 
                            } 
                            if (medium && medium_choice.queue_size <= _t){
                                ecmp_choice = medium_choice.queue_choice;
                                e = (*available_hops_medium)[ecmp_choice];
                            } else {
                                // if there isn't switch in lower priority with queue size lower than T, then choose switch in higher priority
                                if (high){
                                    ecmp_choice = high_choice.queue_choice;
                                    e = (*available_hops_high)[ecmp_choice];
                                } else if (medium) {
                                    ecmp_choice = medium_choice.queue_choice;
                                    e = (*available_hops_medium)[ecmp_choice];
                                } else {
                                    cerr << "ERRORE" << endl;
                                    exit(1);
                                }
                            }
                        }
                        pkt.set_channel(0);
                    } else {
                        ecmp_choice = freeBSDHash(pkt.flow_id(),pkt.pathid(),_hash_salt) % available_hops_high->size();
                        e = (*available_hops_high)[ecmp_choice];
                        pkt.set_channel(1);
                        /*the channel bit prevents a second non-minimal deflection after the first. Once channel=1, the code forces available_hops_high only (the direct global link to dst).
                         This means at most one intermediate group is traversed — the 8-link / 7-switch path is the maximum.
                         
                         problem: get_two_point_diameter_latency is blind to this     !!!!!!!!!!!!!!!!!
                         */
                    }
                } else {
                    uint32_t this_group = this_switch / _s;
                    if (previous_channel == 1){
                        if (available_hops_high){
                            ecmp_choice = freeBSDHash(pkt.flow_id(),pkt.pathid(),_hash_salt) % available_hops_high->size();
                            e = (*available_hops_high)[ecmp_choice];
                        } else if (available_hops_medium){
                            ecmp_choice = freeBSDHash(pkt.flow_id(),pkt.pathid(),_hash_salt) % available_hops_medium->size();
                            e = (*available_hops_medium)[ecmp_choice];
                        } else if (available_hops_low){
                            ecmp_choice = freeBSDHash(pkt.flow_id(),pkt.pathid(),_hash_salt) % available_hops_low->size();
                            e = (*available_hops_low)[ecmp_choice];
                        } else {
                            cerr << "ERRORE" << endl;
                            exit(1);
                        }
                        pkt.set_channel(1);
                    } else {
                        if (this_group == src_group){
                            if (available_hops_high){
                                high = 1;
                                high_choice = fully_progressive_adaptive_route(available_hops_high,fn); 
                            }
                            if (high && high_choice.queue_size <= _t){
                                ecmp_choice = high_choice.queue_choice;
                                e = (*available_hops_high)[ecmp_choice];
                            } else {
                                if (available_hops_medium){
                                    medium = 1;
                                    medium_choice = fully_progressive_adaptive_route(available_hops_medium,fn); 
                                } 
                                if (medium && medium_choice.queue_size <= _t){
                                    ecmp_choice = medium_choice.queue_choice;
                                    e = (*available_hops_medium)[ecmp_choice];
                                } else {
                                    // if there isn't switch in low priority with queue size lower than T, then choose switch in higher priority
                                    if (high){
                                        ecmp_choice = high_choice.queue_choice;
                                        e = (*available_hops_high)[ecmp_choice];
                                    } else if (medium) {
                                        ecmp_choice = medium_choice.queue_choice;
                                        e = (*available_hops_medium)[ecmp_choice];
                                    } else {
                                        cerr << "ERRORE" << endl;
                                        exit(1);
                                    }
                                }
                            }
                            pkt.set_channel(previous_channel);
                        } else {
                            if (available_hops_high){
                                high = 1;
                                high_choice = fully_progressive_adaptive_route(available_hops_high,fn); 
                            }
                            if (high && high_choice.queue_size <= _t){
                                ecmp_choice = high_choice.queue_choice;
                                e = (*available_hops_high)[ecmp_choice];
                                pkt.set_channel(1);
                            } else {
                                if (available_hops_low){
                                    low = 1;
                                    low_choice = fully_progressive_adaptive_route(available_hops_low,fn); 
                                } 
                                if (low && low_choice.queue_size <= _t){
                                    ecmp_choice = low_choice.queue_choice;
                                    e = (*available_hops_low)[ecmp_choice];
                                } else {
                                    // if there isn't switch in low priority with queue size lower than T, then choose switch in higher priority
                                    if (high){
                                        ecmp_choice = high_choice.queue_choice;
                                        e = (*available_hops_high)[ecmp_choice];
                                    } else if (low) {
                                        ecmp_choice = low_choice.queue_choice;
                                        e = (*available_hops_low)[ecmp_choice];
                                    } else {
                                        cerr << "ERRORE" << endl;
                                        exit(1);
                                    }
                                }
                                pkt.set_channel(previous_channel);
                            }
                        }
                    }
                }
                break;
            }
                case REPS_DFP: {
                //Static entropy-based routing that exposes both minimal and non minimal paths as one ECMP set. The path is selected purely from the packet entropy (pathid) set by REPS
                //The FPAR virtual-channel rules (VL0/VL1) are preserved so that a packet still crosses at most one intermediate group 
                bool from_primary = true;
                if (_type == LEAF) {
                    uint32_t this_group = this_switch / _l;
                    if (this_group == src_group) {
                        // Source leaf: every spine in the group is a valid first hop.ECMP draw over MID spreads the entropy across all spines.
                        bool minimal_half = _entropy_partition_threshold > 0 && pkt.pathid() < _entropy_partition_threshold;
                        
                        //vector<FibEntry*>* spines = (minimal_half && available_hops_high)
                                                       //? available_hops_high
                                                       //: (available_hops_medium ? available_hops_medium
                                                                            ////: available_hops_high);
                        (void)minimal_half;
                        vector<FibEntry*>* spines = available_hops_medium ? available_hops_medium : available_hops_high;
                        ecmp_choice = freeBSDHash(pkt.flow_id(),pkt.pathid(),_hash_salt) % spines->size();
                        e = (*spines)[ecmp_choice];
                        pkt.set_channel(0);
                    } else {
                        // Leaf reached in an intermediate group (via a LOW deflection), follow the minimal path to the destination on VL1
                        ecmp_choice = freeBSDHash(pkt.flow_id(),pkt.pathid(),_hash_salt) % available_hops_high->size();
                        e = (*available_hops_high)[ecmp_choice];
                        pkt.set_channel(1);
                    }
                } else {
                    uint32_t this_group = this_switch / _s;
                    if (previous_channel == 1) {
                        // Already committed to minimal path (VL1): stay minimal
                        if (available_hops_high) {
                            ecmp_choice = freeBSDHash(pkt.flow_id(),pkt.pathid(),_hash_salt) % available_hops_high->size();
                            e = (*available_hops_high)[ecmp_choice];
                        } else if (available_hops_medium) {
                            ecmp_choice = freeBSDHash(pkt.flow_id(),pkt.pathid(),_hash_salt) % available_hops_medium->size();
                            e = (*available_hops_medium)[ecmp_choice];
                        } else {
                            ecmp_choice = freeBSDHash(pkt.flow_id(),pkt.pathid(),_hash_salt) % available_hops_low->size();
                            e = (*available_hops_low)[ecmp_choice];
                        }
                        pkt.set_channel(1);
                    } 
                    else if (this_group == src_group) {
                        /*Source spine (VL0): every global link is a valid first global hop. MID contains all of them (the minimal link included), 
                        so an ECMP draw over MID selects either the minimal link or a non-minimal (Valiant) intermediate group from the entropy.*/
                        bool minimal_half = _entropy_partition_threshold > 0 && pkt.pathid() < _entropy_partition_threshold;
                        // The entropy partition and the minimal share both govern decision, so only one may be active at a time. The partition wins
                        const bool minshare_active = (_entropy_partition_threshold == 0) && (_minimal_share_pct > 0);
                        bool force_minimal = minimal_half;
                        if (minshare_active && available_hops_high) {
                            force_minimal = (freeBSDHash(pkt.flow_id(), pkt.pathid(), _hash_salt) % 100) < _minimal_share_pct;
                        }
                        vector<FibEntry*>* globals = (force_minimal && available_hops_high)
                                                        ? available_hops_high
                                                        : (available_hops_medium ? available_hops_medium
                                                                                  : available_hops_high);
                        // inverted salt so the index does not correlate with the draw above 
                        //only while the share is actually active, so every other configuration keeps its original hash
                        ecmp_choice = freeBSDHash(pkt.flow_id(), pkt.pathid(), minshare_active ? ~_hash_salt : _hash_salt) % globals->size();
                        e = (*globals)[ecmp_choice];
                        pkt.set_channel(0);
                    } 
                    else {
                        /*Intermediate spine (VL0): choose between the minimal global link toward the destination group (HIGH) and deflecting down to a leaf in this group (LOW). 
                        MID is never offered here, so at most one intermediate group is traversed.*/
                        e = ecmp_select_combined(available_hops_high, available_hops_low, pkt, from_primary);
                        if (from_primary) {
                            // minimal global link
                            pkt.set_channel(1);
                        } else {
                            // Deflected down to a leaf
                            pkt.set_channel(0);
                        }
                    }
                }
            }break;
        }

        pkt.set_direction(e->getDirection());
        
        return e->getEgressPort();
    }

    //no route table entries for this destination. Add them to FIB or fail. 
    if (_type == LEAF){
        if (this_switch == dst_switch) {
            HostFibEntry* host_entry = _fib->getHostRoute(pkt.dst(), pkt.flow_id());
            assert(host_entry);
            pkt.set_direction(DOWN);
            pkt.set_channel(0);
            return host_entry->getEgressPort();
        } else {
            //route packet up!
            //high priority
            uint32_t next_switch = get_next_switch_minimal(this_switch, dst_switch, pkt);

            uint32_t local_id_next = next_switch % _a;
            
            if (local_id_next < _l){
                cerr << "ERRORE" << endl;
                exit(1);
            }

            uint32_t group_id = next_switch / _a;
            uint32_t next_switch_local = group_id * _s + (local_id_next - _l);

            Route * route_high = new Route();

            route_high->push_back(_topo->queues_leaf_spine[_id][next_switch_local]);
            route_high->push_back(_topo->pipes_leaf_spine[_id][next_switch_local]);
            route_high->push_back(_topo->queues_leaf_spine[_id][next_switch_local]->getRemoteEndpoint());
            
            _fib->addRoutePriority(pkt.dst(),route_high,1, UP, HIGH, 0);

            //medium and low priority

            for (int num : _neighbours){

                Route * route_mid = new Route();

                route_mid->push_back(_topo->queues_leaf_spine[_id][num]);
                route_mid->push_back(_topo->pipes_leaf_spine[_id][num]);
                route_mid->push_back(_topo->queues_leaf_spine[_id][num]->getRemoteEndpoint());
                _fib->addRoutePriority(pkt.dst(),route_mid,1,UP, MID, 0);
            }
            
        }
    } else if (_type == SPINE) {
        uint32_t this_group = this_switch / _s;
        if ( dst_group == this_group) {
            //must go down!
            
            Route * route = new Route();

            route->push_back(_topo->queues_spine_leaf[_id][dst_switch]);
            route->push_back(_topo->pipes_spine_leaf[_id][dst_switch]);
            route->push_back(_topo->queues_spine_leaf[_id][dst_switch]->getRemoteEndpoint());
            _fib->addRoute(pkt.dst(),route,1, DOWN);
            
        } else {
            uint32_t next_switch = get_next_switch_minimal(this_switch, dst_switch, pkt);

            uint32_t local_id_next = next_switch % _a;  

            if (local_id_next < _l){
                cerr << "ERRORE" << endl;
                exit(1);
            }

            uint32_t next_switch_local = 0;
            uint32_t group_id = next_switch / _a;

            // next is a spine switch
            next_switch_local = group_id * _s + (local_id_next - _l);
            
            // devo controllare che next-switch local sia tra i vicini di this 

            for (uint32_t num : _neighbours) {
                if (num == next_switch_local){
                    // high priority
                    for (uint32_t b = 0; b < _topo->get_no_parallel_link(); b++){

                        Route * route_high = new Route();

                        route_high->push_back(_topo->queues_spine_spine[_id][next_switch_local][b]);
                        route_high->push_back(_topo->pipes_spine_spine[_id][next_switch_local][b]);
                        route_high->push_back(_topo->queues_spine_spine[_id][next_switch_local][b]->getRemoteEndpoint());
                        _fib->addRoutePriority(pkt.dst(),route_high,1, UP, HIGH, 0);
                    }
                }

                // medium priority
                // spine router connected with this_switch
                for (uint32_t b = 0; b < _topo->get_no_parallel_link(); b++){

                    Route *route_mid = new Route();

                    route_mid->push_back(_topo->queues_spine_spine[_id][num][b]);
                    route_mid->push_back(_topo->pipes_spine_spine[_id][num][b]);
                    route_mid->push_back(_topo->queues_spine_spine[_id][num][b]->getRemoteEndpoint());

                    _fib->addRoutePriority(pkt.dst(),route_mid,1,UP, MID, 0);
                }
            }

            //low priority
            //leaf router connected with this_switch
            for (uint32_t k=(this_group*_l); k<((this_group + 1)*_l);k++){
                Route * route_low = new Route();

                route_low->push_back(_topo->queues_spine_leaf[_id][k]);
                route_low->push_back(_topo->pipes_spine_leaf[_id][k]);
                route_low->push_back(_topo->queues_spine_leaf[_id][k]->getRemoteEndpoint());
                _fib->addRoutePriority(pkt.dst(),route_low,1,DOWN, LOW, 0);

            }   
            
        }
    }
    else {
        cerr << "Route lookup on switch with no proper type: " << _type << endl;
        abort();
    }

    //FIB has been filled in; return choice. 
    return getNextHop(pkt, ingress_port);


} 