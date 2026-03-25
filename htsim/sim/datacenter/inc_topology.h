// -*- c-basic-offset: 4; indent-tabs-mode: nil -*-
#ifndef _INCTOPOLOGY_H
#define _INCTOPOLOGY_H

#include "fat_tree_topology.h"
#include "inc_switch.h"

class IncTopology : public FatTreeTopology {
public:
    IncTopology(const FatTreeTopologyCfg* cfg, 
                QueueLoggerFactory* logger_factory, 
                EventList* ev, 
                FirstFit* fit);
    virtual ~IncTopology() {}
};

#endif