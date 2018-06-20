//
// Copyright (C) 2014 OpenSim Ltd.
// Author: Benjamin Seregi
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU Lesser General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU Lesser General Public License for more details.
//
// You should have received a copy of the GNU Lesser General Public License
// along with this program.  If not, see http://www.gnu.org/licenses/.
//

#ifndef __INET_AODVLDROUTING_H
#define __INET_AODVLDROUTING_H

#include "inet/common/INETDefs.h"
#include "inet/networklayer/contract/IInterfaceTable.h"
#include "inet/networklayer/contract/IL3AddressType.h"
#include "inet/networklayer/contract/IRoutingTable.h"
#include "inet/networklayer/contract/INetfilter.h"
#include "inet/common/lifecycle/ILifecycle.h"
#include "inet/common/lifecycle/NodeStatus.h"
#include "inet/transportlayer/contract/udp/UDPSocket.h"
#include <aodvLD/AODVLDRouteData.h>
#include "inet/transportlayer/udp/UDPPacket.h"
#include <aodvLD/AODVLDControlPackets_m.h>
#include <LinkDuration/ResidualLinklifetime.h>
#include <map>

namespace inet {

/*
 * This class implements AODVLD routing protocol and Netfilter hooks
 * in the IP-layer required by this protocol.
 */

class INET_API AODVLDRouting : public cSimpleModule, public ILifecycle, public INetfilter::IHook, public cListener
{
  protected:
    /*
     * It implements a unique identifier for an arbitrary RREQ message
     * in the network. See: rreqsArrivalTime.metrikmodule
     */
    class RREQIdentifier
    {
      public:
        L3Address originatorAddr;
        unsigned int rreqID;

        RREQIdentifier(const L3Address& originatorAddr, unsigned int rreqID) : originatorAddr(originatorAddr), rreqID(rreqID) {};
        //Operator overloading. "==" compares address and rreqIDs now
        bool operator==(const RREQIdentifier& other) const
        {
            return this->originatorAddr == other.originatorAddr && this->rreqID == other.rreqID;
        }
        L3Address getOrignatorAddress(){return originatorAddr;}

    };

    class RREQAdditionalInfo
    {
    private:
        L3Address sourceAddr;
        unsigned int packetTTL;
    public:
        //RREQAdditionalInfo(L3Address sourceAddr,unsigned int packetTTL) : sourceAddr (sourceAddr),packetTTL (packetTTL) {};
        L3Address getSourceAddr()  {return sourceAddr;}
        unsigned int getPacketTTL()  {return packetTTL;}
        void setSourceAddr(L3Address sourceAddr){this->sourceAddr=sourceAddr;}
        void setPacketTTL(unsigned int packetTTL){this->packetTTL=packetTTL;}

    };

    class RREQIdentifierCompare
    {
      public:
        bool operator()(const RREQIdentifier& lhs, const RREQIdentifier& rhs) const
        {
            return ((lhs.rreqID < rhs.rreqID)&&(lhs.originatorAddr==rhs.originatorAddr));
        }
    };

    //statistics
    double firstRREPArrives;
    simtime_t RREQsent;
    int numHops=0;

    // context
    IL3AddressType *addressType = nullptr;    // to support both IPv4 and v6 addresses.

    // environment
    cModule *host = nullptr;
    IRoutingTable *routingTable = nullptr;
    IInterfaceTable *interfaceTable = nullptr;
    INetfilter *networkProtocol = nullptr;
    ResidualLinklifetime *metrikmodule=nullptr;
    SimpleNeighborDiscovery* neighborModule=nullptr;

    // AODVLD parameters: the following parameters are configurable, see the NED file for more info.
    unsigned int rerrRatelimit = 0;
    unsigned int aodvLDUDPPort = 0;//AODVLDUDPPort
    bool askGratuitousRREP = false;
    bool useHelloMessages = false;
    simtime_t maxJitter;
    simtime_t activeRouteTimeout;
    simtime_t helloInterval;
    unsigned int netDiameter = 0;
    unsigned int rreqRetries = 0;
    unsigned int rreqRatelimit = 0;
    unsigned int timeoutBuffer = 0;
    unsigned int ttlStart = 0;
    unsigned int ttlIncrement = 0;
    unsigned int ttlThreshold = 0;
    unsigned int localAddTTL = 0;
    unsigned int allowedHelloLoss = 0;
    simtime_t nodeTraversalTime;
    //L3Address multicastAddress;//for SIMULTE since LTE-A uses Multicast instead of Broadcast. Same address as in demo.xml
    cPar *jitterPar = nullptr;
    cPar *periodicJitter = nullptr;
    int routeErrorCounter =0; //Counts the number of unicast packets passing aodvld without having a route to destination


    // the following parameters are calculated from the parameters defined above
    // see the NED file for more info
    simtime_t deletePeriod;
    simtime_t myRouteTimeout;
    simtime_t blacklistTimeout;
    simtime_t netTraversalTime;
    simtime_t nextHopWait;
    simtime_t pathDiscoveryTime;
    simtime_t RREQCollectionTimeMin;
    simtime_t RREQCollectionTimeMean;
    simtime_t RREQinLastTransTimer;

    bool RREQTimerHopDependeny;
    simtime_t RREP_Arrival_timestamp=0;
    simtime_t RREQ_Created_timestamp=0;

    IPv4Datagram* tmpdatagram;

    //statistics

    simsignal_t numRREQsent;
    simsignal_t routeAvailability;
    simsignal_t interRREQRREPTime;
    simsignal_t interRREPRouteDiscoveryTime;
    simsignal_t numFinalHops;
    simsignal_t numRREQForwarded;
    simsignal_t numSentRERR;
    simsignal_t numReceivedRERR;
    simsignal_t newSeqNum;
    simsignal_t theoreticalRL;
    simsignal_t sentAODVLDpackets;




    // state
    unsigned int rreqId = 0;    // when sending a new RREQ packet, rreqID incremented by one from the last id used by this node
    unsigned int sequenceNum = 0;    // it helps to prevent loops in the routes (RFC 3561 6.1 p11.)
    std::map<L3Address, WaitForRREP *> waitForRREPTimers;    // timeout for Route Replies
    std::map<RREQIdentifier, simtime_t, RREQIdentifierCompare> rreqsArrivalTime;    // maps RREQ id to its arriving time
    L3Address failedNextHop;    // next hop to the destination who failed to send us RREP-ACK
    std::map<L3Address, simtime_t> blacklist;    // we don't accept RREQs from blacklisted nodes
    unsigned int rerrCount = 0;    // num of originated RERR in the last second
    unsigned int rreqCount = 0;    // num of originated RREQ in the last second
    simtime_t lastBroadcastTime;    // the last time when any control packet was broadcasted
    std::map<L3Address, unsigned int> addressToRreqRetries;    // number of re-discovery attempts per address
    L3Address DestinationAddress;


    std::map<const RREQIdentifier,std::pair<RREQAdditionalInfo,AODVLDRREQ*>,RREQIdentifierCompare>CurrentBestRREQ; // the current best rreq to transmit again from a certain originator
    std::map<const RREQIdentifier,std::pair<RREQAdditionalInfo,AODVLDRREQ*>,RREQIdentifierCompare>LastTransmittedRREQ; //last transmitted rreq to be able to compare it a later one is better

    // self messages
    cMessage *helloMsgTimer = nullptr;    // timer to send hello messages (only if the feature is enabled)
    cMessage *expungeTimer = nullptr;    // timer to clean the routing table out
    cMessage *counterTimer = nullptr;    // timer to set rrerCount = rreqCount = 0 in each second
    cMessage *rrepAckTimer = nullptr;    // timer to wait for RREP-ACKs (RREP-ACK timeout)
    cMessage *blacklistTimer = nullptr;    // timer to clean the blacklist out
    cMessage *updateTimer = nullptr; // Statistics: Used for timetamp, if route is there or not to DestinationAddress
    std::vector<WaitForRREQ *>rreqcollectionTimer;

    std::vector<LastTransDel *>rreqkeepingTimer; // How long are rreq kept in lasttransmitted buffer
    int lengthColTimer=0;
    int lengthKeepTimer=0;
    int lengthBestBuffer=0;
    int lengthLastBuffer=0;
       // lifecycle
    simtime_t rebootTime;    // the last time when the node rebooted
    bool isOperational = false;


    // internal
    std::multimap<L3Address, INetworkDatagram *> targetAddressToDelayedPackets;    // queue for the datagrams we have no route for

  protected:
    void handleMessage(cMessage *msg) override;
    void initialize(int stage) override;
    virtual int numInitStages() const override { return NUM_INIT_STAGES; }

    /* Route Discovery */
    void startRouteDiscovery(const L3Address& target, unsigned int timeToLive = 0);
    void completeRouteDiscovery(const L3Address& target);
    bool hasOngoingRouteDiscovery(const L3Address& destAddr);
    void cancelRouteDiscovery(const L3Address& destAddr);

    /* Routing Table management */
    void updateRoutingTable(IRoute *route, const L3Address& nextHop, unsigned int hopCount, bool hasValidDestNum, unsigned int destSeqNum, bool isActive, simtime_t lifeTime, simtime_t residualRouteLifetime);
    IRoute *createRoute(const L3Address& destAddr, const L3Address& nextHop, unsigned int hopCount, bool hasValidDestNum, unsigned int destSeqNum, bool isActive, simtime_t lifeTime, simtime_t residualRouteLifetime);
    bool updateValidRouteLifeTime(const L3Address& destAddr, simtime_t lifetime);
    void scheduleExpungeRoutes();
    unsigned int calculateResidualLinklifetime();

    void expungeRoutes();

    /* Control packet creators */
    AODVLDRREPACK *createRREPACK();
    AODVLDRREP *createHelloMessage();
    AODVLDRREQ *createRREQ(const L3Address& destAddr);
    AODVLDRREP *createRREP(AODVLDRREQ *rreq, IRoute *destRoute, IRoute *originatorRoute, const L3Address& sourceAddr);
    AODVLDRREP *createGratuitousRREP(AODVLDRREQ *rreq, IRoute *originatorRoute);
    AODVLDRERR *createRERR(const std::vector<UnreachableNode>& unreachableNodes);

    /* Control Packet handlers */
    void handleRREP(AODVLDRREP *rrep, const L3Address& sourceAddr);
    void prehandleRREQ(AODVLDRREQ *rreq, const L3Address& sourceAddr, unsigned int timeToLive);
    void handleRERR(AODVLDRERR *rerr, const L3Address& sourceAddr);
    void handleHelloMessage(AODVLDRREP *helloMessage);
    void handleRREPACK(AODVLDRREPACK *rrepACK, const L3Address& neighborAddr);

    /* Control Packet sender methods */
    void sendRREQ(AODVLDRREQ *rreq, const L3Address& destAddr, unsigned int timeToLive);
    void sendRREPACK(AODVLDRREPACK *rrepACK, const L3Address& destAddr);
    void sendRREP(AODVLDRREP *rrep, const L3Address& destAddr, unsigned int timeToLive);
    void sendGRREP(AODVLDRREP *grrep, const L3Address& destAddr, unsigned int timeToLive);

    /* Control Packet forwarders */
    void forwardRREP(AODVLDRREP *rrep, const L3Address& destAddr, unsigned int timeToLive);
    void forwardRREQ(AODVLDRREQ *rreq, unsigned int timeToLive);

    /* Self message handlers */
    void handleRREPACKTimer();
    void handleBlackListTimer();
    void sendHelloMessagesIfNeeded();
    void handleWaitForRREP(WaitForRREP *rrepTimer);
    /*Handle best RREQ after timer has expired*/
    void handleRREQ(WaitForRREQ *rreqTimer);
    void deleteLastTrans(LastTransDel* delTimer);

    /* General functions to handle route errors */
    void sendRERRWhenNoRouteToForward(const L3Address& unreachableAddr);
    void handleLinkBreakSendRERR(const L3Address& unreachableAddr);
    virtual void receiveSignal(cComponent *source, simsignal_t signalID, cObject *obj DETAILS_ARG) override;

    /* Netfilter hooks */
    Result ensureRouteForDatagram(INetworkDatagram *datagram);
    virtual Result datagramPreRoutingHook(INetworkDatagram *datagram, const InterfaceEntry *inputInterfaceEntry, const InterfaceEntry *& outputInterfaceEntry, L3Address& nextHopAddress) override { Enter_Method("datagramPreRoutingHook"); return ensureRouteForDatagram(datagram); }
    virtual Result datagramForwardHook(INetworkDatagram *datagram, const InterfaceEntry *inputInterfaceEntry, const InterfaceEntry *& outputInterfaceEntry, L3Address& nextHopAddress) override;
    virtual Result datagramPostRoutingHook(INetworkDatagram *datagram, const InterfaceEntry *inputInterfaceEntry, const InterfaceEntry *& outputInterfaceEntry, L3Address& nextHopAddress) override { return ACCEPT; }
    virtual Result datagramLocalInHook(INetworkDatagram *datagram, const InterfaceEntry *inputInterfaceEntry) override { return ACCEPT; }
    virtual Result datagramLocalOutHook(INetworkDatagram *datagram, const InterfaceEntry *& outputInterfaceEntry, L3Address& nextHopAddress) override { Enter_Method("datagramLocalOutHook"); return ensureRouteForDatagram(datagram); }
    void delayDatagram(INetworkDatagram *datagram);

    /* Helper functions */
    L3Address getSelfIPAddress() const;
    void sendAODVLDPacket(AODVLDControlPacket *packet, const L3Address& destAddr, unsigned int timeToLive, double delay);
    void clearState();

    /* Lifecycle */
    virtual bool handleOperationStage(LifecycleOperation *operation, int stage, IDoneCallback *doneCallback) override;

  public:
    AODVLDRouting();
    virtual ~AODVLDRouting();
};

} // namespace inet

#endif    // ifndef AODVROUTING_H_

