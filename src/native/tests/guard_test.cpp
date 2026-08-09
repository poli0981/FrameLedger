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
          "nameFragments": ["anticheat", "antitamper", "guard", "protect"],
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
std::string RulesWithBlockedExecutable() {
    std::string       json = GoodRulesJson();
    const std::string needle = "\"blockedExecutables\": []";
    const std::size_t at = json.find(needle);
    if (at == std::string::npos) {
        return json;
    }
    json.replace(at, needle.size(),
                 R"("blockedExecutables": [{ "family": "Example Online", "match": "exact", )"
                 R"("values": ["ranked.exe"], "reason": "competitive online title" }])");
    return json;
}

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

    // Check 3's evidence. Defaults to a name no blocklist entry matches, so every
    // pre-existing case keeps its meaning: check 3 runs, looks, and allows.
    std::string imageFileName = "hook-harness.exe";
    Collected   imageFileNameResult = Collected::kOk;

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

    // §S18/§S22(b) — which MODULES the seam reports as FrameLedger's own, by base
    // name, and whether it can answer at all. Empty by default, so no existing
    // case is silently exempted from the fragment tier by adding this.
    //
    // Keyed on the module rather than the pid because that is what the guard now
    // asks. One entry covers every process that loads it, which is the point: the
    // old pid form needed the same DLL listed per-process and made the exemption
    // depend on where the HOST lived.
    std::set<std::string> ourModules;
    Collected             moduleIsOursResult = Collected::kOk;

    // Modules the enumerator reports WITHOUT a path. Not an enumeration failure —
    // the name still matches the blocklist — but an unlocatable module cannot be
    // exempted, so it must refuse.
    std::set<std::string> pathlessModules;

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

// Fixture module paths are synthesised from the base name, so a test says which
// modules exist and separately which of them are OURS — rather than every case
// having to spell a path it does not care about.
std::wstring FakeModulePath(const std::string& name) {
    std::wstring w = L"C:\\Fixture\\";
    for (char c : name) {
        w.push_back(static_cast<wchar_t>(static_cast<unsigned char>(c)));
    }
    return w;
}

Collected FakeEnumModules(std::uint32_t pid, ModuleSink sink, void* ctx) {
    const auto  it = g.modulesByPid.find(pid);
    const auto& list = (it != g.modulesByPid.end()) ? it->second : g.modules;
    for (const auto& m : list) {
        const std::wstring path = FakeModulePath(m);
        const bool         pathless = g.pathlessModules.count(m) != 0;
        if (!sink(ctx, m.c_str(), pathless ? nullptr : path.c_str())) {
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

Collected FakeImageFileName(std::uint32_t, char* out, std::size_t cap) {
    if (g.imageFileNameResult != Collected::kOk) {
        return g.imageFileNameResult;
    }
    if (g.imageFileName.size() + 1 > cap) {
        return Collected::kFailed;
    }
    strcpy_s(out, cap, g.imageFileName.c_str());
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

Collected FakeModuleIsOurOwn(const wchar_t* modulePath, bool* isOurs) {
    if (g.moduleIsOursResult != Collected::kOk) {
        // Writes TRUE and THEN fails, deliberately. A seam that touches the
        // out-param before giving up is the realistic shape, and it is the only
        // version of this fixture that can catch a caller which ignores the
        // return code — with the out-param left false, "cannot determine" and
        // "not ours" are indistinguishable and the test passes for the wrong
        // reason.
        if (isOurs != nullptr) {
            *isOurs = true;
        }
        return g.moduleIsOursResult;
    }
    if (isOurs == nullptr) {
        return Collected::kFailed;
    }
    // A null path is answered "OURS", deliberately, and this fixture is wrong on
    // purpose. The real seam returns kFailed here — so with a faithful fake, the
    // guard's own `modulePath == nullptr` clause is redundant and the test that
    // covers it passes whether the clause exists or not. Measured: removing the
    // clause left the suite green.
    //
    // Modelling a seam that mishandles null is what makes that clause
    // load-bearing, and the guard must not depend on every future implementation
    // of a seam being careful.
    if (modulePath == nullptr) {
        *isOurs = true;
        return Collected::kOk;
    }
    *isOurs = false;
    for (const auto& name : g.ourModules) {
        if (FakeModulePath(name) == modulePath) {
            *isOurs = true;
            break;
        }
    }
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
        // True, then fail — see FakeModuleIsOurOwn. Left false, this case would
        // pass against a caller that never looked at the return code.
        if (isOurs != nullptr) {
            *isOurs = true;
        }
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
    s.ImageFileName = &FakeImageFileName;
    s.EnumerateDirEntries = &FakeEnumDirEntries;
    s.ModuleIsOurOwn = &FakeModuleIsOurOwn;
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
        s.EnumerateModules = [](std::uint32_t pid, ModuleSink sink, void* ctx) -> Collected {
            seen.push_back(pid);
            sink(ctx, "kernel32.dll", L"C:\\Fixture\\kernel32.dll");
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
        //
        // Note what ONE entry now covers (§S22(b)): ownership is a property of
        // the module, so both processes are handled without naming either. The
        // pid form needed the DLL listed per-process AND made the answer depend
        // on where each host happened to live.
        g.modulesByPid[4000] = {"kernel32.dll", "FrameLedger.Guard.dll"};
        g.modulesByPid[4001] = {"kernel32.dll", "FrameLedger.Guard.dll"};
        g.ourModules = {"FrameLedger.Guard.dll"};

        const Verdict v = EvaluateWithSources(1234, FakeSources());
        INFO("reason was " << ReasonName(v.reason) << " signal=" << v.signal);
        CHECK(v.Allowed());
    }

    SECTION("the same module in the TARGET still refuses — the exception never covers it") {
        g.modulesByPid[1234] = {"kernel32.dll", "FrameLedger.Guard.dll"};
        g.ourModules = {"FrameLedger.Guard.dll"};    // even though the module really is ours

        const Verdict v = EvaluateWithSources(1234, FakeSources());
        REQUIRE_FALSE(v.Allowed());
        CHECK(v.reason == Reason::kSuspiciousUnsigned);
    }

    SECTION("a module that BORROWS the name is not exempt") {
        // Closes the spoofing route a name allowlist would have opened. The fake
        // resolves ownership by PATH, so a DLL carrying our name from somewhere
        // else answers "not ours" exactly as the real seam would.
        g.modulesByPid[4000] = {"FrameLedger.Guard.dll"};
        g.ourModules = {};

        const Verdict v = EvaluateWithSources(1234, FakeSources());
        REQUIRE_FALSE(v.Allowed());
        CHECK(v.reason == Reason::kSuspiciousUnsigned);
    }

    SECTION("a seam that CANNOT DETERMINE does not suppress") {
        g.modulesByPid[4000] = {"FrameLedger.Guard.dll"};
        g.ourModules = {"FrameLedger.Guard.dll"};
        g.moduleIsOursResult = Collected::kFailed;

        const Verdict v = EvaluateWithSources(1234, FakeSources());
        REQUIRE_FALSE(v.Allowed());
        CHECK(v.reason == Reason::kSuspiciousUnsigned);
    }

    SECTION("a MISSING seam does not suppress") {
        // Sources members default to nullptr, so forgetting to wire this has to
        // fail towards refusing rather than towards allowing.
        g.modulesByPid[4000] = {"FrameLedger.Guard.dll"};
        g.ourModules = {"FrameLedger.Guard.dll"};
        Sources s = FakeSources();
        s.ModuleIsOurOwn = nullptr;

        const Verdict v = EvaluateWithSources(1234, s);
        REQUIRE_FALSE(v.Allowed());
        CHECK(v.reason == Reason::kSuspiciousUnsigned);
    }

    SECTION("a module we could not LOCATE is not exempt") {
        // §S22(b)'s new failure mode. GetModuleFileNameExW can fail where
        // GetModuleBaseNameA succeeded, and the scan is not thereby incomplete —
        // the name still matched. What is lost is only the ability to exempt, so
        // the answer must be a refusal and not an allow.
        g.modulesByPid[4000] = {"FrameLedger.Guard.dll"};
        g.ourModules = {"FrameLedger.Guard.dll"};
        g.pathlessModules = {"FrameLedger.Guard.dll"};

        const Verdict v = EvaluateWithSources(1234, FakeSources());
        REQUIRE_FALSE(v.Allowed());
        CHECK(v.reason == Reason::kSuspiciousUnsigned);
        CHECK(std::strcmp(v.signal, "FrameLedger.Guard.dll") == 0);
    }

    SECTION("only the FUZZY tier is suppressed — an exact blocklist hit in our own process still refuses") {
        // The clause that keeps this a narrow exception rather than a hole. If
        // real anti-cheat is somehow loaded in a FrameLedger process, we are not
        // injecting into anything.
        g.modulesByPid[4000] = {"FrameLedger.Guard.dll", "EasyAntiCheat_x64.dll"};
        g.ourModules = {"FrameLedger.Guard.dll"};

        const Verdict v = EvaluateWithSources(1234, FakeSources());
        REQUIRE_FALSE(v.Allowed());
        CHECK(v.reason == Reason::kBlockedModule);
        CHECK(std::strcmp(v.family, "Easy Anti-Cheat") == 0);
    }

    SECTION("an unreadable process is still unreadable, exempt or not") {
        g.ourModules = {"FrameLedger.Guard.dll"};
        g.moduleResultByPid[4000] = Collected::kFailed;

        const Verdict v = EvaluateWithSources(1234, FakeSources());
        REQUIRE_FALSE(v.Allowed());
        CHECK(v.reason == Reason::kProcessUnreadable);
    }
}

// ===========================================================================
// §S22(b) — the fail-open that per-module suppression would have created.
//
// The old sink latched the FIRST fragment-matching module and stopped testing
// fragments. Harmless while every hit refused: the latched name only had to be
// *a* reason. The moment the exemption became per-module, that latch became a
// fail-open REACHABLE BY LOAD ORDER — our own DLL matches first, is exempted,
// and a genuinely suspicious module loaded afterwards is never recorded.
//
// §S19(b) predicted this and said the detection half had to be RESTRUCTURED,
// not extended. These two cases are what hold the restructure in place, and
// they are order-symmetric on purpose: a fix that only walked the list backwards
// would pass one and fail the other.
// ===========================================================================
TEST_CASE("§S22(b) — an exempt module never hides a suspicious one after it", "[guard][failclosed][S22]") {
    ResetFake();
    g.scanSet = {1234, 4000};
    g.modulesByPid[1234] = {"kernel32.dll"};
    g.ourModules = {"FrameLedger.Guard.dll"};

    // mskeyprotect.dll trips `protect` and is NOT an exact-blocklist family, so
    // it can only ever be caught by the fuzzy tier. It is also real: measured
    // loaded on this machine, from System32 (§S19(b)).
    SECTION("ours FIRST, suspicious SECOND — the classic load order") {
        g.modulesByPid[4000] = {"FrameLedger.Guard.dll", "mskeyprotect.dll"};

        const Verdict v = EvaluateWithSources(1234, FakeSources());
        INFO("reason was " << ReasonName(v.reason) << " signal=" << v.signal);
        REQUIRE_FALSE(v.Allowed());
        CHECK(v.reason == Reason::kSuspiciousUnsigned);
        CHECK(std::strcmp(v.signal, "mskeyprotect.dll") == 0);
    }

    SECTION("suspicious FIRST, ours SECOND — the same verdict, the same signal") {
        g.modulesByPid[4000] = {"mskeyprotect.dll", "FrameLedger.Guard.dll"};

        const Verdict v = EvaluateWithSources(1234, FakeSources());
        REQUIRE_FALSE(v.Allowed());
        CHECK(v.reason == Reason::kSuspiciousUnsigned);
        CHECK(std::strcmp(v.signal, "mskeyprotect.dll") == 0);
    }

    SECTION("the SAME module list allows once both are ours — ownership is the only variable") {
        // The green half, and it deliberately reuses the exact module list of the
        // first section so the only thing that changed is the seam's answer.
        // Without this an implementation that simply never exempts anything would
        // pass both cases above, and "the exemption still works" would be
        // untested — which is how §S18 shipped a gate that could not pass.
        g.modulesByPid[4000] = {"FrameLedger.Guard.dll", "mskeyprotect.dll"};
        g.ourModules = {"FrameLedger.Guard.dll", "mskeyprotect.dll"};

        const Verdict v = EvaluateWithSources(1234, FakeSources());
        INFO("reason was " << ReasonName(v.reason) << " signal=" << v.signal);
        CHECK(v.Allowed());
    }
}

TEST_CASE("§S22(b) — the real ModuleIsOurOwn answers both directions", "[guard][S18][S22][live]") {
    // The seam above is what makes the matrix forceable; this is what keeps the
    // seam honest about the machine. Without it the whole exception rests on a
    // fake agreeing with itself.
    //
    // It also asserts the property the exemption now depends on and the fakes
    // structurally cannot reach: that "ours" is decided by the file's directory,
    // so it holds for any host that loads the binary rather than only for a host
    // that happens to live beside it. That was §S22(b)'s entire defect.
    const Sources sys = SystemSources();
    REQUIRE(sys.ModuleIsOurOwn != nullptr);
    // One implementation behind both identity seams (fl_guard.h §TWO SEAMS). If
    // they ever diverge, they are two answers to one question.
    REQUIRE(sys.ModuleIsOurOwn == sys.PayloadIsOurOwn);

    SECTION("this test binary IS ours — the guard code is compiled into it") {
        wchar_t self[MAX_PATH]{};
        REQUIRE(GetModuleFileNameW(nullptr, self, MAX_PATH) != 0);

        bool            ours = false;
        const Collected c = sys.ModuleIsOurOwn(self, &ours);
        REQUIRE(c == Collected::kOk);
        CHECK(ours);
    }

    SECTION("a System32 module is NOT ours") {
        // The green case above passes against an implementation that returns
        // true unconditionally; this is the half that catches it.
        wchar_t sys32[MAX_PATH]{};
        REQUIRE(GetSystemDirectoryW(sys32, MAX_PATH) != 0);
        const std::wstring k32 = std::wstring(sys32) + L"\\kernel32.dll";

        // Seeded true so a seam that writes nothing cannot pass by accident.
        bool            ours = true;
        const Collected c = sys.ModuleIsOurOwn(k32.c_str(), &ours);
        REQUIRE(c == Collected::kOk);
        CHECK_FALSE(ours);
    }

    SECTION("a module that no longer exists cannot be exempted") {
        // A mapped module can be deleted from disk while still loaded. That must
        // be kFailed — "could not tell" — and never "ours".
        bool            ours = true;
        const Collected c = sys.ModuleIsOurOwn(LR"(C:\definitely\not\here.dll)", &ours);
        CHECK(c == Collected::kFailed);
        CHECK_FALSE(ours);
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

// The shared-memory contract the injected Overlay publishes. Included here rather
// than at the top of the file so it travels with the injection block it belongs
// to, which is the only part of this suite that has a live Overlay to inspect.
#include <fl_ring.h>
#include <fl_shm.h>
#include <vector>

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

TEST_CASE("the injected Overlay publishes a handshake the Agent can validate", "[guard][inject][shm]") {
    // End to end, through the real DLL: inject, then open Local\FrameLedger.Ring.<pid>
    // from THIS process and check every field 04_CAPTURE says the Agent validates
    // before attaching. Until this existed, FlShmHandshake had no producer at all
    // and the version handshake compared "" with "" forever.
    Child        child;
    std::wstring cmd = std::wstring(L"\"") + FL_HARNESS_EXE + L"\" --hold 30";
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    REQUIRE(CreateProcessW(FL_HARNESS_EXE, cmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si,
                           &child.pi));
    Sleep(800);
    REQUIRE(WaitForSingleObject(child.pi.hProcess, 0) == WAIT_TIMEOUT);

    ResetFake();
    g.modules = {"kernel32.dll"};
    g.scanSet = {child.pi.dwProcessId};
    const Verdict v = GuardedInjectWithSources(child.pi.dwProcessId, FL_OVERLAY_DLL, FakeSources());
    INFO("reason " << ReasonName(v.reason) << " signal=" << v.signal);
    REQUIRE(v.Allowed());

    // The init thread runs off the loader lock, so the mapping appears shortly
    // after LoadLibrary returns rather than during it. Poll instead of sleeping a
    // guessed amount: a fixed sleep is either flaky or slow, and on a loaded CI
    // runner it is both.
    wchar_t name[128]{};
    REQUIRE(_snwprintf_s(name, _TRUNCATE, L"Local\\FrameLedger.Ring.%lu", child.pi.dwProcessId) > 0);
    HANDLE mapping = nullptr;
    for (int i = 0; i < 100 && mapping == nullptr; ++i) {
        mapping = OpenFileMappingW(FILE_MAP_READ, FALSE, name);
        if (mapping == nullptr) {
            Sleep(50);
        }
    }
    REQUIRE(mapping != nullptr);

    const void* base = MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, 0);
    REQUIRE(base != nullptr);

    const auto* h =
        reinterpret_cast<const fl::FlShmHandshake*>(static_cast<const unsigned char*>(base) + FL_SHM_HANDSHAKE_OFFSET);
    const auto* st =
        reinterpret_cast<const fl::FlWriterState*>(static_cast<const unsigned char*>(base) + FL_SHM_WRITER_OFFSET);

    CHECK(h->layoutVersion == FL_SHM_LAYOUT_VERSION);
    CHECK(h->recordSize == sizeof(fl::FlFrameRecord));
    CHECK(h->capacity == FL_SHM_DEFAULT_CAPACITY);
    CHECK(h->pid == child.pi.dwProcessId);
    // The Overlay's half of the S23-1 comparison. The Agent's half is
    // FlGuardBuildId on FrameLedger.Guard.dll, exercised from the managed side
    // (ShmHandshakeValidatorTests) because fl_guard_abi.cpp is not compiled into
    // this binary -- it exists only in the shared library.
    //
    // WHAT NOTHING MEASURES, stated rather than implied: that the guard's id and
    // the Overlay's id are equal. They are equal BY CONSTRUCTION -- FL_BUILD_ID is
    // one INTERFACE compile definition on FrameLedger.Shm, set once per CMake
    // configure, and both targets link it -- so there is no code path that could
    // make them differ without changing the build. That is an argument, not a
    // measurement, and it is the right place to notice if someone ever gives
    // either target its own id.
    CHECK(std::strcmp(h->buildId, FL_BUILD_ID) == 0);
    CHECK(h->qpcEpoch != 0);
    // 0 = NOT YET KNOWN, and it must stay 0 until first present: at init we are
    // two steps before any graphics module is resolved, and a confident answer
    // about the wrong GPU is worse than an admission (#36).
    CHECK(h->adapterLuid == 0);

    // READY once the present hooks are installed. This assertion read
    // FL_STATUS_INIT in the slice that added the handshake, when nothing was
    // hooked and READY would have claimed a capture side that did not exist.
    for (int i = 0; i < 100 && st->status != fl::FL_STATUS_READY; ++i) {
        Sleep(50);
    }
    CHECK(st->status == fl::FL_STATUS_READY);

    // AND YET NO RECORDS, which is the trap worth keeping a test on.
    // hook-harness --hold presents 240 frames and THEN sleeps; we inject ~800 ms
    // later, by which time those frames are long gone. Hooks installed, target
    // alive, ring empty. Any acceptance criterion of the form "N presents -> N
    // records" written against --hold would be vacuous -- which is why
    // --hold-presenting exists and why the next case uses it.
    CHECK(st->writeIndex == 0);
    CHECK(st->faultCount == 0);

    UnmapViewOfFile(base);
    CloseHandle(mapping);
}

TEST_CASE("a D3D12 title is recorded as D3D12, not as the D3D11 we used to assume", "[guard][inject][shm]") {
    // One hook on the SHARED dxgi.dll class vtable catches D3D11 and D3D12 alike,
    // so the present call itself cannot tell them apart -- and `api` was
    // hardcoded to D3D11 until now, which was a guess written into a field
    // 03_METRICS consumes and 06_DATA_MODEL persists.
    //
    // The fixture presents through a swapchain created from a D3D12 COMMAND
    // QUEUE, which is the case that catches a naive QueryInterface for
    // ID3D12Device: CreateSwapChainForComposition takes the queue, so the device
    // interface is not what GetDevice returns.
    Child        child;
    std::wstring cmd = std::wstring(L"\"") + FL_HARNESS_EXE + L"\" --real --hold-presenting-d3d12 10";
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    REQUIRE(CreateProcessW(FL_HARNESS_EXE, cmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si,
                           &child.pi));
    Sleep(800);
    REQUIRE(WaitForSingleObject(child.pi.hProcess, 0) == WAIT_TIMEOUT);

    ResetFake();
    g.modules = {"kernel32.dll"};
    g.scanSet = {child.pi.dwProcessId};
    REQUIRE(GuardedInjectWithSources(child.pi.dwProcessId, FL_OVERLAY_DLL, FakeSources()).Allowed());

    wchar_t name[128]{};
    REQUIRE(_snwprintf_s(name, _TRUNCATE, L"Local\\FrameLedger.Ring.%lu", child.pi.dwProcessId) > 0);
    HANDLE mapping = nullptr;
    for (int i = 0; i < 100 && mapping == nullptr; ++i) {
        mapping = OpenFileMappingW(FILE_MAP_READ | FILE_MAP_WRITE, FALSE, name);
        if (mapping == nullptr) {
            Sleep(50);
        }
    }
    REQUIRE(mapping != nullptr);
    void* base = MapViewOfFile(mapping, FILE_MAP_READ | FILE_MAP_WRITE, 0, 0, 0);
    REQUIRE(base != nullptr);

    auto* st = reinterpret_cast<fl::FlWriterState*>(static_cast<unsigned char*>(base) + FL_SHM_WRITER_OFFSET);
    auto* ctl = reinterpret_cast<fl::FlControlBlock*>(static_cast<unsigned char*>(base) + FL_SHM_CONTROL_OFFSET);
    for (int i = 0; i < 100 && st->status != fl::FL_STATUS_READY; ++i) {
        Sleep(50);
    }
    REQUIRE(st->status == fl::FL_STATUS_READY);

    fl::RingReader rd;
    REQUIRE(rd.Init(base, FL_SHM_DEFAULT_CAPACITY));
    std::vector<fl::FlFrameRecord> all;
    for (int i = 0; i < 50 && all.size() < 30; ++i) {
        ++ctl->guardTicks;    // keep supervision alive; this test is not about the deadline
        fl::FlFrameRecord buf[256]{};
        const auto        r = rd.Drain(buf, 256);
        for (std::uint32_t k = 0; k < r.copied; ++k) {
            all.push_back(buf[k]);
        }
        Sleep(100);
    }
    REQUIRE(all.size() > 10);

    bool allD3D12 = true;
    for (const auto& rec : all) {
        if (rec.api != fl::FL_API_D3D12) {
            allD3D12 = false;
        }
    }
    CHECK(allD3D12);
    // apiMask records what was actually SEEN presenting, not what the process
    // loaded -- a title can link d3d12.dll and present through D3D11.
    CHECK((st->apiMask & (1u << fl::FL_API_D3D12)) != 0);
    CHECK((st->apiMask & (1u << fl::FL_API_D3D11)) == 0);

    UnmapViewOfFile(base);
    CloseHandle(mapping);
}

TEST_CASE("the Agent's safety stop halts recording within a frame", "[guard][inject][shm]") {
    // 19_SAFETY calls the mid-session stop "the single most important runtime
    // behavior in the whole capture layer", and legal/DISCLAIMER.md promises it
    // to the user. Until this case existed nothing drove it.
    //
    // Both directions, because a stop that was never shown to have been running
    // proves nothing: records must arrive BEFORE the flag and must cease AFTER.
    Child        child;
    std::wstring cmd = std::wstring(L"\"") + FL_HARNESS_EXE + L"\" --real --hold-presenting 14";
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    REQUIRE(CreateProcessW(FL_HARNESS_EXE, cmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si,
                           &child.pi));
    Sleep(800);
    REQUIRE(WaitForSingleObject(child.pi.hProcess, 0) == WAIT_TIMEOUT);

    ResetFake();
    g.modules = {"kernel32.dll"};
    g.scanSet = {child.pi.dwProcessId};
    REQUIRE(GuardedInjectWithSources(child.pi.dwProcessId, FL_OVERLAY_DLL, FakeSources()).Allowed());

    wchar_t name[128]{};
    REQUIRE(_snwprintf_s(name, _TRUNCATE, L"Local\\FrameLedger.Ring.%lu", child.pi.dwProcessId) > 0);
    HANDLE mapping = nullptr;
    for (int i = 0; i < 100 && mapping == nullptr; ++i) {
        mapping = OpenFileMappingW(FILE_MAP_READ | FILE_MAP_WRITE, FALSE, name);
        if (mapping == nullptr) {
            Sleep(50);
        }
    }
    REQUIRE(mapping != nullptr);
    // WRITE access: the control block is the Agent's half of the shared memory,
    // and this test is standing in for the Agent.
    void* base = MapViewOfFile(mapping, FILE_MAP_READ | FILE_MAP_WRITE, 0, 0, 0);
    REQUIRE(base != nullptr);

    auto* st = reinterpret_cast<fl::FlWriterState*>(static_cast<unsigned char*>(base) + FL_SHM_WRITER_OFFSET);
    auto* ctl = reinterpret_cast<fl::FlControlBlock*>(static_cast<unsigned char*>(base) + FL_SHM_CONTROL_OFFSET);

    for (int i = 0; i < 100 && st->status != fl::FL_STATUS_READY; ++i) {
        Sleep(50);
    }
    REQUIRE(st->status == fl::FL_STATUS_READY);

    // The supervision clock is running, so keep the guard looking alive while we
    // establish that recording works. Without this the 65 s deadline would be the
    // thing under test, which is a different case.
    std::uint64_t before = 0;
    for (int i = 0; i < 40 && before < 5; ++i) {
        ++ctl->guardTicks;
        Sleep(100);
        before = st->writeIndex;
    }
    REQUIRE(before > 5);    // it really was recording

    // THE STOP.
    ctl->unhookRequested = 1;
    for (int i = 0; i < 100 && st->status != fl::FL_STATUS_UNHOOKED; ++i) {
        Sleep(50);
    }
    CHECK(st->status == fl::FL_STATUS_UNHOOKED);

    // And it stays stopped. The harness is still presenting for several more
    // seconds, so an Overlay that merely reported the status while continuing to
    // write would be caught here.
    const std::uint64_t atStop = st->writeIndex;
    for (int i = 0; i < 10; ++i) {
        ++ctl->guardTicks;    // ticks resume: stopping is ONE-WAY, not a pause
        Sleep(100);
    }
    CHECK(st->writeIndex == atStop);

    UnmapViewOfFile(base);
    CloseHandle(mapping);
}

TEST_CASE("the injected Overlay records real presents into the ring", "[guard][inject][shm]") {
    // The end-to-end claim the whole hook layer exists for, against a target that
    // is PRESENTING WHILE WE INJECT. --hold cannot be used here: it presents 240
    // frames and then sleeps, and we inject ~800 ms later, so an Overlay in a
    // --hold target observes exactly zero. --hold-presenting exists for this.
    //
    // --real matters just as much: DXGI_PRESENT_TEST submits nothing, and an
    // acceptance criterion that counts presents against a stream of non-frames is
    // satisfiable only by a writer that counts non-frames (#35).
    Child        child;
    std::wstring cmd = std::wstring(L"\"") + FL_HARNESS_EXE + L"\" --real --hold-presenting 8";
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    REQUIRE(CreateProcessW(FL_HARNESS_EXE, cmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si,
                           &child.pi));
    Sleep(800);
    REQUIRE(WaitForSingleObject(child.pi.hProcess, 0) == WAIT_TIMEOUT);

    ResetFake();
    g.modules = {"kernel32.dll"};
    g.scanSet = {child.pi.dwProcessId};
    const Verdict v = GuardedInjectWithSources(child.pi.dwProcessId, FL_OVERLAY_DLL, FakeSources());
    INFO("reason " << ReasonName(v.reason) << " signal=" << v.signal);
    REQUIRE(v.Allowed());

    wchar_t name[128]{};
    REQUIRE(_snwprintf_s(name, _TRUNCATE, L"Local\\FrameLedger.Ring.%lu", child.pi.dwProcessId) > 0);
    HANDLE mapping = nullptr;
    for (int i = 0; i < 100 && mapping == nullptr; ++i) {
        mapping = OpenFileMappingW(FILE_MAP_READ, FALSE, name);
        if (mapping == nullptr) {
            Sleep(50);
        }
    }
    REQUIRE(mapping != nullptr);
    const void* base = MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, 0);
    REQUIRE(base != nullptr);

    const auto* st =
        reinterpret_cast<const fl::FlWriterState*>(static_cast<const unsigned char*>(base) + FL_SHM_WRITER_OFFSET);

    // READY means hooks are installed. Poll: hook installation happens on the
    // init thread after the mapping is published.
    for (int i = 0; i < 100 && st->status != fl::FL_STATUS_READY; ++i) {
        Sleep(50);
    }
    REQUIRE(st->status == fl::FL_STATUS_READY);

    fl::RingReader rd;
    REQUIRE(rd.Init(base, FL_SHM_DEFAULT_CAPACITY));

    std::vector<fl::FlFrameRecord> all;
    for (int i = 0; i < 60 && all.size() < 200; ++i) {
        fl::FlFrameRecord buf[256]{};
        const auto        r = rd.Drain(buf, 256);
        for (std::uint32_t k = 0; k < r.copied; ++k) {
            all.push_back(buf[k]);
        }
        CHECK(r.dropped == 0);    // 8192 slots against ~120/s: a drop means we stalled for seconds
        Sleep(100);
    }

    INFO("drained " << all.size() << " record(s)");
    REQUIRE(all.size() > 20);    // ~120/s for several seconds; 20 is a floor, not a target

    // N PRESENTS -> N RECORDS, in the only form this side can verify exactly:
    // frameIndex is assigned by the writer once per observed present, so a
    // contiguous run from 0 proves no present was double-counted and none was
    // dropped between the hook and the ring.
    for (std::size_t i = 0; i < all.size(); ++i) {
        REQUIRE(all[i].frameIndex == static_cast<std::uint32_t>(i));
    }

    bool timeMovesForward = true;
    bool honest = true;
    bool identified = true;
    for (std::size_t i = 0; i < all.size(); ++i) {
        if (i > 0 && all[i].qpc <= all[i - 1].qpc) {
            timeMovesForward = false;
        }
        // The honesty property: a present-only writer may claim the output size
        // and its own call arguments, and NOTHING else.
        //
        // Layout v3 changed how "nothing else" is spelled. rtFlags bits are now
        // *_OBSERVED, so 0 is the honest value and FL_MEASURED_RT staying clear
        // is what says nobody looked; the old opt-in FL_RT_NOT_MEASURED is
        // retired. The enums moved with it: FL_UPSCALER_NOT_REPORTED and
        // FL_FG_NOT_REPORTED are 0, so the zero-defaults no longer assert "no
        // upscaler, no frame generation" and are checked here as such.
        const uint16_t entitled = static_cast<uint16_t>(fl::FL_MEASURED_OUTPUT_RES | fl::FL_MEASURED_PRESENT_ARGS);
        if (all[i].measuredMask != entitled || all[i].rtFlags != 0 || all[i].upscaler != fl::FL_UPSCALER_NOT_REPORTED ||
            all[i].fgMode != fl::FL_FG_NOT_REPORTED || all[i].featureFlags != 0) {
            honest = false;
        }
        if (all[i].swapchainId == 0) {
            identified = false;
        }
        // The OTHER direction of the api resolution. Without this, a resolver
        // that always answered D3D12 would pass the D3D12 case and nothing would
        // notice -- the harness here presents through a D3D11 device.
        if (all[i].api != fl::FL_API_D3D11) {
            identified = false;
        }
    }
    CHECK(timeMovesForward);
    CHECK(honest);
    CHECK(identified);
    CHECK((st->apiMask & (1u << fl::FL_API_D3D11)) != 0);
    CHECK((st->apiMask & (1u << fl::FL_API_D3D12)) == 0);

    // Published at first present, never at init: our dummy device's adapter is
    // not the game's (#36).
    const auto* h =
        reinterpret_cast<const fl::FlShmHandshake*>(static_cast<const unsigned char*>(base) + FL_SHM_HANDSHAKE_OFFSET);
    CHECK(h->adapterLuid != 0);
    CHECK(st->faultCount == 0);

    UnmapViewOfFile(base);
    CloseHandle(mapping);
}

TEST_CASE("a writer with no output size claims none", "[guard][inject][shm]") {
    // §S29(g). The record used to set FL_MEASURED_OUTPUT_RES unconditionally, so
    // in the one path where the writer KNOWS it has no size it published
    // "output resolution MEASURED: 0 x 0" ~120 times a second. 03_METRICS
    // computes the upscale ratio as sqrt((outW*outH)/(renW*renH)) from exactly
    // those two fields, so that is not a harmless zero.
    //
    // The path: dllmain.cpp's FindOrAdd is a fixed 16-slot linear scan that
    // returns nullptr once they are taken, and RecordPresent then leaves
    // outputW/H at zero. --hold-presenting-overflow fills the table and then
    // presents on a swapchain that cannot get a slot; nothing in the harness
    // could reach that branch before, which is why the defect survived the
    // end-to-end test sitting directly above this one.
    //
    // BOTH DIRECTIONS ARE ALREADY HERE: the test above asserts the mask is
    // exactly FL_MEASURED_OUTPUT_RES on a normal target. This one asserts it is
    // exactly 0 on an overflowed one. A writer that always claimed, or never
    // claimed, fails one of the two.
    Child        child;
    std::wstring cmd = std::wstring(L"\"") + FL_HARNESS_EXE + L"\" --real --hold-presenting-overflow 8";
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    REQUIRE(CreateProcessW(FL_HARNESS_EXE, cmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si,
                           &child.pi));
    Sleep(800);
    REQUIRE(WaitForSingleObject(child.pi.hProcess, 0) == WAIT_TIMEOUT);

    ResetFake();
    g.modules = {"kernel32.dll"};
    g.scanSet = {child.pi.dwProcessId};
    REQUIRE(GuardedInjectWithSources(child.pi.dwProcessId, FL_OVERLAY_DLL, FakeSources()).Allowed());

    wchar_t name[128]{};
    REQUIRE(_snwprintf_s(name, _TRUNCATE, L"Local\\FrameLedger.Ring.%lu", child.pi.dwProcessId) > 0);
    HANDLE mapping = nullptr;
    for (int i = 0; i < 100 && mapping == nullptr; ++i) {
        mapping = OpenFileMappingW(FILE_MAP_READ, FALSE, name);
        if (mapping == nullptr) {
            Sleep(50);
        }
    }
    REQUIRE(mapping != nullptr);
    const void* base = MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, 0);
    REQUIRE(base != nullptr);

    const auto* st =
        reinterpret_cast<const fl::FlWriterState*>(static_cast<const unsigned char*>(base) + FL_SHM_WRITER_OFFSET);
    for (int i = 0; i < 100 && st->status != fl::FL_STATUS_READY; ++i) {
        Sleep(50);
    }
    REQUIRE(st->status == fl::FL_STATUS_READY);

    fl::RingReader rd;
    REQUIRE(rd.Init(base, FL_SHM_DEFAULT_CAPACITY));

    // The harness round-robins 17 swapchains, so the overflowed one is exactly
    // 1/17 of presents. Collect enough that its share is a meaningful count
    // rather than a handful: at ~1100 presents/s, 1000 records arrive in about a
    // second and ~59 of them are the stream under test.
    std::vector<fl::FlFrameRecord> all;
    for (int i = 0; i < 120 && all.size() < 1000; ++i) {
        fl::FlFrameRecord buf[512]{};
        const auto        r = rd.Drain(buf, 512);
        for (std::uint32_t k = 0; k < r.copied; ++k) {
            all.push_back(buf[k]);
        }
        Sleep(50);
    }

    // Select the overflowed stream by its own signature rather than by position:
    // id 0 is what fl_shm.h defines as "unidentified", and a chain that could not
    // get a slot is the only thing that can carry it.
    std::size_t overflowed = 0;
    for (const auto& rec : all) {
        if (rec.swapchainId == 0) {
            ++overflowed;
            // The claim under test. Not "the OUTPUT_RES bit is clear" -- the
            // whole mask, because a writer that swapped one wrong claim for
            // another would satisfy a narrower check. PRESENT_ARGS stays set:
            // this writer genuinely does have the present call's own arguments,
            // and losing that would be the opposite defect.
            CHECK(rec.measuredMask == fl::FL_MEASURED_PRESENT_ARGS);
            CHECK(rec.outputW == 0);
            CHECK(rec.outputH == 0);
            // Still honest about RT under v3's flipped polarity: no OBSERVED bit
            // set, and FL_MEASURED_RT clear so the Agent reads N/A rather than a
            // measured absence.
            CHECK(rec.rtFlags == 0);
            CHECK((rec.measuredMask & fl::FL_MEASURED_RT) == 0);
        }
    }

    INFO("drained " << all.size() << " record(s), " << overflowed << " from the overflowed swapchain");
    // WITHOUT THESE TWO THE LOOP ABOVE IS VACUOUS. If the harness failed to
    // overflow -- a creation failure, or a future kMaxSwapChains raised past 16
    // -- every record carries a real id, the loop body never runs, and every
    // CHECK inside it passes by never executing. That is the shape this suite has
    // been caught by before, and it caught the first version of this very test:
    // the harness filled its table BEFORE injection, so the Overlay saw an empty
    // one, and this assertion reported 0.
    //
    // An absolute floor alone is not enough. A harness that presented on ONE
    // chain for the whole hold would sail past it, so the second check pins the
    // SHARE: 17 chains round-robin means the overflowed stream is ~1/17 of
    // records, and half of that is the tolerance for drain timing.
    CHECK(overflowed > 30);
    CHECK(overflowed * 34 > all.size());
    CHECK(st->faultCount == 0);

    UnmapViewOfFile(base);
    CloseHandle(mapping);
}

TEST_CASE("occlusion probes are not frames and reach the ring as nothing", "[guard][inject][shm]") {
    // --hold-presenting WITHOUT --real: the harness presents continuously with
    // DXGI_PRESENT_TEST, which runs the presentation test and submits nothing
    // (main.cpp:790, `flags = real ? 0u : DXGI_PRESENT_TEST`).
    //
    // This is the fixture that once made an acceptance criterion vacuous: every
    // present in this harness used to be a probe, so "N presents -> N records"
    // was satisfiable ONLY by a writer that counts non-frames, and a correct
    // writer had no green path (#35). Now it is the negative control.
    //
    // The target is alive, hooked, READY and presenting hard. The ring must stay
    // empty -- not because nothing happened, but because none of it was a frame.
    Child        child;
    std::wstring cmd = std::wstring(L"\"") + FL_HARNESS_EXE + L"\" --hold-presenting 12";
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    REQUIRE(CreateProcessW(FL_HARNESS_EXE, cmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si,
                           &child.pi));
    Sleep(800);
    REQUIRE(WaitForSingleObject(child.pi.hProcess, 0) == WAIT_TIMEOUT);

    ResetFake();
    g.modules = {"kernel32.dll"};
    g.scanSet = {child.pi.dwProcessId};
    REQUIRE(GuardedInjectWithSources(child.pi.dwProcessId, FL_OVERLAY_DLL, FakeSources()).Allowed());

    wchar_t name[128]{};
    REQUIRE(_snwprintf_s(name, _TRUNCATE, L"Local\\FrameLedger.Ring.%lu", child.pi.dwProcessId) > 0);
    HANDLE mapping = nullptr;
    for (int i = 0; i < 100 && mapping == nullptr; ++i) {
        mapping = OpenFileMappingW(FILE_MAP_READ | FILE_MAP_WRITE, FALSE, name);
        if (mapping == nullptr) {
            Sleep(50);
        }
    }
    REQUIRE(mapping != nullptr);
    void* base = MapViewOfFile(mapping, FILE_MAP_READ | FILE_MAP_WRITE, 0, 0, 0);
    REQUIRE(base != nullptr);

    auto* st = reinterpret_cast<fl::FlWriterState*>(static_cast<unsigned char*>(base) + FL_SHM_WRITER_OFFSET);
    auto* ctl = reinterpret_cast<fl::FlControlBlock*>(static_cast<unsigned char*>(base) + FL_SHM_CONTROL_OFFSET);

    for (int i = 0; i < 100 && st->status != fl::FL_STATUS_READY; ++i) {
        Sleep(50);
    }
    REQUIRE(st->status == fl::FL_STATUS_READY);

    // Hold supervision alive so a stop cannot be the reason the ring is empty --
    // "no records because we unhooked" would pass this test for the wrong reason.
    for (int i = 0; i < 20; ++i) {
        ++ctl->guardTicks;
        Sleep(100);
    }

    CHECK(st->status == fl::FL_STATUS_READY);    // still observing
    CHECK(st->faultCount == 0);                  // the filter is a branch, not a fault
    CHECK(st->writeIndex == 0);                  // and not one probe became a frame

    UnmapViewOfFile(base);
    CloseHandle(mapping);
}

TEST_CASE("a PAUSED session records nothing, including across a guard tick", "[guard][inject][shm]") {
    // pauseRequested had exactly one reader and it was UNREACHABLE on any frame
    // where guardTicks had changed: the freshness check sat in front of it and
    // returned true as soon as the tick differed from the cached value. So the
    // first present after every guard evaluation was recorded regardless of
    // pause.
    //
    // "One record per 30 s" undersells it. That record's qpc is ~30 s after its
    // predecessor, which is a fabricated 30-second frame interval in the series
    // 03_METRICS computes 1% and 0.1% lows from -- the exact artefact 07_IPC
    // forbids for torn records.
    //
    // THIS TEST TICKS WHILE PAUSED ON PURPOSE. A version that paused and then
    // stopped ticking would pass against the broken code, because the leak only
    // happens on the frames where the tick CHANGES.
    Child        child;
    std::wstring cmd = std::wstring(L"\"") + FL_HARNESS_EXE + L"\" --real --hold-presenting 20";
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    REQUIRE(CreateProcessW(FL_HARNESS_EXE, cmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si,
                           &child.pi));
    Sleep(800);
    REQUIRE(WaitForSingleObject(child.pi.hProcess, 0) == WAIT_TIMEOUT);

    ResetFake();
    g.modules = {"kernel32.dll"};
    g.scanSet = {child.pi.dwProcessId};
    REQUIRE(GuardedInjectWithSources(child.pi.dwProcessId, FL_OVERLAY_DLL, FakeSources()).Allowed());

    wchar_t name[128]{};
    REQUIRE(_snwprintf_s(name, _TRUNCATE, L"Local\\FrameLedger.Ring.%lu", child.pi.dwProcessId) > 0);
    HANDLE mapping = nullptr;
    for (int i = 0; i < 100 && mapping == nullptr; ++i) {
        mapping = OpenFileMappingW(FILE_MAP_READ | FILE_MAP_WRITE, FALSE, name);
        if (mapping == nullptr) {
            Sleep(50);
        }
    }
    REQUIRE(mapping != nullptr);
    void* base = MapViewOfFile(mapping, FILE_MAP_READ | FILE_MAP_WRITE, 0, 0, 0);
    REQUIRE(base != nullptr);

    auto* st = reinterpret_cast<fl::FlWriterState*>(static_cast<unsigned char*>(base) + FL_SHM_WRITER_OFFSET);
    auto* ctl = reinterpret_cast<fl::FlControlBlock*>(static_cast<unsigned char*>(base) + FL_SHM_CONTROL_OFFSET);

    for (int i = 0; i < 100 && st->status != fl::FL_STATUS_READY; ++i) {
        Sleep(50);
    }
    REQUIRE(st->status == fl::FL_STATUS_READY);

    // GREEN DIRECTION FIRST: it really is recording. A pause test against a
    // capture side that was never writing proves nothing.
    std::uint64_t running = 0;
    for (int i = 0; i < 40 && running < 5; ++i) {
        ++ctl->guardTicks;
        Sleep(100);
        running = st->writeIndex;
    }
    REQUIRE(running > 5);

    // PAUSE, and keep the guard ticking. Every tick change is a frame on which
    // the old code returned early, before it ever looked at pauseRequested.
    ctl->pauseRequested = 1;
    Sleep(200);    // let any present already in flight land
    const std::uint64_t atPause = st->writeIndex;
    for (int i = 0; i < 12; ++i) {
        ++ctl->guardTicks;
        Sleep(100);
    }
    CHECK(st->writeIndex == atPause);
    // Still supervised and still hooked -- pausing is not stopping, and a pause
    // that quietly unhooked would pass the line above for the wrong reason.
    CHECK(st->status == fl::FL_STATUS_READY);

    // AND IT RESUMES. Pause is the one control-block signal that is NOT one-way,
    // so an implementation that latched it would satisfy every assertion above.
    ctl->pauseRequested = 0;
    std::uint64_t after = st->writeIndex;
    for (int i = 0; i < 40 && after <= atPause; ++i) {
        ++ctl->guardTicks;
        Sleep(100);
        after = st->writeIndex;
    }
    CHECK(after > atPause);

    UnmapViewOfFile(base);
    CloseHandle(mapping);
}

TEST_CASE("the safety stop fires in a target that has STOPPED presenting", "[guard][inject][shm]") {
    // The case the present path structurally cannot serve, and the reason the
    // watchdog exists. MayObserve has one caller (RecordPresent), which has two
    // (the Present and Present1 hooks) -- so with no presents, unhookRequested
    // was never read and the hooks stayed patched in for the life of the process.
    //
    // fl_shm.h says over FL_GUARD_TICK_DEADLINE_MS, in capitals, that this must
    // not be driven by the present hook, "because the clock would stop when
    // presents stop, which is the exact scenario this exists for -- a game that
    // has hung, or been alt-tabbed, while anti-cheat loads behind it". It was.
    //
    // --hold, NOT --hold-presenting: it presents 240 frames and then sleeps, so
    // by the time we inject ~800 ms later the presents are long over. That
    // property is already asserted one test above via writeIndex == 0, which is
    // what makes this fixture the right one rather than a hopeful one.
    Child        child;
    std::wstring cmd = std::wstring(L"\"") + FL_HARNESS_EXE + L"\" --hold 30";
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    REQUIRE(CreateProcessW(FL_HARNESS_EXE, cmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si,
                           &child.pi));
    Sleep(800);
    REQUIRE(WaitForSingleObject(child.pi.hProcess, 0) == WAIT_TIMEOUT);

    ResetFake();
    g.modules = {"kernel32.dll"};
    g.scanSet = {child.pi.dwProcessId};
    REQUIRE(GuardedInjectWithSources(child.pi.dwProcessId, FL_OVERLAY_DLL, FakeSources()).Allowed());

    wchar_t name[128]{};
    REQUIRE(_snwprintf_s(name, _TRUNCATE, L"Local\\FrameLedger.Ring.%lu", child.pi.dwProcessId) > 0);
    HANDLE mapping = nullptr;
    for (int i = 0; i < 100 && mapping == nullptr; ++i) {
        mapping = OpenFileMappingW(FILE_MAP_READ | FILE_MAP_WRITE, FALSE, name);
        if (mapping == nullptr) {
            Sleep(50);
        }
    }
    REQUIRE(mapping != nullptr);
    void* base = MapViewOfFile(mapping, FILE_MAP_READ | FILE_MAP_WRITE, 0, 0, 0);
    REQUIRE(base != nullptr);

    auto* st = reinterpret_cast<fl::FlWriterState*>(static_cast<unsigned char*>(base) + FL_SHM_WRITER_OFFSET);
    auto* ctl = reinterpret_cast<fl::FlControlBlock*>(static_cast<unsigned char*>(base) + FL_SHM_CONTROL_OFFSET);

    for (int i = 0; i < 100 && st->status != fl::FL_STATUS_READY; ++i) {
        Sleep(50);
    }
    REQUIRE(st->status == fl::FL_STATUS_READY);
    // The precondition that makes this case what it says it is: hooked, alive,
    // and NOT presenting. If a future harness change made --hold present for the
    // whole hold, this REQUIRE fails rather than the test silently becoming a
    // duplicate of the one that uses --hold-presenting.
    REQUIRE(st->writeIndex == 0);

    ctl->unhookRequested = 1;
    for (int i = 0; i < 100 && st->status != fl::FL_STATUS_UNHOOKED; ++i) {
        Sleep(100);
    }
    CHECK(st->status == fl::FL_STATUS_UNHOOKED);

    UnmapViewOfFile(base);
    CloseHandle(mapping);
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
TEST_CASE("check 3 refuses a blocked executable, by that reason and not another", "[guard][check3]") {
    // §S14: MatchesBlockedExecutable was implemented, tested, and asked by NOBODY --
    // EvaluateImpl ran LoadRules -> drivers -> services -> modules -> pre-scan and
    // stopped. Populating the data would have changed nothing, so "check 3 passed"
    // read as "this title is not a known online title" while nothing had looked.
    //
    // THE REASON IS ASSERTED SPECIFICALLY. "It refuses" is indistinguishable from
    // the four refusals the guard already makes, so a test that only checked
    // !Allowed() would pass against a build where check 3 still does not exist.
    ResetFake();
    g.rulesJson = RulesWithBlockedExecutable();
    g.imageFileName = "ranked.exe";

    const Verdict v = EvaluateWithSources(1234, FakeSources());
    REQUIRE_FALSE(v.Allowed());
    CHECK(v.reason == Reason::kBlockedExecutable);
    CHECK(std::string(v.family) == "Example Online");
}

TEST_CASE("check 3 matches case-insensitively, the way a real exe name arrives", "[guard][check3]") {
    ResetFake();
    g.rulesJson = RulesWithBlockedExecutable();
    g.imageFileName = "RANKED.EXE";

    CHECK(EvaluateWithSources(1234, FakeSources()).reason == Reason::kBlockedExecutable);
}

TEST_CASE("check 3 allows a title that is not on the list", "[guard][check3]") {
    // The GREEN direction, and it is not decoration: a check that refused every
    // target would satisfy the two cases above and be worthless.
    ResetFake();
    g.rulesJson = RulesWithBlockedExecutable();
    g.imageFileName = "singleplayer.exe";

    CHECK(EvaluateWithSources(1234, FakeSources()).Allowed());
}

TEST_CASE("an unnameable target refuses: unknown identity is never clean", "[guard][check3][failclosed]") {
    // §S14's second decision, and 19_SAFETY's wording: an unresolvable identity
    // "must read UNKNOWN, never clean". Both tri-state failures refuse.
    for (const Collected bad : {Collected::kFailed, Collected::kIncomplete}) {
        ResetFake();
        g.rulesJson = RulesWithBlockedExecutable();
        g.imageFileNameResult = bad;

        const Verdict v = EvaluateWithSources(1234, FakeSources());
        REQUIRE_FALSE(v.Allowed());
        CHECK(v.reason == Reason::kProcessUnreadable);
    }

    // And an empty name is the same state wearing a success code.
    ResetFake();
    g.rulesJson = RulesWithBlockedExecutable();
    g.imageFileName = "";
    CHECK(EvaluateWithSources(1234, FakeSources()).reason == Reason::kProcessUnreadable);
}

TEST_CASE("a null image-name seam refuses rather than skipping check 3", "[guard][check3][failclosed]") {
    // A seam nobody wired must not become a check nobody runs -- which is exactly
    // the state check 3 spent months in.
    ResetFake();
    g.rulesJson = RulesWithBlockedExecutable();
    g.imageFileName = "ranked.exe";

    Sources s = FakeSources();
    s.ImageFileName = nullptr;

    const Verdict v = EvaluateWithSources(1234, s);
    REQUIRE_FALSE(v.Allowed());
    CHECK(v.reason == Reason::kProcessUnreadable);
}

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

// ---------------------------------------------------------------------------
// The upscaler identity hook, end to end: injected Overlay, live target, real
// presents, a real evaluation before each one.
//
// WHAT THIS ADDS OVER ctest fl_upscaler_resolve. That probe proves the Overlay
// CAN FIND sl.interposer.dll!slEvaluateFeature, module-scoped, with a decoy
// present. It runs entirely in the harness process and never injects. Only this
// case proves the detour is INSTALLED and RUNS inside a target the Overlay was
// injected into, which is the difference between a resolver and a hook.
//
// THE LAZY-INSTALL VEHICLE IS UNDER TEST HERE TOO, deliberately. The Overlay
// resolves Streamline from its 1 Hz watchdog rather than at init, because a game
// loads the interposer when it creates its device, long after DllMain. The
// harness loads the stub at startup and injection happens ~800 ms later, so a
// watchdog that never re-tried, or that latched on ATTEMPT rather than on
// SUCCESS, would leave the hook uninstalled and every assertion below red.
// ---------------------------------------------------------------------------
namespace {

// Drain a live target until `want` records are seen or the budget expires.
//
// WAITS FOR THE STATE, bounded by a generous wall clock, never for a duration
// derived from the harness measured rate: the suite runs several assemblies in
// parallel, each spawning a harness and a WARP device, so the presenting thread
// is descheduled far longer than a rate-derived constant allows (HANDOFF
// §Traps).
std::vector<fl::FlFrameRecord> DrainAtLeast(fl::RingReader& rd, std::size_t want, int budgetMs) {
    std::vector<fl::FlFrameRecord> all;
    const ULONGLONG                until = GetTickCount64() + static_cast<ULONGLONG>(budgetMs);
    while (all.size() < want && GetTickCount64() < until) {
        fl::FlFrameRecord buf[256]{};
        const auto        r = rd.Drain(buf, 256);
        for (std::uint32_t k = 0; k < r.copied; ++k) {
            all.push_back(buf[k]);
        }
        if (r.copied == 0) {
            Sleep(50);
        }
    }
    return all;
}

// Throw away everything already in the ring, so a later drain judges only
// records written AFTER a state change the caller just waited for.
//
// NEEDED, and the first version of these tests did not do it: RingReader starts
// at the beginning of the ring, so a drain taken after the identity hook goes
// live still returns the ~70 records written BEFORE it. Those legitimately carry
// no upscaler -- the hook did not exist yet -- and judging them reported
// "identified 4 of 75", which reads like a broken hook and is really a reader
// looking at the wrong frames.
void DiscardBacklog(fl::RingReader& rd) {
    for (;;) {
        fl::FlFrameRecord buf[256]{};
        if (rd.Drain(buf, 256).copied == 0) {
            return;
        }
    }
}

// Wait for the watchdog to install the identity hook. POLLED, never read once:
// FL_STATUS_READY does not imply this hook exists, because the watchdog installs
// it on a later tick. A single read would race the very mechanism under test
// (HANDOFF §Traps: a test that reads a writer state ONCE is racing InitThread).
bool WaitForIdentityHook(const fl::FlWriterState* st) {
    for (int i = 0; i < 200; ++i) {
        std::atomic_ref<const uint32_t> hooks{st->hooksInstalledMask};
        if ((hooks.load(std::memory_order_acquire) & fl::FL_HOOK_UPSCALER_IDENTITY) != 0) {
            return true;
        }
        Sleep(50);
    }
    return false;
}

}    // namespace

TEST_CASE("the injected Overlay measures an upscaler it can identify", "[guard][inject][shm][upscaler]") {
    Child        child;
    std::wstring cmd = std::wstring(L"\"") + FL_HARNESS_EXE + L"\" --real --hold-presenting-upscaled 12";
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    REQUIRE(CreateProcessW(FL_HARNESS_EXE, cmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si,
                           &child.pi));
    Sleep(800);
    REQUIRE(WaitForSingleObject(child.pi.hProcess, 0) == WAIT_TIMEOUT);

    ResetFake();
    g.modules = {"kernel32.dll"};
    g.scanSet = {child.pi.dwProcessId};
    const Verdict v = GuardedInjectWithSources(child.pi.dwProcessId, FL_OVERLAY_DLL, FakeSources());
    INFO("reason " << ReasonName(v.reason) << " signal=" << v.signal);
    REQUIRE(v.Allowed());

    wchar_t name[128]{};
    REQUIRE(_snwprintf_s(name, _TRUNCATE, L"Local\\FrameLedger.Ring.%lu", child.pi.dwProcessId) > 0);
    HANDLE mapping = nullptr;
    for (int i = 0; i < 100 && mapping == nullptr; ++i) {
        mapping = OpenFileMappingW(FILE_MAP_READ, FALSE, name);
        if (mapping == nullptr) {
            Sleep(50);
        }
    }
    REQUIRE(mapping != nullptr);
    const void* base = MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, 0);
    REQUIRE(base != nullptr);

    const auto* st =
        reinterpret_cast<const fl::FlWriterState*>(static_cast<const unsigned char*>(base) + FL_SHM_WRITER_OFFSET);
    for (int i = 0; i < 100 && st->status != fl::FL_STATUS_READY; ++i) {
        Sleep(50);
    }
    REQUIRE(st->status == fl::FL_STATUS_READY);

    INFO("hooksInstalledMask=" << st->hooksInstalledMask);
    REQUIRE(WaitForIdentityHook(st));

    // hooksInstalledMask had NO PRODUCER anywhere in the tree before this PR,
    // not even the present bit, which the present hook was always entitled to.
    CHECK((st->hooksInstalledMask & fl::FL_HOOK_PRESENT) != 0u);
    // The split HANDOFF item 2 requires, demonstrated by one of the pair being
    // genuinely OFF: identity has a producer, params does not.
    CHECK((st->hooksInstalledMask & fl::FL_HOOK_UPSCALER_PARAMS) == 0u);

    fl::RingReader rd;
    REQUIRE(rd.Init(base, FL_SHM_DEFAULT_CAPACITY));
    // Records written BEFORE the hook went live legitimately carry no upscaler,
    // so judge a batch drained AFTER it rather than the whole session.
    DiscardBacklog(rd);
    const std::vector<fl::FlFrameRecord> all = DrainAtLeast(rd, 40, 8000);
    INFO("drained " << all.size() << " record(s) after the identity hook went live");
    REQUIRE(all.size() > 20);

    int identified = 0;
    int measured = 0;
    int claimedParams = 0;
    int claimedNone = 0;
    for (const auto& r : all) {
        if ((r.measuredMask & fl::FL_MEASURED_UPSCALER) != 0u) {
            ++measured;
        }
        if (r.upscaler == fl::FL_UPSCALER_DLSS) {
            ++identified;
        }
        if ((r.measuredMask & fl::FL_MEASURED_UPSCALER_PARAMS) != 0u) {
            ++claimedParams;
        }
        if (r.upscaler == fl::FL_UPSCALER_NONE) {
            ++claimedNone;
        }
    }

    // The hook is live, so every record drained after it may say so.
    CHECK(measured == static_cast<int>(all.size()));

    // One evaluation per present, so essentially every record should name DLSS.
    // A floor rather than equality: the drain can begin mid-frame and the first
    // record after installation may precede the first evaluation the detour saw.
    INFO("identified " << identified << " of " << all.size());
    CHECK(identified > static_cast<int>(all.size()) - 3);

    // NEVER `NONE`. A Streamline-only writer cannot see FSR, XeSS or NGX-direct,
    // so "we looked and there was none" is a claim it is not entitled to make,
    // and NONE is the only one of the three states fl_shm.h allows to be
    // aggregated as a negative.
    CHECK(claimedNone == 0);

    // The params bit has no producer, and saying so is the point: upscalerQuality
    // has no in-band "not measured" sentinel, because 0 is NGX MaxPerf, a real
    // preset. This bit is the only thing between an unhooked writer and
    // "DLSS Performance" published as a measurement.
    CHECK(claimedParams == 0);

    CHECK(st->faultCount == 0);

    UnmapViewOfFile(base);
    CloseHandle(mapping);
}

TEST_CASE("an upscaler the Overlay cannot identify is UNKNOWN, never NONE", "[guard][inject][shm][upscaler]") {
    // THE OTHER DIRECTION, and the one that makes the case above mean anything.
    // A writer that simply hardcoded FL_UPSCALER_DLSS would pass every assertion
    // there. Here the target evaluates feature id 0xF00D, which is not a
    // Streamline feature and which the Overlay must not pretend to recognise.
    //
    // fl_shm.h keeps three states apart and this is the middle one: UNKNOWN
    // (0xFF) means "a hook RAN and could not identify what it saw" -- N/A to the
    // user, but a DIFFERENT N/A from NOT_REPORTED, because it says our coverage
    // is short rather than that nobody looked.
    Child        child;
    std::wstring cmd = std::wstring(L"\"") + FL_HARNESS_EXE + L"\" --real --hold-presenting-upscaled-unknown 12";
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    REQUIRE(CreateProcessW(FL_HARNESS_EXE, cmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si,
                           &child.pi));
    Sleep(800);
    REQUIRE(WaitForSingleObject(child.pi.hProcess, 0) == WAIT_TIMEOUT);

    ResetFake();
    g.modules = {"kernel32.dll"};
    g.scanSet = {child.pi.dwProcessId};
    REQUIRE(GuardedInjectWithSources(child.pi.dwProcessId, FL_OVERLAY_DLL, FakeSources()).Allowed());

    wchar_t name[128]{};
    REQUIRE(_snwprintf_s(name, _TRUNCATE, L"Local\\FrameLedger.Ring.%lu", child.pi.dwProcessId) > 0);
    HANDLE mapping = nullptr;
    for (int i = 0; i < 100 && mapping == nullptr; ++i) {
        mapping = OpenFileMappingW(FILE_MAP_READ, FALSE, name);
        if (mapping == nullptr) {
            Sleep(50);
        }
    }
    REQUIRE(mapping != nullptr);
    const void* base = MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, 0);
    REQUIRE(base != nullptr);

    const auto* st =
        reinterpret_cast<const fl::FlWriterState*>(static_cast<const unsigned char*>(base) + FL_SHM_WRITER_OFFSET);
    REQUIRE(WaitForIdentityHook(st));

    fl::RingReader rd;
    REQUIRE(rd.Init(base, FL_SHM_DEFAULT_CAPACITY));
    DiscardBacklog(rd);
    const std::vector<fl::FlFrameRecord> all = DrainAtLeast(rd, 40, 8000);
    REQUIRE(all.size() > 20);

    int unknown = 0;
    int dlss = 0;
    int none = 0;
    int measured = 0;
    for (const auto& r : all) {
        if ((r.measuredMask & fl::FL_MEASURED_UPSCALER) != 0u) {
            ++measured;
        }
        if (r.upscaler == fl::FL_UPSCALER_UNKNOWN) {
            ++unknown;
        }
        if (r.upscaler == fl::FL_UPSCALER_DLSS) {
            ++dlss;
        }
        if (r.upscaler == fl::FL_UPSCALER_NONE) {
            ++none;
        }
    }
    CHECK(measured == static_cast<int>(all.size()));    // a hook ran, so the field may be read
    CHECK(unknown == static_cast<int>(all.size()));     // and what it says is "I could not tell"
    CHECK(dlss == 0);                                   // never invented
    CHECK(none == 0);                                   // and never a measured negative
    CHECK(st->faultCount == 0);

    UnmapViewOfFile(base);
    CloseHandle(mapping);
}
