// The safety-guard test matrix (docs/14_TESTING.md §Safety-guard tests).
//
// "The anti-cheat guard is the one component where a bug can cost someone an
// account. It gets the most rigorous treatment in the codebase."
//
// Every evidence source is a seam, so each failure below is FORCED rather than
// hoped for. The tests that matter most are not the ones proving a blocked
// module is blocked — they are the ones proving that a source which FAILS, or
// returns a PARTIAL answer, refuses. That is the shape of the worst defect this
// project has found (spike-notes.md §1: a driver enumeration that succeeds,
// reports 258 drivers, and yields nothing usable).

#include <windows.h>

#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <cstring>
#include <cwchar>
#include <fl_ac_rules.h>
#include <fl_guard.h>
#include <fl_prescan.h>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

using namespace fl::guard;

namespace {

// ---------------------------------------------------------------------------
// A rules file that is complete enough to gate. Tests mutate copies of this.
// ---------------------------------------------------------------------------
const char* GoodRulesJson() {
    return R"({
      "schemaVersion": 2,
      "anticheat": {
        "modules": [
          { "family": "Easy Anti-Cheat", "match": "prefix", "values": ["EasyAntiCheat", "EasyAntiCheat_EOS"] },
          { "family": "BattlEye", "match": "prefix", "values": ["BEClient", "BEService"] },
          { "family": "Denuvo Anti-Cheat", "match": "prefix", "values": ["denuvo"] },
          { "family": "nProtect GameGuard", "match": "prefix", "values": ["GameGuard", "npgg", "GameMon"] },
          { "family": "Xigncode3", "match": "prefix", "values": ["xhunter"] },
          { "family": "PunkBuster", "match": "prefix", "values": ["PnkBstr", "pbcl", "pbsv"] },
          { "family": "FACEIT", "match": "prefix", "values": ["faceit"] },
          { "family": "ESEA", "match": "prefix", "values": ["esea"] }
        ],
        "drivers": [
          { "family": "Riot Vanguard", "match": "exact", "values": ["vgk.sys"] },
          { "family": "mihoyo protect", "match": "prefix", "values": ["mhyprot"] }
        ],
        "directories": [ { "family": "Easy Anti-Cheat", "values": ["EasyAntiCheat"] } ],
        "services": [ { "family": "Riot Vanguard", "values": ["vgc"] } ],
        "files": [ { "family": "Xigncode3", "values": ["x3.xem"] } ],
        "blockedExecutables": [],
        "blockedStoreIds": [],
        "heuristic": {
          "signerField": "O",
          "nameFragments": ["anticheat", "antitamper", "gameguard", "guard", "protect"],
          "trustedSigners": ["Microsoft Corporation", "NVIDIA Corporation", "Valve Corp."],
          "action": "warn_and_refuse"
        }
      }
    })";
}

// ---------------------------------------------------------------------------
// Fakes. Globals rather than lambdas-with-capture because Sources holds plain
// function pointers on purpose: a std::function in the guard would allocate.
// ---------------------------------------------------------------------------
struct Fake {
    std::string rulesJson = GoodRulesJson();
    bool        rulesReadable = true;

    // Modules the target — and, when `modulesByPid` names them, any other process
    // in the scan set — reports.
    //
    // The per-pid map exists because the flat list made a whole class of §S16
    // fixture INEXPRESSIBLE: FakeEnumModules ignored its pid, so "anti-cheat is
    // loaded in the launcher but not in the renderer" — the arrangement §S16 was
    // written to catch — could not be written down at all. The scan-set test
    // worked around it with a visit counter, which proves the guard LOOKED at
    // three processes and not that it looked at the right ones or acted on what
    // it saw there.
    //
    // A pid absent from the map falls back to `modules`, so every existing case
    // keeps its meaning and only the tests that care about identity pay for it.
    std::vector<std::string>                          modules;
    std::map<std::uint32_t, std::vector<std::string>> modulesByPid;
    std::map<std::uint32_t, Collected>                moduleResultByPid;
    Collected                                         moduleResult = Collected::kOk;

    std::vector<std::string> drivers;
    Collected                driverResult = Collected::kOk;

    std::vector<std::string> presentServices;
    Collected                serviceResult = Collected::kOk;

    std::vector<std::uint32_t> scanSet{1234};
    Collected                  scanSetResult = Collected::kOk;

    // Check 4 — the static pre-scan. Entries are (name, isDirectory), because
    // directories and files are matched against different blocklist groups and
    // guessing from the string would be a second, weaker classifier.
    std::vector<std::pair<std::string, bool>> dirEntries;
    Collected                                 dirEntriesResult = Collected::kOk;
    std::wstring                              imageDirectory = L"C:\\Games\\Example";
    Collected                                 imageDirectoryResult = Collected::kOk;

    // §S18 — which pids the seam reports as FrameLedger's own, and whether it can
    // answer at all. Empty by default, so no existing case is silently exempted
    // from the fragment tier by adding this.
    std::set<std::uint32_t> ourOwnPids;
    Collected               ourOwnResult = Collected::kOk;

    // §S22 — the payload identity seam.
    //
    // DEFAULTS TO THE REAL IMPLEMENTATION, unlike every other member here, and
    // the asymmetry is deliberate. A fake that answered "ours" by default would
    // make every injection test below green whether or not the real check works
    // and whether or not the payload is actually where the guard requires it —
    // which is the whole subject of §S22. With kReal, the cross-process cases
    // exercise PayloadIsOurOwnImpl against a real file on a real disk, so the
    // CMake staging that puts the Overlay beside this binary is load-bearing:
    // break it and these tests go red.
    enum class Payload : std::uint8_t {
        kReal,          // call SystemSources().PayloadIsOurOwn
        kOurs,          // force "ours"
        kForeign,       // force "not ours"
        kCannotTell,    // force kFailed
    };
    Payload payload = Payload::kReal;
};

Fake g;

std::size_t FakeReadRules(char* buf, std::size_t cap) {
    if (!g.rulesReadable) {
        return static_cast<std::size_t>(-1);
    }
    if (g.rulesJson.size() >= cap) {
        return static_cast<std::size_t>(-1);
    }
    std::memcpy(buf, g.rulesJson.data(), g.rulesJson.size());
    return g.rulesJson.size();
}

Collected FakeEnumModules(std::uint32_t pid, NameSink sink, void* ctx) {
    const auto  it = g.modulesByPid.find(pid);
    const auto& list = (it != g.modulesByPid.end()) ? it->second : g.modules;
    for (const auto& m : list) {
        if (!sink(ctx, m.c_str())) {
            break;
        }
    }
    const auto rit = g.moduleResultByPid.find(pid);
    return (rit != g.moduleResultByPid.end()) ? rit->second : g.moduleResult;
}

Collected FakeEnumDrivers(NameSink sink, void* ctx) {
    for (const auto& d : g.drivers) {
        if (!sink(ctx, d.c_str())) {
            break;
        }
    }
    return g.driverResult;
}

Collected FakeQueryService(const char* name, bool* present) {
    *present = false;
    for (const auto& s : g.presentServices) {
        if (_stricmp(s.c_str(), name) == 0) {
            *present = true;
        }
    }
    return g.serviceResult;
}

Collected FakeEnumScanSet(std::uint32_t, bool (*sink)(void*, std::uint32_t), void* ctx) {
    for (auto pid : g.scanSet) {
        if (!sink(ctx, pid)) {
            break;
        }
    }
    return g.scanSetResult;
}

Collected FakeImageDirectory(std::uint32_t, wchar_t* out, std::size_t cap) {
    if (g.imageDirectoryResult != Collected::kOk) {
        return g.imageDirectoryResult;
    }
    if (g.imageDirectory.size() + 1 > cap) {
        return Collected::kFailed;
    }
    wcscpy_s(out, cap, g.imageDirectory.c_str());
    return Collected::kOk;
}

Collected FakeEnumDirEntries(const wchar_t*, DirEntrySink sink, void* ctx) {
    for (const auto& e : g.dirEntries) {
        if (!sink(ctx, e.first.c_str(), e.second)) {
            break;
        }
    }
    return g.dirEntriesResult;
}

Collected FakeProcessIsOurOwn(std::uint32_t pid, bool* isOurs) {
    if (g.ourOwnResult != Collected::kOk) {
        return g.ourOwnResult;
    }
    *isOurs = g.ourOwnPids.count(pid) != 0;
    return Collected::kOk;
}

Collected FakePayloadIsOurOwn(const wchar_t* dllPath, bool* isOurs) {
    switch (g.payload) {
    case Fake::Payload::kOurs:
        *isOurs = true;
        return Collected::kOk;
    case Fake::Payload::kForeign:
        *isOurs = false;
        return Collected::kOk;
    case Fake::Payload::kCannotTell:
        return Collected::kFailed;
    case Fake::Payload::kReal:
    default:
        return SystemSources().PayloadIsOurOwn(dllPath, isOurs);
    }
}

Sources FakeSources() {
    Sources s;
    s.ReadRulesFile = &FakeReadRules;
    s.EnumerateModules = &FakeEnumModules;
    s.EnumerateDrivers = &FakeEnumDrivers;
    s.QueryService = &FakeQueryService;
    s.EnumerateScanSet = &FakeEnumScanSet;
    s.ImageDirectory = &FakeImageDirectory;
    s.EnumerateDirEntries = &FakeEnumDirEntries;
    s.ProcessIsOurOwn = &FakeProcessIsOurOwn;
    s.PayloadIsOurOwn = &FakePayloadIsOurOwn;
    return s;
}

void ResetFake() {
    g = Fake{};
}

}    // namespace

// ===========================================================================
// The baseline. If this does not pass, every refusal below is meaningless —
// a guard that refuses everything is not a strict guard, it is a broken one.
// ===========================================================================
TEST_CASE("a clean process on a clean machine is allowed", "[guard]") {
    ResetFake();
    g.modules = {"kernel32.dll", "d3d11.dll", "UnityPlayer.dll"};
    g.drivers = {"\\SystemRoot\\system32\\ntoskrnl.exe", "\\SystemRoot\\system32\\drivers\\tcpip.sys"};

    const Verdict v = EvaluateWithSources(1234, FakeSources());
    INFO("reason was " << ReasonName(v.reason) << " signal=" << v.signal);
    REQUIRE(v.Allowed());
}

// ===========================================================================
// Blocklist matching: every family in 19_SAFETY §Blocklist seed has a fixture,
// each asserting a positive match, a case-flipped match, and a near miss.
// ===========================================================================
TEST_CASE("every seeded module family matches, case-insensitively", "[guard][blocklist]") {
    Rules rules;
    REQUIRE(ParseRules(GoodRulesJson(), std::strlen(GoodRulesJson()), rules) == ParseResult::kOk);

    struct Case {
        const char* family;
        const char* hit;
        const char* flipped;
        const char* nearMiss;
    };
    const Case cases[] = {
        {"Easy Anti-Cheat", "EasyAntiCheat_x64.dll", "easyanticheat_x64.dll", "EasyAnti.dll"},
        {"BattlEye", "BEClient_x64.dll", "beclient_x64.dll", "BEClien.dll"},
        {"Denuvo Anti-Cheat", "denuvo64.dll", "DENUVO64.DLL", "denu.dll"},
        {"nProtect GameGuard", "GameGuard.des", "gameguard.des", "GameGuar"},
        {"Xigncode3", "xhunter1.sys", "XHUNTER1.SYS", "xhunte"},
        {"PunkBuster", "PnkBstrA.exe", "pnkbstra.exe", "PnkBst"},
        {"FACEIT", "faceitclient.dll", "FACEITCLIENT.DLL", "facei"},
        {"ESEA", "eseadriver.sys", "ESEADRIVER.SYS", "ese"},
    };

    for (const auto& c : cases) {
        INFO("family " << c.family);
        const Family* hit = MatchName(rules, Group::kModules, c.hit);
        REQUIRE(hit != nullptr);
        CHECK(std::strcmp(hit->name, c.family) == 0);

        const Family* flipped = MatchName(rules, Group::kModules, c.flipped);
        REQUIRE(flipped != nullptr);
        CHECK(std::strcmp(flipped->name, c.family) == 0);

        CHECK(MatchName(rules, Group::kModules, c.nearMiss) == nullptr);
    }
}

TEST_CASE("Vanguard is matched as a DRIVER, on the path leaf", "[guard][blocklist]") {
    Rules rules;
    REQUIRE(ParseRules(GoodRulesJson(), std::strlen(GoodRulesJson()), rules) == ParseResult::kOk);

    // Drivers arrive as native paths.
    const Family* f = MatchName(rules, Group::kDrivers, "\\SystemRoot\\system32\\drivers\\vgk.sys");
    REQUIRE(f != nullptr);
    CHECK(std::strcmp(f->name, "Riot Vanguard") == 0);

    // Group membership is load-bearing: the same name must NOT match as a
    // module, because that would let a data edit move the machine-wide gate
    // into a per-process check without anything noticing.
    CHECK(MatchName(rules, Group::kModules, "vgk.sys") == nullptr);
}

TEST_CASE("Ricochet and VAC have no data, and the fixture says so explicitly", "[guard][blocklist]") {
    Rules rules;
    REQUIRE(ParseRules(GoodRulesJson(), std::strlen(GoodRulesJson()), rules) == ParseResult::kOk);

    // 19_SAFETY lists both families with "no data yet". A fixture that quietly
    // passed on an empty rule set would report coverage the gate does not have,
    // so assert the ABSENCE deliberately — this test should start failing the
    // day someone adds the data, which is exactly when it should be revisited.
    for (std::size_t i = 0; i < rules.familyCount; ++i) {
        CHECK(std::strcmp(rules.families[i].name, "Activision Ricochet") != 0);
        CHECK(std::strcmp(rules.families[i].name, "Valve VAC") != 0);
    }
}

// ===========================================================================
// Fail-closed proofs. 14_TESTING names five; the measurements in spike-notes
// §1 added four more that the matrix did not have.
// ===========================================================================
TEST_CASE("a blocked module in the target refuses", "[guard][failclosed]") {
    ResetFake();
    g.modules = {"kernel32.dll", "EasyAntiCheat_EOS.dll"};

    const Verdict v = EvaluateWithSources(1234, FakeSources());
    REQUIRE_FALSE(v.Allowed());
    CHECK(v.reason == Reason::kBlockedModule);
    CHECK(std::strcmp(v.family, "Easy Anti-Cheat") == 0);
    CHECK(std::strcmp(v.signal, "EasyAntiCheat_EOS.dll") == 0);
}

TEST_CASE("a blocked driver refuses for ALL titles", "[guard][failclosed]") {
    ResetFake();
    g.modules = {"kernel32.dll"};    // the game itself is clean
    g.drivers = {"\\SystemRoot\\system32\\drivers\\vgk.sys"};

    const Verdict v = EvaluateWithSources(1234, FakeSources());
    REQUIRE_FALSE(v.Allowed());
    CHECK(v.reason == Reason::kBlockedDriver);
    CHECK(std::strcmp(v.family, "Riot Vanguard") == 0);
}

TEST_CASE("module enumeration FAILING refuses — it is not an empty list", "[guard][failclosed]") {
    ResetFake();
    g.modules = {};
    g.moduleResult = Collected::kFailed;

    const Verdict v = EvaluateWithSources(1234, FakeSources());
    REQUIRE_FALSE(v.Allowed());
    CHECK(v.reason == Reason::kProcessUnreadable);
}

TEST_CASE("a PARTIAL module list refuses — the WOW64 case", "[guard][failclosed]") {
    // Measured: on a live 32-bit target the default filter returned 7 of 15
    // modules AS A SUCCESS (spike-notes.md §1). A guard that accepted that
    // would be least accurate exactly where it was asked about a 32-bit title.
    ResetFake();
    g.modules = {"kernel32.dll"};
    g.moduleResult = Collected::kIncomplete;

    const Verdict v = EvaluateWithSources(1234, FakeSources());
    REQUIRE_FALSE(v.Allowed());
    CHECK(v.reason == Reason::kModuleScanFailed);
}

TEST_CASE("driver enumeration failing refuses", "[guard][failclosed]") {
    ResetFake();
    g.driverResult = Collected::kFailed;

    const Verdict v = EvaluateWithSources(1234, FakeSources());
    REQUIRE_FALSE(v.Allowed());
    CHECK(v.reason == Reason::kDriverScanFailed);
}

TEST_CASE("a service query that is DENIED refuses; ABSENT does not", "[guard][failclosed]") {
    // The distinction the guard depends on, and the one branch fl-probe-guard
    // could NOT measure — a standard user holds SERVICE_QUERY_STATUS on stock
    // services, so ACCESS_DENIED is not producible against real ones. It exists
    // only here, which is precisely why it has to exist here.
    ResetFake();
    g.serviceResult = Collected::kFailed;

    const Verdict denied = EvaluateWithSources(1234, FakeSources());
    REQUIRE_FALSE(denied.Allowed());
    CHECK(denied.reason == Reason::kServiceQueryFailed);

    ResetFake();
    g.modules = {"kernel32.dll"};
    g.serviceResult = Collected::kOk;    // queried fine, service simply absent
    CHECK(EvaluateWithSources(1234, FakeSources()).Allowed());
}

TEST_CASE("a present blocked service refuses", "[guard][failclosed]") {
    ResetFake();
    g.modules = {"kernel32.dll"};
    g.presentServices = {"vgc"};

    const Verdict v = EvaluateWithSources(1234, FakeSources());
    REQUIRE_FALSE(v.Allowed());
    CHECK(v.reason == Reason::kBlockedService);
    CHECK(std::strcmp(v.family, "Riot Vanguard") == 0);
}

TEST_CASE("an empty or failed scan set refuses — it is not 'nothing to scan'", "[guard][failclosed]") {
    ResetFake();
    g.scanSet = {};
    const Verdict empty = EvaluateWithSources(1234, FakeSources());
    REQUIRE_FALSE(empty.Allowed());
    CHECK(empty.reason == Reason::kProcessTreeUnavailable);

    ResetFake();
    g.scanSetResult = Collected::kFailed;
    const Verdict failed = EvaluateWithSources(1234, FakeSources());
    REQUIRE_FALSE(failed.Allowed());
    CHECK(failed.reason == Reason::kProcessTreeUnavailable);
}

TEST_CASE("EVERY process in the scan set is scanned, not just the target", "[guard][failclosed][S16]") {
    // §S16: a game's launcher routinely initialises anti-cheat and then spawns
    // the renderer. Scanning only the process we inject into would miss it.
    //
    // This used to assert a visit COUNT, because the fake ignored its pid and the
    // real fixture could not be written. A count proves the guard looked at three
    // processes; it does not prove it looked at the right three, and it cannot
    // prove it ACTED on what it found in one of them. Both are now assertable.
    ResetFake();
    // TARGET FIRST, mirroring EnumerateScanSetImpl (fl_guard_sources.cpp emits
    // the injection target, then ancestors, then descendants). The order is not
    // cosmetic here: with the target last, a guard that scanned only the FIRST
    // entry would still refuse in the launcher case below and the section would
    // pass while covering nothing. Measured — that is exactly what happened when
    // this fixture was written {1000, 1001, 1234} and the loop was canaried to a
    // single iteration.
    g.scanSet = {1234, 1000, 1001};
    g.modules = {"kernel32.dll"};

    SECTION("a clean tree is allowed — the direction that makes the rest mean something") {
        CHECK(EvaluateWithSources(1234, FakeSources()).Allowed());
    }

    SECTION("anti-cheat in the LAUNCHER refuses, though the target itself is clean") {
        // The arrangement §S16 exists for, and until the per-pid map it was
        // inexpressible: the renderer we inject into carries nothing.
        g.modulesByPid[1000] = {"kernel32.dll", "EasyAntiCheat_x64.dll"};
        g.modulesByPid[1234] = {"kernel32.dll", "d3d11.dll"};

        const Verdict v = EvaluateWithSources(1234, FakeSources());
        REQUIRE_FALSE(v.Allowed());
        CHECK(v.reason == Reason::kBlockedModule);
        CHECK(std::strcmp(v.family, "Easy Anti-Cheat") == 0);
    }

    SECTION("anti-cheat in a SIBLING/descendant refuses too") {
        g.modulesByPid[1001] = {"BEService_x64.dll"};
        g.modulesByPid[1234] = {"kernel32.dll"};

        const Verdict v = EvaluateWithSources(1234, FakeSources());
        REQUIRE_FALSE(v.Allowed());
        CHECK(v.reason == Reason::kBlockedModule);
        CHECK(std::strcmp(v.family, "BattlEye") == 0);
    }

    SECTION("a process in the set that cannot be read refuses, even when the target is clean") {
        // "Scanned what we could" is not a state §S16 has. Previously only
        // expressible for ALL processes at once, which cannot distinguish
        // "the target failed" from "one of its ancestors did".
        g.modulesByPid[1234] = {"kernel32.dll"};
        g.moduleResultByPid[1000] = Collected::kFailed;

        const Verdict v = EvaluateWithSources(1234, FakeSources());
        REQUIRE_FALSE(v.Allowed());
        CHECK(v.reason == Reason::kProcessUnreadable);
    }

    SECTION("and every member really is visited, by identity rather than by count") {
        static std::vector<std::uint32_t> seen;
        seen.clear();
        Sources s = FakeSources();
        s.EnumerateModules = [](std::uint32_t pid, NameSink sink, void* ctx) -> Collected {
            seen.push_back(pid);
            sink(ctx, "kernel32.dll");
            return Collected::kOk;
        };
        CHECK(EvaluateWithSources(1234, s).Allowed());

        std::vector<std::uint32_t> sorted = seen;
        std::sort(sorted.begin(), sorted.end());
        CHECK(sorted == std::vector<std::uint32_t>{1000, 1001, 1234});
    }
}

// ===========================================================================
// §S18 — the guard refused ITSELF.
//
// FrameLedger.Guard.dll contains the substring `guard`, one of the heuristic's
// nameFragments, and the project ships unsigned (CLAUDE.md rule 9) so the signer
// half can never rescue it. In launch mode the Agent is the game's parent and
// therefore inside the §S16 scan set, so every launch-mode injection refused —
// and Vulkan Tier 1 with it, because the layer's only enable path
// (FRAMELEDGER_ENABLE_VK_LAYER=1) can only be set by the launching process.
//
// Measured on a real title 2026-08-03: ancestor with the DLL loaded ->
// SuspiciousUnsigned; not an ancestor -> Allow.
//
// This is the ONLY exception in the gate, so the cases below spend most of their
// effort on what it must NOT do.
// ===========================================================================
TEST_CASE("§S18 — the fragment tier is suppressed for our own processes, and for nothing else",
          "[guard][failclosed][S18]") {
    ResetFake();
    g.scanSet = {1234, 4000, 4001};    // target first, mirroring EnumerateScanSetImpl
    g.modulesByPid[1234] = {"kernel32.dll", "d3d11.dll"};

    SECTION("TWO FrameLedger processes in the scan set, both carrying the DLL, target clean -> Allow") {
        // §S18's own blocker 2: a one-pid fixture cannot go red on this, and
        // GetCurrentProcessId() — the answer the panel first reached for — gets
        // it wrong, because the defect is a property of the BINARY and more than
        // one FrameLedger process can carry it.
        g.modulesByPid[4000] = {"kernel32.dll", "FrameLedger.Guard.dll"};
        g.modulesByPid[4001] = {"kernel32.dll", "FrameLedger.Guard.dll"};
        g.ourOwnPids = {4000, 4001};

        const Verdict v = EvaluateWithSources(1234, FakeSources());
        INFO("reason was " << ReasonName(v.reason) << " signal=" << v.signal);
        CHECK(v.Allowed());
    }

    SECTION("the same module in the TARGET still refuses — the exception never covers it") {
        g.modulesByPid[1234] = {"kernel32.dll", "FrameLedger.Guard.dll"};
        g.ourOwnPids = {1234, 4000};    // even when the seam claims the target is ours

        const Verdict v = EvaluateWithSources(1234, FakeSources());
        REQUIRE_FALSE(v.Allowed());
        CHECK(v.reason == Reason::kSuspiciousUnsigned);
    }

    SECTION("a scan-set process that is NOT ours still refuses on the same module name") {
        // Closes the spoofing route a name allowlist would have opened: a DLL
        // that borrows the name inside the game's own tree is not exempt.
        g.modulesByPid[4000] = {"FrameLedger.Guard.dll"};
        g.ourOwnPids = {};

        const Verdict v = EvaluateWithSources(1234, FakeSources());
        REQUIRE_FALSE(v.Allowed());
        CHECK(v.reason == Reason::kSuspiciousUnsigned);
    }

    SECTION("a seam that CANNOT DETERMINE does not suppress") {
        g.modulesByPid[4000] = {"FrameLedger.Guard.dll"};
        g.ourOwnPids = {4000};
        g.ourOwnResult = Collected::kFailed;

        const Verdict v = EvaluateWithSources(1234, FakeSources());
        REQUIRE_FALSE(v.Allowed());
        CHECK(v.reason == Reason::kSuspiciousUnsigned);
    }

    SECTION("a MISSING seam does not suppress") {
        // Sources members default to nullptr, so forgetting to wire this has to
        // fail towards refusing rather than towards allowing.
        g.modulesByPid[4000] = {"FrameLedger.Guard.dll"};
        g.ourOwnPids = {4000};
        Sources s = FakeSources();
        s.ProcessIsOurOwn = nullptr;

        const Verdict v = EvaluateWithSources(1234, s);
        REQUIRE_FALSE(v.Allowed());
        CHECK(v.reason == Reason::kSuspiciousUnsigned);
    }

    SECTION("only the FUZZY tier is suppressed — an exact blocklist hit in our own process still refuses") {
        // The clause that keeps this a narrow exception rather than a hole. If
        // real anti-cheat is somehow loaded in a FrameLedger process, we are not
        // injecting into anything.
        g.modulesByPid[4000] = {"FrameLedger.Guard.dll", "EasyAntiCheat_x64.dll"};
        g.ourOwnPids = {4000};

        const Verdict v = EvaluateWithSources(1234, FakeSources());
        REQUIRE_FALSE(v.Allowed());
        CHECK(v.reason == Reason::kBlockedModule);
        CHECK(std::strcmp(v.family, "Easy Anti-Cheat") == 0);
    }

    SECTION("an unreadable process is still unreadable, exempt or not") {
        g.ourOwnPids = {4000};
        g.moduleResultByPid[4000] = Collected::kFailed;

        const Verdict v = EvaluateWithSources(1234, FakeSources());
        REQUIRE_FALSE(v.Allowed());
        CHECK(v.reason == Reason::kProcessUnreadable);
    }
}

TEST_CASE("§S18 — the real ProcessIsOurOwn answers both directions", "[guard][S18][live]") {
    // The seam above is what makes the matrix forceable; this is what keeps the
    // seam honest about the machine. Without it the whole exception rests on a
    // fake agreeing with itself.
    const Sources sys = SystemSources();
    REQUIRE(sys.ProcessIsOurOwn != nullptr);

    SECTION("this test process IS ours — it runs the guard code from its own directory") {
        bool            ours = false;
        const Collected c = sys.ProcessIsOurOwn(GetCurrentProcessId(), &ours);
        REQUIRE(c == Collected::kOk);
        CHECK(ours);
    }

    SECTION("a System32 process is NOT ours") {
        // The green case above passes against an implementation that returns
        // true unconditionally; this is the half that catches it. cmd.exe is
        // chosen because its directory is guaranteed to differ from ours and it
        // exits on its own.
        wchar_t      cmdline[] = L"cmd.exe /c exit";
        STARTUPINFOW si{};
        si.cb = sizeof(si);
        PROCESS_INFORMATION pi{};
        REQUIRE(CreateProcessW(nullptr, cmdline, nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si,
                               &pi) != 0);

        // Seeded true so a seam that writes nothing cannot pass by accident.
        bool            ours = true;
        const Collected c = sys.ProcessIsOurOwn(pi.dwProcessId, &ours);

        // Either answer is acceptable and both must say "not ours": kOk with
        // ours=false, or kFailed if the process exited before we could look —
        // which is the fail-closed value, and the implementation clears the
        // out-param before it can fail.
        INFO("collected=" << static_cast<int>(c));
        CHECK_FALSE(ours);

        WaitForSingleObject(pi.hProcess, 5000);
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
    }
}

// ===========================================================================
// §S21 — the compiled-in floor. A rules file may EXTEND the blocklist and may
// not shrink it.
//
// The defect this closes: IsCompleteEnoughToGate validates that three family
// NAMES appear in the right GROUPS and never reads their `values`. So the
// document below — twelve lines, syntactically perfect, "complete" by every
// check the guard had — parsed kOk over a blocklist that matched nothing. Paired
// with a rules path built from an inherited LOCALAPPDATA, one environment
// variable and one file allowed injection on a machine running Vanguard: the
// override CLAUDE.md rule 2 says does not exist.
//
// Proven red by emptying FloorFamilies(): every REQUIRE below fails and the
// verdict becomes Allow, which is precisely the pre-fix behaviour.
// ===========================================================================
namespace {

// The three required families present by NAME and GROUP, with values that can
// never match anything real.
std::string DisarmedRulesJson() {
    return R"({"anticheat": {
        "modules": [
          { "family": "Easy Anti-Cheat", "match": "exact", "values": ["zzzz-not-real.dll"] },
          { "family": "BattlEye",        "match": "exact", "values": ["zzzz-also-not.dll"] }
        ],
        "drivers": [
          { "family": "Riot Vanguard", "match": "exact", "values": ["zzzz-nothing.sys"] }
        ],
        "directories": [], "services": [], "files": [],
        "blockedExecutables": [], "blockedStoreIds": []
    }})";
}

}    // namespace

TEST_CASE("a rules file that names the required families but disarms their values still refuses",
          "[guard][failclosed][rules][floor]") {
    ResetFake();
    g.rulesJson = DisarmedRulesJson();

    SECTION("the document is still ACCEPTED — the refusal comes from the floor, not from ParseRules") {
        // If this ever became kRulesMalformed/kIncomplete the sections below
        // would pass for the wrong reason, and the floor would be untested.
        Rules             parsed;
        const std::string json = DisarmedRulesJson();
        CHECK(ParseRules(json.c_str(), json.size(), parsed) == ParseResult::kOk);
    }

    SECTION("a real EAC module in the target is still blocked") {
        g.modules = {"EasyAntiCheat_x64.dll"};
        const Verdict v = EvaluateWithSources(1234, FakeSources());
        REQUIRE_FALSE(v.Allowed());
        CHECK(v.reason == Reason::kBlockedModule);
        CHECK(std::strcmp(v.family, "Easy Anti-Cheat") == 0);
    }

    SECTION("a real BattlEye module in the target is still blocked") {
        g.modules = {"BEClient_x64.dll"};
        const Verdict v = EvaluateWithSources(1234, FakeSources());
        REQUIRE_FALSE(v.Allowed());
        CHECK(v.reason == Reason::kBlockedModule);
        CHECK(std::strcmp(v.family, "BattlEye") == 0);
    }

    SECTION("the machine-wide Vanguard driver is still blocked") {
        g.drivers = {R"(\SystemRoot\system32\drivers\vgk.sys)"};
        const Verdict v = EvaluateWithSources(1234, FakeSources());
        REQUIRE_FALSE(v.Allowed());
        CHECK(v.reason == Reason::kBlockedDriver);
        CHECK(std::strcmp(v.family, "Riot Vanguard") == 0);
    }

    SECTION("and a genuinely clean process under the same file is still ALLOWED") {
        // The direction that makes the four above mean something. A floor that
        // refused everything would satisfy every assertion in this test case and
        // be a gate that cannot pass — this project has shipped two of those.
        g.modules = {"kernel32.dll", "d3d11.dll"};
        CHECK(EvaluateWithSources(1234, FakeSources()).Allowed());
    }
}

TEST_CASE("the floor does not make the completeness check unfailable", "[guard][failclosed][rules][floor]") {
    // The floor satisfies IsCompleteEnoughToGate by construction, so checking the
    // MERGED set would silently retire this refusal — a gate that cannot fail,
    // arrived at by fixing a different bug. ParseRules therefore asks the
    // question of the FILE's families only, and this is what holds it there.
    ResetFake();
    g.rulesJson = R"({"anticheat": {
        "modules": [ { "family": "Easy Anti-Cheat", "match": "prefix", "values": ["EasyAntiCheat"] } ],
        "drivers": [ { "family": "Riot Vanguard", "match": "exact", "values": ["vgk.sys"] } ],
        "blockedExecutables": [], "blockedStoreIds": []
    }})";    // BattlEye is absent from the FILE, though the floor carries it

    const Verdict v = EvaluateWithSources(1234, FakeSources());
    REQUIRE_FALSE(v.Allowed());
    CHECK(v.reason == Reason::kRulesIncomplete);
}

// ===========================================================================
// The rules data itself is an input, and a hostile or broken one must refuse.
// ===========================================================================
TEST_CASE("an unreadable rules file refuses", "[guard][failclosed][rules]") {
    ResetFake();
    g.rulesReadable = false;

    const Verdict v = EvaluateWithSources(1234, FakeSources());
    REQUIRE_FALSE(v.Allowed());
    CHECK(v.reason == Reason::kRulesUnreadable);
}

TEST_CASE("malformed JSON refuses", "[guard][failclosed][rules]") {
    ResetFake();
    g.rulesJson = R"({"anticheat": {"modules": [)";

    const Verdict v = EvaluateWithSources(1234, FakeSources());
    REQUIRE_FALSE(v.Allowed());
    CHECK(v.reason == Reason::kRulesMalformed);
}

TEST_CASE("a missing anticheat block refuses", "[guard][failclosed][rules]") {
    ResetFake();
    g.rulesJson = R"({"schemaVersion": 2, "engines": []})";

    const Verdict v = EvaluateWithSources(1234, FakeSources());
    REQUIRE_FALSE(v.Allowed());
    CHECK(v.reason == Reason::kRulesMalformed);
}

TEST_CASE("an EMPTY blocklist refuses — it is a fixture, never a ship state", "[guard][failclosed][rules]") {
    ResetFake();
    g.rulesJson = R"({"anticheat": {"modules": [], "drivers": [], "blockedExecutables": [], "blockedStoreIds": []}})";

    const Verdict v = EvaluateWithSources(1234, FakeSources());
    REQUIRE_FALSE(v.Allowed());
    CHECK(v.reason == Reason::kRulesIncomplete);
}

TEST_CASE("dropping a required family refuses, and moving its GROUP does too", "[guard][failclosed][rules]") {
    // The second half is the subtle one. Riot Vanguard in `drivers` is the
    // machine-wide gate; the same family moved into `modules` would satisfy a
    // group-agnostic completeness check while that gate silently lost its only
    // entry. tools/rules-validate.ps1 catches this in CI — but rules ship as
    // updatable data, so CI is not in the loop at injection time.
    ResetFake();
    std::string       moved = GoodRulesJson();
    const std::string from = R"({ "family": "Riot Vanguard", "match": "exact", "values": ["vgk.sys"] },)";
    const auto        at = moved.find(from);
    REQUIRE(at != std::string::npos);
    moved.erase(at, from.size());
    g.rulesJson = moved;

    const Verdict v = EvaluateWithSources(1234, FakeSources());
    REQUIRE_FALSE(v.Allowed());
    CHECK(v.reason == Reason::kRulesIncomplete);
}

TEST_CASE("a prefix shorter than the floor refuses the whole file", "[guard][failclosed][rules]") {
    // A 1-3 character prefix over-matches, which fails CLOSED and so cannot get
    // anyone banned — but it refuses every title on the machine, and that is
    // how a user ends up looking for an override.
    ResetFake();
    std::string bad = GoodRulesJson();
    const auto  at = bad.find(R"("denuvo")");
    REQUIRE(at != std::string::npos);
    bad.replace(at, std::strlen(R"("denuvo")"), R"("den")");
    g.rulesJson = bad;

    const Verdict v = EvaluateWithSources(1234, FakeSources());
    REQUIRE_FALSE(v.Allowed());
    CHECK(v.reason == Reason::kRulesMalformed);
}

TEST_CASE("a blank value refuses — it would match everything", "[guard][failclosed][rules]") {
    ResetFake();
    std::string bad = GoodRulesJson();
    const auto  at = bad.find(R"("xhunter")");
    REQUIRE(at != std::string::npos);
    bad.replace(at, std::strlen(R"("xhunter")"), R"("")");
    g.rulesJson = bad;

    const Verdict v = EvaluateWithSources(1234, FakeSources());
    REQUIRE_FALSE(v.Allowed());
    CHECK(v.reason == Reason::kRulesMalformed);
}

// ===========================================================================
// Every source is required. A null one is "no evidence", never "no problem".
// ===========================================================================
TEST_CASE("a missing evidence source refuses", "[guard][failclosed]") {
    ResetFake();
    g.modules = {"kernel32.dll"};

    struct Case {
        const char* what;
        Sources (*make)();
    };

    Sources noRules = FakeSources();
    noRules.ReadRulesFile = nullptr;
    CHECK_FALSE(EvaluateWithSources(1234, noRules).Allowed());
    CHECK(EvaluateWithSources(1234, noRules).reason == Reason::kRulesUnreadable);

    Sources noDrivers = FakeSources();
    noDrivers.EnumerateDrivers = nullptr;
    CHECK(EvaluateWithSources(1234, noDrivers).reason == Reason::kDriverScanFailed);

    Sources noServices = FakeSources();
    noServices.QueryService = nullptr;
    CHECK(EvaluateWithSources(1234, noServices).reason == Reason::kServiceQueryFailed);

    Sources noModules = FakeSources();
    noModules.EnumerateModules = nullptr;
    CHECK(EvaluateWithSources(1234, noModules).reason == Reason::kProcessTreeUnavailable);

    Sources noTree = FakeSources();
    noTree.EnumerateScanSet = nullptr;
    CHECK(EvaluateWithSources(1234, noTree).reason == Reason::kProcessTreeUnavailable);
}

TEST_CASE("a default-constructed Verdict is a refusal", "[guard][failclosed]") {
    // If someone adds a code path that forgets to set a reason, the value it
    // leaves behind must not read as permission.
    const Verdict v;
    CHECK_FALSE(v.Allowed());
}

// ===========================================================================
// The heuristic. 19_SAFETY: fragment AND not signed by a known vendor.
// ===========================================================================
TEST_CASE("signer comparison is against O=, exact, and untrusted-by-default", "[guard][heuristic]") {
    Rules rules;
    REQUIRE(ParseRules(GoodRulesJson(), std::strlen(GoodRulesJson()), rules) == ParseResult::kOk);

    CHECK(IsTrustedSigner(rules, "Microsoft Corporation"));
    CHECK(IsTrustedSigner(rules, "microsoft corporation"));

    // Measured: every WHQL-signed binary, including the NVIDIA display driver,
    // carries CN='Microsoft Windows Hardware Compatibility Publisher' while
    // O='Microsoft Corporation'. Matching the CN would make the entire driver
    // stack read as untrusted (spike-notes.md §1).
    CHECK_FALSE(IsTrustedSigner(rules, "Microsoft Windows Hardware Compatibility Publisher"));

    // Never a substring: a substring signer rule is a forgery surface.
    CHECK_FALSE(IsTrustedSigner(rules, "NVIDIA Corporation Ltd"));

    // Absent, invalid, or simply unchecked are all untrusted.
    CHECK_FALSE(IsTrustedSigner(rules, nullptr));
    CHECK_FALSE(IsTrustedSigner(rules, ""));
}

TEST_CASE("a suspicious module name refuses while the signer path is unwired", "[guard][heuristic]") {
    ResetFake();
    g.modules = {"kernel32.dll", "someantitamper64.dll"};

    const Verdict v = EvaluateWithSources(1234, FakeSources());
    REQUIRE_FALSE(v.Allowed());
    CHECK(v.reason == Reason::kSuspiciousUnsigned);
}

// ===========================================================================
// The injection primitive, end to end.
//
// A real cross-process injection: spawn hook-harness (our own dummy D3D11 app,
// no game and no anti-cheat surface at all), inject FrameLedger.Overlay.dll,
// and confirm the module is really there. 14_TESTING §Integration tests
// contemplates exactly this — the harness is what makes the architecture
// testable without touching a title.
//
// Guarded by FL_INJECT_TARGETS so the suite still builds if the paths are not
// wired; a silently absent test would be worse than no test.
// ===========================================================================
#if defined(FL_HARNESS_EXE) && defined(FL_OVERLAY_DLL)

#include <windows.h>

#include <psapi.h>

namespace {

bool TargetHasModule(DWORD pid, const wchar_t* leaf) {
    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_VM_READ, FALSE, pid);
    if (h == nullptr) {
        return false;
    }
    HMODULE mods[1024]{};
    DWORD   needed = 0;
    bool    found = false;
    if (EnumProcessModulesEx(h, mods, sizeof(mods), &needed, LIST_MODULES_ALL)) {
        const size_t n = (needed > sizeof(mods) ? sizeof(mods) : needed) / sizeof(HMODULE);
        for (size_t i = 0; i < n && !found; ++i) {
            wchar_t name[MAX_PATH]{};
            if (GetModuleBaseNameW(h, mods[i], name, MAX_PATH) != 0 && _wcsicmp(name, leaf) == 0) {
                found = true;
            }
        }
    }
    CloseHandle(h);
    return found;
}

struct Child {
    PROCESS_INFORMATION pi{};
    ~Child() {
        if (pi.hProcess != nullptr) {
            TerminateProcess(pi.hProcess, 0);
            WaitForSingleObject(pi.hProcess, 3000);
            CloseHandle(pi.hThread);
            CloseHandle(pi.hProcess);
        }
    }
};

// Spawn the harness and wait for its loader. Returns false if it died, which the
// caller must REQUIRE on: a dead child makes every assertion below fail for a
// reason unrelated to what is being tested, and that is exactly what happened
// the first time the injection tests were written.
bool StartHarness(Child& child) {
    std::wstring cmd = std::wstring(L"\"") + FL_HARNESS_EXE + L"\" --hold 30";
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    if (!CreateProcessW(FL_HARNESS_EXE, cmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si,
                        &child.pi)) {
        return false;
    }
    Sleep(800);
    return WaitForSingleObject(child.pi.hProcess, 0) == WAIT_TIMEOUT;
}

}    // namespace

TEST_CASE("the injection primitive really loads our DLL into another process", "[guard][inject]") {
    Child        child;
    std::wstring cmd = std::wstring(L"\"") + FL_HARNESS_EXE + L"\" --hold 30";
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    REQUIRE(CreateProcessW(FL_HARNESS_EXE, cmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si,
                           &child.pi));
    Sleep(800);    // let its loader finish

    // The target must still be RUNNING. A dead child would make every
    // assertion below fail for a reason unrelated to injection — which is
    // exactly what happened the first time this test was written.
    REQUIRE(WaitForSingleObject(child.pi.hProcess, 0) == WAIT_TIMEOUT);

    CHECK_FALSE(TargetHasModule(child.pi.dwProcessId, L"FrameLedger.Overlay.dll"));

    ResetFake();
    g.modules = {"kernel32.dll"};
    g.scanSet = {child.pi.dwProcessId};

    const Verdict v = GuardedInjectWithSources(child.pi.dwProcessId, FL_OVERLAY_DLL, FakeSources());
    INFO("reason " << ReasonName(v.reason) << " signal=" << v.signal);
    REQUIRE(v.Allowed());
    CHECK(v.signal[0] == '\0');    // an empty signal means the injection took

    CHECK(TargetHasModule(child.pi.dwProcessId, L"FrameLedger.Overlay.dll"));
}

TEST_CASE("a REFUSED guard injects nothing", "[guard][inject]") {
    // The assertion the whole design exists for. The target is a real process,
    // the DLL is real and loadable, and the only thing standing between them is
    // the verdict.
    Child        child;
    std::wstring cmd = std::wstring(L"\"") + FL_HARNESS_EXE + L"\" --hold 30";
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    REQUIRE(CreateProcessW(FL_HARNESS_EXE, cmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si,
                           &child.pi));
    Sleep(800);
    REQUIRE(WaitForSingleObject(child.pi.hProcess, 0) == WAIT_TIMEOUT);

    ResetFake();
    g.modules = {"kernel32.dll", "BEClient_x64.dll"};    // BattlEye in the target
    g.scanSet = {child.pi.dwProcessId};

    const Verdict v = GuardedInjectWithSources(child.pi.dwProcessId, FL_OVERLAY_DLL, FakeSources());
    REQUIRE_FALSE(v.Allowed());
    CHECK(v.reason == Reason::kBlockedModule);
    CHECK_FALSE(TargetHasModule(child.pi.dwProcessId, L"FrameLedger.Overlay.dll"));
}

TEST_CASE("a missing payload is refused before any process is opened", "[guard][inject]") {
    ResetFake();
    g.modules = {"kernel32.dll"};
    g.scanSet = {GetCurrentProcessId()};

    const Verdict v = GuardedInjectWithSources(GetCurrentProcessId(), LR"(C:\definitely\not\here.dll)", FakeSources());

    // The gate passed and the injection still did not happen — and that is NOT
    // an allow. This case used to assert v.Allowed(), which meant a caller
    // reading Allowed() got `true` for a DLL that was never loaded.
    CHECK_FALSE(v.Allowed());
    CHECK(v.reason == Reason::kInjectionFailed);

    // Distinguishable from a refusal: no anti-cheat family is named, because
    // none was found.
    CHECK(v.family[0] == '\0');
}

TEST_CASE("a 32-bit target gets its own reason, not a generic failure", "[guard][inject][wow64]") {
    // 14_TESTING's manual matrix: "a 32-bit title, asserting it is correctly
    // refused and routed to Tier 2". MEASURED against a real one first —
    // Deadly Heart Gambit is an x86 Unity title, and the old code reported the
    // refusal as Allow with the truth in a string.
    //
    // SysWOW64\cmd.exe is a guaranteed 32-bit process on any x64 Windows,
    // including CI.
    wchar_t sysWow64[MAX_PATH]{};
    REQUIRE(GetSystemWow64DirectoryW(sysWow64, MAX_PATH) != 0);
    std::wstring cmd = std::wstring(sysWow64) + L"\\cmd.exe";

    STARTUPINFOW        si{};
    PROCESS_INFORMATION pi{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    std::wstring cmdline = L"\"" + cmd + L"\" /c pause";
    REQUIRE(CreateProcessW(nullptr, cmdline.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si,
                           &pi) != 0);

    ResetFake();
    g.modules = {"kernel32.dll"};
    g.scanSet = {pi.dwProcessId};

    // Our own Overlay: a real, existing x64 DLL, so the refusal below is about
    // the TARGET's bitness and nothing else.
    const Verdict v = GuardedInjectWithSources(pi.dwProcessId, FL_OVERLAY_DLL, FakeSources());

    TerminateProcess(pi.hProcess, 0);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);

    INFO("reason was " << ReasonName(v.reason) << " signal=" << v.signal);
    CHECK_FALSE(v.Allowed());
    CHECK(v.reason == Reason::kTargetIsWow64);
    CHECK(v.reason != Reason::kInjectionFailed);    // specific, not the catch-all
}

// ===========================================================================
// §S22 — the payload, not just the target.
//
// Until these existed, the ONLY thing ever asked of `dllPath` was
// GetFileAttributesW. The shipped, exported FlGuardedInject would load any DLL
// on the machine into any x64 process that happened to carry no anti-cheat —
// §S9's user-runnable injector, re-shipped as a documented C ABI.
//
// Every case here runs the REAL PayloadIsOurOwnImpl (Fake::Payload::kReal is the
// default) except the two that exist to force the seam's failure paths. Real
// files, real directories, a real child process, and the assertion that matters
// is always "and nothing was loaded".
// ===========================================================================

TEST_CASE("a payload that is not ours is refused, and nothing is loaded", "[guard][inject][payload]") {
    // The measured defect, as a test. C:\Windows\System32\winmm.dll is a real,
    // loadable, Microsoft-signed DLL that has nothing to do with FrameLedger —
    // and before §S22 this call returned Allow and put it in the target.
    Child child;
    REQUIRE(StartHarness(child));

    ResetFake();
    g.modules = {"kernel32.dll"};    // a clean target: the payload is the only objection
    g.scanSet = {child.pi.dwProcessId};

    wchar_t sys[MAX_PATH]{};
    REQUIRE(GetSystemDirectoryW(sys, MAX_PATH) != 0);
    const std::wstring foreign = std::wstring(sys) + L"\\winmm.dll";
    REQUIRE(GetFileAttributesW(foreign.c_str()) != INVALID_FILE_ATTRIBUTES);    // it really is loadable

    // If the harness already carries it, the assertion below proves nothing —
    // "not loaded by us" and "was there all along" would look identical. Fail
    // loudly rather than pass vacuously.
    REQUIRE_FALSE(TargetHasModule(child.pi.dwProcessId, L"winmm.dll"));

    const Verdict v = GuardedInjectWithSources(child.pi.dwProcessId, foreign.c_str(), FakeSources());

    INFO("reason " << ReasonName(v.reason) << " signal=" << v.signal);
    CHECK_FALSE(v.Allowed());
    CHECK(v.reason == Reason::kPayloadNotOurs);
    CHECK(v.family[0] == '\0');    // no anti-cheat was found; this is about us
    CHECK_FALSE(TargetHasModule(child.pi.dwProcessId, L"winmm.dll"));
}

TEST_CASE("the payload check discriminates on DIRECTORY, not on filename", "[guard][inject][payload]") {
    // Same file name, same bytes, wrong place. A check keyed on
    // "FrameLedger.Overlay.dll" would pass this and would be defeated by any
    // DLL that borrowed the name.
    //
    // Guarded against becoming vacuous: if the staged copy and the build output
    // ever resolve to one directory there is nothing here to discriminate, and a
    // test that cannot fail is worse than an absent one.
    REQUIRE(std::wstring(FL_OVERLAY_DLL) != std::wstring(FL_OVERLAY_DLL_ELSEWHERE));

    Child child;
    REQUIRE(StartHarness(child));

    ResetFake();
    g.modules = {"kernel32.dll"};
    g.scanSet = {child.pi.dwProcessId};

    const Verdict v = GuardedInjectWithSources(child.pi.dwProcessId, FL_OVERLAY_DLL_ELSEWHERE, FakeSources());

    INFO("reason " << ReasonName(v.reason) << " signal=" << v.signal);
    CHECK_FALSE(v.Allowed());
    CHECK(v.reason == Reason::kPayloadNotOurs);
    CHECK_FALSE(TargetHasModule(child.pi.dwProcessId, L"FrameLedger.Overlay.dll"));
}

TEST_CASE("a payload identity seam that cannot answer refuses", "[guard][inject][payload]") {
    // The ordinary polarity of this file. "I could not tell whose DLL this is"
    // must never be spelled the same way as "it is ours".
    Child child;
    REQUIRE(StartHarness(child));

    ResetFake();
    g.modules = {"kernel32.dll"};
    g.scanSet = {child.pi.dwProcessId};
    g.payload = Fake::Payload::kCannotTell;

    const Verdict v = GuardedInjectWithSources(child.pi.dwProcessId, FL_OVERLAY_DLL, FakeSources());

    CHECK_FALSE(v.Allowed());
    CHECK(v.reason == Reason::kPayloadNotOurs);
    CHECK_FALSE(TargetHasModule(child.pi.dwProcessId, L"FrameLedger.Overlay.dll"));
}

TEST_CASE("a guard wired without the payload seam refuses", "[guard][inject][payload]") {
    // Sources members default to nullptr, so the failure mode of FORGETTING to
    // wire this must be a refusal rather than a load. Every other seam in this
    // file carries the same case for the same reason.
    Child child;
    REQUIRE(StartHarness(child));

    ResetFake();
    g.modules = {"kernel32.dll"};
    g.scanSet = {child.pi.dwProcessId};

    Sources s = FakeSources();
    s.PayloadIsOurOwn = nullptr;

    const Verdict v = GuardedInjectWithSources(child.pi.dwProcessId, FL_OVERLAY_DLL, s);

    CHECK_FALSE(v.Allowed());
    CHECK(v.reason == Reason::kPayloadNotOurs);
    CHECK_FALSE(TargetHasModule(child.pi.dwProcessId, L"FrameLedger.Overlay.dll"));
}

TEST_CASE("a blocked TARGET outranks a foreign payload", "[guard][inject][payload]") {
    // Ordering, asserted rather than left to reading order. Both objections are
    // real; the user must be told the one that matters — anti-cheat is in the
    // target — and not "your payload is wrong", which would send them looking at
    // their FrameLedger install for a problem in the game.
    ResetFake();
    g.modules = {"kernel32.dll", "BEClient_x64.dll"};
    g.scanSet = {GetCurrentProcessId()};
    g.payload = Fake::Payload::kForeign;

    const Verdict v = GuardedInjectWithSources(GetCurrentProcessId(), FL_OVERLAY_DLL, FakeSources());

    CHECK_FALSE(v.Allowed());
    CHECK(v.reason == Reason::kBlockedModule);
    CHECK(v.reason != Reason::kPayloadNotOurs);
}

TEST_CASE("the payload check can PASS — the staged Overlay is accepted", "[guard][inject][payload]") {
    // The green direction, against the real implementation. A gate that refuses
    // every input carries exactly as much information as one that accepts every
    // input, and this project has shipped both kinds. Asserted here without
    // injecting anything: the identity seam alone, called the way the guard
    // calls it.
    bool            ours = false;
    const Collected c = SystemSources().PayloadIsOurOwn(FL_OVERLAY_DLL, &ours);

    INFO("payload " << "FL_OVERLAY_DLL" << " must resolve into this binary's own directory");
    REQUIRE(c == Collected::kOk);
    CHECK(ours);

    // ...and the same call must say NO for the same file staged elsewhere.
    bool elsewhere = true;
    REQUIRE(SystemSources().PayloadIsOurOwn(FL_OVERLAY_DLL_ELSEWHERE, &elsewhere) == Collected::kOk);
    CHECK_FALSE(elsewhere);

    // A path that names nothing cannot be answered, and must not be "ours".
    bool absent = true;
    CHECK(SystemSources().PayloadIsOurOwn(LR"(C:\definitely\not\here.dll)", &absent) == Collected::kFailed);
    CHECK_FALSE(absent);

    // A directory is not a payload. FILE_READ_ATTRIBUTES without
    // FILE_FLAG_BACKUP_SEMANTICS cannot open one, which is what makes this
    // fail-closed rather than a separate test we would have to remember.
    wchar_t sys[MAX_PATH]{};
    REQUIRE(GetSystemDirectoryW(sys, MAX_PATH) != 0);
    bool dir = true;
    CHECK(SystemSources().PayloadIsOurOwn(sys, &dir) == Collected::kFailed);
    CHECK_FALSE(dir);

    // Null and empty are the caller getting it wrong, and get the same answer.
    bool n = true;
    CHECK(SystemSources().PayloadIsOurOwn(nullptr, &n) == Collected::kFailed);
    CHECK(SystemSources().PayloadIsOurOwn(L"", &n) == Collected::kFailed);
    CHECK(SystemSources().PayloadIsOurOwn(FL_OVERLAY_DLL, nullptr) == Collected::kFailed);
}

#endif    // FL_HARNESS_EXE && FL_OVERLAY_DLL

// ===========================================================================
// Check 4 — the static pre-scan (19_SAFETY item 4, 05_DETECTION §Anti-cheat
// pre-scan). ctest fl_prescan runs exactly this block by tag.
//
// This check was DECLARED and never implemented: kAntiCheatDirectory and
// kAntiCheatFile were named in ReasonName and mirrored into the managed enum
// while nothing produced either. Most of what follows is the fail-closed half,
// because the dangerous direction here is not "it refused" — it is "it came
// back clean without having looked".
// ===========================================================================
TEST_CASE("a blocklisted DIRECTORY beside the game is a hit", "[guard][prescan]") {
    ResetFake();
    g.dirEntries = {{"Binaries", true}, {"EasyAntiCheat", true}, {"game.exe", false}};

    const Verdict v = EvaluateWithSources(1234, FakeSources());
    REQUIRE_FALSE(v.Allowed());
    CHECK(v.reason == Reason::kAntiCheatDirectory);
    CHECK(std::strcmp(v.family, "Easy Anti-Cheat") == 0);
    CHECK(std::strcmp(v.signal, "EasyAntiCheat") == 0);
}

TEST_CASE("a blocklisted FILE beside the game is a hit, through a different group", "[guard][prescan]") {
    ResetFake();
    g.dirEntries = {{"game.exe", false}, {"x3.xem", false}};

    const Verdict v = EvaluateWithSources(1234, FakeSources());
    REQUIRE_FALSE(v.Allowed());
    CHECK(v.reason == Reason::kAntiCheatFile);
    CHECK(std::strcmp(v.family, "Xigncode3") == 0);
}

TEST_CASE("group membership is load-bearing: a directory named like a FILE entry is not a hit",
          "[guard][prescan][blocklist]") {
    // x3.xem is in `files`. Seeing a DIRECTORY of that name must not fire the
    // file rule, or a data edit could move one gate into the other silently.
    ResetFake();
    g.dirEntries = {{"x3.xem", true}};
    CHECK(EvaluateWithSources(1234, FakeSources()).Allowed());

    // ...and the converse: EasyAntiCheat is in `directories`, not `files`.
    ResetFake();
    g.dirEntries = {{"EasyAntiCheat", false}};
    CHECK(EvaluateWithSources(1234, FakeSources()).Allowed());
}

TEST_CASE("a clean game directory is allowed — the direction that can pass by accident", "[guard][prescan]") {
    // Without this, every case above would pass against a pre-scan that refuses
    // unconditionally, and the whole matrix would be decorative.
    ResetFake();
    g.dirEntries = {{"game.exe", false}, {"Binaries", true}, {"Content", true}, {"UnityPlayer.dll", false}};

    const Verdict v = EvaluateWithSources(1234, FakeSources());
    INFO("reason was " << ReasonName(v.reason) << " signal=" << v.signal);
    CHECK(v.Allowed());
}

TEST_CASE("pre-scan matching is case-insensitive in both directions", "[guard][prescan][blocklist]") {
    ResetFake();
    g.dirEntries = {{"easyanticheat", true}};
    CHECK(EvaluateWithSources(1234, FakeSources()).reason == Reason::kAntiCheatDirectory);

    ResetFake();
    g.dirEntries = {{"EASYANTICHEAT", true}};
    CHECK(EvaluateWithSources(1234, FakeSources()).reason == Reason::kAntiCheatDirectory);

    // A near miss must NOT fire, or case-insensitivity would be indistinguishable
    // from matching everything.
    ResetFake();
    g.dirEntries = {{"EasyAntiCheatery", true}};
    CHECK(EvaluateWithSources(1234, FakeSources()).Allowed());
}

TEST_CASE("a directory listing that FAILED is undetermined, not clean", "[guard][prescan][failclosed]") {
    ResetFake();
    g.dirEntries = {};
    g.dirEntriesResult = Collected::kFailed;

    const Verdict v = EvaluateWithSources(1234, FakeSources());
    REQUIRE_FALSE(v.Allowed());
    CHECK(v.reason == Reason::kPreScanFailed);
}

TEST_CASE("a TRUNCATED directory listing is undetermined, not clean", "[guard][prescan][failclosed]") {
    // The entry cap, the depth cap and an unfollowed reparse point all arrive
    // here as kIncomplete. An empty-but-incomplete listing is the exact shape
    // of this project's worst defect: it reads as "nothing found" when it means
    // "we did not finish looking".
    ResetFake();
    g.dirEntries = {{"game.exe", false}};
    g.dirEntriesResult = Collected::kIncomplete;

    const Verdict v = EvaluateWithSources(1234, FakeSources());
    REQUIRE_FALSE(v.Allowed());
    CHECK(v.reason == Reason::kPreScanFailed);
}

TEST_CASE("a hit still wins over a truncated listing", "[guard][prescan][failclosed]") {
    // Both refuse, but the reason the user is shown should be the one we are
    // sure about.
    ResetFake();
    g.dirEntries = {{"EasyAntiCheat", true}};
    g.dirEntriesResult = Collected::kIncomplete;

    const Verdict v = EvaluateWithSources(1234, FakeSources());
    REQUIRE_FALSE(v.Allowed());
    CHECK(v.reason == Reason::kAntiCheatDirectory);
    CHECK(std::strcmp(v.family, "Easy Anti-Cheat") == 0);
}

TEST_CASE("a target whose directory cannot be established is undetermined", "[guard][prescan][failclosed]") {
    ResetFake();
    g.dirEntries = {{"game.exe", false}};
    g.imageDirectoryResult = Collected::kFailed;

    const Verdict v = EvaluateWithSources(1234, FakeSources());
    REQUIRE_FALSE(v.Allowed());
    CHECK(v.reason == Reason::kPreScanFailed);
}

TEST_CASE("a missing pre-scan evidence source refuses", "[guard][prescan][failclosed]") {
    // The seam being absent must not mean the check quietly does not run. This
    // is what caught the wiring the first time it was built.
    ResetFake();
    Sources s = FakeSources();
    s.EnumerateDirEntries = nullptr;
    CHECK(EvaluateWithSources(1234, s).reason == Reason::kPreScanFailed);

    s = FakeSources();
    s.ImageDirectory = nullptr;
    CHECK(EvaluateWithSources(1234, s).reason == Reason::kPreScanFailed);
}

TEST_CASE("unusable rules make the pre-scan undetermined, never clean", "[guard][prescan][failclosed][rules]") {
    ResetFake();
    g.dirEntries = {{"EasyAntiCheat", true}};
    g.rulesReadable = false;
    CHECK(EvaluateWithSources(1234, FakeSources()).reason == Reason::kRulesUnreadable);

    ResetFake();
    g.dirEntries = {{"EasyAntiCheat", true}};
    g.rulesJson = "{ not json";
    CHECK(EvaluateWithSources(1234, FakeSources()).reason == Reason::kRulesMalformed);
}

TEST_CASE("the pre-scan uses the SAME matcher: removing a family stops it firing", "[guard][prescan][blocklist]") {
    // The claim "it reuses fl_ac_rules" is proved rather than reviewed. Drop a
    // family from the data and its directory hit must disappear — if it survived,
    // there would be a second matcher somewhere.
    //
    // It uses a family the FLOOR does not carry, and that is now the whole
    // difficulty. Removing `Easy Anti-Cheat` from the file no longer stops it
    // matching, because the generated floor carries the shipped blocklist and
    // data cannot shrink it — which is §S21 working, not a regression. Proving
    // "one matcher" therefore needs a family that exists only in the fixture.
    static constexpr const char* kFixtureOnlyDir = "ZzFixtureOnlyAcDir";

    std::string       withExtra = GoodRulesJson();
    const std::string dirs = R"("directories": [ { "family": "Easy Anti-Cheat", "values": ["EasyAntiCheat"] } ],)";
    const std::size_t at = withExtra.find(dirs);
    REQUIRE(at != std::string::npos);    // the fixture moved; this test is not testing what it thinks
    withExtra.replace(at, dirs.size(),
                      R"("directories": [ { "family": "Easy Anti-Cheat", "values": ["EasyAntiCheat"] },)"
                      R"( { "family": "Fixture Only", "values": ["ZzFixtureOnlyAcDir"] } ],)");

    ResetFake();
    g.dirEntries = {{kFixtureOnlyDir, true}};
    g.rulesJson = withExtra;
    REQUIRE(EvaluateWithSources(1234, FakeSources()).reason == Reason::kAntiCheatDirectory);

    // Same input, family removed from the data: the hit must be gone.
    ResetFake();
    g.dirEntries = {{kFixtureOnlyDir, true}};
    g.rulesJson = GoodRulesJson();
    CHECK(EvaluateWithSources(1234, FakeSources()).Allowed());
}

TEST_CASE("a family the FLOOR carries cannot be removed by editing the data", "[guard][prescan][floor]") {
    // The other direction, and the one §S21's narrow floor could not deliver.
    // Strip Easy Anti-Cheat from every group of the file and the directory hit
    // survives, because the floor is generated from the shipped blocklist.
    ResetFake();
    g.dirEntries = {{"EasyAntiCheat", true}};
    REQUIRE(EvaluateWithSources(1234, FakeSources()).reason == Reason::kAntiCheatDirectory);

    std::string       stripped = GoodRulesJson();
    const std::string dirs = R"("directories": [ { "family": "Easy Anti-Cheat", "values": ["EasyAntiCheat"] } ],)";
    const std::size_t at = stripped.find(dirs);
    REQUIRE(at != std::string::npos);
    stripped.replace(at, dirs.size(), R"("directories": [],)");

    ResetFake();
    g.dirEntries = {{"EasyAntiCheat", true}};
    g.rulesJson = stripped;
    const Verdict v = EvaluateWithSources(1234, FakeSources());
    INFO("reason was " << ReasonName(v.reason));
    REQUIRE_FALSE(v.Allowed());
    CHECK(v.reason == Reason::kAntiCheatDirectory);
}

TEST_CASE("the install root is resolved from the exe directory, not used as-is", "[guard][prescan]") {
    // MEASURED 2026-08-03. Lies of P puts its executable at
    // <root>\LiesofP\Binaries\Win64\, and the pre-scan saw seven files there —
    // none of which could ever have been an anti-cheat SDK, because
    // EasyAntiCheat/ sits at the install root. For exactly the layout most
    // likely to carry EAC, check 4 was looking in the wrong directory.
    wchar_t out[kMaxPreScanPathLen] = {};

    REQUIRE(ResolveInstallRoot(LR"(D:\SteamLibrary\steamapps\common\Lies of P\LiesofP\Binaries\Win64)", out,
                               kMaxPreScanPathLen));
    CHECK(std::wcscmp(out, LR"(D:\SteamLibrary\steamapps\common\Lies of P)") == 0);

    // A game already at its root stays there.
    REQUIRE(ResolveInstallRoot(LR"(D:\SteamLibrary\steamapps\common\Deadly Heart Gambit)", out, kMaxPreScanPathLen));
    CHECK(std::wcscmp(out, LR"(D:\SteamLibrary\steamapps\common\Deadly Heart Gambit)") == 0);

    // Unreal titles conventionally ship TWO executables — a shim at the install
    // root and the real shipping binary nested underneath (Lies of P has LOP.exe
    // and LOP-Win64-Shipping.exe). BOTH must resolve to the same place, or which
    // process the guard happens to be handed decides where check 4 looks.
    wchar_t viaShim[kMaxPreScanPathLen] = {};
    wchar_t viaShipping[kMaxPreScanPathLen] = {};
    REQUIRE(ResolveInstallRoot(LR"(D:\SteamLibrary\steamapps\common\Lies of P)", viaShim, kMaxPreScanPathLen));
    REQUIRE(ResolveInstallRoot(LR"(D:\SteamLibrary\steamapps\common\Lies of P\LiesofP\Binaries\Win64)", viaShipping,
                               kMaxPreScanPathLen));
    CHECK(std::wcscmp(viaShim, LR"(D:\SteamLibrary\steamapps\common\Lies of P)") == 0);
    CHECK(std::wcscmp(viaShim, viaShipping) == 0);

    // Case-insensitive, and forward slashes are separators too.
    REQUIRE(ResolveInstallRoot(LR"(D:/SteamLibrary/STEAMAPPS/Common/Title/Bin)", out, kMaxPreScanPathLen));
    CHECK(std::wcscmp(out, LR"(D:/SteamLibrary/STEAMAPPS/Common/Title)") == 0);

    REQUIRE(ResolveInstallRoot(LR"(C:\Program Files\Epic Games\SomeTitle\Sub\Dir)", out, kMaxPreScanPathLen));
    CHECK(std::wcscmp(out, LR"(C:\Program Files\Epic Games\SomeTitle)") == 0);
}

TEST_CASE("an unrecognised layout keeps the exe's own directory", "[guard][prescan][failclosed]") {
    // Walking up blindly is WORSE than staying put: one level above
    // D:\another\epic\AlanWake2 is a folder of unrelated games, and refusing
    // this title because a sibling ships anti-cheat is a false refusal with no
    // appeal — which is how a user goes looking for the override rule 2 says
    // does not exist.
    wchar_t out[kMaxPreScanPathLen] = {};

    REQUIRE(ResolveInstallRoot(LR"(D:\another\epic\AlanWake2)", out, kMaxPreScanPathLen));
    CHECK(std::wcscmp(out, LR"(D:\another\epic\AlanWake2)") == 0);

    // A boundary needs a child after it; a trailing `steamapps` is not one.
    REQUIRE(ResolveInstallRoot(LR"(D:\backup\steamapps)", out, kMaxPreScanPathLen));
    CHECK(std::wcscmp(out, LR"(D:\backup\steamapps)") == 0);

    // A result that does not fit is a refusal, never a truncation.
    wchar_t tiny[8] = {};
    CHECK_FALSE(ResolveInstallRoot(LR"(D:\SteamLibrary\steamapps\common\Some Title\Bin)", tiny, 8));
    CHECK_FALSE(ResolveInstallRoot(nullptr, out, kMaxPreScanPathLen));
}

TEST_CASE("the advisory pre-scan reaches the same verdict as the one inside the guard", "[guard][prescan]") {
    // FR-2.2's UI question. Same matcher, same polarity — the only difference is
    // that it takes a directory instead of a pid.
    ResetFake();
    g.dirEntries = {{"EasyAntiCheat", true}};
    const Verdict v = StaticPreScanWithSources(L"C:\\Games\\Example", FakeSources());
    CHECK(v.reason == Reason::kAntiCheatDirectory);

    ResetFake();
    g.dirEntries = {{"game.exe", false}};
    CHECK(StaticPreScanWithSources(L"C:\\Games\\Example", FakeSources()).Allowed());

    // A null directory is not an empty one.
    ResetFake();
    CHECK(StaticPreScanWithSources(nullptr, FakeSources()).reason == Reason::kPreScanFailed);
}

// ===========================================================================
// Check 2b against the REAL service control manager.
//
// The fakes cannot catch this one: what changed is what `present` MEANS, and
// the fake has always just echoed a list. Only the live implementation can be
// asked whether it distinguishes a running service from an installed one.
// ===========================================================================
TEST_CASE("a service that is installed but STOPPED is not present", "[guard][services]") {
    // MEASURED 2026-08-03: EasyAntiCheat_EOS is installed machine-wide by any
    // EOS title and sits Stopped/Manual until its own game runs. Reporting it as
    // present made the guard refuse EVERY process on the machine — explorer.exe
    // and steam.exe included — for a Unity indie game with no anti-cheat
    // anywhere in its install tree.
    //
    // 19_SAFETY on exactly this shape: "a gate that refuses everything is not a
    // strict gate but a broken one, and it is how a user ends up looking for the
    // override CLAUDE.md rule 2 says does not exist."
    const Sources s = SystemSources();
    REQUIRE(s.QueryService != nullptr);

    SC_HANDLE scm = OpenSCManagerA(nullptr, nullptr, SC_MANAGER_ENUMERATE_SERVICE);
    REQUIRE(scm != nullptr);

    DWORD needed = 0;
    DWORD returned = 0;
    DWORD resume = 0;
    EnumServicesStatusExA(scm, SC_ENUM_PROCESS_INFO, SERVICE_WIN32, SERVICE_STATE_ALL, nullptr, 0, &needed, &returned,
                          &resume, nullptr);
    std::vector<unsigned char> buf(needed + 1024);
    const BOOL ok = EnumServicesStatusExA(scm, SC_ENUM_PROCESS_INFO, SERVICE_WIN32, SERVICE_STATE_ALL, buf.data(),
                                          static_cast<DWORD>(buf.size()), &needed, &returned, &resume, nullptr);
    CloseServiceHandle(scm);
    REQUIRE(ok);

    const auto* entries = reinterpret_cast<const ENUM_SERVICE_STATUS_PROCESSA*>(buf.data());
    std::string running;
    std::string stopped;
    for (DWORD i = 0; i < returned && (running.empty() || stopped.empty()); ++i) {
        const DWORD state = entries[i].ServiceStatusProcess.dwCurrentState;
        if (state == SERVICE_RUNNING && running.empty()) {
            running = entries[i].lpServiceName;
        } else if (state == SERVICE_STOPPED && stopped.empty()) {
            stopped = entries[i].lpServiceName;
        }
    }

    // Deliberately NOT a skip. Every Windows install has both, and a machine
    // that somehow has neither would make this test meaningless rather than
    // inapplicable — which is the state that should be loud.
    INFO("running='" << running << "' stopped='" << stopped << "'");
    REQUIRE_FALSE(running.empty());
    REQUIRE_FALSE(stopped.empty());

    bool present = false;
    CHECK(s.QueryService(running.c_str(), &present) == Collected::kOk);
    CHECK(present);

    present = true;    // poison it, so a no-op implementation fails
    CHECK(s.QueryService(stopped.c_str(), &present) == Collected::kOk);
    CHECK_FALSE(present);

    // And absent is still absent — the third state, distinguishable from both.
    present = true;
    CHECK(s.QueryService("FrameLedgerDefinitelyNotAService", &present) == Collected::kOk);
    CHECK_FALSE(present);
}

// ===========================================================================
// The reason table. This is the gate that FlGuardReasonCount and the managed
// mirror both stand on.
// ===========================================================================
TEST_CASE("every Reason has a distinct name, and the count is derived", "[guard][mirror]") {
    // Reason::kCount replaced a static_assert that could not fire on the one
    // change it existed to catch: it pinned kRulesIncomplete == 16, and
    // kRulesIncomplete was the LAST enumerator, so appending a reason left it
    // at 16, the assert passed, FlGuardReasonCount stayed at 17, and the
    // managed mirror test iterated 0-16 and never compared the new value.
    const int count = static_cast<int>(Reason::kCount);
    CHECK(count > 0);
    CHECK(static_cast<int>(Reason::kAllow) == 0);

    // Naming is NOT compiler-enforced. C4061/C4062 are off by default even at
    // /W4 — verified by appending an enumerator with no case and watching the
    // build stay green — so ReasonName's exhaustiveness is checked here or
    // nowhere. An unnamed reason falls through to "Unknown".
    std::vector<std::string> seen;
    for (int i = 0; i < count; ++i) {
        const char* name = ReasonName(static_cast<Reason>(i));
        REQUIRE(name != nullptr);
        const std::string s = name;

        INFO("Reason " << i << " reported '" << s << "'");
        CHECK_FALSE(s.empty());
        CHECK(s != "Unknown");    // the fallthrough: this reason has no case label
        CHECK(std::find(seen.begin(), seen.end(), s) == seen.end());
        seen.push_back(s);
    }
    CHECK(seen.size() == static_cast<std::size_t>(count));
}
