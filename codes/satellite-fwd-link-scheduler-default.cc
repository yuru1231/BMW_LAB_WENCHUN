/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
 * Copyright ...
 * Author: ...
 */

#include "satellite-fwd-link-scheduler-default.h"

#include <ns3/boolean.h>
#include <ns3/uinteger.h>
#include <ns3/log.h>

#include <algorithm>
#include <map>
#include <cmath>   

NS_LOG_COMPONENT_DEFINE("SatFwdLinkSchedulerDefault");

namespace ns3
{

NS_OBJECT_ENSURE_REGISTERED(SatFwdLinkSchedulerDefault);

void
SatFwdLinkSchedulerDefault::GetSchedulingObjects(std::vector<Ptr<SatSchedulingObject>>& output)
{
    NS_LOG_FUNCTION(this);

    // Avoid reusing stale entries from previous round
    output.clear();

    if (m_bbFrameContainer->GetTotalDuration() < m_schedulingStopThresholdTime)
    {
        // Always fetch objects from LLC
        m_schedContextCallback(output);

        // Debug sampling gate (attribute-controlled)
        const bool dbgOn = m_dbgEnable && (m_dbgEvery > 0);
        const uint64_t roundCtr = m_dbgCtr; // snapshot
        if (dbgOn)
        {
            ++m_dbgCtr;
        }

        const bool doPrint = dbgOn && ((roundCtr % m_dbgEvery) == 0);

        if (doPrint)
        {
            NS_LOG_UNCOND("### SCHEDDBG v20260302 ctr=" << roundCtr << " ###");
            NS_LOG_UNCOND("=== BEFORE SORT ===");
            for (auto& o : output)
            {
                NS_LOG_UNCOND("flowId=" << static_cast<uint32_t>(o->GetFlowId())
                                        << " buf=" << o->GetBufferedBytes()
                                        << " hol=" << o->GetHolDelay().GetSeconds());
            }
        }

        // QoS policy: strict by flowId, then HOL desc, then buffer desc
        std::sort(output.begin(), output.end(),
                  [](const Ptr<SatSchedulingObject>& a, const Ptr<SatSchedulingObject>& b)
                  {
                      const uint32_t fa = static_cast<uint32_t>(a->GetFlowId());
                      const uint32_t fb = static_cast<uint32_t>(b->GetFlowId());
                      if (fa != fb)
                          return fa < fb;
                      if (a->GetHolDelay() != b->GetHolDelay())
                          return a->GetHolDelay() > b->GetHolDelay();
                      return a->GetBufferedBytes() > b->GetBufferedBytes();
                  });

        if (doPrint)
        {
            NS_LOG_UNCOND("=== AFTER QoS SORT ===");

            std::map<uint32_t, uint32_t> cnt;
            for (auto& o : output)
                cnt[static_cast<uint32_t>(o->GetFlowId())]++;

            NS_LOG_UNCOND("=== FLOWID COUNT ===");
            for (auto& kv : cnt)
                NS_LOG_UNCOND("flowId=" << kv.first << " count=" << kv.second);
        }
    }
}

TypeId
SatFwdLinkSchedulerDefault::GetTypeId(void)
{
    static TypeId tid =
        TypeId("ns3::SatFwdLinkSchedulerDefault")
            .SetParent<SatFwdLinkScheduler>()
            .AddConstructor<SatFwdLinkSchedulerDefault>()
            .AddAttribute(
                "SchedulingStartThresholdTime",
                "Threshold time of total transmissions in BB Frame container to trigger a "
                "scheduling round.",
                TimeValue(MilliSeconds(5)),
                MakeTimeAccessor(&SatFwdLinkSchedulerDefault::m_schedulingStartThresholdTime),
                MakeTimeChecker())
            .AddAttribute(
                "SchedulingStopThresholdTime",
                "Threshold time of total transmissions in BB Frame container to stop a scheduling "
                "round.",
                TimeValue(MilliSeconds(15)),
                MakeTimeAccessor(&SatFwdLinkSchedulerDefault::m_schedulingStopThresholdTime),
                MakeTimeChecker())
            .AddAttribute("BBFrameContainer",
                          "BB frame container of this scheduler.",
                          PointerValue(),
                          MakePointerAccessor(&SatFwdLinkSchedulerDefault::m_bbFrameContainer),
                          MakePointerChecker<SatBbFrameContainer>())
            // ===== Debug sampling attributes (NEW) =====
            .AddAttribute("DebugEnable",
                          "Enable scheduler debug prints (sampled).",
                          BooleanValue(false),
                          MakeBooleanAccessor(&SatFwdLinkSchedulerDefault::m_dbgEnable),
                          MakeBooleanChecker())
            .AddAttribute("DebugEvery",
                          "Print 1 out of N scheduler rounds (when DebugEnable=1).",
                          UintegerValue(500),
                          MakeUintegerAccessor(&SatFwdLinkSchedulerDefault::m_dbgEvery),
                          MakeUintegerChecker<uint32_t>(1)); // <-- match member type!

    return tid;
}

TypeId
SatFwdLinkSchedulerDefault::GetInstanceTypeId(void) const
{
    NS_LOG_FUNCTION(this);
    return GetTypeId();
}

SatFwdLinkSchedulerDefault::SatFwdLinkSchedulerDefault()
    : SatFwdLinkScheduler()
{
    NS_LOG_FUNCTION(this);
    NS_FATAL_ERROR("Default constructor for SatFwdLinkSchedulerDefault not supported");
}

SatFwdLinkSchedulerDefault::SatFwdLinkSchedulerDefault(Ptr<SatBbFrameConf> conf,
                                                       Mac48Address address,
                                                       double carrierBandwidthInHz)
    : SatFwdLinkScheduler(conf, address, carrierBandwidthInHz),
      m_symbolsSent(0)
{
    NS_LOG_FUNCTION(this);

    ObjectBase::ConstructSelf(AttributeConstructionList());

    NS_LOG_UNCOND("[SCHED-CTOR] type=SatFwdLinkSchedulerDefault dbgEnable="
                  << (m_dbgEnable ? "true" : "false")
                  << " dbgEvery=" << m_dbgEvery);

    std::vector<SatEnums::SatModcod_t> modCods = conf->GetModCodsUsed();
    m_bbFrameContainer = CreateObject<SatBbFrameContainer>(modCods, m_bbFrameConf);

    Simulator::Schedule(m_periodicInterval,
                        &SatFwdLinkSchedulerDefault::PeriodicTimerExpired,
                        this);
}

SatFwdLinkSchedulerDefault::~SatFwdLinkSchedulerDefault()
{
    NS_LOG_FUNCTION(this);
}

void
SatFwdLinkSchedulerDefault::DoDispose()
{
    NS_LOG_FUNCTION(this);
    SatFwdLinkScheduler::DoDispose();
    m_bbFrameContainer = nullptr;
}

std::pair<Ptr<SatBbFrame>, const Time>
SatFwdLinkSchedulerDefault::GetNextFrame()
{
    NS_LOG_FUNCTION(this);

    if (m_bbFrameContainer->GetTotalDuration() < m_schedulingStartThresholdTime)
    {
        ScheduleBbFrames();
    }

    Ptr<SatBbFrame> frame = m_bbFrameContainer->GetNextFrame();
    Time frameDuration;

    if (frame != nullptr)
    {
        m_symbolsSent += static_cast<uint32_t>(
    std::ceil(frame->GetDuration().GetSeconds() * m_carrierBandwidthInHz));
    }

    // create dummy frame
    if (m_dummyFrameSendingEnabled && frame == nullptr)
    {
        frame = Create<SatBbFrame>(m_bbFrameConf->GetDefaultModCod(),
                                   SatEnums::DUMMY_FRAME,
                                   m_bbFrameConf);

        Ptr<Packet> dummyPacket = Create<Packet>(1);

        SatMacTag mTag;
        mTag.SetDestAddress(Mac48Address::GetBroadcast());
        mTag.SetSourceAddress(m_macAddress);
        dummyPacket->AddPacketTag(mTag);

        SatAddressE2ETag addressE2ETag;
        addressE2ETag.SetE2EDestAddress(Mac48Address::GetBroadcast());
        addressE2ETag.SetE2ESourceAddress(m_macAddress);
        dummyPacket->AddPacketTag(addressE2ETag);

        frame->AddPayload(dummyPacket);
        frameDuration = frame->GetDuration();
    }
    else if (frame == nullptr)
    {
        frameDuration = m_bbFrameConf->GetDummyBbFrameDuration();
    }

    if (frame != nullptr)
    {
        frameDuration = frame->GetDuration();
        frame->SetSliceId(0);
    }

    return std::make_pair(frame, frameDuration);
}

void
SatFwdLinkSchedulerDefault::ClearAllPackets()
{
    NS_LOG_FUNCTION(this);
    m_bbFrameContainer->ClearAllFrames();
}

void
SatFwdLinkSchedulerDefault::PeriodicTimerExpired()
{
    NS_LOG_FUNCTION(this);

    SendAndClearSymbolsSentStat();
    ScheduleBbFrames();

    Simulator::Schedule(m_periodicInterval,
                        &SatFwdLinkSchedulerDefault::PeriodicTimerExpired,
                        this);
}

void
SatFwdLinkSchedulerDefault::SendAndClearSymbolsSentStat()
{
    NS_LOG_FUNCTION(this);

    m_schedulingSymbolRateTrace(0, m_symbolsSent / Seconds(1).GetSeconds());
    m_symbolsSent = 0;
}

void
SatFwdLinkSchedulerDefault::ScheduleBbFrames()
{
    NS_LOG_FUNCTION(this);

    std::vector<Ptr<SatSchedulingObject>> so;
    GetSchedulingObjects(so);

    for (auto it = so.begin();
         (it != so.end()) &&
         (m_bbFrameContainer->GetTotalDuration() < m_schedulingStopThresholdTime);
         ++it)
    {
        uint32_t currentObBytes = (*it)->GetBufferedBytes();
        uint32_t currentObMinReqBytes = (*it)->GetMinTxOpportunityInBytes();

        uint32_t flowId = static_cast<uint32_t>((*it)->GetFlowId());

        SatEnums::SatModcod_t modcod =
            m_bbFrameContainer->GetModcod(flowId, GetSchedulingObjectCno(*it));

        uint32_t frameBytes = m_bbFrameContainer->GetBytesLeftInTailFrame(flowId, modcod);

        while ((m_bbFrameContainer->GetTotalDuration() < m_schedulingStopThresholdTime) &&
               (currentObBytes > 0))
        {
            if (frameBytes < currentObMinReqBytes)
            {
                frameBytes = m_bbFrameContainer->GetMaxFramePayloadInBytes(flowId, modcod) -
                             m_bbFrameConf->GetBbFrameHeaderSizeInBytes();

                if (frameBytes < currentObMinReqBytes)
                {
                    NS_FATAL_ERROR("Control package too probably too long!!!");
                }
            }

            Ptr<Packet> p = m_txOpportunityCallback(frameBytes,
                                                    (*it)->GetMacAddress(),
                                                    flowId,
                                                    currentObBytes,
                                                    currentObMinReqBytes);

            if (p)
            {
                m_bbFrameContainer->AddData(flowId, modcod, p);
                frameBytes = m_bbFrameContainer->GetBytesLeftInTailFrame(flowId, modcod);
            }
            else if (m_bbFrameContainer->GetMaxFramePayloadInBytes(flowId, modcod) !=
                     m_bbFrameContainer->GetBytesLeftInTailFrame(flowId, modcod))
            {
                frameBytes = m_bbFrameContainer->GetMaxFramePayloadInBytes(flowId, modcod);
            }
            else
            {
                NS_FATAL_ERROR("Packet does not fit in empty BB Frame. Control package too long or "
                               "fragmentation problem in user package!!!");
            }
        }

        m_bbFrameContainer->MergeBbFrames(m_carrierBandwidthInHz);
    }
}

} // namespace ns3
