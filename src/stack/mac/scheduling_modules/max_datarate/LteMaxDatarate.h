/*
 * LteMaxDatarate.h
 */

#ifndef STACK_MAC_SCHEDULING_MODULES_LTEMAXDATARATE_H_
#define STACK_MAC_SCHEDULING_MODULES_LTEMAXDATARATE_H_

#include <omnetpp.h>
#include <LteScheduler.h>
#include "LteCommon.h"
#include <OmniscientEntity.h>
#include <MaxDatarateSorter.h>

/**
 * Implementation of the algorithm proposed by
 *  Ma, Ruofei
 *  Xia, Nian
 *  Chen, Hsiao-Hwa
 *  Chiu, Chun-Yuan
 *  Yang, Chu-Sing
 * from the National Cheng Kung University in Taiwan,
 * from their paper 'Mode Selection, Radio Resource Allocation, and Power Coordination in D2D Communications',
 * published in 'IEEE Wireless Communications' in 2017.
 */
class LteMaxDatarate : public virtual LteScheduler {
public:
    enum SchedulingResult {
        OK = 0, TERMINATE, INACTIVE, INELIGIBLE
    };

    LteMaxDatarate();
    virtual ~LteMaxDatarate();

    /**
     * Apply the algorithm on temporal object 'activeConnectionTempSet_'.
     */
    virtual void prepareSchedule() override;

    /**
     * Put the results from prepareSchedule() into the actually-used object 'activeConnectionSet_'.
     */
    virtual void commitSchedule() override;

    /**
     * When the LteSchedulerEnb learns of an active connection it notifies the LteScheduler.
     * It is essential to save this information. (I think this should be the default behaviour and be done in the LteScheduler class)
     */
    void notifyActiveConnection(MacCid cid) override;

    /**
     * When the LteSchedulerEnb learns of a connection going inactive it notifies the LteScheduler.
     */
    void removeActiveConnection(MacCid cid) override;

protected:
    OmniscientEntity* mOracle = nullptr;

    MaxDatarateSorter* sortBandsByDatarate();

    /**
     * Assigns each resource block to that device that is expected to have the largest channel capacity on it.
     * Corresponds to the algorithm's first step.
     */
    void preparePhase1();
    /**
     * Double-assigns resource blocks to devices that left the first round empty-handed.
     * Corresponds to the algorithm's second step.
     */
    void preparePhase2();

    /**
     * Determines first match for a D2D endpoint for this node
     * according to the mode selection map provided by the MaxDatarate mode selector.
     * @TODO Find a better way of finding the endpoint. Right now only one D2D peer is considered.
     * @param srcNode D2D channel starting node.
     * @return D2D channel end node.
     */
    MacNodeId determineD2DEndpoint(MacNodeId srcNode) const;

    /**
     * Helper method that requests a grant for the specified connection.
     */
    LteMaxDatarate::SchedulingResult schedule(MacCid connectionId, Band band);
};

#endif /* STACK_MAC_SCHEDULING_MODULES_LTEMAXDATARATE_H_ */
