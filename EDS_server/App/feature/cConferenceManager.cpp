#pragma warning(suppress : 6)

#include "cConferenceManager.h"
#include <random>

#include "utils/fnWordGenerator.h"

using namespace Sys::Conference;

[[deprecated("Используйте utils/wordGenerator.h")]]std::string cConferenceManager::fnGenInvite()//Удалить
{
    static const char* alphabet = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789";
    std::random_device rd;
    std::mt19937 rng(rd());
    std::uniform_int_distribution<int> d(0, 31);

    std::string out;
    out.reserve(8);
    for (int i = 0; i < 8; ++i) out.push_back(alphabet[d(rng)]);
    return out;
}

std::pair<int, std::string> cConferenceManager::fnCreateConference(const std::string& title)
{
    std::lock_guard<std::mutex> lg(m_mtx);

    sConference c;
    c.id = m_nextId++;
    c.title = title;
    //c.invite = fnGenInvite();
    c.invite = utils::fnWordGenerator();

    m_inviteToConf[c.invite] = c.id;
    m_confs[c.id] = c;
    return { c.id, c.invite };
}

std::optional<int> cConferenceManager::fnJoinByInvite(const std::string& invite, const std::string& peerKey)
{
    std::lock_guard<std::mutex> lg(m_mtx);

    auto it = m_inviteToConf.find(invite);
    if (it == m_inviteToConf.end()) return std::nullopt;

    int confId = it->second;
    auto& conf = m_confs[confId];

    conf.peers.insert(peerKey);
    m_peerToConf[peerKey] = confId;
    return confId;
}

void cConferenceManager::fnLeave(const std::string& peerKey)
{
    std::lock_guard<std::mutex> lg(m_mtx);

    auto it = m_peerToConf.find(peerKey);
    if (it == m_peerToConf.end()) return;

    int confId = it->second;
    m_peerToConf.erase(it);

    auto cit = m_confs.find(confId);
    if (cit == m_confs.end()) return;

    cit->second.peers.erase(peerKey);

    // optional: удалять пустые конференции
    if (cit->second.peers.empty()) {
        m_inviteToConf.erase(cit->second.invite);
        m_confs.erase(cit);
    }
}

std::unordered_set<std::string> cConferenceManager::fnGetPeersInSameConf(const std::string& peerKey)
{
    std::lock_guard<std::mutex> lg(m_mtx);

    std::unordered_set<std::string> out;
    auto it = m_peerToConf.find(peerKey);
    if (it == m_peerToConf.end()) return out;

    int confId = it->second;
    auto cit = m_confs.find(confId);
    if (cit == m_confs.end()) return out;

    out = cit->second.peers;
    return out;
}
