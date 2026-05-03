// -*- c-basic-offset: 4; indent-tabs-mode: nil -*-
#ifndef _INCTOPOLOGY_H
#define _INCTOPOLOGY_H

#include "inc_fat_tree_topology.h"
#include "inc_switch.h"

class IncTopology : public IncFatTreeTopology {
public:
    IncTopology(const IncFatTreeTopologyCfg* cfg, 
                QueueLoggerFactory* logger_factory, 
                EventList* ev, 
                FirstFit* fit);
    virtual ~IncTopology() {}
};

#endif
