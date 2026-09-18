//
// This file is a part of UERANSIM project.
// Copyright (c) 2023 ALİ GÜNGÖR.
//
// https://github.com/aligungr/UERANSIM/
// See README, LICENSE, and CONTRIBUTING files for licensing details.
//

#include "sm.hpp"

#include <stdexcept>
#include <string>
#include <lib/nas/utils.hpp>
#include <ue/nas/mm/mm.hpp>


#include <cstdlib>
#include <atomic>
#include <chrono>

namespace nr::ue
{

static ENasTransportHint MapMsgTypeToHint(nas::EMessageType msgType)
{
    switch (msgType)
    {
    case nas::EMessageType::PDU_SESSION_ESTABLISHMENT_REQUEST:
        return ENasTransportHint::PDU_SESSION_ESTABLISHMENT_REQUEST;
    case nas::EMessageType::PDU_SESSION_ESTABLISHMENT_ACCEPT:
        return ENasTransportHint::PDU_SESSION_ESTABLISHMENT_ACCEPT;
    case nas::EMessageType::PDU_SESSION_ESTABLISHMENT_REJECT:
        return ENasTransportHint::PDU_SESSION_ESTABLISHMENT_REJECT;
    case nas::EMessageType::PDU_SESSION_AUTHENTICATION_COMMAND:
        return ENasTransportHint::PDU_SESSION_AUTHENTICATION_COMMAND;
    case nas::EMessageType::PDU_SESSION_AUTHENTICATION_COMPLETE:
        return ENasTransportHint::PDU_SESSION_AUTHENTICATION_COMPLETE;
    case nas::EMessageType::PDU_SESSION_AUTHENTICATION_RESULT:
        return ENasTransportHint::PDU_SESSION_AUTHENTICATION_RESULT;
    case nas::EMessageType::PDU_SESSION_MODIFICATION_REQUEST:
        return ENasTransportHint::PDU_SESSION_MODIFICATION_REQUEST;
    case nas::EMessageType::PDU_SESSION_MODIFICATION_REJECT:
        return ENasTransportHint::PDU_SESSION_MODIFICATION_REJECT;
    case nas::EMessageType::PDU_SESSION_MODIFICATION_COMMAND:
        return ENasTransportHint::PDU_SESSION_MODIFICATION_COMMAND;
    case nas::EMessageType::PDU_SESSION_MODIFICATION_COMPLETE:
        return ENasTransportHint::PDU_SESSION_MODIFICATION_COMPLETE;
    case nas::EMessageType::PDU_SESSION_MODIFICATION_COMMAND_REJECT:
        return ENasTransportHint::PDU_SESSION_MODIFICATION_COMMAND_REJECT;
    case nas::EMessageType::PDU_SESSION_RELEASE_REQUEST:
        return ENasTransportHint::PDU_SESSION_RELEASE_REQUEST;
    case nas::EMessageType::PDU_SESSION_RELEASE_REJECT:
        return ENasTransportHint::PDU_SESSION_RELEASE_REJECT;
    case nas::EMessageType::PDU_SESSION_RELEASE_COMMAND:
        return ENasTransportHint::PDU_SESSION_RELEASE_COMMAND;
    case nas::EMessageType::PDU_SESSION_RELEASE_COMPLETE:
        return ENasTransportHint::PDU_SESSION_RELEASE_COMPLETE;
    case nas::EMessageType::FIVEG_SM_STATUS:
        return ENasTransportHint::FIVEG_SM_STATUS;
    default:
        throw std::runtime_error("failure in MapMsgTypeToHint");
    }
}

void NasSm::sendSmMessage(int psi, const nas::SmMessage &msg)
{
    auto &session = m_pduSessions[psi];

    nas::UlNasTransport m;
    m.payloadContainerType.payloadContainerType = nas::EPayloadContainerType::N1_SM_INFORMATION;
    nas::EncodeNasMessage(msg, m.payloadContainer.data);
    m.pduSessionId = nas::IEPduSessionIdentity2{};
    m.pduSessionId->value = psi;

    if (msg.messageType == nas::EMessageType::PDU_SESSION_ESTABLISHMENT_REQUEST ||
        msg.messageType == nas::EMessageType::PDU_SESSION_MODIFICATION_REQUEST)
    {
        m.requestType = nas::IERequestType{};
        m.requestType->requestType =
            session->isEmergency ? nas::ERequestType::INITIAL_EMERGENCY_REQUEST : nas::ERequestType::INITIAL_REQUEST;

        if (!session->isEmergency)
        {
            if (session->sNssai.has_value())
                m.sNssai = nas::utils::SNssaiFrom(*session->sNssai);

            if (session->apn.has_value())
                m.dnn = nas::utils::DnnFromApn(*session->apn);
        }
    }

    m_mm->deliverUlTransport(m, MapMsgTypeToHint(msg.messageType));
}

void NasSm::receiveSmMessage(const nas::SmMessage &msg)
{
    switch (msg.messageType)
    {
    case nas::EMessageType::PDU_SESSION_ESTABLISHMENT_ACCEPT:
        receiveEstablishmentAccept((const nas::PduSessionEstablishmentAccept &)msg);
        break;
    case nas::EMessageType::PDU_SESSION_ESTABLISHMENT_REJECT:
        receiveEstablishmentReject((const nas::PduSessionEstablishmentReject &)msg);
        break;
    case nas::EMessageType::PDU_SESSION_RELEASE_REJECT:
        receiveReleaseReject((const nas::PduSessionReleaseReject &)msg);
        break;
    case nas::EMessageType::PDU_SESSION_RELEASE_COMMAND:
        receiveReleaseCommand((const nas::PduSessionReleaseCommand &)msg);
        break;
    case nas::EMessageType::FIVEG_SM_STATUS:
        receiveSmStatus((const nas::FiveGSmStatus &)msg);
        break;
    default:
        m_logger->err("Unhandled NAS SM message received: %d", (int)msg.messageType);
        break;
    }
}

void NasSm::receiveSmStatus(const nas::FiveGSmStatus &msg)
{
    m_logger->err("SM Status received with cause [%s]", nas::utils::EnumToString(msg.smCause.value));

    if (msg.smCause.value == nas::ESmCause::INVALID_PTI_VALUE)
    {
        // "The UE shall abort any ongoing 5GSM procedure related to the received PTI value and stop any related timer."
        abortProcedureByPti(msg.pti);
    }
    else if (msg.smCause.value == nas::ESmCause::MESSAGE_TYPE_NON_EXISTENT_OR_NOT_IMPLEMENTED)
    {
        // "The UE shall abort any ongoing 5GSM procedure related to the PTI or PDU session Id and stop any related
        // timer."
        abortProcedureByPtiOrPsi(msg.pti, msg.pduSessionId);
    }
}

void NasSm::sendSmCause(const nas::ESmCause &cause, int pti, int psi)
{
    m_logger->warn("Sending SM Cause[%s] for PSI[%d]", nas::utils::EnumToString(cause), psi);

    nas::FiveGSmStatus smStatus;
    smStatus.smCause.value = cause;
    smStatus.pti = pti;
    smStatus.pduSessionId = psi;

    nas::UlNasTransport ulTransport;
    ulTransport.payloadContainerType.payloadContainerType = nas::EPayloadContainerType::N1_SM_INFORMATION;
    nas::EncodeNasMessage(smStatus, ulTransport.payloadContainer.data);
    ulTransport.pduSessionId = nas::IEPduSessionIdentity2{};
    ulTransport.pduSessionId->value = psi;

    m_mm->deliverUlTransport(ulTransport, ENasTransportHint::FIVEG_SM_STATUS);
}



static std::atomic<int> g_ciotSeq{0};

static int getCiotEnvInt(const char *name, int defaultValue)
{
    const char *value = std::getenv(name);

    if (value == nullptr)
        return defaultValue;

    try
    {
        return std::stoi(value);
    }
    catch (...)
    {
        return defaultValue;
    }
}

static long long ciotNowUnixNs()
{
    auto now = std::chrono::system_clock::now();

    auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
        now.time_since_epoch()
    ).count();

    return static_cast<long long>(ns);
}



void NasSm::sendCiotUserData(int psi)
{
    const int minPayloadBytes =
        getCiotEnvInt("CIOT_MIN_BYTES", 1024);

    const int maxPayloadBytes =
        getCiotEnvInt("CIOT_MAX_BYTES", 20 * 1024);

    const int stepPayloadBytes =
        getCiotEnvInt("CIOT_STEP_BYTES", 1024);

    const int delayBetweenMessagesMs =
        getCiotEnvInt("CIOT_DELAY_MS", 500);

    if (minPayloadBytes <= 0 ||
        maxPayloadBytes < minPayloadBytes ||
        stepPayloadBytes <= 0 ||
        delayBetweenMessagesMs < 0)
    {
        m_logger->err("Invalid CIoT experiment configuration");
        return;
    }

    for (int targetPayloadBytes = minPayloadBytes;
         targetPayloadBytes <= maxPayloadBytes;
         targetPayloadBytes += stepPayloadBytes)
    {
        nas::UlNasTransport m;

        m.payloadContainerType.payloadContainerType =
            nas::EPayloadContainerType::CIOT_USER_DATA_CONTAINER;

        int seq = g_ciotSeq.fetch_add(1);
        long long txNs = ciotNowUnixNs();

        std::string header =
            "CIOT_TEST;"
            "seq=" + std::to_string(seq) + ";" +
            "len=" + std::to_string(targetPayloadBytes) + ";" +
            "tx_ns=" + std::to_string(txNs) + ";" +
            "data=";

        if (header.size() > static_cast<std::size_t>(targetPayloadBytes))
        {
            m_logger->warn(
                "CIoT header size[%d] exceeds requested payload size[%d]",
                (int)header.size(),
                targetPayloadBytes
            );

            continue;
        }

        std::string payload = header;

        payload.append(
            targetPayloadBytes - payload.size(),
            'A'
        );

        m.payloadContainer.data =
            OctetString::FromAscii(payload);

        m.pduSessionId = nas::IEPduSessionIdentity2{};
        m.pduSessionId->value = psi;

        m_logger->info(
            "Sending CIoT test payload seq[%d], bytes[%d], tx_ns[%lld]",
            seq,
            (int)payload.size(),
            txNs
        );

        m_mm->deliverUlTransport(
            m,
            ENasTransportHint::FIVEG_SM_STATUS
        );

        std::this_thread::sleep_for(
            std::chrono::milliseconds(delayBetweenMessagesMs)
        );
    }
} 



/*
void NasSm::sendCiotUserData(int psi)
{
    nas::UlNasTransport m;

    m.payloadContainerType.payloadContainerType =
        nas::EPayloadContainerType::CIOT_USER_DATA_CONTAINER;

    constexpr std::size_t targetPayloadBytes = 4 * 1024;

    std::string userData;
    
    
    userData.resize(targetPayloadBytes, 'A');
    long long txNs = ciotNowUnixNs();
    
    std::string payload =
    "CIOT_TEST_1KB;"
    "tx_ns=" + std::to_string(txNs) + ";" +
    "data=" + userData;

    m.payloadContainer.data = OctetString::FromAscii(payload);

    m.pduSessionId = nas::IEPduSessionIdentity2{};
    m.pduSessionId->value = psi;

    m_logger->info(
        "Sending CIoT user data over UL NAS TRANSPORT PSI[%d], bytes[%d]",
        psi,
        (int)payload.size()
    );

    m_mm->deliverUlTransport(m, ENasTransportHint::FIVEG_SM_STATUS);
}
*/
void NasSm::receiveForwardingFailure(const nas::SmMessage &msg, nas::EMmCause cause,
                                     const std::optional<nas::IEGprsTimer3> &backoffTimer)
{

    m_logger->err("SM forwarding failure for message type[%d] with cause[%s]", static_cast<int>(msg.messageType),
                  nas::utils::EnumToString(cause));

    if (!checkPtiAndPsi(msg))
        return;

    abortProcedureByPti(msg.pti);
}

} // namespace nr::ue
