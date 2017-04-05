/*
 * LteMaxDatarate.cpp
 */

#include <LteMaxDatarate.h>
#include <LteSchedulerEnb.h>

LteMaxDatarate::LteMaxDatarate() {
    mOracle = OmniscientEntity::get();
    EV_STATICCONTEXT;
    EV << "LteMaxDatarate::constructor" << std::endl;
    EV << "\t" << (mOracle == nullptr ? "Couldn't find oracle." : "Found oracle.") << std::endl;
    if (mOracle == nullptr)
        throw cRuntimeError("LteMaxDatarate couldn't find the oracle.");
}

LteMaxDatarate::~LteMaxDatarate() {}

MacNodeId LteMaxDatarate::determineD2DEndpoint(MacNodeId srcNode) const {
    EV_STATICCONTEXT;
    EV << NOW << " LteMaxDatarate::determineD2DEndpoint" << std::endl;
    // Grab <from, <to, mode>> map for all 'from' nodes.
    const std::map<MacNodeId, std::map<MacNodeId, LteD2DMode>>* map = mOracle->getModeSelectionMap();
    if (map == nullptr)
        throw cRuntimeError("LteMaxDatarate::determineD2DEndpoint couldn't get a mode selection map from the oracle.");
    // From there, grab <src, <to, mode>> for given 'srcNode'.
    const std::map<MacNodeId, LteD2DMode> destinationModeMap = map->at(srcNode);
    if (destinationModeMap.size() <= 0)
        throw cRuntimeError(std::string("LteMaxDatarate::determineD2DEndpoint <src, <to, mode>> map is empty -> couldn't find an end point from node " + std::to_string(srcNode)).c_str());
    MacNodeId dstNode = 0;
    bool foundIt = false;
    // Go through all possible destinations.
    for (std::map<MacNodeId, LteD2DMode>::const_iterator iterator = destinationModeMap.begin();
            iterator != destinationModeMap.end(); iterator++) {
        EV << "\tChecking candidate node " << (*iterator).first << "... ";
        // Check if it wanted to run in Direct Mode.
        if ((*iterator).second == LteD2DMode::DM) {
            // If yes, then consider this the endpoint.
            // @TODO this seems like a hacky workaround. >1 endpoints are not supported with this approach.
            foundIt = true;
            dstNode = (*iterator).first;
            EV << "found D2D partner." << std::endl;
            break;
        }
        EV << "nope." << std::endl;
    }
    if (!foundIt)
        throw cRuntimeError(std::string("LteMaxDatarate::determineD2DEndpoint couldn't find an end point from node " + std::to_string(srcNode)).c_str());
    EV << NOW << " LteMaxDatarate::determineD2DEndpoint found " << srcNode << " --> " << dstNode << "." << std::endl;
    return dstNode;
}

void LteMaxDatarate::prepareSchedule() {
    EV_STATICCONTEXT;
    EV << NOW << "LteMaxDatarate::prepareSchedule" << std::endl;

    // Copy currently active connections to a working copy.
    activeConnectionTempSet_ = activeConnectionSet_;

    // For all connections, we want all resource blocks sorted by their estimated maximum datarate.
    // The sorter holds each connection's info: from, to, direction, datarate and keeps entries
    // sorted by datarate in descending order.
    MaxDatarateSorter* sorter = sortBandsByDatarate();

    // nullptr is returned if the oracle is not ready yet.
    if (sorter == nullptr)
        return;

    // We now have all <band, id> combinations sorted by expected datarate.
    EV << "RBs sorted according to their throughputs: " << std::endl;
    EV << sorter->toString() << std::endl;

    // Go through all bands.
    for (Band band = 0; band < sorter->size(); band++) {
        std::vector<IdRatePair> list = sorter->at(band);
        if (list.size() <= 0) {
            EV << "Skipping empty list for band " << band << "." << std::endl;
            continue;
        }
        // Assign band to best candidate.
        IdRatePair& bestCandidate = list.at(0);

        EV << NOW << "LteMaxDatarate::prepareSchedule granting " << bestCandidate.from << " -"
           << dirToA(bestCandidate.dir) << "-> " << bestCandidate.to;

        SchedulingResult grantAnswer = schedule(bestCandidate.connectionId, band);

        EV << NOW << "LteMaxDatarate::prepareSchedule grant answer is "
           << (grantAnswer == SchedulingResult::TERMINATE ? "TERMINATE" :
               grantAnswer == SchedulingResult::INACTIVE ? "INACTIVE" :
               grantAnswer == SchedulingResult::INELIGIBLE ? "INELIGIBLE" :
               "OK") << std::endl;

        // Exit immediately if the terminate flag is set.
        if (grantAnswer == SchedulingResult::TERMINATE) {
            EV << NOW << "LteMaxDatarate::prepareSchedule exiting due to terminate flag being set." << std::endl;
            // The 'terminate' flag is set if a codeword is already allocated
            // or if the OFDM space has ended. The way I understand it is that nothing further
            // can be scheduled in this scheduling round.
            return;
        }

        // Set the connection as inactive if indicated by the grant.
        if (grantAnswer == SchedulingResult::INACTIVE) {
            EV << NOW << "LteMaxDatarate::prepareSchedule setting " << bestCandidate.from << " to inactive" << std::endl;
            activeConnectionTempSet_.erase(bestCandidate.from);
            // A connection is set as inactive if the node's queue length is 0.
            // This means nothing needs to be scheduled to this node anymore,
            // so remove it from our container so it's not considered anymore.
            sorter->remove(bestCandidate.from);
            continue;
        }

        // Now check if the same candidate should be assigned consecutive resource blocks.
        for (Band consecutiveBand = band + 1; consecutiveBand < sorter->size(); consecutiveBand++) {
            // Determine throughput for halved transmit power.
            double halvedTxPower = bestCandidate.txPower / 2;
            std::vector<double> consecutiveSINRs = mOracle->getSINR(bestCandidate.from, bestCandidate.to, NOW, halvedTxPower);
            double associatedSinr = consecutiveSINRs.at(consecutiveBand);
            double estimatedThroughput = mOracle->getChannelCapacity(associatedSinr);
            EV << "Determined throughput for consecutive band " << consecutiveBand << " at halved transmit power of "
               << halvedTxPower << ": " << estimatedThroughput << std::endl;

            // Is this better than the next best candidate?
            std::vector<IdRatePair> consecutiveList = sorter->at(consecutiveBand);
            IdRatePair& bestConsecutiveCandidate = consecutiveList.at(0);

            if (bestConsecutiveCandidate.rate < estimatedThroughput) {
                EV << "Consecutive throughput at halved txPower is still better than the best candidate: "
                   << bestConsecutiveCandidate.rate << " < " << estimatedThroughput << std::endl;
                // Assign this band also.
                grantAnswer = schedule(bestCandidate.connectionId, band);

                EV << NOW << "LteMaxDatarate::prepareSchedule grant answer is "
                   << (grantAnswer == SchedulingResult::TERMINATE ? "TERMINATE" :
                       grantAnswer == SchedulingResult::INACTIVE ? "INACTIVE" :
                       grantAnswer == SchedulingResult::INELIGIBLE ? "INELIGIBLE" :
                       "OK") << std::endl;

                // Increment band so that this band is not double-assigned.
                band++;
                // Update txPower so that next consecutive check halves txPower again.
                bestCandidate.txPower = halvedTxPower;

                // Exit immediately if the terminate flag is set.
                if (grantAnswer == SchedulingResult::TERMINATE) {
                    EV << NOW << "LteMaxDatarate::prepareSchedule exiting due to terminate flag being set." << std::endl;
                    break;
                }

                // Set the connection as inactive if indicated by the grant.
                if (grantAnswer == SchedulingResult::INACTIVE) {
                    EV << NOW << "LteMaxDatarate::prepareSchedule setting " << bestCandidate.from << " to inactive" << std::endl;
                    activeConnectionTempSet_.erase(bestCandidate.from);
                    // A connection is set as inactive if the node's queue length is 0.
                    // This means nothing needs to be scheduled to this node anymore,
                    // so remove it from our container so it's not considered anymore.
                    sorter->remove(bestCandidate.from);
                    continue;
                }
            // Current candidate transmitting at halved power has worse throughput than next candidate.
            } else {
                EV << "Consecutive throughput at halved txPower is not better than the best candidate: "
                   << bestConsecutiveCandidate.rate << " >= " << estimatedThroughput << std::endl;
                // Remove current candidate as it shouldn't be assigned any more resource blocks in this scheduling round.
                sorter->remove(bestCandidate.from);
                // Quit checking consecutive bands. Next band will be assigned to the next best candidate.
                break;
            }
        }
    }

    delete sorter;
}

MaxDatarateSorter* LteMaxDatarate::sortBandsByDatarate() {
    int numBands;
    try {
        numBands = mOracle->getNumberOfBands();
    } catch (const cRuntimeError& e) {
        EV << "Oracle not yet ready. Skipping scheduling round." << std::endl;
        return nullptr;
    }

    MaxDatarateSorter* sorter = new MaxDatarateSorter(numBands);

    for (ActiveSet::iterator iterator = activeConnectionTempSet_.begin(); iterator != activeConnectionTempSet_.end (); ++iterator) {
        MacCid currentConnection = *iterator;
        MacNodeId nodeId = MacCidToNodeId(currentConnection);

        // Make sure the current node has not been dynamically removed.
        if (getBinder()->getOmnetId(nodeId) == 0){
            activeConnectionTempSet_.erase(currentConnection);
            EV << NOW << "\t\t has been dynamically removed. Skipping..." << std::endl;
            continue;
        }

        // Determine direction. Uplink and downlink resources are scheduled separately,
        // and LteScheduler's 'direction_' contains the current scheduling direction.
        Direction dir;
        // Uplink may be reused for D2D, so we have to check if the Buffer Status Report (BSR) tells us that this is a D2D link.
        if (direction_ == UL)
            dir = (MacCidToLcid(currentConnection) == D2D_SHORT_BSR) ? D2D : direction_;
        else
            dir = DL;

        EV << "\tNode " << nodeId << " wants to transmit in " << dirToA(dir) << " direction." << std::endl;

        // Determine device's maximum transmission power.
        double maxTransmitPower = mOracle->getTransmissionPower(nodeId, dir);
        // We need the SINR values for each band.
        std::vector<double> SINRs;
        MacNodeId destinationId;
        // SINR computation depends on the direction.
        switch (dir) {
            // Uplink: node->eNB
            case Direction::UL: {
                destinationId = mOracle->getEnodeBId();
                SINRs = mOracle->getSINR(nodeId, destinationId, NOW, maxTransmitPower);
                EV << "From node " << nodeId << " to eNodeB " << mOracle->getEnodeBId() << " (Uplink)" << std::endl;
                break;
            }
            // Downlink: eNB->node
            case Direction::DL: {
                destinationId = nodeId;
                nodeId = mOracle->getEnodeBId();
                SINRs = mOracle->getSINR(nodeId, destinationId , NOW, maxTransmitPower);
                EV << "From eNodeB " << mOracle->getEnodeBId() << " to node " << nodeId << " (Downlink)" << std::endl;
                break;
            }
            // Direct: node->node
            case Direction::D2D: {
                destinationId = determineD2DEndpoint(nodeId);
                SINRs = mOracle->getSINR(nodeId, destinationId, NOW, maxTransmitPower);
                EV << "From node " << nodeId << " to node " << destinationId << " (Direct Link)" << std::endl;
                break;
            }
            default: {
                // This can't happen, dir is specifically set above. Just for the sake of completeness.
                throw cRuntimeError(std::string("LteMaxDatarate::prepareSchedule sees undefined direction: " + std::to_string(dir)).c_str());
            }
        }

        // Go through all bands ...
        for (size_t i = 0; i < SINRs.size(); i++) {
            Band currentBand = Band(i);
            // Estimate maximum throughput.
            double associatedSinr = SINRs.at(currentBand);
            double estimatedThroughput = mOracle->getChannelCapacity(associatedSinr);
            // And put the result into the container.
            sorter->put(currentBand, IdRatePair(currentConnection, nodeId, destinationId, maxTransmitPower, estimatedThroughput, dir));
        }
    }

    return sorter;
}

void LteMaxDatarate::preparePhase1() {

}

LteMaxDatarate::SchedulingResult LteMaxDatarate::schedule(MacCid connectionId, Band band) {
    bool terminate = false;
    bool active = true;
    bool eligible = true;
    // The BandLimit should make sure that only the current band is scheduled.
    BandLimit bandLimit(band);
    std::vector<BandLimit> bandLimitVec;
    bandLimitVec.push_back(bandLimit);
    // requestGrant(...) might alter the three bool values, so we can check them afterwards.
    unsigned int granted = requestGrant(connectionId, 4294967295U, terminate, active, eligible, &bandLimitVec);
    EV << " " << granted << "bytes granted." << std::endl;
    if (terminate)
        return LteMaxDatarate::SchedulingResult::TERMINATE;
    else if (!active)
        return LteMaxDatarate::SchedulingResult::INACTIVE;
    else if (!eligible)
        return LteMaxDatarate::SchedulingResult::INELIGIBLE;
    else
        return LteMaxDatarate::SchedulingResult::OK;
}

void LteMaxDatarate::commitSchedule() {
    EV_STATICCONTEXT;
    EV << "LteMaxDatarate::commitSchedule" << std::endl;
    activeConnectionSet_ = activeConnectionTempSet_;
}

void LteMaxDatarate::notifyActiveConnection(MacCid conectionId) {
    EV << NOW << "LteMaxDatarate::notifyActiveConnection(MacCid=" << conectionId << " [MacNodeId=" << MacCidToNodeId(conectionId) << "])" << std::endl;
    activeConnectionSet_.insert(conectionId);
}

void LteMaxDatarate::removeActiveConnection(MacCid conectionId) {
    EV << NOW << "LteMaxDatarate::removeActiveConnection(MacCid=" << conectionId << " [MacNodeId=" << MacCidToNodeId(conectionId) << "])" << std::endl;
    activeConnectionSet_.erase(conectionId);
}
