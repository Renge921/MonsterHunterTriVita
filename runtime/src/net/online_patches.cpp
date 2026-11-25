// The MH3SP patch (MH3SP.xml, the community server's Riivolution patch for
// RMHP08/RMHE08), applied to the static recompilation: its data patches are
// written into guest memory after the DOL's data sections are set up, and its
// instruction patch is part of the native port of the function it changes.
//
// - The SSL root certificate the game pins for its Capcom server (passed to
//   SSLSetRootCA) becomes the MH3SP CA.
// - The Capcom server (mmh-t1-opn04.mmh-service.capcom.co.jp) becomes
//   opn.mh3sp.com; the Nintendo account server (naswii.nintendowifi.net,
//   HTTPS) becomes http://nas.mh3sp.com; GameSpy's availability check and NAT
//   negotiation servers become available/natneg1-3.mh3sp.com. Another
//   server's domain (the launcher's OnlineServerSetting) takes mh3sp.com's
//   place.
// - NNBeginNegotiationWithSocket uses those NAT negotiation host names as
//   they are instead of prefixing the game name (the sprintf patch).
#include "online.h"

#include "abi_bridge.h"
#include "fatal.h"
#include "guest_call.h"
#include "launcher_cheats.h"
#include "memory.h"
#include "net_ctrl.h"
#include "net_host.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <optional>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace {
// mh3sp/ca.der from MH3SP-Riivolution.zip: the MH3SP CA, which signs the
// community server's certificate (same subject and size as Nintendo's).
constexpr uint8_t kMh3spCa[] = {
#include "mh3sp_ca.inc"
};

static_assert(sizeof(kMh3spCa) == 924);

// The pinned CA in the DOL: Nintendo Class 2 CA, 924 bytes of DER.
constexpr uint32_t kPinnedCaPal = 0x80572840;
constexpr uint32_t kPinnedCaJpn = 0x8063DDC0;
constexpr std::string_view kPinnedCaPrefix{"\x30\x82\x03\x98\x30\x82\x03\x01\xA0\x03\x02\x01\x02\x02\x01", 15};

struct StringPatch {
    uint32_t address;
    std::string_view original; // what must be there, without the terminator
    const char* prefix;        // the replacement: prefix, domain, suffix
    const char* suffix;
    uint32_t length; // bytes the patch writes (the original's slot)
};

// RMHP08 (European) addresses of the strings MH3SP.xml searches for.
constexpr StringPatch kStringsPal[]{
    {0x80601200, "mmh-t1-opn04.mmh-service.capcom.co.jp", "opn.", "", 38},
    {0x80630920, "https://naswii.nintendowifi.net/ac", "http://nas.", "/ac", 34},
    {0x806309A0, "https://naswii.nintendowifi.net/pr", "http://nas.", "/pr", 34},
    {0x80631118, "%s.available.gs.nintendowifi.net", "available.", "", 32},
    {0x806311A8, "natneg1.gs.nintendowifi.net", "natneg1.", "", 28},
    {0x806311C4, "natneg2.gs.nintendowifi.net", "natneg2.", "", 28},
    {0x806311E0, "natneg3.gs.nintendowifi.net", "natneg3.", "", 28},
};

// RMHJ08 (Japanese) addresses of the strings.
constexpr StringPatch kStringsJpn[]{
    {0x806421FC, "mmh-t1-opn02.mmh-service.capcom.co.jp", "opn.", "", 38},
    {0x80729C38, "https://naswii.nintendowifi.net/ac", "http://nas.", "/ac", 34},
    {0x80729CB8, "https://naswii.nintendowifi.net/pr", "http://nas.", "/pr", 34},
    {0x8072A4C0, "%s.available.gs.nintendowifi.net", "available.", "", 32},
    {0x8072CE50, "natneg1.gs.nintendowifi.net", "natneg1.", "", 28},
    {0x8072CE6C, "natneg2.gs.nintendowifi.net", "natneg2.", "", 28},
    {0x8072CE88, "natneg3.gs.nintendowifi.net", "natneg3.", "", 28},
};

// The longest domain every slot holds: natneg1.<domain> in 27 characters.
constexpr size_t kMaxDomain = 19;

std::optional<std::string> serverSetting;
std::optional<bool> skipTlsSetting;

// A server setting taken apart: the domain to patch in, the address that
// answers for it (0: DNS does) and the account server's port (0: 80).
struct Server {
    std::string domain;
    uint32_t ip = 0;
    uint16_t nasPort = 0;
    std::string error;
};

Server ParseServer(std::string text) {
    Server server;
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return char(std::tolower(c)); });

    // A URL's host: no scheme, no path.
    for (const std::string_view scheme : {"http://", "https://"})
        if (text.starts_with(scheme))
            text.erase(0, scheme.size());

    text = text.substr(0, text.find('/'));

    if (const auto colon = text.find(':'); colon != std::string::npos) {
        const std::string port = text.substr(colon + 1);
        text.resize(colon);
        char* end = nullptr;
        const unsigned long value = std::strtoul(port.c_str(), &end, 10);

        if (port.empty() || *end || value == 0 || value > 65535) {
            server.error = "The port after the colon must be a number from 1 to 65535.";
            return server;
        }

        server.nasPort = uint16_t(value);
    }

    if (text.empty()) {
        server.error = "Enter a server: a domain such as mh3sp.com, or an IP address.";
        return server;
    }

    // An address serves every MH3SP host name.
    if (NetHost::ParseIpv4(text, &server.ip)) {
        server.domain = kDefaultOnlineServer;
        return server;
    }

    const bool valid = text.find('.') != std::string::npos && text.front() != '.' && text.back() != '.' &&
                       std::all_of(text.begin(), text.end(), [](unsigned char c) { return std::isalnum(c) || c == '.' || c == '-'; });

    if (!valid)
        server.error = "\"" + text + "\" is not a domain (such as mh3sp.com) or an IPv4 address.";
    else if (text.size() > kMaxDomain)
        server.error = "The game's server names hold a domain of at most " + std::to_string(kMaxDomain) + " characters.";
    else
        server.domain = text;

    return server;
}

bool Matches(uint32_t address, std::string_view bytes) {
    return std::memcmp(Memory::GetPointer(address, uint32_t(bytes.size())), bytes.data(), bytes.size()) == 0;
}

// The CA to install: MH3_ONLINE_CA (a DER file, for a local test server) or
// the MH3SP CA.
std::vector<uint8_t> RootCa() {
    if (const char* path = std::getenv("MH3_ONLINE_CA"); path && *path) {
        std::ifstream file(path, std::ios::binary);
        std::vector<uint8_t> der((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());

        if (der.empty() || der.size() > sizeof(kMh3spCa))
            MH3_THROW(std::runtime_error(std::string("MH3_ONLINE_CA: no DER certificate of at most 924 bytes in ") + path));

        return der;
    }

    return {std::begin(kMh3spCa), std::end(kMh3spCa)};
}

// The last server a TLS session went on with was a community one.
std::atomic<bool> community{false};

// Online play was refused while cheats are on (AdmitOnline).
std::atomic<bool> refused{false};

constexpr uint32_t kOnlineModePal = 0x8065AA84; // u8, GetOnlineMode (0x800CF26C): 2 the City, online
constexpr uint32_t kOnlineModeJpn = 0x80768ACC; // u8, GetOnlineMode (0x802BC6BC): 2 the City, online

inline uint32_t OnlineModeAddress() {
    return Matches(kPinnedCaJpn, kPinnedCaPrefix) ? kOnlineModeJpn : kOnlineModePal;
}

constexpr const char* kRules[]{"Do not harass other players."};
constexpr const char* kRefused[]{"Cheats are not allowed online,", "please don't harass other players."};
} // namespace

bool AdmitOnline() {
    if (!LauncherCheats::EnabledCount())
        return true;

    if (!refused.exchange(true))
        std::fprintf(stderr, "MH3 net: online play refused while cheats are on (%u)\n", unsigned(LauncherCheats::EnabledCount()));

    return false;
}

void NoteTlsServer(NetHost::Tls& tls) {
    const bool signed_by_ca = tls.PeerSignedBy(kMh3spCa, sizeof(kMh3spCa));

    if (signed_by_ca && !community)
        std::fprintf(stderr, "MH3 net: a community server (its certificate signed by the MH3SP CA)\n");

    community = signed_by_ca;
}

std::span<const char* const> OnlineNotice(bool loading) {
    if (loading)
        return community && Memory::Read8(OnlineModeAddress()) == 2 ? std::span<const char* const>(kRules) : std::span<const char* const>();

    // Over the online screens' message window: the connection error the
    // refusal leads to. Their work pointer and window outlive net_data.rso:
    // after the error the Mode Menu came back with the lines still drawn.
    if (!refused || !RecompMod::RsoLinked("net_data.rso"))
        return {};

    const uint32_t work = Memory::Read32(NetCtrl::kWorkPointer);

    if (work && Memory::Read32(work + NetCtrl::kDialog))
        return kRefused;

    return {};
}

std::string OnlineServerSetting() {
    if (serverSetting)
        return *serverSetting;

    // Read at first use, after the launcher set it.
    const char* value = std::getenv("MH3_ONLINE_SERVER");
    return value && *value ? value : kDefaultOnlineServer;
}

void SetOnlineServerSetting(std::string server) {
    serverSetting = std::move(server);
}

std::string CheckOnlineServer(const std::string& server) {
    return ParseServer(server).error;
}

bool SkipTlsSetting() {
    if (skipTlsSetting)
        return *skipTlsSetting;

    const char* value = std::getenv("MH3_ONLINE_SKIP_TLS");
    return value && *value && value[0] != '0';
}

void SetSkipTlsSetting(bool skip) {
    skipTlsSetting = skip;
}

void ApplyOnlinePatches() {
    const Server server = ParseServer(OnlineServerSetting());

    if (!server.error.empty())
        MH3_THROW(std::runtime_error("Online server " + OnlineServerSetting() + ": " + server.error));

    if (server.ip)
        NetHost::AddHostOverride("*." + server.domain, server.ip);

    if (server.nasPort)
        NetHost::AddPortOverride(80, server.nasPort);

    uint32_t pinnedCa = 0;
    std::span<const StringPatch> strings;

    if (Matches(kPinnedCaJpn, kPinnedCaPrefix)) {
        pinnedCa = kPinnedCaJpn;
        strings = kStringsJpn;
    } else if (Matches(kPinnedCaPal, kPinnedCaPrefix)) {
        pinnedCa = kPinnedCaPal;
        strings = kStringsPal;
    } else {
        MH3_THROW(std::runtime_error("Online patch: the pinned CA is not where RMHP08 or RMHJ08 has it"));
    }

    // The certificate's slot keeps its size; a shorter one is padded with zeros.
    const auto ca = RootCa();
    std::memset(Memory::GetPointer(pinnedCa, sizeof(kMh3spCa)), 0, sizeof(kMh3spCa));
    std::memcpy(Memory::GetPointer(pinnedCa, uint32_t(ca.size())), ca.data(), ca.size());

    for (const auto& patch : strings) {
        if (!Matches(patch.address, patch.original))
            MH3_THROW(std::runtime_error("Online patch: unexpected string at " + std::to_string(patch.address)));

        const std::string replacement = patch.prefix + server.domain + patch.suffix;
        auto* slot = Memory::GetPointer(patch.address, patch.length);
        std::memset(slot, 0, patch.length);
        std::memcpy(slot, replacement.data(), replacement.size());
    }
}

namespace {
// GameSpy NAT negotiation (natneg.c) globals, r13-relative.
enum : int32_t {
    kAvailability = -13832, // __GSIACResult: 1 when the backend reported available
    kNegotiators = -13784,  // DArray negotiateList
    kMatchup1Ip = -13780,   // resolved server addresses, 0 until resolved
    kMatchup2Ip = -13776,
    kMatchup3Ip = -13772,
    kMatchup3Hostname = -13752, // override host names (char*), null for the defaults
    kMatchup2Hostname = -13748,
    kMatchup1Hostname = -13744,
};

// The default host names, 28 bytes apart: natneg1, natneg2, natneg3.
constexpr uint32_t kDefaultHostnames = 0x806311A8;

// struct _NATNegotiator (0x54 bytes).
enum : uint32_t {
    kNegotiateSocket = 0x00,
    kGameSocket = 0x04,
    kCookie = 0x08,
    kClientIndex = 0x0C,
    kRetryCount = 0x24,
    kMaxRetryCount = 0x28,
    kGuessedIp = 0x30,
    kGuessedPort = 0x34,       // u16
    kGotRemoteData = 0x36,     // u8
    kSendGotRemoteData = 0x37, // u8
    kProgressCallback = 0x38,
    kCompletedCallback = 0x3C,
    kUserData = 0x40,
    kResult = 0x44,
    kNegotiatorBytes = 0x54,
};

enum : uint32_t {
    Memset = 0x80004350,
    ArrayNew = 0x8050B170,
    ArrayLength = 0x8050B2B0,
    ArrayNth = 0x8050B2C0,
    ArrayAppend = 0x8050B2F0,
    ArrayDeleteAt = 0x8050B5A0,
    Socket = 0x8050BF00,
    InetAddr = 0x8050C140,
    GetHostByName = 0x8050C2C0,
    SendInitPackets = 0x80513510,
};

enum NegotiateError : uint32_t {
    kNoError = 0,
    kAllocError = 1,
    kSocketError = 2,
    kDnsError = 3
};

constexpr uint32_t kNoResult = 5; // nr_noresult
} // namespace

// NNBeginNegotiationWithSocket(gamesocket, cookie, clientindex,
// progresscallback, completedcallback, userdata), 0x80513940, decompiled with
// Ghidra. ResolveServers is inlined: each server not resolved yet is looked up
// by its override host name or, with the MH3SP patch, its default host name
// as it is (the original used "<gamename>.<default>").
namespace {
uint32_t BeginNegotiation(CpuContext* cpu) {
    GuestFrame frame(cpu, 0x140);
    GuestCall call{cpu};
    const uint32_t sda = frame.Arg(13);
    auto global = [sda](int32_t offset) {
        return sda + uint32_t(offset);
    };

    const bool jpn = Matches(kPinnedCaJpn, kPinnedCaPrefix);

    const int32_t availOffset = jpn ? -13824 : -13832;
    const int32_t negListOffset = jpn ? -13712 : -13784;
    const int32_t ips[3]{jpn ? -13708 : -13780, jpn ? -13704 : -13776, jpn ? -13700 : -13772};
    const int32_t overrides[3]{jpn ? -13672 : -13744, jpn ? -13676 : -13748, jpn ? -13680 : -13752};
    const uint32_t defaultHostnames = jpn ? 0x8072CE50 : 0x806311A8;

    const uint32_t fnMemset = jpn ? 0x8000443c : 0x80004350;
    const uint32_t fnArrayNew = jpn ? 0x805e49c4 : 0x8050B170;
    const uint32_t fnArrayLength = jpn ? 0x805e4af4 : 0x8050B2B0;
    const uint32_t fnArrayNth = jpn ? 0x805e4afc : 0x8050B2C0;
    const uint32_t fnArrayAppend = jpn ? 0x805e4b2c : 0x8050B2F0;
    const uint32_t fnArrayDeleteAt = jpn ? 0x805e4dd0 : 0x8050B5A0;
    const uint32_t fnSocket = jpn ? 0x805e67d8 : 0x8050BF00;
    const uint32_t fnInetAddr = jpn ? 0x805e6afc : 0x8050C140;
    const uint32_t fnGetHostByName = jpn ? 0x805e6c64 : 0x8050C2C0;
    const uint32_t fnSendInitPackets = jpn ? 0x806032e8 : 0x80513510;

    if (Memory::Read32(global(availOffset)) != 1)
        return kSocketError;

    for (int server = 0; server < 3; ++server) {
        if (Memory::Read32(global(ips[server])))
            continue;

        uint32_t name = Memory::Read32(global(overrides[server]));

        if (!name)
            name = defaultHostnames + server * 28;

        // NameToIp: a dotted address, else a DNS lookup's first address.
        uint32_t ip = call(fnInetAddr, {name});

        if (ip == 0xFFFFFFFF) {
            const uint32_t host = call(fnGetHostByName, {name});
            ip = host ? Memory::Read32(Memory::Read32(Memory::Read32(host + 12))) : 0;
        }

        Memory::Write32(global(ips[server]), ip);
    }

    if (!Memory::Read32(global(ips[0])) || !Memory::Read32(global(ips[1])) || !Memory::Read32(global(ips[2])))
        return kDnsError;

    // AddNegotiator: a zeroed negotiator appended to the list.
    const uint32_t blank = frame[200];
    call(fnMemset, {blank, 0, kNegotiatorBytes});

    if (!Memory::Read32(global(negListOffset)))
        Memory::Write32(global(negListOffset), call(fnArrayNew, {kNegotiatorBytes, 4, 0}));

    const uint32_t list = Memory::Read32(global(negListOffset));
    call(fnArrayAppend, {list, blank});
    const uint32_t negotiator = call(fnArrayNth, {list, call(fnArrayLength, {list}) - 1});

    if (!negotiator)
        return kAllocError;

    Memory::Write32(negotiator + kGameSocket, frame.Arg(3));
    Memory::Write32(negotiator + kCookie, frame.Arg(4));
    Memory::Write32(negotiator + kClientIndex, frame.Arg(5));
    Memory::Write32(negotiator + kProgressCallback, frame.Arg(6));
    Memory::Write32(negotiator + kCompletedCallback, frame.Arg(7));
    Memory::Write32(negotiator + kUserData, frame.Arg(8));
    const uint32_t socket = call(fnSocket, {2, 2, 17}); // AF_INET, SOCK_DGRAM, IPPROTO_UDP
    Memory::Write32(negotiator + kNegotiateSocket, socket);
    Memory::Write32(negotiator + kRetryCount, 0);
    Memory::Write8(negotiator + kGotRemoteData, 0);
    Memory::Write8(negotiator + kSendGotRemoteData, 0);
    Memory::Write32(negotiator + kGuessedIp, 0);
    Memory::Write16(negotiator + kGuessedPort, 0);
    Memory::Write32(negotiator + kMaxRetryCount, 0);
    Memory::Write32(negotiator + kResult, kNoResult);

    if (socket == 0xFFFFFFFF) {
        // RemoveNegotiator.
        for (int32_t i = 0; i < int32_t(call(fnArrayLength, {list})); ++i)
            if (call(fnArrayNth, {list, uint32_t(i)}) == negotiator) {
                call(fnArrayDeleteAt, {list, uint32_t(i)});
                break;
            }

        return kSocketError;
    }

    call(fnSendInitPackets, {negotiator});
    return kNoError;
}
} // namespace

extern "C" void MH3_NNBeginNegotiationWithSocket(CpuContext* cpu) {
    const uint32_t result = BeginNegotiation(cpu);
    cpu->gpr[3] = result;
}
// Disabled unported PAL address for RMHJ08: REGISTER_NATIVE_FUNCTION_AS(0x80513940, MH3_NNBeginNegotiationWithSocket, "NNBeginNegotiationWithSocket");
REGISTER_NATIVE_FUNCTION_AS(0x8060369C, MH3_NNBeginNegotiationWithSocket, "NNBeginNegotiationWithSocket");
