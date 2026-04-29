#ifndef INC_COMPOSITE_QUEUE_H
#define INC_COMPOSITE_QUEUE_H

#include "compositequeue.h"
#include "network.h"
#include "switch.h"

class IncCompositeQueue : public CompositeQueue {
public:
    IncCompositeQueue(linkspeed_bps bitrate, mem_b maxsize, EventList& eventlist, 
                      QueueLogger* logger, uint16_t trim_size, bool disable_trim)
        : CompositeQueue(bitrate, maxsize, eventlist, logger, trim_size, disable_trim) {}

    // The only thing we’re overriding is the reception
    void receivePacket(Packet& pkt) override;
};

#endif
