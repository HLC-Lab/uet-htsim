// -*- c-basic-offset: 4; indent-tabs-mode: nil -*-
#include <cstdio>
#include <cassert>
#include <cstdlib>
#include <memory>
#include <sstream>
#include <string.h>
#include <math.h>
#include <unistd.h>
#include "network.h"
#include "pipe.h"
#include "eventlist.h"
#include "logfile.h"
#include "uec_logger.h"
#include "clock.h"
#include "uec_base.h"
#include "uec.h"
#include "uec_mp.h"
#include "uec_pdcses.h"
#include "compositequeue.h"
#include "topology.h"
#include "connection_matrix.h"
#include "pciemodel.h"
#include "oversubscribed_cc.h"

#include "dragonfly_plus_topology.h"
#include "dragonfly_plus_switch.h"

#include <list>

#include "main.h"

int DEFAULT_NODES = 432;
uint32_t DEFAULT_TRIMMING_QUEUESIZE_FACTOR = 1;
uint32_t DEFAULT_NONTRIMMING_QUEUESIZE_FACTOR = 5;

EventList eventlist;

// Estimate RTT for dragonfly+ cross-group path:
static const uint32_t DF_DIAMETER_HOPS = 5; // 5 because considers the hosts, host -> leaf -> spine -> spine -> leaf -> host = 5 network hops

simtime_picosec calculate_rtt_df(simtime_picosec hop_latency, simtime_picosec switch_latency, linkspeed_bps host_linkspeed) {
    simtime_picosec rtt = 2 * DF_DIAMETER_HOPS * (hop_latency + switch_latency)
                + (Packet::data_packet_size() * 8 / speedAsGbps(host_linkspeed) * DF_DIAMETER_HOPS * 1000)
                + (UecBasePacket::get_ack_size() * 8 / speedAsGbps(host_linkspeed) * DF_DIAMETER_HOPS * 1000);
    return rtt;
}

uint32_t calculate_bdp_pkt_df(simtime_picosec hop_latency, simtime_picosec switch_latency, linkspeed_bps host_linkspeed) {
    simtime_picosec rtt = calculate_rtt_df(hop_latency, switch_latency, host_linkspeed);
    return (uint32_t)ceil((timeAsSec(rtt) * (host_linkspeed / 8)) / (double)Packet::data_packet_size());
}

void exit_error(char* progr) {
    cout << "Usage " << progr
         << " [-nodes N]\n\t[-cwnd cwnd_size]\n\t[-q queue_size]\n"
            "\t[-queue_type composite|composite_ecn|aeolus|aeolus_ecn]\n"
            "\t[-tm traffic_matrix_file]\n"
            "\t[-strat fpar|minimal]\n"
            "\t[-load_balancing_algo bitmap|reps|reps_legacy|oblivious|mixed]\n"
            "\t[-log sink|nic|traffic|switch|queue_usage|flow_events]\n"
            "\t[-seed random_seed]\n\t[-end end_time_in_usec]\n\t[-mtu MTU]\n"
            "\t[-radix K] router radix\n"
            "\t[-size s|m|l] topology size\n"
            "\t[-s x][-l x][-h x][-p x] dragonfly+ parameters\n"
            "\t[-p_link N] parallel links between groups\n"
            "\t[-hop_latency x] per hop wire latency in us, default 1\n"
            "\t[-switch_latency x] switching latency in us, default 0\n"
            "\t[-target_q_delay x] target queuing delay in us, default 6us\n"
            "\t[-conn_reuse] enable connection reuse\n"
            "\t[-disable_hop_rtt_normalization] force per-hop RTT normalization off "
            "even under fpar (for comparison against the default fpar behaviour)\n"
            "\t[-force_enable_hop_rtt_normalization] force per-hop RTT normalization on "
            "even under minimal (for comparison against the default minimal behaviour)\n"
            "\t[-debug_hops] log per-ACK/NACK hop-count congestion classification (HOPDBG lines)"
            "\t[-tornado] generate a Dragonfly+ group-tornado traffic matrix instead of -tm "
            "(every host paired with one in a maximally-distant group); requires -nodes\n"
            "\t[-tornado_conns N] how many hosts send under -tornado, default: all of them\n"
            "\t[-tornado_flowsize bytes] flow size under -tornado (required with -tornado)\n"
            "\t[-strat fpar|minimal]\n"
            "\t[-reps_partition_entropy] with -strat reps_dfp and -load_balancing_algo reps, "
            "\t[-reps_partition_ratio N] REPS-SWITCH-HOST% of entropy space given to the minimal tier\n"
            "default fresh entropy draws to minimal paths and only after congestion let "
            "exploration draw non-minimal entropies\n"
            << endl;
    exit(1);
}

int main(int argc, char **argv) {
    Clock c(timeFromSec(5 / 100.), eventlist);
    bool param_queuesize_set = false;
    uint32_t queuesize_pkt = 0;
    linkspeed_bps linkspeed = speedFromMbps((double)HOST_NIC);
    int packet_size = 4150;
    uint32_t path_entropy_size = 64;
    uint32_t cwnd = 0, no_of_nodes = 0;
    bool disable_trim = false;
    uint16_t trimsize = 64;
    simtime_picosec logtime = timeFromMs(0.25);
    stringstream filename(ios_base::out);
    simtime_picosec hop_latency = timeFromUs((uint32_t)1);
    simtime_picosec switch_latency = timeFromUs((uint32_t)0);
    queue_type qt = COMPOSITE;

    // Dragonfly+ topology parameters
    topology_type topo_type = LARGE;
    uint32_t k_radix = 2;
    uint32_t s_df = 0;
    uint32_t l_df = 0;
    uint32_t h_df = 0;
    uint32_t p_df = 0;
    uint32_t no_parallel_link = 1;
    uint64_t queue_size_threshold = 0;

    enum LoadBalancing_Algo { BITMAP, REPS, REPS_LEGACY, OBLIVIOUS, MIXED };
    LoadBalancing_Algo load_balancing_algo = MIXED;

    bool log_sink = false;
    bool log_nic = false;
    bool log_flow_events = true;
    bool log_tor_downqueue = false;
    bool log_tor_upqueue = false;
    bool log_traffic = false;
    bool log_switches = false;
    bool log_queue_usage = false;

    simtime_picosec target_Qdelay = 0;
    bool param_ecn_set = false;
    bool ecn = true;
    uint32_t ecn_low = 0;
    uint32_t ecn_high = 0;
    uint32_t queue_size_bdp_factor = 0;

    bool receiver_driven = false;
    bool sender_driven = true;
    bool enable_accurate_base_rtt = true;
    bool force_disable_hop_rtt_normalization = false;
    bool force_enable_hop_rtt_normalization = false;
    bool reps_partition_entropy = false;
    uint32_t reps_partition_ratio = 20;  // REPS-SWITCH-HOST% of entropy space given to the minimal tier

    RouteStrategy route_strategy = NOT_SET;
    DragonflyPlusSwitch::RoutingStrategy df_strategy = DragonflyPlusSwitch::FPAR;

    int seed = 13;
    int i = 1;
    double pcie_rate = 1.1;

    filename << "logout.dat";
    int end_time = 1000;

    queue_type snd_type = FAIR_PRIO;
    int8_t qa_gate = -1;
    bool conn_reuse = false;

    char* tm_file = NULL;
    char* topo_file = NULL;

    bool tornado = false;
    uint32_t tornado_conns = 0;      // 0 -> every host sends (set after -nodes is known)
    mem_b tornado_flowsize = 0; 

    while (i < argc) {
        if (!strcmp(argv[i], "-o")) {
            filename.str(std::string());
            filename << argv[i + 1];
            i++;
        } else if (!strcmp(argv[i], "-conn_reuse")) {
            conn_reuse = true;
            cout << "Enabling connection reuse" << endl;
        } else if (!strcmp(argv[i], "-end")) {
            end_time = atoi(argv[i + 1]);
            cout << "endtime(us) " << end_time << endl;
            i++;
        } else if (!strcmp(argv[i], "-nodes")) {
            no_of_nodes = atoi(argv[i + 1]);
            cout << "no_of_nodes " << no_of_nodes << endl;
            i++;
        } else if (!strcmp(argv[i], "-radix")) {
            k_radix = atoi(argv[i + 1]);
            cout << "router radix " << k_radix << endl;
            i++;
        } else if (!strcmp(argv[i], "-size")) {
            if (!strcmp(argv[i + 1], "s")) {
                topo_type = SMALL;
            } else if (!strcmp(argv[i + 1], "m")) {
                topo_type = MEDIUM;
            } else if (!strcmp(argv[i + 1], "l")) {
                topo_type = LARGE;
            } else {
                cout << "Unknown topology size " << argv[i + 1] << endl;
                exit_error(argv[0]);
            }
            i++;
        } else if (!strcmp(argv[i], "-s")) {
            s_df = atoi(argv[i + 1]);
            i++;
        } else if (!strcmp(argv[i], "-l")) {
            l_df = atoi(argv[i + 1]);
            i++;
        } else if (!strcmp(argv[i], "-h")) {
            h_df = atoi(argv[i + 1]);
            i++;
        } else if (!strcmp(argv[i], "-p")) {
            p_df = atoi(argv[i + 1]);
            i++;
        } else if (!strcmp(argv[i], "-p_link")) {
            no_parallel_link = atoi(argv[i + 1]);
            cout << "number of parallel links " << no_parallel_link << endl;
            i++;
        } else if (!strcmp(argv[i], "-threshold")) {
            queue_size_threshold = atoi(argv[i + 1]);
            cout << "queue size threshold " << queue_size_threshold << endl;
            i++;
        } else if (!strcmp(argv[i], "-receiver_cc_only")) {
            UecSrc::_sender_based_cc = false;
            UecSrc::_receiver_based_cc = true;
            UecSink::_oversubscribed_cc = false;
            sender_driven = false;
            receiver_driven = true;
            cout << "receiver based CC enabled ONLY" << endl;
        } else if (!strcmp(argv[i], "-sender_cc_only")) {
            UecSrc::_sender_based_cc = true;
            UecSrc::_receiver_based_cc = false;
            UecSink::_oversubscribed_cc = false;
            sender_driven = true;
            receiver_driven = false;
            cout << "sender based CC enabled ONLY" << endl;
        } else if (!strcmp(argv[i], "-qa_gate")) {
            qa_gate = atof(argv[i + 1]);
            cout << "qa_gate 2^" << qa_gate << endl;
            i++;
        } else if (!strcmp(argv[i], "-target_q_delay")) {
            target_Qdelay = timeFromUs(atof(argv[i + 1]));
            cout << "target_q_delay " << atof(argv[i + 1]) << " us" << endl;
            i++;
        } else if (!strcmp(argv[i], "-queue_size_bdp_factor")) {
            queue_size_bdp_factor = atoi(argv[i + 1]);
            cout << "Setting queue size to " << queue_size_bdp_factor << "x BDP." << endl;
            i++;
        } else if (!strcmp(argv[i], "-sender_cc_algo")) {
            UecSrc::_sender_based_cc = true;
            sender_driven = true;
            if (!strcmp(argv[i + 1], "dctcp"))
                UecSrc::_sender_cc_algo = UecSrc::DCTCP;
            else if (!strcmp(argv[i + 1], "nscc"))
                UecSrc::_sender_cc_algo = UecSrc::NSCC;
            else if (!strcmp(argv[i + 1], "constant"))
                UecSrc::_sender_cc_algo = UecSrc::CONSTANT;
            else {
                cout << "UNKNOWN CC ALGO " << argv[i + 1] << endl;
                exit(1);
            }
            cout << "sender based algo " << argv[i + 1] << endl;
            i++;
        } else if (!strcmp(argv[i], "-sender_cc")) {
            UecSrc::_sender_based_cc = true;
            UecSink::_oversubscribed_cc = false;
            sender_driven = true;
            cout << "sender based CC enabled" << endl;
        } else if (!strcmp(argv[i], "-receiver_cc")) {
            UecSrc::_receiver_based_cc = true;
            receiver_driven = true;
            cout << "receiver based CC enabled" << endl;
        } else if (!strcmp(argv[i], "-load_balancing_algo")) {
            if (!strcmp(argv[i + 1], "bitmap")) {
                load_balancing_algo = BITMAP;
            } else if (!strcmp(argv[i + 1], "reps")) {
                load_balancing_algo = REPS;
            } else if (!strcmp(argv[i + 1], "reps_legacy")) {
                load_balancing_algo = REPS_LEGACY;
            } else if (!strcmp(argv[i + 1], "oblivious")) {
                load_balancing_algo = OBLIVIOUS;
            } else if (!strcmp(argv[i + 1], "mixed")) {
                load_balancing_algo = MIXED;
            } else {
                cout << "Unknown load balancing algorithm: " << argv[i + 1] << endl;
                exit_error(argv[0]);
            }
            cout << "Load balancing algorithm set to " << argv[i + 1] << endl;
            i++;
        } else if (!strcmp(argv[i], "-queue_type")) {
            if (!strcmp(argv[i + 1], "composite")) {
                qt = COMPOSITE;
            } else if (!strcmp(argv[i + 1], "composite_ecn")) {
                qt = COMPOSITE_ECN;
            } else if (!strcmp(argv[i + 1], "aeolus")) {
                qt = AEOLUS;
            } else if (!strcmp(argv[i + 1], "aeolus_ecn")) {
                qt = AEOLUS_ECN;
            } else {
                cout << "Unknown queue type " << argv[i + 1] << endl;
                exit_error(argv[0]);
            }
            cout << "queue_type " << qt << endl;
            i++;
        } else if (!strcmp(argv[i], "-debug")) {
            UecSrc::_debug = true;
            UecPdcSes::_debug = true;
            } 
            else if (!strcmp(argv[i], "-debug_hops")) {
           UecSrc::_log_hop_classification = true;
        } else if (!strcmp(argv[i], "-host_queue_type")) {
            if (!strcmp(argv[i + 1], "swift")) {
                snd_type = SWIFT_SCHEDULER;
            } else if (!strcmp(argv[i + 1], "prio")) {
                snd_type = PRIORITY;
            } else if (!strcmp(argv[i + 1], "fair_prio")) {
                snd_type = FAIR_PRIO;
            } else {
                cout << "Unknown host queue type " << argv[i + 1] << endl;
                exit_error(argv[0]);
            }
            cout << "host queue_type " << snd_type << endl;
            i++;
        } else if (!strcmp(argv[i], "-log")) {
            if (!strcmp(argv[i + 1], "flow_events")) {
                log_flow_events = true;
            } else if (!strcmp(argv[i + 1], "sink")) {
                cout << "logging sinks\n";
                log_sink = true;
            } else if (!strcmp(argv[i + 1], "nic")) {
                cout << "logging nics\n";
                log_nic = true;
            } else if (!strcmp(argv[i + 1], "tor_downqueue")) {
                cout << "logging tor downqueues\n";
                log_tor_downqueue = true;
            } else if (!strcmp(argv[i + 1], "tor_upqueue")) {
                cout << "logging tor upqueues\n";
                log_tor_upqueue = true;
            } else if (!strcmp(argv[i + 1], "switch")) {
                cout << "logging total switch queues\n";
                log_switches = true;
            } else if (!strcmp(argv[i + 1], "traffic")) {
                cout << "logging traffic\n";
                log_traffic = true;
            } else if (!strcmp(argv[i + 1], "queue_usage")) {
                cout << "logging queue usage\n";
                log_queue_usage = true;
            } else {
                exit_error(argv[0]);
            }
            i++;
        } else if (!strcmp(argv[i], "-cwnd")) {
            cwnd = atoi(argv[i + 1]);
            cout << "cwnd " << cwnd << endl;
            i++;
        } else if (!strcmp(argv[i], "-tm")) {
            tm_file = argv[i + 1];
            cout << "traffic matrix input file: " << tm_file << endl;
            i++;
        } else if (!strcmp(argv[i], "-topo")) {
            topo_file = argv[i + 1];
            cout << "Dragonfly+ topology input file: " << topo_file << endl;
            i++;
        } else if (!strcmp(argv[i], "-tornado")) {
            tornado = true;
            cout << "Using generated Dragonfly+ tornado traffic instead of -tm" << endl;
        } else if (!strcmp(argv[i], "-tornado_conns")) {
            tornado_conns = atoi(argv[i + 1]);
            cout << "Tornado active connections " << tornado_conns << endl;
            i++;
        } else if (!strcmp(argv[i], "-tornado_flowsize")) {
            tornado_flowsize = atoll(argv[i + 1]);
            cout << "Tornado flow size " << tornado_flowsize << " bytes" << endl;
            i++;}
        else if (!strcmp(argv[i], "-q")) {
            param_queuesize_set = true;
            queuesize_pkt = atoi(argv[i + 1]);
            cout << "Setting queuesize to " << queuesize_pkt << " packets" << endl;
            i++;
        } else if (!strcmp(argv[i], "-sack_threshold")) {
            UecSink::_bytes_unacked_threshold = atoi(argv[i + 1]);
            cout << "Setting receiver SACK bytes threshold to " << UecSink::_bytes_unacked_threshold << " bytes" << endl;
            i++;
        } else if (!strcmp(argv[i], "-oversubscribed_cc")) {
            UecSink::_oversubscribed_cc = true;
            cout << "Using receiver oversubscribed CC" << endl;
        } else if (!strcmp(argv[i], "-Ai")) {
            OversubscribedCC::_Ai = atof(argv[i + 1]);
            cout << "Using Ai " << OversubscribedCC::_Ai << endl;
            i++;
        } else if (!strcmp(argv[i], "-Md")) {
            OversubscribedCC::_Md = atof(argv[i + 1]);
            cout << "Using Md " << OversubscribedCC::_Md << endl;
            i++;
        } else if (!strcmp(argv[i], "-alpha")) {
            OversubscribedCC::_alpha = atof(argv[i + 1]);
            cout << "Using Alpha " << OversubscribedCC::_alpha << endl;
            i++;
        } else if (!strcmp(argv[i], "-force_disable_oversubscribed_cc")) {
            UecSink::_oversubscribed_cc = false;
            cout << "Disabling receiver oversubscribed CC" << endl;
        } else if (!strcmp(argv[i], "-enable_accurate_base_rtt")) {
            enable_accurate_base_rtt = true;
            cout << "Accurate per-flow base RTT enabled: each flow uses its actual src/dst path latency." << endl;
        } else if (!strcmp(argv[i], "-disable_base_rtt_update_on_nack")) {
            UecSrc::update_base_rtt_on_nack = false;
            cout << "Disables using NACKs to update the base RTT." << endl;
        } else if (!strcmp(argv[i], "-disable_hop_rtt_normalization")) {
            force_disable_hop_rtt_normalization = true;
            cout << "Forcing per-hop RTT normalization OFF regardless of strategy." << endl;
        } else if (!strcmp(argv[i], "-force_enable_hop_rtt_normalization")) {
            force_enable_hop_rtt_normalization = true;
            cout << "Forcing per-hop RTT normalization ON regardless of strategy." << endl;
        } else if (!strcmp(argv[i], "-sleek")) {
            UecSrc::_enable_sleek = true;
            cout << "Using SLEEK" << endl;
        } else if (!strcmp(argv[i], "-ecn")) {
            param_ecn_set = true;
            ecn = true;
            ecn_low = atoi(argv[i + 1]);
            ecn_high = atoi(argv[i + 2]);
            i += 2;
        } else if (!strcmp(argv[i], "-reps_partition_ratio")) {
            reps_partition_ratio = atoi(argv[i + 1]);
            cout << "REPS entropy partition ratio " << argv[i + 1]
                 << "% to the minimal tier" << endl;
            i++;
        } else if (!strcmp(argv[i], "-reps_partition_entropy")) {
            reps_partition_entropy = true;
            cout << "REPS partition entropy enabled." << endl;
        } else if (!strcmp(argv[i], "-disable_trim")) {
            disable_trim = true;
            cout << "Trimming disabled, dropping instead." << endl;
        } else if (!strcmp(argv[i], "-trimsize")) {
            trimsize = atoi(argv[i + 1]);
            cout << "trimmed packet size: " << trimsize << " bytes\n";
            i++;
        } else if (!strcmp(argv[i], "-logtime")) {
            double log_ms = atof(argv[i + 1]);
            logtime = timeFromMs(log_ms);
            cout << "logtime " << log_ms << " ms" << endl;
            i++;
        } else if (!strcmp(argv[i], "-logtime_us")) {
            double log_us = atof(argv[i + 1]);
            logtime = timeFromUs(log_us);
            cout << "logtime " << log_us << " us" << endl;
            i++;
        } else if (!strcmp(argv[i], "-linkspeed")) {
            linkspeed = speedFromMbps(atof(argv[i + 1]));
            i++;
        } else if (!strcmp(argv[i], "-dfp_low_share")) {                               
            DragonflyPlusSwitch::set_low_share(atoi(argv[i + 1]));                      
            cout << "Dragonfly+ LOW (7-hop) entropy share " << argv[i + 1] << "%" << endl;  
            i++;                                                                        
        } else if (!strcmp(argv[i], "-reps_escalate_threshold")) {                      
            UecMpReps::setEscalateThreshold(atof(argv[i + 1]));                         
            cout << "REPS open-tier escalation threshold " << argv[i + 1] << endl;      
            i++;                                                                        
        } else if (!strcmp(argv[i], "-reps_warmup_explore_rtts")) {
            UecMpReps::setWarmupExploreRtts(atof(argv[i + 1]));
            cout << "REPS warmup explore duration " << argv[i + 1] << " x this flow's base RTT" << endl;
            i++;
        } else if (!strcmp(argv[i], "-reps_explore_prob")) {
            UecMpReps::setExploreProb((uint16_t)atoi(argv[i + 1]));
            cout << "REPS continuous explore probability " << argv[i + 1] << "%" << endl;
            i++;
        } else if (!strcmp(argv[i], "-dfp_low_uniform")) {
            DragonflyPlusSwitch::set_low_uniform(true);
        } else if (!strcmp(argv[i], "-dfp_minimal_share")) {
            DragonflyPlusSwitch::set_minimal_share(atoi(argv[i + 1]));
            i++;
        } else if (!strcmp(argv[i], "-seed")) {
            seed = atoi(argv[i + 1]);
            cout << "random seed " << seed << endl;
            i++;
        } else if (!strcmp(argv[i], "-mtu")) {
            packet_size = atoi(argv[i + 1]);
            i++;
        } else if (!strcmp(argv[i], "-paths")) {
            path_entropy_size = atoi(argv[i + 1]);
            cout << "no of paths " << path_entropy_size << endl;
            i++;
        } else if (!strcmp(argv[i], "-hop_latency")) {
            hop_latency = timeFromUs(atof(argv[i + 1]));
            cout << "Hop latency set to " << timeAsUs(hop_latency) << endl;
            i++;
        } else if (!strcmp(argv[i], "-pcie")) {
            UecSink::_model_pcie = true;
            pcie_rate = atof(argv[i + 1]);
            i++;
        } else if (!strcmp(argv[i], "-switch_latency")) {
            switch_latency = timeFromUs(atof(argv[i + 1]));
            cout << "Switch latency set to " << timeAsUs(switch_latency) << endl;
            i++;
        } else if (!strcmp(argv[i], "-strat")) {
            if (!strcmp(argv[i + 1], "fpar")) {
                route_strategy = ECMP_FIB;
                df_strategy = DragonflyPlusSwitch::FPAR;
                DragonflyPlusSwitch::set_strategy(DragonflyPlusSwitch::FPAR);
                cout << "Dragonfly+ strategy: FPAR" << endl;
            } else if (!strcmp(argv[i + 1], "minimal")) {
                route_strategy = ECMP_FIB;
                df_strategy = DragonflyPlusSwitch::MINIMAL;
                DragonflyPlusSwitch::set_strategy(DragonflyPlusSwitch::MINIMAL);
                cout << "Dragonfly+ strategy: MINIMAL" << endl;
            } else if (!strcmp(argv[i + 1], "reps_dfp")
                       || !strcmp(argv[i + 1], "mixed")) {
                route_strategy = ECMP_FIB;
                df_strategy = DragonflyPlusSwitch::REPS_DFP;
                DragonflyPlusSwitch::set_strategy(DragonflyPlusSwitch::REPS_DFP);
                cout << "Dragonfly+ strategy: REPS_DFP" << endl;
            } else {
                cout << "Unknown strategy " << argv[i + 1] << ", expecting fpar|minimal|reps_dfp|mixed" << endl;
                exit_error(argv[0]);
            }
            i++;
        } else {
            cout << "Unknown parameter " << argv[i] << endl;
            exit_error(argv[0]);
        }
        i++;
    }

    if (end_time > 0 && logtime >= timeFromUs((uint32_t)end_time)) {
        cout << "Logtime set to endtime" << endl;
        logtime = timeFromUs((uint32_t)end_time) - 1;
    }

    assert(trimsize >= 64 && trimsize <= (uint32_t)packet_size);

    if (topo_type != SMALL && no_parallel_link != 1) {
        fprintf(stderr, "Parallel links can only be changed for SMALL dragonfly+ topology\n");
        exit(1);
    }

    cout << "Packet size (MTU) is " << packet_size << endl;

    if (tornado) {
        if (tm_file) {
            fprintf(stderr, "-tornado and -tm are mutually exclusive\n");
            exit(1);
        }
        if (no_of_nodes == 0) {
            fprintf(stderr, "-tornado requires -nodes to be set explicitly "
                            "(there's no connection matrix file to infer it from)\n");
            exit(1);
        }
        if (tornado_flowsize == 0) {
            fprintf(stderr, "-tornado requires -tornado_flowsize to be set explicitly\n");
            exit(1);
        }
        if (tornado_conns == 0) {
            tornado_conns = no_of_nodes;  // default: every host sends 
        }
    }

    srand(seed);
    srandom(seed);
    cout << "Parsed args\n";
    Packet::set_packet_size(packet_size);

    UecSrc::_mtu = Packet::data_packet_size();
    UecSrc::_mss = UecSrc::_mtu - UecSrc::_hdr_size;

    if (route_strategy == NOT_SET) {
        route_strategy = ECMP_FIB;
        df_strategy = DragonflyPlusSwitch::FPAR;
        DragonflyPlusSwitch::set_strategy(DragonflyPlusSwitch::FPAR);
        cout << "Defaulting to FPAR routing strategy" << endl;
    }

    DragonflyPlusSwitch::set_config(df_strategy, disable_trim, trimsize);

    if (reps_partition_entropy && df_strategy != DragonflyPlusSwitch::REPS_DFP) {
        cerr << "-reps_partition_entropy only has an effect with -strat reps_dfp "
                "(MINIMAL_NONMINIMAL); ignoring." << endl;
        reps_partition_entropy = false;
    }

    uint16_t partition_threshold = 0;
    if (reps_partition_entropy) {
        uint32_t t = (path_entropy_size * reps_partition_ratio) / 100;
        if (t < 1) t = 1;
        if (t > path_entropy_size - 1) t = path_entropy_size - 1;
        partition_threshold = (uint16_t)t;
        cout << "REPS entropy partition: minimal tier = [0," << partition_threshold
             << "), open tier = [" << partition_threshold << "," << path_entropy_size
             << ")" << endl;
    }
    DragonflyPlusSwitch::set_entropy_partition(partition_threshold);
    UecMpReps::setPartitionThreshold(partition_threshold);
    eventlist.setEndtime(timeFromUs((uint32_t)end_time));

    cout << "Logging to " << filename.str() << endl;
    Logfile logfile(filename.str(), eventlist);
    logfile.setStartTime(timeFromSec(0));

    vector<unique_ptr<UecNIC>> nics;

    UecSinkLoggerSampling* sink_logger = NULL;
    if (log_sink) {
        sink_logger = new UecSinkLoggerSampling(logtime, eventlist);
        logfile.addLogger(*sink_logger);
    }
    NicLoggerSampling* nic_logger = NULL;
    if (log_nic) {
        nic_logger = new NicLoggerSampling(logtime, eventlist);
        logfile.addLogger(*nic_logger);
    }
    TrafficLoggerSimple* traffic_logger = NULL;
    if (log_traffic) {
        traffic_logger = new TrafficLoggerSimple();
        logfile.addLogger(*traffic_logger);
    }
    FlowEventLoggerSimple* event_logger = NULL;
    if (log_flow_events) {
        event_logger = new FlowEventLoggerSimple();
        logfile.addLogger(*event_logger);
    }

    UecSrc* uec_src;
    UecSink* uec_snk;

    QueueLoggerFactory* qlf = 0;
    if (log_tor_downqueue || log_tor_upqueue) {
        qlf = new QueueLoggerFactory(&logfile, QueueLoggerFactory::LOGGER_SAMPLING, eventlist);
        qlf->set_sample_period(logtime);
    } else if (log_queue_usage) {
        qlf = new QueueLoggerFactory(&logfile, QueueLoggerFactory::LOGGER_EMPTY, eventlist);
        qlf->set_sample_period(logtime);
    }

    auto conns = std::make_unique<ConnectionMatrix>(no_of_nodes);

        if (tornado) {
        // conns->conns is populated later via setTornado(), once the
        // topology exists and can supply the real p/l/no_groups -- N is
        // already known here since -tornado requires -nodes explicitly.
        conns->N = no_of_nodes;
    } else if (tm_file) {
        cout << "Loading connection matrix from " << tm_file << endl;
        if (!conns->load(tm_file)) {
            cout << "Failed to load connection matrix " << tm_file << endl;
            exit(-1);
        }
    } else {
        cout << "Loading connection matrix from standard input" << endl;
        conns->load(cin);
    }

    if (conns->N != no_of_nodes && no_of_nodes != 0) {
        cout << "Connection matrix number of nodes is " << conns->N
             << " while I am using " << no_of_nodes << endl;
        exit(-1);
    }

    no_of_nodes = conns->N;

    if (!conns->failures.empty()) {
        cerr << "Error: link failures in connection matrix are not supported for Dragonfly+ topology." << endl;
        exit(1);
    }

    // Automatic queue sizing
    if (!param_queuesize_set) {
        cout << "Automatic queue sizing enabled ";
        if (queue_size_bdp_factor == 0) {
            if (disable_trim) {
                queue_size_bdp_factor = DEFAULT_NONTRIMMING_QUEUESIZE_FACTOR;
                cout << "non-trimming";
            } else {
                queue_size_bdp_factor = DEFAULT_TRIMMING_QUEUESIZE_FACTOR;
                cout << "trimming";
            }
        }
        cout << " queue-size-to-bdp-factor is " << queue_size_bdp_factor << "xBDP" << endl;
    }

    // Pre-topology BDP estimate: used only for queue and ECN threshold sizing before
    // the topology object exists. Uses a single hop_latency for all link types, which
    // is correct when -hop_latency is passed (all types equal) and a rough approximation
    // otherwise. Accurate network_max_unloaded_rtt is computed below after topology
    // construction using per-tier latencies from the topology object.
    mem_b queuesize = 0;
    if (!param_queuesize_set) {
        uint32_t bdp_pkt = calculate_bdp_pkt_df(hop_latency, switch_latency, linkspeed);
        queuesize = memFromPkt(bdp_pkt * queue_size_bdp_factor);
    } else {
        queuesize = memFromPkt(queuesize_pkt);
    }

    if (ecn) {
        uint32_t bdp_pkt = calculate_bdp_pkt_df(hop_latency, switch_latency, linkspeed);
    
        if (!param_ecn_set) {
            ecn_low = memFromPkt((uint32_t)ceil(bdp_pkt * 0.2));
            ecn_high = memFromPkt((uint32_t)ceil(bdp_pkt * 0.8));
        } else {
            ecn_low = memFromPkt(ecn_low);
            ecn_high = memFromPkt(ecn_high);
        }
        cout << "Setting ECN low " << ecn_low << " high " << ecn_high << endl;
        DragonflyPlusTopology::set_ecn(true, ecn_low, ecn_high);
        assert(ecn_low <= ecn_high);
        assert(ecn_high <= queuesize);
    }

    // Create dragonfly+ topology
    DragonflyPlusTopology* top;
    if (topo_file) {
        top = DragonflyPlusTopology::load(topo_file, qlf, eventlist, queuesize, qt);
    } else {
        top = new DragonflyPlusTopology(k_radix, s_df, l_df, h_df, p_df, no_of_nodes,
                                        qt, queuesize, qlf, &eventlist,
                                        topo_type, queue_size_threshold,
                                        no_parallel_link, linkspeed,
                                        hop_latency, switch_latency);
    }

    // Validate that the topology has enough hosts for the connection matrix.
    // The constructor always creates a "full" topology (possible_nodes >= no_of_hosts),
    // but medium/small topologies have fewer switches so actual addressable hosts may be less.
    uint32_t actual_hosts = top->get_no_groups() * top->get_l() * top->get_p();
    if (actual_hosts < no_of_nodes) {
        cerr << "Error: topology supports " << actual_hosts << " hosts but connection matrix requires "
             << no_of_nodes << " nodes. Use a larger topology size (-size l) or larger radix (-radix)." << endl;
        exit(1);
    }
    if (actual_hosts != no_of_nodes) {
        cout << "Note: topology supports " << actual_hosts << " hosts; "
             << (actual_hosts - no_of_nodes) << " host port(s) will be unused." << endl;
    }

    if (tornado) {
        // Generate the tornado connection matrix via the Python generator
        // (connection_matrices/gen_tornado_dfp.py) now that the topology
        // exists and can supply its real p/l -- avoids re-deriving/
        // duplicating the auto-sizing logic here or in ConnectionMatrix.
        std::ostringstream tmp_path;
        tmp_path << "/tmp/dfp_tornado_" << getpid() << ".cm";
        std::string tmp_cm = tmp_path.str();

        std::ostringstream cmd;
        cmd << "python3 connection_matrices/gen_tornado_dfp.py " << tmp_cm << " "
            << no_of_nodes << " " << tornado_conns << " " << tornado_flowsize
            << " 0.0 " << seed << " " << top->get_p() << " " << top->get_l();
        cout << "Generating tornado connection matrix: " << cmd.str() << endl;
        if (system(cmd.str().c_str()) != 0) {
            cerr << "Failed to generate tornado connection matrix" << endl;
            exit(1);
        }

        cout << "Loading generated tornado connection matrix from " << tmp_cm << endl;
        if (!conns->load(tmp_cm.c_str())) {
            cout << "Failed to load generated tornado connection matrix " << tmp_cm << endl;
            exit(-1);
        }
        remove(tmp_cm.c_str());
    }

    if (log_switches) {
        top->add_switch_loggers(logfile, logtime);
    }

    // Accurate worst-case (cross-group) unloaded RTT, using per-tier link latencies

    //   propagation = 2 * get_diameter_latency()  (round-trip)
    //   serialization = data + ack over get_diameter() hops
    simtime_picosec network_max_unloaded_rtt =
          2 * top->get_diameter_latency()
        + (Packet::data_packet_size() * 8 / speedAsGbps(linkspeed) * top->get_diameter() * 1000)
        + (UecBasePacket::get_ack_size() * 8 / speedAsGbps(linkspeed) * top->get_diameter() * 1000);
    cout << "network_max_unloaded_rtt " << timeAsUs(network_max_unloaded_rtt) << " us" << endl;

    UecSrc::_min_rto = timeFromUs(15 + queuesize * 6.0 * 8 * 1000000 / linkspeed);
    cout << "Setting min RTO to " << timeAsUs(UecSrc::_min_rto) << endl;

  
    bool routing_mixes_hop_counts = (/*df_strategy == DragonflyPlusSwitch::FPAR || */df_strategy == DragonflyPlusSwitch::REPS_DFP);
    bool hop_rtt_normalization = (routing_mixes_hop_counts
                                   || force_enable_hop_rtt_normalization)
                                  && !force_disable_hop_rtt_normalization;

    const char* strategy_name = (df_strategy == DragonflyPlusSwitch::FPAR) ? "fpar"
                              : (df_strategy == DragonflyPlusSwitch::REPS_DFP) ? "reps_dfp"
                              : "minimal";
    cout << "Per-hop RTT normalization: " << (hop_rtt_normalization ? "ON" : "OFF")
         << " (strategy " << strategy_name << ")" << endl;
    UecSrc::setPerHopRttNormalization(hop_rtt_normalization);

    if (UecSink::_oversubscribed_cc)
        OversubscribedCC::_base_rtt = network_max_unloaded_rtt;

    // Initialize CC algorithms
    if (sender_driven) {
        bool trimming_enabled = !disable_trim;
        UecSrc::initNsccParams(network_max_unloaded_rtt, linkspeed, target_Qdelay, qa_gate, trimming_enabled);
    }

    const uint32_t ports = 1; // dragonfly+ uses single NIC port per host
    vector<unique_ptr<UecPullPacer>> pacers;
    vector<PCIeModel*> pcie_models;
    vector<OversubscribedCC*> oversubscribed_ccs;

    for (size_t ix = 0; ix < no_of_nodes; ix++) {
        auto& pacer = pacers.emplace_back(make_unique<UecPullPacer>(linkspeed, 0.99,
            UecBasePacket::unquantize(UecSink::_credit_per_pull), eventlist, ports));

        if (UecSink::_model_pcie)
            pcie_models.push_back(new PCIeModel(linkspeed * pcie_rate, UecSrc::_mtu, eventlist,
                pacer.get()));

        if (UecSink::_oversubscribed_cc)
            oversubscribed_ccs.push_back(new OversubscribedCC(eventlist, pacer.get()));

        auto& nic = nics.emplace_back(make_unique<UecNIC>(ix, eventlist, linkspeed, ports));
        if (log_nic) {
            nic_logger->monitorNic(nic.get());
        }
    }

    vector<connection*>* all_conns = conns->getAllConnections();
    vector<UecSrc*> uec_srcs;

    map<flowid_t, pair<UecSrc*, UecSink*>> flowmap;
    map<flowid_t, UecPdcSes*> flow_pdc_map;

    mem_b cwnd_b = cwnd * Packet::data_packet_size();

    for (size_t c = 0; c < all_conns->size(); c++) {
        connection* crt = all_conns->at(c);
        int src = crt->src;
        int dest = crt->dst;

        if (!conn_reuse && crt->msgid.has_value()) {
            cout << "msg keyword can only be used when conn_reuse is enabled.\n";
            abort();
        }

        // Per-flow RTT: propagation uses actual src/dst path type (same-leaf / same-group
        // / cross-group), serialization uses the per-flow hop count.
        // When -enable_accurate_base_rtt is not set, fall back to worst-case RTT.
        uint32_t flow_hops = top->get_two_point_diameter(src, dest);
        simtime_picosec flow_rtt =
              2 * top->get_two_point_diameter_latency(src, dest)
            + (Packet::data_packet_size() * 8 / speedAsGbps(linkspeed) * flow_hops * 1000)
            + (UecBasePacket::get_ack_size() * 8 / speedAsGbps(linkspeed) * flow_hops * 1000);
        simtime_picosec base_rtt = enable_accurate_base_rtt ? flow_rtt : network_max_unloaded_rtt;

        if (!conn_reuse
            || (crt->flowid && flowmap.find(crt->flowid) == flowmap.end())) {

            unique_ptr<UecMultipath> mp = nullptr;
            if (load_balancing_algo == BITMAP) {
                mp = make_unique<UecMpBitmap>(path_entropy_size, UecSrc::_debug);
            } else if (load_balancing_algo == REPS) {
                mp = make_unique<UecMpReps>(path_entropy_size, UecSrc::_debug, !disable_trim, reps_partition_entropy, base_rtt);
            } else if (load_balancing_algo == REPS_LEGACY) {
                mp = make_unique<UecMpRepsLegacy>(path_entropy_size, UecSrc::_debug);
            } else if (load_balancing_algo == OBLIVIOUS) {
                mp = make_unique<UecMpOblivious>(path_entropy_size, UecSrc::_debug);
            } else if (load_balancing_algo == MIXED) {
                mp = make_unique<UecMpMixed>(path_entropy_size, UecSrc::_debug);
            } else {
                cout << "ERROR: Failed to set multipath algorithm, abort." << endl;
                abort();
            }

            uec_src = new UecSrc(traffic_logger, eventlist, move(mp), *nics.at(src), ports);

            if (crt->flowid) {
                uec_src->setFlowId(crt->flowid);
                assert(flowmap.find(crt->flowid) == flowmap.end());
            }

            if (conn_reuse) {
                stringstream uec_src_dbg_tag;
                uec_src_dbg_tag << "flow_id " << uec_src->flowId();
                UecPdcSes* pdc = new UecPdcSes(uec_src, EventList::getTheEventList(),
                                                UecSrc::_mss, UecSrc::_hdr_size,
                                                uec_src_dbg_tag.str());
                uec_src->makeReusable(pdc);
                flow_pdc_map[uec_src->flowId()] = pdc;
            }

            if (receiver_driven)
                uec_snk = new UecSink(NULL, pacers[dest].get(), *nics.at(dest), ports);
            else
                uec_snk = new UecSink(NULL, linkspeed, 1.1,
                                      UecBasePacket::unquantize(UecSink::_credit_per_pull),
                                      eventlist, *nics.at(dest), ports);

            flowmap[uec_src->flowId()] = { uec_src, uec_snk };

            if (crt->flowid) {
                uec_snk->setFlowId(crt->flowid);
            }

            if (receiver_driven)
                uec_src->initRccc(cwnd_b, base_rtt);

            if (sender_driven)
                uec_src->initNscc(cwnd_b, base_rtt);

            uec_srcs.push_back(uec_src);
            uec_src->setDst(dest);

            if (log_flow_events) {
                uec_src->logFlowEvents(*event_logger);
            }

            uec_src->setName("Uec_" + ntoa(src) + "_" + ntoa(dest));
            logfile.writeName(*uec_src);
            uec_src->setSrc(src);
            uec_snk->setSrc(src);

            if (UecSink::_model_pcie)
                uec_snk->setPCIeModel(pcie_models[dest]);

            if (UecSink::_oversubscribed_cc)
                uec_snk->setOversubscribedCC(oversubscribed_ccs[dest]);

            ((DataReceiver*)uec_snk)->setName("Uec_sink_" + ntoa(src) + "_" + ntoa(dest));
            logfile.writeName(*(DataReceiver*)uec_snk);

            // Build host-to-leaf-switch routes for UEC connection setup. DragonflyPlusSwitch handles all subsequent routing internally.
            Route* srctotor = new Route();
            srctotor->push_back(top->queues_host_leaf[src][top->get_host_switch(src)]);
            srctotor->push_back(top->pipes_host_leaf[src][top->get_host_switch(src)]);
            srctotor->push_back(top->queues_host_leaf[src][top->get_host_switch(src)]->getRemoteEndpoint());

            Route* dsttotor = new Route();
            dsttotor->push_back(top->queues_host_leaf[dest][top->get_host_switch(dest)]);
            dsttotor->push_back(top->pipes_host_leaf[dest][top->get_host_switch(dest)]);
            dsttotor->push_back(top->queues_host_leaf[dest][top->get_host_switch(dest)]->getRemoteEndpoint());

            uec_src->connectPort(0, *srctotor, *dsttotor, *uec_snk, crt->start);

            // Register src and snk with their leaf switches for downlink delivery
            assert(top->switches_lf[top->get_host_switch(src)]);
            assert(top->switches_lf[top->get_host_switch(dest)]);
            top->switches_lf[top->get_host_switch(src)]->addHostPort(src, uec_snk->flowId(), uec_src->getPort(0));
            top->switches_lf[top->get_host_switch(dest)]->addHostPort(dest, uec_src->flowId(), uec_snk->getPort(0));

            if (!conn_reuse) {
                if (crt->size > 0)
                    uec_src->setFlowsize(crt->size);

                if (crt->trigger) {
                    Trigger* trig = conns->getTrigger(crt->trigger, eventlist);
                    trig->add_target(*uec_src);
                }
                if (crt->send_done_trigger) {
                    Trigger* trig = conns->getTrigger(crt->send_done_trigger, eventlist);
                    uec_src->setEndTrigger(*trig);
                }
                if (crt->recv_done_trigger) {
                    Trigger* trig = conns->getTrigger(crt->recv_done_trigger, eventlist);
                    uec_snk->setEndTrigger(*trig);
                }
            } else {
                assert(crt->size > 0);

                optional<simtime_picosec> start_ts = {};
                if (crt->start != TRIGGER_START)
                    start_ts.emplace(timeFromUs((uint32_t)crt->start));

                UecPdcSes* pdc = flow_pdc_map.find(crt->flowid)->second;
                UecMsg* msg = pdc->enque(crt->size, start_ts, true);

                if (crt->trigger) {
                    Trigger* trig = conns->getTrigger(crt->trigger, eventlist);
                    trig->add_target(*msg);
                }
                if (crt->send_done_trigger) {
                    Trigger* trig = conns->getTrigger(crt->send_done_trigger, eventlist);
                    msg->setTrigger(UecMsg::MsgStatus::SentLast, trig);
                }
                if (crt->recv_done_trigger) {
                    Trigger* trig = conns->getTrigger(crt->recv_done_trigger, eventlist);
                    uec_snk->setEndTrigger(*trig);
                    msg->setTrigger(UecMsg::MsgStatus::RecvdLast, trig);
                }
            }

            if (log_sink) {
                sink_logger->monitorSink(uec_snk);
            }
        } else {
            // Connection reuse: enqueue a new message on an existing flow
            assert(crt->msgid.has_value());

            UecPdcSes* pdc = flow_pdc_map.find(crt->flowid)->second;

            optional<simtime_picosec> start_ts = {};
            if (crt->start != TRIGGER_START)
                start_ts.emplace(timeFromUs((uint32_t)crt->start));

            UecMsg* msg = pdc->enque(crt->size, start_ts, true);

            if (crt->trigger) {
                Trigger* trig = conns->getTrigger(crt->trigger, eventlist);
                trig->add_target(*msg);
            }
            if (crt->send_done_trigger) {
                Trigger* trig = conns->getTrigger(crt->send_done_trigger, eventlist);
                msg->setTrigger(UecMsg::MsgStatus::SentLast, trig);
            }
            if (crt->recv_done_trigger) {
                Trigger* trig = conns->getTrigger(crt->recv_done_trigger, eventlist);
                msg->setTrigger(UecMsg::MsgStatus::RecvdLast, trig);
            }
        }
    }

    Logged::dump_idmap();

    int pktsize = Packet::data_packet_size();
    logfile.write("# pktsize=" + ntoa(pktsize) + " bytes");
    logfile.write("# hostnicrate = " + ntoa(linkspeed / 1000000) + " Mbps");

    cout << "Starting simulation" << endl;
    while (eventlist.doNextEvent()) {
    }

    cout << "Done" << endl;
    int new_pkts = 0, rtx_pkts = 0, bounce_pkts = 0, rts_pkts = 0,
        ack_pkts = 0, nack_pkts = 0, pull_pkts = 0, sleek_pkts = 0;
    for (size_t ix = 0; ix < uec_srcs.size(); ix++) {
        const struct UecSrc::Stats& s = uec_srcs[ix]->stats();
        new_pkts += s.new_pkts_sent;
        rtx_pkts += s.rtx_pkts_sent;
        rts_pkts += s.rts_pkts_sent;
        bounce_pkts += s.bounces_received;
        ack_pkts += s.acks_received;
        nack_pkts += s.nacks_received;
        pull_pkts += s.pulls_received;
        sleek_pkts += s._sleek_counter;
    }
    cout << "New: " << new_pkts << " Rtx: " << rtx_pkts << " RTS: " << rts_pkts
         << " Bounced: " << bounce_pkts << " ACKs: " << ack_pkts
         << " NACKs: " << nack_pkts << " Pulls: " << pull_pkts
         << " sleek_pkts: " << sleek_pkts << endl;

    return EXIT_SUCCESS;
}