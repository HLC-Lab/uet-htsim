// -*- c-basic-offset: 4; indent-tabs-mode: nil -*-
#include "uec_inc_src.h"
#include "network.h"
#include "uecpacket.h"
#include "uec.h"
#include "incpacket.h"
#include <iostream>

using namespace std;


UecIncNIC::UecIncNIC(id_t src_num, EventList& eventList, linkspeed_bps linkspeed, uint32_t ports)
    : UecNIC(src_num, eventList, linkspeed, ports) {
    // Il costruttore chiama la base, le variabili protected sono accessibili
}

void UecIncNIC::doNextEvent() {
    // 1. Trova e libera la porta
    uint32_t last_port = _no_of_ports;
    for (uint32_t p = 0; p < _no_of_ports; p++) {
        if (_ports[p].busy && _ports[p].send_end_time == eventlist().now()) {
            last_port = p;
            break;
        }
    }
    assert(last_port != _no_of_ports);
    _busy_ports--;
    _ports[last_port].busy = false;

    // --- PRIORITÀ 1: TRAFFICO INC (All-Reduce) ---
    if (!_active_srcs.empty()) {
        for (auto it = _active_srcs.begin(); it != _active_srcs.end(); ++it) {
            if ((*it)->dst() == UINT32_MAX) { 
                UecSrc* inc_src = *it;
                _active_srcs.erase(it); 
                const Route* route = inc_src->getPortRoute(findFreePort());
                inc_src->timeToSend(*route);
                return; 
            }
        }
    }

    // --- PRIORITÀ 2 e 3: Traffico Standard (Copia esatta da uec.cpp) ---
    if (!_active_srcs.empty() && !_control.empty()) {
        _crt++;
        if (_crt >= (_ratio_control + _ratio_data))
            _crt = 0;

        if (_crt < _ratio_data) {
            UecSrc* queued_src = _active_srcs.front();
            const Route* route = queued_src->getPortRoute(findFreePort());
            queued_src->timeToSend(*route);
            return;
        } else {
            sendControlPktNow();
            return;
        }
    }

    if (!_active_srcs.empty()) {
        UecSrc* queued_src = _active_srcs.front();
        const Route* route = queued_src->getPortRoute(findFreePort());
        queued_src->timeToSend(*route);
    } else if (!_control.empty()) {
        sendControlPktNow();
    }
}


////////////////////////////////////////////////////////////////
//  UEC INC SRC Implementation
////////////////////////////////////////////////////////////////

UecIncSrc::UecIncSrc(TrafficLogger* trafficLogger, 
                     EventList& eventList, 
                     unique_ptr<UecMultipath> mp, 
                     UecNIC& nic, 
                     uint32_t no_of_ports, 
                     bool rts)
    : UecSrc(trafficLogger, eventList, move(mp), nic, no_of_ports, rts) 
{
    // Destinazione fittizia per triggerare la logica INC negli switch
    setDst(UINT32_MAX);
    _nodename = "UecIncSrc " + to_string(_node_num);
}

void UecIncSrc::initIncNscc(mem_b cwnd, simtime_picosec peer_rtt) {
    // Configurazione aggressiva per INC
    _sender_based_cc = true;
    _receiver_based_cc = false; // All-Reduce solitamente non aspetta Pull

    // Eseguiamo il setup base ereditato
    initNscc(cwnd, peer_rtt);

    // Sovrascriviamo per All-Reduce: Fast Start massiccio
    double multiplier = 10.0;
    setMaxWnd(multiplier * _bdp);
    setConfiguredMaxWnd(multiplier * _bdp);
    _cwnd = _maxwnd; 
    
    cout << "INC_SRC [" << _nodename << "] Initialized for All-Reduce Job." << endl;
}

void UecIncSrc::receivePacket(Packet& pkt, uint32_t portnum) {
    if (pkt.type() == INC_RESULT) {
        IncPacket* inc_pkt = (IncPacket*)&pkt;
        UecDataPacket::seq_t seqno = inc_pkt->_inc_block_id;

        mem_b original_pkt_size = _mss; 
        auto it = _tx_bitmap.find(seqno);
        if (it != _tx_bitmap.end()) {
            original_pkt_size = it->second.pkt_size;
        }

        uint64_t fake_recvd_bytes = _recvd_bytes + original_pkt_size;
        _recvd_bytes += original_pkt_size; 
        if (_highest_recv_seqno < seqno) {
            _highest_recv_seqno = seqno; 
        }

        UecAckPacket* fake_ack = UecAckPacket::newpkt(
            _flow, NULL, seqno + 1, seqno, seqno, 0, false, fake_recvd_bytes, 0, 0
        );
        
        processAck(*fake_ack);

        if (_receiver_based_cc) {
            handlePull(_pull_target);
        }

        // cout << "INC_SRC [" << _nodename << "] ALL-REDUCE COMPLETE - Block: " << seqno << endl;
        
        fake_ack->free();
        pkt.free();
        return;
    }

    UecSrc::receivePacket(pkt, portnum);
}

mem_b UecIncSrc::sendNewPacket(const Route& route) {
    // Qui dobbiamo marcare i pacchetti come INC prima che vengano inviati
    // Per farlo in modo pulito, dobbiamo fare l'override della logica di creazione.
    
    // NOTA: Poiché in uec.cpp sendNewPacket crea il pacchetto e lo invia subito,
    // dobbiamo copiare qui la logica di creazione originale e aggiungere i tag INC:
    
    mem_b full_pkt_size = _mtu;
    if (_backlog < _mtu) full_pkt_size = _backlog;

    // Creazione del pacchetto dati standard
    UecDataPacket* p = UecDataPacket::newpkt(_flow, route, _highest_sent, full_pkt_size, 
                                            UecDataPacket::DATA_SPEC, _pull_target, _dstaddr);

    // --- AGGIUNTA TAG INC ---
    p->_is_inc = true;
    p->_inc_job_id = 100; // Esempio ID Job
    p->_inc_block_id = _highest_sent;
    p->_inc_int_data = (rand() % 100) + 1; // Dato simulato
    p->_inc_last_switch_id = -1;

    uint16_t ev = _mp->nextEntropy(_highest_sent, (uint64_t)_cwnd/_mss);
    p->set_pathid(ev);
    
    createSendRecord(_highest_sent, full_pkt_size);
    _in_flight += full_pkt_size;
    _backlog -= full_pkt_size;

    p->sendOn();
    
    _highest_sent++;
    _stats.new_pkts_sent++;
    startRTO(eventlist().now());

    return full_pkt_size;
}

mem_b UecIncSrc::sendRtxPacket(const Route& route) {
    if (_rtx_queue.empty()) {
        return 0;
    }
    auto it = _rtx_queue.begin();
    UecBasePacket::seq_t seqno = it->first;
    mem_b pkt_size = it->second;
    _rtx_queue.erase(it);

    UecDataPacket* p = UecDataPacket::newpkt(_flow, route, seqno, pkt_size, 
                                            UecDataPacket::DATA_SPEC, _pull_target, _dstaddr);

    p->_is_inc = true;
    p->_inc_job_id = 100;         // same job ID
    p->_inc_block_id = seqno;     // use seqno as block_id for simplicity
    p->_inc_int_data = 1;         // dummy data
    p->_inc_last_switch_id = -1;  // Reset 


    uint16_t ev = _mp->nextEntropy(seqno, (uint64_t)_cwnd/_mss);
    p->set_pathid(ev);


    _in_flight += pkt_size;
    _rtx_backlog -= pkt_size;
    _stats.rtx_pkts_sent++;

    p->sendOn();

    
    startRTO(eventlist().now());

    return pkt_size;
}