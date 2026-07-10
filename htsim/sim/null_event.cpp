// -*- c-basic-offset: 4; tab-width: 8; indent-tabs-mode: t -*-
#include "null_event.h"
#include "queue.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <math.h>
#include <regex>
#include <stdio.h>
#include <utility>

#define timeInf 0

NullEvent::NullEvent(EventList &eventList)
        : EventSource(eventList, "compute_event") {} 

void NullEvent::doNextEvent() {
    /* printf("NullEventOver at %lu ps\n", eventlist().now());
    fflush(stdout);  */
    if (f_null_over_hook) {
        f_null_over_hook(1);
    }

    return;
}

void NullEvent::setCompute(simtime_picosec computation_time) {
    simtime_picosec target_ps = computation_time * 1000; // ns to ps
    simtime_picosec now_ps = eventlist().now();
    // Guard against truncation from ns/ps round-trip: if target is at or
    // before current time, schedule immediately instead of underflowing.
    simtime_picosec rel = (target_ps > now_ps) ? (target_ps - now_ps) : 0;
    eventlist().sourceIsPendingRel(*this, rel);
}

void NullEvent::startComputations() { eventlist().doNextEvent(); }