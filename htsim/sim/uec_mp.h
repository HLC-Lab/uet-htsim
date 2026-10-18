// -*- c-basic-offset: 4; indent-tabs-mode: nil -*-
#ifndef UEC_MP_H
#define UEC_MP_H

#include <list>
#include <optional>
#include "eventlist.h"
#include "buffer_reps.h"

class UecMultipath {
public:
    enum PathFeedback {PATH_GOOD, PATH_ECN, PATH_NACK, PATH_TIMEOUT};
    enum EvDefaults {UNKNOWN_EV};
    UecMultipath(bool debug): _debug(debug), _debug_tag("") {};
    virtual ~UecMultipath() {};
    virtual void set_debug_tag(string debug_tag) { _debug_tag = debug_tag; };
    /**
     * @param uint16_t path_id The path ID/entropy value as received by ACK/NACK
     * @param PathFeedback path_id The ACK/NACK response
     */
    virtual void processEv(uint16_t path_id, PathFeedback feedback) = 0;
    /**
     * @param uint64_t seq_sent The sequence number to be sent
     * @param uint64_t cur_cwnd_in_pkts The current congestion window in packets.
     */
    virtual uint16_t nextEntropy(uint64_t seq_sent, uint64_t cur_cwnd_in_pkts) = 0;
protected:
    bool _debug;
    string _debug_tag;
};

class UecMpOblivious : public UecMultipath {
public:
    UecMpOblivious(uint16_t no_of_paths, bool debug);
    void processEv(uint16_t path_id, PathFeedback feedback) override;
    uint16_t nextEntropy(uint64_t seq_sent, uint64_t cur_cwnd_in_pkts) override;
private:
    uint16_t _no_of_paths;       // must be a power of 2
    uint16_t _path_random;       // random upper bits of EV, set at startup and never changed
    uint16_t _path_xor;          // random value set each time we wrap the entropy values - XOR with
                                 // _current_ev_index
    uint16_t _current_ev_index;  // count through _no_of_paths and then wrap.  XOR with _path_xor to
};

class UecMpBitmap : public UecMultipath {
public:
    UecMpBitmap(uint16_t no_of_paths, bool debug);
    void processEv(uint16_t path_id, PathFeedback feedback) override;
    uint16_t nextEntropy(uint64_t seq_sent, uint64_t cur_cwnd_in_pkts) override;
private:
    uint16_t _no_of_paths;       // must be a power of 2
    uint16_t _path_random;       // random upper bits of EV, set at startup and never changed
    uint16_t _path_xor;          // random value set each time we wrap the entropy values - XOR with
                                 // _current_ev_index
    uint16_t _current_ev_index;  // count through _no_of_paths and then wrap.  XOR with _path_xor to
    vector<uint8_t> _ev_skip_bitmap;  // paths scores for load balancing

    uint16_t _ev_skip_count;
    uint8_t _max_penalty;             // max value we allow in _path_penalties (typically 1 or 2).
};

class UecMpRepsLegacy : public UecMultipath {
public:
    UecMpRepsLegacy(uint16_t no_of_paths, bool debug);
    void processEv(uint16_t path_id, PathFeedback feedback) override;
    uint16_t nextEntropy(uint64_t seq_sent, uint64_t cur_cwnd_in_pkts) override;
    optional<uint16_t> nextEntropyRecycle();
private:
    uint16_t _no_of_paths;
    uint16_t _crt_path;
    list<uint16_t> _next_pathid;
};


class UecMpReps : public UecMultipath {
public:
    
    UecMpReps(uint16_t no_of_paths, bool debug, bool is_trimming_enabled, bool partition_entropy = false,
              simtime_picosec base_rtt = 0);
    void processEv(uint16_t path_id, PathFeedback feedback) override;
    uint16_t nextEntropy(uint64_t seq_sent, uint64_t cur_cwnd_in_pkts) override;
private:
    uint16_t _no_of_paths;
    CircularBufferREPS<uint16_t> *circular_buffer_reps;
    uint16_t _crt_path;
    list<uint16_t> _next_pathid;
    bool _is_trimming_enabled = true;  // whether to trim the circular buffer
    bool _partition_entropy = false;   // whether to partition the entropy space into two halves
    uint16_t drawEntropy(bool open_tier); /*if partition_entropy = true, the entropy space s split: 
                                            open_tier=false draws from the low half, open_tier=true draws from the high half*/                                
    double _min_tier_congestion = 0.0;                                             
    bool _escalated = false;         
                                                  
public:                                                                            
    static void setEscalateThreshold(double hi) { _escalate_hi = hi; _escalate_lo = hi / 2; }  
    static double _escalate_hi;                                                    
    static double _escalate_lo;  


   
    //static double _warmup_explore_us; // absolute duration, same for every flow
    //static void setWarmupExploreUs(double us) { _warmup_explore_us = us; } 
    
    
    // Time-based warmup: spray openly for the first part of the flow's life
    static double _warmup_explore_rtt_mult; // a multiple of flow's own base_rtt
    static void setWarmupExploreRtts(double mult) { _warmup_explore_rtt_mult = mult; }
    simtime_picosec _base_rtt = 0;
    simtime_picosec _warmup_deadline = 0;
        
    static uint16_t _explore_prob_pct; // Continuous low-level exploration
    static void setExploreProb(uint16_t pct) { _explore_prob_pct = pct; }
    static uint16_t _partition_threshold;
    static void setPartitionThreshold(uint16_t t) { _partition_threshold = t; }
    uint16_t tierThreshold() const {
        return _partition_threshold ? _partition_threshold
                                    : (uint16_t)((_no_of_paths * 20) / 100);
    }
};

class UecMpMixed : public UecMultipath {
public:
    UecMpMixed(uint16_t no_of_paths, bool debug);
    void processEv(uint16_t path_id, PathFeedback feedback) override;
    uint16_t nextEntropy(uint64_t seq_sent, uint64_t cur_cwnd_in_pkts) override;
    void set_debug_tag(string debug_tag) override;
private:
    UecMpBitmap _bitmap;
    UecMpRepsLegacy _reps_legacy;
};

class UecMpEcmp : public UecMultipath {
public:
    UecMpEcmp(uint16_t no_of_paths, bool debug);
    void processEv(uint16_t path_id, PathFeedback feedback) override;
    uint16_t nextEntropy(uint64_t seq_sent, uint64_t cur_cwnd_in_pkts) override;
private:
    uint16_t _crt_path;
};

#endif  // UEC_MP_H
