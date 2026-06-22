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

void NasSm::sendCiotUserData(int psi)
{
    nas::UlNasTransport m;

    m.payloadContainerType.payloadContainerType =
        nas::EPayloadContainerType::CIOT_USER_DATA_CONTAINER;

    // constexpr std::size_t targetPayloadBytes = 1 * 1024;

    std::string payload = "CIOT_Lorem_Ipsum_TEST:Lorem ipsum dolor sit amet, consectetur adipiscing elit. Ut pharetra mattis fermentum. Aenean ac placerat odio. Curabitur sit amet fermentum nisl. Donec porttitor porta nisi, id auctor sem convallis ut. Integer aliquet nec mauris in sagittis. Fusce nec eros ut justo consequat sodales. Donec euismod a purus non congue. Fusce augue orci, fermentum nec facilisis eu, elementum et felis. Duis a nisi eu velit cursus facilisis. Praesent at vulputate mauris. Aliquam in diam non sapien pellentesque pulvinar. Donec molestie lectus sit amet ornare ultrices. Pellentesque ac est commodo, feugiat lectus nec, vestibulum sapien. Sed nisl dolor, accumsan a vestibulum ut, eleifend nec libero. In scelerisque laoreet tincidunt. Lorem ipsum dolor sit amet, consectetur adipiscing elit. Cras tincidunt rutrum sapien ac hendrerit. Nullam volutpat enim non semper consectetur. In ac massa vel tortor lacinia tristique. Nullam rhoncus nulla pretium, tincidunt risus quis, pellentesque nibh. Phasellus lacinia vehicula orci sit amet feugiat. Praesent sit amet odio sed nisi pulvinar finibus. Proin blandit malesuada suscipit. Aliquam imperdiet tristique magna id iaculis. Curabitur magna justo, pulvinar in justo eget, ultrices tincidunt urna. Curabitur gravida nunc in aliquam ultrices. Proin tristique dolor sagittis risus sodales, eu iaculis ipsum interdum. Sed id sem auctor, ornare nibh sed, viverra dui. Fusce sed lectus dictum, tincidunt elit a, volutpat urna. Donec non massa lobortis, vulputate tortor vitae, iaculis quam. Quisque ut blandit felis, varius condimentum nunc. Vivamus auctor ligula quis nisl accumsan vulputate. Nam ac tortor nunc. Praesent maximus libero nulla, ac cursus nisi euismod condimentum. Cras at sem non urna accumsan mattis vel sed ligula. Ut quam leo, volutpat ut est vitae, molestie blandit nisi. Etiam elementum auctor dui ac elementum. Aliquam erat volutpat. Aenean vel ullamcorper nisi. Suspendisse pulvinar erat in augue bibendum vulputate. Phasellus facilisis augue lacus, eu efficitur mi faucibus at. Vivamus tristique lacinia nisi. Morbi eleifend a velit vel pretium. Phasellus convallis, lacus quis vehicula finibus, lorem justo egestas tortor, eget dapibus est augue scelerisque ligula. Morbi ac pellentesque tellus. Integer ultrices luctus lectus a aliquet. Suspendisse nec ipsum posuere, iaculis sem ut, luctus neque. Quisque ut accumsan mi, nec convallis augue. Quisque ut cursus augue. Sed semper, nunc et tristique venenatis, neque felis vehicula mauris, vel molestie ante odio interdum orci. Donec malesuada volutpat enim, sed tincidunt dolor viverra eu. Mauris bibendum commodo dui blandit fringilla. Morbi semper tellus id pharetra viverra. Pellentesque consequat elit mi, ut scelerisque dolor laoreet a";
    // payload.resize(targetPayloadBytes, 'A');

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
