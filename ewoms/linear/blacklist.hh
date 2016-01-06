// -*- mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*-
// vi: set et ts=4 sw=4 sts=4:
/*
  Copyright (C) 2014 by Andreas Lauser

  This file is part of the Open Porous Media project (OPM).

  OPM is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 2 of the License, or
  (at your option) any later version.

  OPM is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with OPM.  If not, see <http://www.gnu.org/licenses/>.
*/
/*!
 * \file
 * \copydoc Ewoms::Linear::BlackList
 */
#ifndef EWOMS_BLACK_LIST_HH
#define EWOMS_BLACK_LIST_HH

#include "overlaptypes.hh"

#if HAVE_MPI
#include <ewoms/parallel/mpibuffer.hh>

#include <dune/grid/common/datahandleif.hh>
#include <dune/grid/common/gridenums.hh>
#endif // HAVE_MPI

#include <algorithm>

namespace Ewoms {
namespace Linear {
/*!
 * \brief Expresses which degrees of freedom are blacklisted for the parallel linear
 *        solvers and which domestic indices they correspond to.
 */
class BlackList
{
public:
    struct PeerBlackListedEntry {
        Index nativeIndexOfPeer;
        Index myOwnNativeIndex;
    };
    typedef std::vector<PeerBlackListedEntry> PeerBlackList;
    typedef std::map<ProcessRank, PeerBlackList> PeerBlackLists;

    BlackList()
    { }

    BlackList(const BlackList&) = default;

    bool hasIndex(Index nativeIdx) const
    { return nativeBlackListedIndices_.count(nativeIdx) > 0; }

    void addIndex(Index nativeIdx)
    { nativeBlackListedIndices_.insert(nativeIdx); }

    Index nativeToDomestic(Index nativeIdx) const
    {
        auto it = nativeToDomesticMap_.find(nativeIdx);
        if (it == nativeToDomesticMap_.end())
            return -1;
        return it->second;
    }

    void setPeerList(ProcessRank peerRank, const PeerBlackList& peerBlackList)
    { peerBlackLists_[peerRank] = peerBlackList; }

    template <class DomesticOverlap>
    void updateNativeToDomesticMap(const DomesticOverlap &domesticOverlap)
    {
#if HAVE_MPI
        auto peerListIt = peerBlackLists_.begin();
        const auto& peerListEndIt = peerBlackLists_.end();
        for (; peerListIt != peerListEndIt; ++peerListIt) {
            sendGlobalIndices_(peerListIt->first,
                               peerListIt->second,
                               domesticOverlap);
        }

        peerListIt = peerBlackLists_.begin();
        for (; peerListIt != peerListEndIt; ++peerListIt) {
            receiveGlobalIndices_(peerListIt->first, domesticOverlap);
        }

        peerListIt = peerBlackLists_.begin();
        for (; peerListIt != peerListEndIt; ++peerListIt) {
            numGlobalIdxSendBuff_.at(peerListIt->first).wait();
            globalIdxSendBuff_.at(peerListIt->first).wait();
        }
#endif // HAVE_MPI
    }

    void print() const
    {
        std::cout << "my own blacklisted indices:\n";
        auto idxIt = nativeBlackListedIndices_.begin();
        const auto &idxEndIt = nativeBlackListedIndices_.end();
        for (; idxIt != idxEndIt; ++idxIt)
            std::cout << " (native index: " << *idxIt
                      << ", domestic index: " << nativeToDomestic(*idxIt) << ")\n";
        std::cout << "blacklisted indices of the peers in my own domain:\n";
        auto peerListIt = peerBlackLists_.begin();
        const auto& peerListEndIt = peerBlackLists_.end();
        for (; peerListIt != peerListEndIt; ++peerListIt) {
            int peerRank = peerListIt->first;
            std::cout << " peer " << peerRank << ":\n";
            auto idxIt = peerListIt->second.begin();
            const auto& idxEndIt = peerListIt->second.end();
            for (; idxIt != idxEndIt; ++ idxIt)
                std::cout << "   (native index: " << idxIt->myOwnNativeIndex
                          << ", native peer index: " << idxIt->nativeIndexOfPeer << ")\n";
        }
    }

private:
#if HAVE_MPI
    template <class DomesticOverlap>
    void sendGlobalIndices_(ProcessRank peerRank,
                            const PeerBlackList& peerIndices,
                            const DomesticOverlap& domesticOverlap)
    {
        auto& numIdxBuff = numGlobalIdxSendBuff_[peerRank];
        auto& idxBuff = globalIdxSendBuff_[peerRank];

        numIdxBuff.resize(1);
        numIdxBuff[0] = peerIndices.size();
        numIdxBuff.send(peerRank);

        idxBuff.resize(2*peerIndices.size());
        for (size_t i = 0; i < peerIndices.size(); ++i) {
            // global index
            Index myNativeIdx = peerIndices[i].myOwnNativeIndex;
            Index myDomesticIdx = domesticOverlap.nativeToDomestic(myNativeIdx);
            idxBuff[2*i + 0] = domesticOverlap.domesticToGlobal(myDomesticIdx);

            // native peer index
            idxBuff[2*i + 1] = peerIndices[i].nativeIndexOfPeer;
        }
        idxBuff.send(peerRank);
    }

    template <class DomesticOverlap>
    void receiveGlobalIndices_(ProcessRank peerRank,
                               const DomesticOverlap& domesticOverlap)
    {
        MpiBuffer<int> numGlobalIdxBuf(1);
        numGlobalIdxBuf.receive(peerRank);
        int numIndices = numGlobalIdxBuf[0];

        MpiBuffer<Index> globalIdxBuf(2*numIndices);
        globalIdxBuf.receive(peerRank);
        for (int i = 0; i < numIndices; ++i) {
            Index globalIdx = globalIdxBuf[2*i + 0];
            Index nativeIdx = globalIdxBuf[2*i + 1];

            nativeToDomesticMap_[nativeIdx] = domesticOverlap.globalToDomestic(globalIdx);
        }
    }
#endif // HAVE_MPI

    std::set<Index> nativeBlackListedIndices_;
    std::map<Index, Index> nativeToDomesticMap_;
#if HAVE_MPI
    std::map<ProcessRank, MpiBuffer<int>> numGlobalIdxSendBuff_;
    std::map<ProcessRank, MpiBuffer<Index>> globalIdxSendBuff_;
#endif // HAVE_MPI

    PeerBlackLists peerBlackLists_;
};

} // namespace Linear
} // namespace Ewoms

#endif
