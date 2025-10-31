// -*- c-basic-offset: 4; indent-tabs-mode: nil -*-  
#include <climits>
#include "routetable.h"
#include "network.h"
#include "queue.h"
#include "pipe.h"

void RouteTable::addRoute(int destination, Route* port, int cost, packet_direction direction){  
    if (_fib.find(destination) == _fib.end())
        _fib[destination] = new vector<FibEntry*>(); 
    
    assert(port!=NULL);

    _fib[destination]->push_back(new FibEntry(port,cost,direction));
}

void RouteTable::addRoutePriority(int destination, Route* port, int cost, packet_direction direction, Priority prio, uint32_t channel){  
    if (prio == HIGH){
        if (_fib_high.find(destination) == _fib_high.end())
            _fib_high[destination] = new vector<FibEntry*>(); 
        
        assert(port!=NULL);

        _fib_high[destination]->push_back(new FibEntry(port,cost,direction,channel));
    } else if (prio == MID){
        if (_fib_medium.find(destination) == _fib_medium.end())
            _fib_medium[destination] = new vector<FibEntry*>(); 
        
        assert(port!=NULL);

        _fib_medium[destination]->push_back(new FibEntry(port,cost,direction,channel));
    } else {
        if (_fib_low.find(destination) == _fib_low.end())
            _fib_low[destination] = new vector<FibEntry*>(); 
        
        assert(port!=NULL);

        _fib_low[destination]->push_back(new FibEntry(port,cost,direction,channel));
    }
}

void RouteTable::addHostRoute(int destination, Route* port, int flowid){  
    if (_hostfib.find(destination) == _hostfib.end())
        _hostfib[destination] = new unordered_map<int, HostFibEntry*>(); 
    
    assert(port!=NULL);

    (*_hostfib[destination])[flowid] = new HostFibEntry(port,flowid);
}


vector<FibEntry*>* RouteTable::getRoutes(int destination){
    if (_fib.find(destination) == _fib.end())
        return NULL;
    else        
        return _fib[destination];
}

vector<FibEntry*>* RouteTable::getRoutesHigh(int destination){
    if (_fib_high.find(destination) == _fib_high.end())
        return NULL;
    else        
        return _fib_high[destination];
}

vector<FibEntry*>* RouteTable::getRoutesMedium(int destination){
    if (_fib_medium.find(destination) == _fib_medium.end())
        return NULL;
    else        
        return _fib_medium[destination];
}

vector<FibEntry*>* RouteTable::getRoutesLow(int destination){
    if (_fib_low.find(destination) == _fib_low.end())
        return NULL;
    else        
        return _fib_low[destination];
}

HostFibEntry* RouteTable::getHostRoute(int destination,int flowid){
    if (_hostfib.find(destination) == _hostfib.end() ||
        _hostfib[destination]->find(flowid) == _hostfib[destination]->end())
        return NULL;
    else {
        return (*_hostfib[destination])[flowid];
    }
}

void RouteTable::setRoutes(int destination, vector<FibEntry*>* routes){
    _fib[destination] = routes;
}

void RouteTable::setRoutesPriority(int destination, vector<FibEntry*>* routes, Priority prio){
    if (prio == HIGH){
        _fib_high[destination] = routes;
    } else if (prio == MID){
        _fib_medium[destination] = routes;
    } else {
        _fib_low[destination] = routes;
    }
}
