// -*- c-basic-offset: 4; indent-tabs-mode: nil -*-
#include "dragonfly_plus_switch.h"
#include <random>
#include <stdexcept>
#include "dragonfly_plus_topology.h"

bool DragonflyPlusSwitch::_trim_disable = false;
uint16_t DragonflyPlusSwitch::_trim_size = 0;
DragonflyPlusSwitch::RoutingStrategy DragonflyPlusSwitch::_routing_strategy = DragonflyPlusSwitch::MINIMAL;

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
        const Route* next_hop = getNextHop(pkt, NULL);
        if (next_hop == NULL){
            cout << "è qui il problema" <<endl;
        }
        pkt.set_route(*next_hop);

        _pipe->receivePacket(pkt);
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

QueueInfo (*DragonflyPlusSwitch::fn)(FibEntry*,FibEntry*)= &DragonflyPlusSwitch::compare_queuesize;

Route* DragonflyPlusSwitch::getNextHop(Packet& pkt, BaseQueue* ingress_port) {

    vector<FibEntry*> * available_hops = _fib->getRoutes(pkt.dst());
    vector<FibEntry*> * available_hops_high = _fib->getRoutesHigh(pkt.dst());
    vector<FibEntry*> * available_hops_medium = _fib->getRoutesMedium(pkt.dst());
    vector<FibEntry*> * available_hops_low = _fib->getRoutesLow(pkt.dst());
    uint32_t previous_channel = pkt.get_channel(); 
    uint32_t src_switch = _topo->get_host_switch(pkt.src());
    uint32_t src_group = src_switch / _l;

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
                ecmp_choice = freeBSDHash(pkt.flow_id(),pkt.pathid(),_hash_salt) % available_hops_high->size();
                e = (*available_hops_high)[ecmp_choice];
                break;
            case FPAR:
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
                    }
                } else {
                    uint32_t this_group = this_switch / _s;
                    if (previous_channel == 1){
                        ecmp_choice = freeBSDHash(pkt.flow_id(),pkt.pathid(),_hash_salt) % available_hops_high->size();
                        e = (*available_hops_high)[ecmp_choice];
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
