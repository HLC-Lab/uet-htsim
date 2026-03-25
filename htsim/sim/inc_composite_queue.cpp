#include "inc_composite_queue.h"

void IncCompositeQueue::receivePacket(Packet& pkt) {
    // --- FLARE/CANARY INTERCEPTION ---
    if (pkt._is_inc) {
        // cout << "DEBUG_COMP: Packet INC (" << pkt._inc_block_id << ") arrived at " << _nodename << endl;
        
        if (_switch) {
             // Condizione di intercettazione: se il pacchetto NON è stato appena generato da questo switch
             if (pkt._inc_last_switch_id != (int)_switch->getID()) {
                 // cout << "   -> INTERCEPTION TRIGGERED! Handing over to switch." << endl;
                 _switch->receivePacket(pkt);
                 return;
             }
        }
    }
    // ---------------------------------

    // Qui sotto richiami il comportamento normale della CompositeQueue
    CompositeQueue::receivePacket(pkt);
}