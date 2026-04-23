#include "Chat.h"
#include "Player.h"
#include "PlayerbotMgr.h"
#include "PlayerbotAI.h"
#include "ChatHelper.h"
#include "ScriptMgr.h"
#include "ScriptedGossip.h"
#include "WorldPacket.h"

#include <algorithm>
#include <map>
#include <cctype>
#include <sstream>
#include <string>
#include <vector>

namespace
{
char const* const kAddonPrefix = "MBOT";
char const* const kBridgeName = "mod-multibot-bridge";
char const* const kProtocolVersion = "1";
char const kFieldSeparator = '~';

Player* FindBotByName(Player* player, std::string const& botName);
void SendAddonPacket(Player* player, ChatMsg chatType, std::string const& opcode, std::string const& payload = "");

std::string Trim(std::string const& value)
{
    size_t start = value.find_first_not_of(" \t\r\n");
    if (start == std::string::npos)
        return "";

    size_t end = value.find_last_not_of(" \t\r\n");
    return value.substr(start, end - start + 1);
}

std::string ToUpper(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) { return std::toupper(c); });
    return value;
}

std::pair<std::string, std::string> SplitOnce(std::string const& value, char separator)
{
    size_t const pos = value.find(separator);
    if (pos == std::string::npos)
        return {value, ""};

    return {value.substr(0, pos), value.substr(pos + 1)};
}

bool TryExtractBridgePayload(uint32 lang, std::string const& msg, std::string& payload)
{
    if (lang != LANG_ADDON)
        return false;

    payload = Trim(msg);
    if (payload.empty())
        return false;

    if (payload.rfind(kAddonPrefix, 0) == 0)
    {
        payload.erase(0, std::char_traits<char>::length(kAddonPrefix));
        while (!payload.empty() && (payload.front() == '	' || payload.front() == ' '))
            payload.erase(payload.begin());
    }

    return !payload.empty();
}

std::string UrlEncodeField(std::string const& value)
{
    std::ostringstream out;
    char const* const hex = "0123456789ABCDEF";

    for (unsigned char c : value)
    {
        if (c == '%' || c == '~' || c == '\r' || c == '\n')
        {
            out << '%';
            out << hex[(c >> 4) & 0x0F];
            out << hex[c & 0x0F];
        }
        else
            out << static_cast<char>(c);
    }

    return out.str();
}

struct InventorySummaryData
{
    uint32 gold = 0;
    uint32 silver = 0;
    uint32 copper = 0;
    uint32 bagUsed = 0;
    uint32 bagTotal = 16;
};

InventorySummaryData BuildInventorySummary(Player* bot)
{
    InventorySummaryData summary;
    if (!bot)
        return summary;

    uint32 const money = bot->GetMoney();
    summary.gold = money / 10000;
    summary.silver = (money % 10000) / 100;
    summary.copper = money % 100;

    uint32 used = 0;
    uint32 total = 16;

    for (uint8 slot = INVENTORY_SLOT_ITEM_START; slot < INVENTORY_SLOT_ITEM_END; ++slot)
    {
        if (bot->GetItemByPos(INVENTORY_SLOT_BAG_0, slot))
            ++used;
    }

    for (uint8 bag = INVENTORY_SLOT_BAG_START; bag < INVENTORY_SLOT_BAG_END; ++bag)
    {
        if (Bag const* const pBag = (Bag*)bot->GetItemByPos(INVENTORY_SLOT_BAG_0, bag))
        {
            ItemTemplate const* const proto = pBag->GetTemplate();
            if (proto && proto->Class == ITEM_CLASS_CONTAINER && proto->SubClass == ITEM_SUBCLASS_CONTAINER)
            {
                total += pBag->GetBagSize();
                used += pBag->GetBagSize() - pBag->GetFreeSlots();
            }
        }
    }

    summary.bagUsed = used;
    summary.bagTotal = total;
    return summary;
}

void SendInventorySnapshot(Player* requester, ChatMsg replyType, std::string const& botName, std::string const& requestToken)
{
    std::string const trimmedBotName = Trim(botName);
    Player* const bot = FindBotByName(requester, trimmedBotName);

    std::string const prefixPayload = trimmedBotName + std::string(1, kFieldSeparator) + requestToken;
    SendAddonPacket(requester, replyType, "INV_BEGIN", prefixPayload);

    if (!bot)
    {
        SendAddonPacket(requester, replyType, "INV_END", prefixPayload);
        return;
    }

    InventorySummaryData const summary = BuildInventorySummary(bot);
    std::ostringstream summaryPayload;
    summaryPayload << bot->GetName() << kFieldSeparator << requestToken << kFieldSeparator << summary.gold << kFieldSeparator
                   << summary.silver << kFieldSeparator << summary.copper << kFieldSeparator << summary.bagUsed
                   << kFieldSeparator << summary.bagTotal;
    SendAddonPacket(requester, replyType, "INV_SUMMARY", summaryPayload.str());

    PlayerbotAI* const botAI = sPlayerbotsMgr.GetPlayerbotAI(bot);
    if (botAI)
    {
        std::map<uint32, uint32> itemCounts;
        std::map<uint32, ItemTemplate const*> itemTemplates;
        std::map<uint32, bool> soulboundByEntry;

        std::vector<Item*> const items = botAI->GetInventoryItems();
        for (Item* const item : items)
        {
            if (!item)
                continue;

            ItemTemplate const* const proto = item->GetTemplate();
            if (!proto)
                continue;

            uint32 const itemId = proto->ItemId;
            itemCounts[itemId] += item->GetCount();
            itemTemplates[itemId] = proto;
            if (item->IsSoulBound())
                soulboundByEntry[itemId] = true;
        }

        for (auto const& entry : itemCounts)
        {
            ItemTemplate const* const proto = itemTemplates[entry.first];
            if (!proto)
                continue;

            std::string line = ChatHelper::FormatItem(proto, entry.second);
            if (soulboundByEntry[entry.first])
                line += " (soulbound)";

            std::string payload = bot->GetName();
            payload += kFieldSeparator;
            payload += requestToken;
            payload += kFieldSeparator;
            payload += UrlEncodeField(line);
            SendAddonPacket(requester, replyType, "INV_ITEM", payload);
        }
    }

    SendAddonPacket(requester, replyType, "INV_END", bot->GetName() + std::string(1, kFieldSeparator) + requestToken);
}

ChatMsg NormalizeReplyChatType(uint32 type)
{
    switch (type)
    {
        case CHAT_MSG_PARTY:
        case CHAT_MSG_RAID:
        case CHAT_MSG_GUILD:
        case CHAT_MSG_OFFICER:
        case CHAT_MSG_WHISPER:
        case CHAT_MSG_CHANNEL:
            return static_cast<ChatMsg>(type);
        default:
            return CHAT_MSG_WHISPER;
    }
}

void SendAddonPacket(Player* player, ChatMsg chatType, std::string const& opcode, std::string const& payload)
{
    if (!player || !player->GetSession())
        return;

    std::string wire = std::string(kAddonPrefix) + "\t" + opcode;
    if (!payload.empty())
        wire += std::string(1, kFieldSeparator) + payload;

    LOG_INFO("playerbots", "MultiBotBridge TX [{}] type={}", wire, static_cast<uint32>(chatType));

    WorldPacket data;
    ChatHandler::BuildChatPacket(data, chatType, LANG_ADDON, player, nullptr, wire.c_str());
    player->SendDirectMessage(&data);
}

uint32 GetPct(uint32 current, uint32 max)
{
    if (!max)
        return 0;

    return static_cast<uint32>((current * 100u) / max);
}

Player* FindBotByName(Player* player, std::string const& botName)
{
    PlayerbotMgr* const mgr = sPlayerbotsMgr.GetPlayerbotMgr(player);
    if (!mgr)
        return nullptr;

    std::string const wantedName = Trim(botName);
    if (wantedName.empty())
        return nullptr;

    for (PlayerBotMap::const_iterator it = mgr->GetPlayerBotsBegin(); it != mgr->GetPlayerBotsEnd(); ++it)
    {
        Player* const bot = it->second;
        if (!bot)
            continue;

        if (bot->GetName() == wantedName)
            return bot;
    }

    return nullptr;
}

std::string JoinStrategies(std::vector<std::string> const& strategies)
{
    std::ostringstream out;

    for (size_t index = 0; index < strategies.size(); ++index)
    {
        if (index)
            out << ", ";

        out << strategies[index];
    }

    return out.str();
}

std::string BuildRosterPayload(Player* player)
{
    PlayerbotMgr* const mgr = sPlayerbotsMgr.GetPlayerbotMgr(player);
    if (!mgr)
        return "";

    std::ostringstream out;
    bool first = true;

    for (PlayerBotMap::const_iterator it = mgr->GetPlayerBotsBegin(); it != mgr->GetPlayerBotsEnd(); ++it)
    {
        Player* const bot = it->second;
        if (!bot)
            continue;

        if (!first)
            out << ';';
        first = false;

        out << bot->GetName() << ',' << static_cast<uint32>(bot->getClass()) << ',' << static_cast<uint32>(bot->GetLevel())
            << ',' << static_cast<uint32>(bot->GetMapId()) << ',' << (bot->IsAlive() ? '1' : '0') << ','
            << GetPct(bot->GetHealth(), bot->GetMaxHealth()) << ',' << GetPct(bot->GetPower(POWER_MANA), bot->GetMaxPower(POWER_MANA));
    }

    return out.str();
}

std::string BuildStatePayload(Player* player, std::string const& botName)
{
    Player* const bot = FindBotByName(player, botName);
    if (!bot)
        return Trim(botName) + std::string(1, kFieldSeparator) + kFieldSeparator;

    PlayerbotAI* const botAI = sPlayerbotsMgr.GetPlayerbotAI(bot);
    if (!botAI)
        return bot->GetName() + std::string(1, kFieldSeparator) + kFieldSeparator;

    std::ostringstream out;
    out << bot->GetName() << kFieldSeparator << JoinStrategies(botAI->GetStrategies(BOT_STATE_COMBAT)) << kFieldSeparator
        << JoinStrategies(botAI->GetStrategies(BOT_STATE_NON_COMBAT));
    return out.str();
}

std::string BuildStatesPayload(Player* player)
{
    PlayerbotMgr* const mgr = sPlayerbotsMgr.GetPlayerbotMgr(player);
    if (!mgr)
        return "";

    std::ostringstream out;
    bool first = true;

    for (PlayerBotMap::const_iterator it = mgr->GetPlayerBotsBegin(); it != mgr->GetPlayerBotsEnd(); ++it)
    {
        Player* const bot = it->second;
        if (!bot)
            continue;

        PlayerbotAI* const botAI = sPlayerbotsMgr.GetPlayerbotAI(bot);
        std::string combatStrategies;
        std::string nonCombatStrategies;
        if (botAI)
        {
            combatStrategies = JoinStrategies(botAI->GetStrategies(BOT_STATE_COMBAT));
            nonCombatStrategies = JoinStrategies(botAI->GetStrategies(BOT_STATE_NON_COMBAT));
        }

        if (!first)
            out << ';';
        first = false;

        out << bot->GetName() << kFieldSeparator << combatStrategies << kFieldSeparator << nonCombatStrategies;
    }

    return out.str();
}


bool HandleBridgeOpcode(Player* player, ChatMsg replyType, std::string const& opcode, std::string const& payload)
{
    std::string const normalized = ToUpper(Trim(opcode));

    if (normalized == "HELLO")
    {
        SendAddonPacket(player, replyType, "HELLO_ACK", std::string(kProtocolVersion) + kFieldSeparator + kBridgeName);
        return true;
    }

    if (normalized == "PING")
    {
        SendAddonPacket(player, replyType, "PONG", payload);
        return true;
    }

    if (normalized == "GET")
    {
        std::pair<std::string, std::string> const request = SplitOnce(payload, kFieldSeparator);
        std::string const requestType = ToUpper(Trim(request.first));

        if (requestType == "ROSTER")
        {
            SendAddonPacket(player, replyType, "ROSTER", BuildRosterPayload(player));
            return true;
        }

        if (requestType == "STATE")
        {
            SendAddonPacket(player, replyType, "STATE", BuildStatePayload(player, request.second));
            return true;
        }

        if (requestType == "STATES")
        {
            SendAddonPacket(player, replyType, "STATES", BuildStatesPayload(player));
            return true;
        }

        if (requestType == "INVENTORY")
        {
            std::pair<std::string, std::string> const inventoryRequest = SplitOnce(request.second, kFieldSeparator);
            SendInventorySnapshot(player, replyType, inventoryRequest.first, Trim(inventoryRequest.second));
            return true;
        }

        return false;
    }

    return false;
}

class MultiBotBridgePlayerScript final : public PlayerScript
{
public:
    MultiBotBridgePlayerScript() : PlayerScript("MultiBotBridgePlayerScript") {}

    bool TryHandle(Player* player, uint32 type, uint32 lang, std::string& msg)
    {
        if (!player)
            return false;

        std::string payload;
        if (!TryExtractBridgePayload(lang, msg, payload))
            return false;

        LOG_INFO("playerbots", "MultiBotBridge RX [{}] type={}", payload, type);

        std::pair<std::string, std::string> const packet = SplitOnce(payload, kFieldSeparator);
        return HandleBridgeOpcode(player, NormalizeReplyChatType(type), packet.first, packet.second);
    }

    bool OnPlayerCanUseChat(Player* player, uint32 type, uint32 lang, std::string& msg, Player* /*receiver*/) override
    {
        return !TryHandle(player, type, lang, msg);
    }

    bool OnPlayerCanUseChat(Player* player, uint32 type, uint32 lang, std::string& msg, Group* /*group*/) override
    {
        return !TryHandle(player, type, lang, msg);
    }

    bool OnPlayerCanUseChat(Player* player, uint32 type, uint32 lang, std::string& msg, Guild* /*guild*/) override
    {
        return !TryHandle(player, type, lang, msg);
    }

    bool OnPlayerCanUseChat(Player* player, uint32 type, uint32 lang, std::string& msg, Channel* /*channel*/) override
    {
        return !TryHandle(player, type, lang, msg);
    }
};
} // namespace

void AddSC_multibot_bridge()
{
    LOG_INFO("server.loading", "mod-multibot-bridge loaded");
    new MultiBotBridgePlayerScript();
}
