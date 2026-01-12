#include "cRtcPeer.h"
#include "../util/cLogger.h"
#include <cstring>

using namespace Sys::Rtc;

cRtcPeer::cRtcPeer() {
    static std::once_flag once;
    std::call_once(once, []() {
        rtc::InitLogger(rtc::LogLevel::Info);
        });

    rtc::Configuration cfg;
    cfg.iceServers.emplace_back("stun:stun.l.google.com:19302");

    m_pc = std::make_shared<rtc::PeerConnection>(cfg);

    m_pc->onStateChange([this](rtc::PeerConnection::State s) {
        if (m_onState) m_onState(s);
        });

    m_pc->onLocalDescription([this](rtc::Description const& desc) {
        // Это главный путь: сюда прилетает ANSWER после setLocalDescription()
        if (m_onLocalDesc) m_onLocalDesc(desc);
        });

    m_pc->onLocalCandidate([this](rtc::Candidate const& cand) {
        if (m_onLocalCand) m_onLocalCand(cand);
        });

    // Если клиент создаёт DC первым — принимаем
    m_pc->onDataChannel([this](std::shared_ptr<rtc::DataChannel> dc) {
        fnBindDataChannel(std::move(dc));
        });

    // Сервер может создать DC тоже, но делаем это не всегда сразу,
    // чтобы не получить дубликаты — "ensure once"
    fnEnsureDataChannel();
}

cRtcPeer::~cRtcPeer() {
    fnClose();
}

void cRtcPeer::fnEnsureDataChannel() {
    std::lock_guard<std::mutex> lg(m_mtx);
    if (m_closed || m_dcEnsured) return;
    m_dcEnsured = true;

    try {
        auto dc = m_pc->createDataChannel("audio");
        fnBindDataChannel(std::move(dc));
    }
    catch (...) {
        // не критично — если клиент создаст, мы его примем
    }
}

void cRtcPeer::fnBindDataChannel(std::shared_ptr<rtc::DataChannel> dc) {
    std::lock_guard<std::mutex> lg(m_mtx);
    if (m_closed) return;

    // если уже есть открытый/назначенный — оставим первый
    if (m_dc) return;
    m_dc = std::move(dc);

    m_dc->onMessage([this](rtc::message_variant msg) {
        if (!m_onBinary) return;
        if (std::holds_alternative<rtc::binary>(msg)) {
            const auto& b = std::get<rtc::binary>(msg);
            std::vector<uint8_t> out;
            out.resize(b.size());
            if (!out.empty()) std::memcpy(out.data(), b.data(), b.size());
            m_onBinary(out);
        }
        // текст на сервере не нужен — игнор
        });
}

void cRtcPeer::fnApplyRemoteOffer(const std::string& sdpOffer) {
    std::lock_guard<std::mutex> lg(m_mtx);
    if (m_closed || !m_pc) return;

    // Remote offer
    rtc::Description remoteDesc(sdpOffer, "offer");
    m_pc->setRemoteDescription(remoteDesc);

    // Async: libdatachannel сгенерит answer и дернет onLocalDescription
    m_pc->setLocalDescription();
}

void cRtcPeer::fnApplyRemoteIce(const std::string& mid, const std::string& cand) {
    std::lock_guard<std::mutex> lg(m_mtx);
    if (m_closed || !m_pc) return;

    rtc::Candidate c(cand, mid);
    m_pc->addRemoteCandidate(c);
}

void cRtcPeer::fnSendBinary(const std::vector<uint8_t>& data) {
    if (data.empty()) return;

    std::shared_ptr<rtc::DataChannel> dc;
    {
        std::lock_guard<std::mutex> lg(m_mtx);
        if (m_closed || !m_dc || !m_dc->isOpen()) return;
        dc = m_dc;
    }

    rtc::binary b;
    b.resize(data.size());
    std::memcpy(b.data(), data.data(), data.size());
    dc->send(b);
}

bool cRtcPeer::fnIsReady() const {
    std::lock_guard<std::mutex> lg(m_mtx);
    return !m_closed && m_dc && m_dc->isOpen();
}

void cRtcPeer::fnClose() {
    std::lock_guard<std::mutex> lg(m_mtx);
    if (m_closed) return;
    m_closed = true;

    try { if (m_dc) m_dc->close(); }
    catch (...) {}
    try { if (m_pc) m_pc->close(); }
    catch (...) {}

    m_dc.reset();
    m_pc.reset();
}
