// -*- c-basic-offset: 4; indent-tabs-mode: nil -*-
#include "inc_topology.h"
#include "inc_switch.h"
#include "queue_lossless_input.h"

IncTopology::IncTopology(const FatTreeTopologyCfg* cfg, 
                         QueueLoggerFactory* logger_factory, 
                         EventList* ev, 
                         FirstFit* fit) 
    : FatTreeTopology(cfg, logger_factory, ev, fit) 
{
    // DISTRUZIONE degli switch standard creati dalla classe base
    //for (auto* swc: switches_lp) delete swc;
    //for (auto* swc: switches_up) delete swc;
    //for (auto* swc: switches_c) delete swc;

    switches_lp.clear();
    switches_up.clear();
    switches_c.clear();

    // Creazione degli IncSwitch
    switches_lp.resize(_cfg->NTOR, nullptr);
    switches_up.resize(_cfg->NAGG, nullptr);
    switches_c.resize(_cfg->NCORE, nullptr);

    // ToR Switches
    for (uint32_t j=0; j<_cfg->NTOR; j++){
        simtime_picosec latency = (_cfg->_switch_latencies[TOR_TIER] > 0) ? _cfg->_switch_latencies[TOR_TIER] : _cfg->_switch_latency;
        switches_lp[j] = new IncSwitch(*_eventlist, "IncSwitch_ToR_"+ntoa(j), FatTreeSwitch::TOR, j, latency, this);
    }
    
    // Aggregation Switches
    for (uint32_t j=0; j<_cfg->NAGG; j++){
        simtime_picosec latency = (_cfg->_switch_latencies[AGG_TIER] > 0) ? _cfg->_switch_latencies[AGG_TIER] : _cfg->_switch_latency;
        switches_up[j] = new IncSwitch(*_eventlist, "IncSwitch_Agg_"+ntoa(j), FatTreeSwitch::AGG, j, latency, this);
    }
    
    // Core Switches
    for (uint32_t j=0; j<_cfg->NCORE; j++){
        simtime_picosec latency = (_cfg->_switch_latencies[CORE_TIER] > 0) ? _cfg->_switch_latencies[CORE_TIER] : _cfg->_switch_latency;
        switches_c[j] = new IncSwitch(*_eventlist, "IncSwitch_Core_"+ntoa(j), FatTreeSwitch::CORE, j, latency, this);
    }


    for (uint32_t tor = 0; tor < _cfg->NTOR; tor++) {
        uint32_t link_bundles = _cfg->_radix_down[TOR_TIER]/_cfg->_bundlesize[TOR_TIER];
        for (uint32_t l = 0; l < link_bundles; l++) {
            uint32_t srv = tor * link_bundles + l;
            for (uint32_t b = 0; b < _cfg->_bundlesize[TOR_TIER]; b++) {
                // La coda che esce dall'host deve puntare al nuovo ToR
                queues_ns_nlp[srv][tor][b]->setRemoteEndpoint(switches_lp[tor]);
            }
        }
    }

    for (uint32_t tor = 0; tor < _cfg->NTOR; tor++) {
        uint32_t podid = tor / _cfg->_tor_switches_per_pod;
        uint32_t agg_min = (_cfg->_tiers == 3) ? _cfg->MIN_POD_AGG_SWITCH(podid) : 0;
        uint32_t agg_max = (_cfg->_tiers == 3) ? _cfg->MAX_POD_AGG_SWITCH(podid) : _cfg->NAGG - 1;

        for (uint32_t agg = agg_min; agg <= agg_max; agg++) {
            for (uint32_t b = 0; b < _cfg->_bundlesize[AGG_TIER]; b++) {
                // Coda ToR -> Agg
                queues_nlp_nup[tor][agg][b]->setRemoteEndpoint(switches_up[agg]);
                // Coda Agg -> ToR
                queues_nup_nlp[agg][tor][b]->setRemoteEndpoint(switches_lp[tor]);
            }
        }
    }

    if (_cfg->_tiers == 3) {
        for (uint32_t agg = 0; agg < _cfg->NAGG; agg++) {
            uint32_t podpos = agg % (_cfg->_agg_switches_per_pod);
            for (uint32_t l = 0; l < _cfg->_radix_up[AGG_TIER]/_cfg->_bundlesize[CORE_TIER]; l++) {
                uint32_t core = podpos + _cfg->_agg_switches_per_pod * l;
                for (uint32_t b = 0; b < _cfg->_bundlesize[CORE_TIER]; b++) {
                    // Coda Agg -> Core
                    queues_nup_nc[agg][core][b]->setRemoteEndpoint(switches_c[core]);
                    // Coda Core -> Agg
                    queues_nc_nup[core][agg][b]->setRemoteEndpoint(switches_up[agg]);
                }
            }
        }
    }
    for (uint32_t srv = 0; srv < _cfg->NSRV; srv++) {
        uint32_t tor = _cfg->HOST_POD_SWITCH(srv);
        for (uint32_t b = 0; b < _cfg->_bundlesize[0]; b++) {
            if (queues_ns_nlp[srv][tor][b]) {
                queues_ns_nlp[srv][tor][b]->setRemoteEndpoint(switches_lp[tor]);
            }
        }
    }
}