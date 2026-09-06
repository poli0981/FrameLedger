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
    // Launch mode's fixture: after `moduleCallsBeforeLate` enumerations have answered
    // with `modules`, every later one answers with `modulesLate` (when non-empty).
    // The poll runs on the caller's thread, so a call count is what "later" means
    // and no second thread has to write into this struct.
    std::vector<std::string>           modulesLate;
    int                                moduleCallsBeforeLate = 0;
    int                                moduleCalls = 0;
    std::map<std::uint32_t, Collected> moduleResultByPid;
    Collected                          moduleResult = Collected::kOk;

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

    // §S19(b) — the signer seam: which modules verify, and as whom. A module absent
    // from the map has no valid embedded signature (kFailed), which is what an
    // unsigned DLL and a catalog-only system binary both look like to the embedded
    // half. Empty by default, so no existing case is silently trusted.
    std::map<std::string, std::string> signerByModule;
    Collected                          signerResult = Collected::kOk;

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
    const int   call = g.moduleCalls++;
    const auto  it = g.modulesByPid.find(pid);
    const bool  late = !g.modulesLate.empty() && call >= g.moduleCallsBeforeLate;
    const auto& list = (it != g.modulesByPid.end()) ? it->second : (late ? g.modulesLate : g.modules);
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

Collected FakeModuleSignerOrganisation(const wchar_t* modulePath, char* out, std::size_t cap) {
    if (g.signerResult != Collected::kOk) {
        // Writes a TRUSTED name and THEN fails — the same trap FakeModuleIsOurOwn
        // sets: a caller that ignores the return code and reads the buffer would
        // trust a module the seam could not verify.
        if (out != nullptr && cap > 0) {
            strcpy_s(out, cap, "Microsoft Corporation");
        }
        return g.signerResult;
    }
    if (out == nullptr || cap == 0) {
        return Collected::kFailed;
    }
    out[0] = '\0';
    if (modulePath == nullptr) {
        // As the ours fake: answer "trusted" for a null path, deliberately wrong, so
        // the guard's own null clause is load-bearing rather than redundant.
        strcpy_s(out, cap, "Microsoft Corporation");
        return Collected::kOk;
    }
    for (const auto& [name, org] : g.signerByModule) {
        if (FakeModulePath(name) == modulePath) {
            strcpy_s(out, cap, org.c_str());
            return Collected::kOk;
        }
    }
    return Collected::kFailed;
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
    s.ModuleSignerOrganisation = &FakeModuleSignerOrganisation;
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

TEST_CASE("a suspicious module name refuses when the signer seam cannot vouch for it", "[guard][heuristic]") {
    ResetFake();
    g.modules = {"kernel32.dll", "someantitamper64.dll"};    // not in signerByModule: no valid signature

    const Verdict v = EvaluateWithSources(1234, FakeSources());
    REQUIRE_FALSE(v.Allowed());
    CHECK(v.reason == Reason::kSuspiciousUnsigned);
}

// ===========================================================================
// §S19(b) — the signer half, wired 2026-09-06 on row G1.
//
// 19_SAFETY's heuristic is "name fragment AND not signed by a known vendor". The
// fragment half has refused alone since the guard was written; this is the other
// conjunct. Most of the cases below are about what it must NOT do, because a
// clause that can turn a refusal into an allow is a clause that can be reached
// by the wrong module.
// ===========================================================================
TEST_CASE("§S19(b) — the compiled-in bound: the rules file may only INTERSECT it", "[guard][heuristic][S19]") {
    ResetFake();
    Rules rules;
    REQUIRE(ParseRules(g.rulesJson.data(), g.rulesJson.size(), rules) == ParseResult::kOk);

    // On both lists: trusted.
    CHECK(IsCompiledTrustedSigner("Microsoft Corporation"));
    CHECK(IsTrustedSigner(rules, "Microsoft Corporation"));

    // On the compiled list, NOT in this rules file (the fixture names three): the
    // data may narrow, so it is not trusted here.
    CHECK(IsCompiledTrustedSigner("Intel Corporation"));
    CHECK_FALSE(IsTrustedSigner(rules, "Intel Corporation"));

    // In the rules file, NOT on the compiled list: the data may not widen. This is
    // the owner's decision of 2026-09-06 in one assertion.
    std::string       widened = g.rulesJson;
    const std::string needle = "\"Valve Corp.\"";
    const std::size_t at = widened.find(needle);
    REQUIRE(at != std::string::npos);
    widened.replace(at, needle.size(), "\"Valve Corp.\", \"Evil Corp\"");
    Rules wide;
    REQUIRE(ParseRules(widened.data(), widened.size(), wide) == ParseResult::kOk);
    CHECK_FALSE(IsCompiledTrustedSigner("Evil Corp"));
    CHECK_FALSE(IsTrustedSigner(wide, "Evil Corp"));
    CHECK(IsTrustedSigner(wide, "Valve Corp."));

    // Never a substring, never a prefix, never empty.
    CHECK_FALSE(IsCompiledTrustedSigner("Microsoft"));
    CHECK_FALSE(IsCompiledTrustedSigner("NVIDIA Corporation Ltd"));
    CHECK_FALSE(IsCompiledTrustedSigner(""));
    CHECK_FALSE(IsCompiledTrustedSigner(nullptr));
}

TEST_CASE(
    "§S19(b) — the fragment tier is suppressed for a module signed by a trusted organisation, and for nothing else",
    "[guard][failclosed][S19]") {
    ResetFake();
    g.scanSet = {1234, 4000};

    SECTION("a Microsoft-signed fragment module in an ANCESTOR -> Allow (the CI blocker's shape)") {
        g.modulesByPid[1234] = {"kernel32.dll", "d3d11.dll"};
        g.modulesByPid[4000] = {"kernel32.dll", "System.Security.Cryptography.ProtectedData.dll"};
        g.signerByModule["System.Security.Cryptography.ProtectedData.dll"] = "Microsoft Corporation";

        const Verdict v = EvaluateWithSources(1234, FakeSources());
        INFO("reason was " << ReasonName(v.reason) << " signal=" << v.signal);
        CHECK(v.Allowed());
    }

    SECTION("a Microsoft-signed fragment module in the TARGET -> Allow, unlike the ours exemption") {
        // A game loading a signed key-protection provider is the false refusal
        // this half exists to remove, so the target is NOT excluded here.
        g.modulesByPid[1234] = {"kernel32.dll", "mskeyprotect.dll"};
        g.signerByModule["mskeyprotect.dll"] = "Microsoft Corporation";

        const Verdict v = EvaluateWithSources(1234, FakeSources());
        INFO("reason was " << ReasonName(v.reason) << " signal=" << v.signal);
        CHECK(v.Allowed());
    }

    SECTION("signed by an organisation the rules name but the binary does not -> refuse") {
        std::string       widened = g.rulesJson;
        const std::string needle = "\"Valve Corp.\"";
        widened.replace(widened.find(needle), needle.size(), "\"Valve Corp.\", \"Evil Corp\"");
        g.rulesJson = widened;
        g.modulesByPid[4000] = {"evilprotect.dll"};
        g.signerByModule["evilprotect.dll"] = "Evil Corp";

        const Verdict v = EvaluateWithSources(1234, FakeSources());
        REQUIRE_FALSE(v.Allowed());
        CHECK(v.reason == Reason::kSuspiciousUnsigned);
    }

    SECTION("signed by an organisation on the compiled list but NOT in this rules file -> refuse") {
        g.modulesByPid[4000] = {"intelprotect.dll"};
        g.signerByModule["intelprotect.dll"] = "Intel Corporation";    // the fixture rules name three, not five

        const Verdict v = EvaluateWithSources(1234, FakeSources());
        REQUIRE_FALSE(v.Allowed());
        CHECK(v.reason == Reason::kSuspiciousUnsigned);
    }

    SECTION("a seam that CANNOT VERIFY does not suppress, even when it wrote a trusted name") {
        g.modulesByPid[4000] = {"mskeyprotect.dll"};
        g.signerByModule["mskeyprotect.dll"] = "Microsoft Corporation";
        g.signerResult = Collected::kFailed;

        const Verdict v = EvaluateWithSources(1234, FakeSources());
        REQUIRE_FALSE(v.Allowed());
        CHECK(v.reason == Reason::kSuspiciousUnsigned);
    }

    SECTION("a MISSING seam does not suppress") {
        g.modulesByPid[4000] = {"mskeyprotect.dll"};
        g.signerByModule["mskeyprotect.dll"] = "Microsoft Corporation";
        Sources s = FakeSources();
        s.ModuleSignerOrganisation = nullptr;

        const Verdict v = EvaluateWithSources(1234, s);
        REQUIRE_FALSE(v.Allowed());
        CHECK(v.reason == Reason::kSuspiciousUnsigned);
    }

    SECTION("a module we could not LOCATE is not verified") {
        g.modulesByPid[4000] = {"mskeyprotect.dll"};
        g.signerByModule["mskeyprotect.dll"] = "Microsoft Corporation";
        g.pathlessModules = {"mskeyprotect.dll"};

        const Verdict v = EvaluateWithSources(1234, FakeSources());
        REQUIRE_FALSE(v.Allowed());
        CHECK(v.reason == Reason::kSuspiciousUnsigned);
    }

    SECTION("LOAD ORDER: a trusted fragment module first, an unsigned one second -> refuse, naming the second") {
        // The fail-open §S19(b) predicted for a latch-and-skip sink. The trusted
        // module returns KEEP LOOKING, so the unsigned one after it is still seen.
        g.modulesByPid[4000] = {"mskeyprotect.dll", "shadyprotect.dll"};
        g.signerByModule["mskeyprotect.dll"] = "Microsoft Corporation";

        const Verdict v = EvaluateWithSources(1234, FakeSources());
        REQUIRE_FALSE(v.Allowed());
        CHECK(v.reason == Reason::kSuspiciousUnsigned);
        CHECK(std::strcmp(v.signal, "shadyprotect.dll") == 0);
    }

    SECTION("only the FUZZY tier is suppressed — an EXACT blocklist hit stays a hit whoever signed it") {
        g.modulesByPid[4000] = {"EasyAntiCheat.dll"};
        g.signerByModule["EasyAntiCheat.dll"] = "Microsoft Corporation";

        const Verdict v = EvaluateWithSources(1234, FakeSources());
        REQUIRE_FALSE(v.Allowed());
        CHECK(v.reason == Reason::kBlockedModule);
    }
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
// The compiled floor, so the early-stop case computes the family index the Overlay
// publishes from the same table rather than asserting a literal.
#include <fl_ac_floor.generated.h>
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

// What a writer carrying `hooks` is entitled to claim, and whether a record stayed
// inside it.
//
// DERIVED FROM hooksInstalledMask, NOT HARDCODED, and that is the whole change. The
// assertion below used to compare measuredMask against the constant
// `FL_MEASURED_OUTPUT_RES | FL_MEASURED_PRESENT_ARGS` with `!=` -- which was exactly
// right while the Overlay hooked only presents, and states a fact about ONE BUILD
// rather than about honesty. The managed side (MeasuredFacts.EntitledBy) was already
// derived and already a SUBSET test; this side had drifted into an equality that a
// correct writer fails the moment any feature bit is per-frame rather than per-session
// -- FL_MEASURED_UPSCALER_PARAMS is gated on `seen != 0` (dllmain.cpp:1053), so under
// frame generation three records in four legitimately lack it.
//
// TWO COPIES OF ONE CONTRACT, deliberately, and NOTHING GATES THEIR AGREEMENT. The
// struct mirror has fl-layout-dump; this does not. Stated here rather than left to be
// discovered: a change to MeasuredFacts.EntitledBy must be made here too.
uint16_t EntitledBy(uint32_t hooks) noexcept {
    uint16_t allowed = 0;
    if ((hooks & fl::FL_HOOK_PRESENT) != 0u) {
        allowed |= static_cast<uint16_t>(fl::FL_MEASURED_OUTPUT_RES | fl::FL_MEASURED_PRESENT_ARGS |
                                         fl::FL_MEASURED_DXGI_PRESENTS);
    }
    if ((hooks & fl::FL_HOOK_UPSCALER_IDENTITY) != 0u) {
        allowed |= static_cast<uint16_t>(fl::FL_MEASURED_UPSCALER | fl::FL_MEASURED_FG);
    }
    if ((hooks & fl::FL_HOOK_UPSCALER_PARAMS) != 0u) {
        allowed |= static_cast<uint16_t>(fl::FL_MEASURED_UPSCALER_PARAMS);
    }
    if ((hooks & fl::FL_HOOK_FG_EVALUATIONS) != 0u) {
        allowed |= static_cast<uint16_t>(fl::FL_MEASURED_FG_COUNTS);
    }
    if ((hooks & (fl::FL_HOOK_RT_DISPATCH | fl::FL_HOOK_RT_AS_BUILD | fl::FL_HOOK_RT_PSO)) != 0u) {
        allowed |= static_cast<uint16_t>(fl::FL_MEASURED_RT);
    }
    if ((hooks & fl::FL_HOOK_PSO) != 0u) {
        allowed |= static_cast<uint16_t>(fl::FL_MEASURED_PSO);
    }
    if ((hooks & fl::FL_HOOK_COLOR_SPACE) != 0u) {
        allowed |= static_cast<uint16_t>(fl::FL_MEASURED_HDR);
    }
    if ((hooks & fl::FL_HOOK_VRAM) != 0u) {
        allowed |= static_cast<uint16_t>(fl::FL_MEASURED_VRAM);
    }
    if ((hooks & fl::FL_HOOK_REFLEX) != 0u) {
        allowed |= static_cast<uint16_t>(fl::FL_MEASURED_LATENCY);
    }
    return allowed;
}

// BOTH DIRECTIONS, because a mask bit with no hook behind it and a VALUE with no mask
// bit behind it are the same defect seen from two sides. Layout v3 makes the zero of
// every enum "nobody said", so the second list is what a writer publishes when it
// forgets.
bool IsHonest(const fl::FlFrameRecord& r, uint16_t entitled) noexcept {
    if ((r.measuredMask & static_cast<uint16_t>(~entitled)) != 0u) {
        return false;
    }
    if ((r.measuredMask & fl::FL_MEASURED_UPSCALER) == 0u && r.upscaler != fl::FL_UPSCALER_NOT_REPORTED) {
        return false;
    }
    if ((r.measuredMask & fl::FL_MEASURED_FG) == 0u && r.fgMode != fl::FL_FG_NOT_REPORTED) {
        return false;
    }
    if ((r.measuredMask & fl::FL_MEASURED_FG_COUNTS) == 0u && r.fgEvaluations != 0u) {
        return false;
    }
    // dxgiUnseen has no in-band sentinel either -- 0 is DXGI agreeing with the hook -- so
    // a value under a clear bit is the same defect as an unclaimed count.
    if ((r.measuredMask & fl::FL_MEASURED_DXGI_PRESENTS) == 0u && r.dxgiUnseen != 0u) {
        return false;
    }
    // featureFlags carries Ray Reconstruction's fact and OBSERVED bits, produced by the
    // same Streamline detour as the upscaler identity. A writer not entitled to claim
    // an upscaler is not entitled to say anything about RR either.
    if ((entitled & fl::FL_MEASURED_UPSCALER) == 0u && r.featureFlags != 0u) {
        return false;
    }
    // BOTH RT FIELDS, not just the flags. dispatchRaysVolume has no in-band sentinel --
    // 0 is a real measurement of a frame that recorded no dispatch -- so only the mask
    // bit can say whether anyone looked, exactly as with fgEvaluations. Kept in step
    // with the managed twin in MeasuredFacts.IsHonest, which nothing gates.
    return (r.measuredMask & fl::FL_MEASURED_RT) != 0u || (r.rtFlags == 0u && r.dispatchRaysVolume == 0u);
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

// §S19(b) against the REAL seams. The fakes above prove the decision; these two
// prove the seam: a real Authenticode verification of a real file the real
// module walk reported, in a process the real injection targets.
namespace {

// The CI blocker itself, where the test host's copy is staged from (the NuGet cache).
// fl-probe-signer looks in the same place, for the same reason.
std::wstring FindProtectedDataDll() {
    wchar_t profile[MAX_PATH]{};
    if (GetEnvironmentVariableW(L"USERPROFILE", profile, MAX_PATH) == 0) {
        return {};
    }
    const std::wstring root = std::wstring(profile) + L"\\.nuget\\packages\\system.security.cryptography.protecteddata";
    WIN32_FIND_DATAW   fd{};
    HANDLE             h = FindFirstFileW((root + L"\\*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) {
        return {};
    }
    std::wstring best;
    do {
        if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0 || fd.cFileName[0] == L'.') {
            continue;
        }
        for (const wchar_t* under : {L"\\runtimes\\win\\lib\\net6.0\\", L"\\lib\\net6.0\\"}) {
            const std::wstring c =
                root + L"\\" + fd.cFileName + under + L"System.Security.Cryptography.ProtectedData.dll";
            if (GetFileAttributesW(c.c_str()) != INVALID_FILE_ATTRIBUTES) {
                best = c;
            }
        }
    } while (FindNextFileW(h, &fd) != 0);
    FindClose(h);
    return best;
}

// Real module walk and real signer, fake everything else: the scan set is the
// child alone, the rules are the fixture's, and the payload check is real (kReal).
Sources RealModulesAndSigner() {
    Sources       s = FakeSources();
    const Sources real = SystemSources();
    s.EnumerateModules = real.EnumerateModules;
    s.ModuleSignerOrganisation = real.ModuleSignerOrganisation;
    s.ModuleIsOurOwn = real.ModuleIsOurOwn;
    return s;
}

bool StartHarnessLoading(Child& child, const std::wstring& dll) {
    std::wstring cmd = std::wstring(L"\"") + FL_HARNESS_EXE + L"\" --load \"" + dll + L"\" --hold 30";
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

TEST_CASE("§S19(b), real seams — a target that loaded the Microsoft-signed CI blocker is ALLOWED",
          "[guard][inject][S19]") {
    const std::wstring dll = FindProtectedDataDll();
    if (dll.empty()) {
        // The NuGet cache is what stages it; a machine that never restored the test
        // SDK has no copy. Loud, not green: the managed integration cases are the
        // gate that cannot skip this.
        SKIP("System.Security.Cryptography.ProtectedData.dll not in the NuGet cache on this machine");
    }

    Child child;
    REQUIRE(StartHarnessLoading(child, dll));
    REQUIRE(TargetHasModule(child.pi.dwProcessId, L"System.Security.Cryptography.ProtectedData.dll"));

    ResetFake();
    g.scanSet = {child.pi.dwProcessId};
    const Verdict v = GuardedInjectWithSources(child.pi.dwProcessId, FL_OVERLAY_DLL, RealModulesAndSigner());
    INFO("reason " << ReasonName(v.reason) << " signal=" << v.signal);
    REQUIRE(v.Allowed());
    CHECK(TargetHasModule(child.pi.dwProcessId, L"FrameLedger.Overlay.dll"));
}

TEST_CASE("§S19(b), real seams — a target that loaded an UNSIGNED fragment module is REFUSED, naming it",
          "[guard][inject][S19]") {
    // The negative: our own unsigned Overlay, copied under a fragment-bearing name to
    // a directory that is not the guard's — unsigned (CLAUDE.md rule 9), `protect`
    // in the name, and not ours by file id. Every clause of the heuristic, true.
    wchar_t temp[MAX_PATH]{};
    REQUIRE(GetTempPathW(MAX_PATH, temp) != 0);
    const std::wstring copy = std::wstring(temp) + L"fl_s19_unsigned_protect_fixture.dll";
    REQUIRE(CopyFileW(FL_OVERLAY_DLL, copy.c_str(), FALSE));

    {
        Child child;
        REQUIRE(StartHarnessLoading(child, copy));
        REQUIRE(TargetHasModule(child.pi.dwProcessId, L"fl_s19_unsigned_protect_fixture.dll"));

        ResetFake();
        g.scanSet = {child.pi.dwProcessId};
        const Verdict v = GuardedInjectWithSources(child.pi.dwProcessId, FL_OVERLAY_DLL, RealModulesAndSigner());
        INFO("reason " << ReasonName(v.reason) << " signal=" << v.signal);
        REQUIRE_FALSE(v.Allowed());
        CHECK(v.reason == Reason::kSuspiciousUnsigned);
        CHECK(_stricmp(v.signal, "fl_s19_unsigned_protect_fixture.dll") == 0);
        CHECK_FALSE(TargetHasModule(child.pi.dwProcessId, L"FrameLedger.Overlay.dll"));
    }    // the child is gone before its module file is deleted
    DeleteFileW(copy.c_str());
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

    // rtTier: ASSERT THAT WE ASKED, REPORT WHAT THE ANSWER WAS.
    //
    // 03_METRICS' RT `No` needs an RT-capable device, and until this producer
    // existed rtTier was 0 on every session -- so `No` was unreachable and, since
    // hooksInstalledMask is the other conjunct, so was `Yes`.
    //
    // What is asserted is the PRODUCER'S promise: a D3D12 device was identified,
    // so the query ran and the field holds one of FlRtTier's legal encodings. What
    // is NOT asserted is which one. The fixture's device is WARP, and whether WARP
    // supports DXR is the open question docs/HANDOFF.md item 4 says to check
    // rather than assume -- asserting a tier here would turn that unknown into a
    // test that fails on some runners for a reason unrelated to this code.
    //
    // CAPTURE surfaces the value WHEN THIS FAILS, and only then. An earlier
    // version of this comment claimed the run "records the answer" and that the
    // test was therefore also the measurement -- both false, and caught by going
    // and reading a green CI log for the number that was supposed to be in it.
    // Catch2 discards a CAPTURE on success, and `ctest --preset` (build.ps1:199)
    // suppresses a passing test's output anyway.
    //
    // So WHETHER WARP SUPPORTS DXR IS STILL UNANSWERED, and item 4's harness DXR
    // mode must query it at runtime and skip WITH A REASON rather than assume it
    // either way. Left as a CAPTURE because it costs nothing and is exactly what
    // a reader wants the moment this does fail.
    CAPTURE(st->rtTier);
    CHECK(st->rtTier != fl::FL_RT_TIER_NOT_QUERIED);
    CHECK((st->rtTier == fl::FL_RT_TIER_UNSUPPORTED || st->rtTier >= fl::FL_RT_TIER_CAPABLE_MIN));

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

// ===========================================================================
// P1 item 1 -- the LoadLibrary detour (17_HOOK_ENGINE §DLL entry step 3, §H2, §S6).
// Both jobs, against the real Overlay in a real target: a vendor module that
// arrives LATE is hooked within milliseconds of its arrival, and an anti-cheat-
// named module that arrives late stops the Overlay by itself.
// ===========================================================================
namespace {

struct MappedRing {
    HANDLE              mapping = nullptr;
    void*               base = nullptr;
    fl::FlWriterState*  st = nullptr;
    fl::FlControlBlock* ctl = nullptr;
    ~MappedRing() {
        if (base != nullptr) {
            UnmapViewOfFile(base);
        }
        if (mapping != nullptr) {
            CloseHandle(mapping);
        }
    }
};

bool OpenRingFor(DWORD pid, MappedRing& r) {
    wchar_t name[128]{};
    if (_snwprintf_s(name, _TRUNCATE, L"Local\\FrameLedger.Ring.%lu", pid) <= 0) {
        return false;
    }
    for (int i = 0; i < 100 && r.mapping == nullptr; ++i) {
        r.mapping = OpenFileMappingW(FILE_MAP_READ | FILE_MAP_WRITE, FALSE, name);
        if (r.mapping == nullptr) {
            Sleep(50);
        }
    }
    if (r.mapping == nullptr) {
        return false;
    }
    r.base = MapViewOfFile(r.mapping, FILE_MAP_READ | FILE_MAP_WRITE, 0, 0, 0);
    if (r.base == nullptr) {
        return false;
    }
    r.st = reinterpret_cast<fl::FlWriterState*>(static_cast<unsigned char*>(r.base) + FL_SHM_WRITER_OFFSET);
    r.ctl = reinterpret_cast<fl::FlControlBlock*>(static_cast<unsigned char*>(r.base) + FL_SHM_CONTROL_OFFSET);
    for (int i = 0; i < 100 && r.st->status != fl::FL_STATUS_READY; ++i) {
        Sleep(50);
    }
    return r.st->status == fl::FL_STATUS_READY;
}

bool StartHarness(Child& child, const std::wstring& args) {
    std::wstring cmd = std::wstring(L"\"") + FL_HARNESS_EXE + L"\" " + args;
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

TEST_CASE("the LoadLibrary detour hooks a vendor module that arrives LATE within milliseconds, not on the next tick",
          "[guard][inject][shm][loader]") {
    // The harness presents with no vendor module, then loads sl.common (an inert
    // decoy) and sl.interposer 2.0 / 2.3 s in. Before: no upscaler hook. After: the
    // hook family appears within 500 ms of the module -- the detour woke the
    // watchdog. The 1 Hz tick alone would land anywhere in the next second, so a
    // latency bound under half of that is what discriminates the wake from the tick.
    Child        child;
    std::wstring args = L"--real --hold-presenting 14 --load-after-ms 2000 \"" + std::wstring(FL_STUB_SL_COMMON) +
                        L"\" --load-after-ms 2300 \"" + std::wstring(FL_STUB_SL_INTERPOSER) + L"\"";
    REQUIRE(StartHarness(child, args));

    ResetFake();
    g.modules = {"kernel32.dll"};
    g.scanSet = {child.pi.dwProcessId};
    REQUIRE(GuardedInjectWithSources(child.pi.dwProcessId, FL_OVERLAY_DLL, FakeSources()).Allowed());

    MappedRing r;
    REQUIRE(OpenRingFor(child.pi.dwProcessId, r));
    CHECK((r.st->loaderSignals & 0x8000u) != 0u);    // the detour installed

    // Nothing vendor-shaped yet: the identity family is absent and stays absent.
    Sleep(300);
    ++r.ctl->guardTicks;
    REQUIRE((r.st->hooksInstalledMask & fl::FL_HOOK_UPSCALER_IDENTITY) == 0u);

    // Wait for the interposer to appear in the target, then time the family.
    ULONGLONG appeared = 0;
    for (int i = 0; i < 400 && appeared == 0; ++i) {
        if (TargetHasModule(child.pi.dwProcessId, L"sl.interposer.dll")) {
            appeared = GetTickCount64();
            break;
        }
        Sleep(10);
    }
    REQUIRE(appeared != 0);
    ULONGLONG hooked = 0;
    for (int i = 0; i < 300 && hooked == 0; ++i) {
        if ((r.st->hooksInstalledMask & fl::FL_HOOK_UPSCALER_IDENTITY) != 0u) {
            hooked = GetTickCount64();
            break;
        }
        ++r.ctl->guardTicks;
        Sleep(10);
    }
    REQUIRE(hooked != 0);
    const ULONGLONG latencyMs = hooked - appeared;
    INFO("late-load hook latency " << latencyMs << " ms, loaderSignals=0x" << std::hex << r.st->loaderSignals);
    CHECK(latencyMs < 500);
    // The wake count is published by the watchdog, on its own iteration; give it
    // the iteration rather than racing the install that the wake caused.
    for (int i = 0; i < 200 && (r.st->loaderSignals & 0x7FFFu) == 0u; ++i) {
        ++r.ctl->guardTicks;
        Sleep(10);
    }
    CHECK((r.st->loaderSignals & 0x7FFFu) >= 1u);    // the detour saw an inventoried module and woke the watchdog
    CHECK(r.st->earlyStopFamily == 0u);
    CHECK(r.st->status == fl::FL_STATUS_READY);
}

TEST_CASE("the LoadLibrary detour stops the Overlay by itself when an anti-cheat-named module arrives",
          "[guard][inject][shm][loader][S6]") {
    // The in-process half of 19_SAFETY §During a session. A decoy DLL copied under a
    // name the compiled floor's first module family matches by prefix is loaded 2.5 s
    // in; the Overlay must stop within a frame, name the family, and stay stopped.
    wchar_t temp[MAX_PATH]{};
    REQUIRE(GetTempPathW(MAX_PATH, temp) != 0);
    const std::wstring copy = std::wstring(temp) + L"EasyAntiCheat_fl_fixture.dll";
    REQUIRE(CopyFileW(FL_STUB_SL_COMMON, copy.c_str(), FALSE));

    // The expected index, computed from the same generated table the Overlay
    // compiled in -- never a literal 1, so a reordered rules file moves both sides.
    uint32_t expected = 0;
    {
        uint32_t index = 0;
        for (const fl::guard::Family& f : fl::guard::generated::kFloorFamilies) {
            if (f.group != fl::guard::Group::kModules) {
                continue;
            }
            ++index;
            for (std::size_t v = 0; v < f.valueCount && expected == 0; ++v) {
                if (f.match == fl::guard::MatchKind::kPrefix &&
                    _strnicmp("EasyAntiCheat_fl_fixture.dll", f.values[v], std::strlen(f.values[v])) == 0) {
                    expected = index;
                }
            }
            if (expected != 0) {
                break;
            }
        }
    }
    REQUIRE(expected != 0);

    {
        Child        child;
        std::wstring args = L"--real --hold-presenting 14 --load-after-ms 2500 \"" + copy + L"\"";
        REQUIRE(StartHarness(child, args));

        ResetFake();
        g.modules = {"kernel32.dll"};
        g.scanSet = {child.pi.dwProcessId};
        REQUIRE(GuardedInjectWithSources(child.pi.dwProcessId, FL_OVERLAY_DLL, FakeSources()).Allowed());

        MappedRing r;
        REQUIRE(OpenRingFor(child.pi.dwProcessId, r));

        // It really was recording, with the supervision clock kept alive.
        std::uint64_t before = 0;
        for (int i = 0; i < 40 && before < 5; ++i) {
            ++r.ctl->guardTicks;
            Sleep(100);
            before = r.st->writeIndex;
        }
        REQUIRE(before > 5);

        // THE STOP, from inside: no unhookRequested from this side, ever.
        for (int i = 0; i < 100 && r.st->status != fl::FL_STATUS_STOPPED_BLOCKLISTED; ++i) {
            ++r.ctl->guardTicks;
            Sleep(50);
        }
        CHECK(r.st->status == fl::FL_STATUS_STOPPED_BLOCKLISTED);
        CHECK(r.st->earlyStopFamily == expected);
        CHECK(r.ctl->unhookRequested == 0u);

        // And it stays stopped while the harness keeps presenting and the ticks keep coming.
        const std::uint64_t atStop = r.st->writeIndex;
        for (int i = 0; i < 10; ++i) {
            ++r.ctl->guardTicks;
            Sleep(100);
        }
        CHECK(r.st->writeIndex == atStop);
    }    // the child is gone before its module file is deleted
    DeleteFileW(copy.c_str());
}

// ===========================================================================
// P1 item 2 -- launch mode as "inject late" (20_OPEN_QUESTIONS §S1 / §S13(c)):
// the poll decides WHEN the guard runs, never WHETHER it passes.
// ===========================================================================
TEST_CASE("launch mode injects the moment a presentation runtime is mapped, and not one poll before",
          "[guard][inject][launch]") {
    Child child;
    REQUIRE(StartHarness(child));

    ResetFake();
    g.modules = {"kernel32.dll"};    // a target still in its loader: no runtime yet
    g.modulesLate = {"kernel32.dll", "dxgi.dll", "d3d11.dll"};
    g.moduleCallsBeforeLate = 6;    // the seventh poll (~300 ms) is the first that sees it
    g.scanSet = {child.pi.dwProcessId};

    const ULONGLONG t0 = GetTickCount64();
    const Verdict   v = GuardedInjectWhenReadyWithSources(child.pi.dwProcessId, FL_OVERLAY_DLL, 10000, FakeSources());
    const ULONGLONG waited = GetTickCount64() - t0;
    INFO("reason " << ReasonName(v.reason) << " signal=" << v.signal << " waited " << waited << " ms, polls "
                   << g.moduleCalls);
    REQUIRE(v.Allowed());
    CHECK(g.moduleCalls >= 7);    // it really waited for the runtime rather than injecting on sight
    CHECK(waited >= 250);
    CHECK(waited < 3000);
    CHECK(TargetHasModule(child.pi.dwProcessId, L"FrameLedger.Overlay.dll"));
}

TEST_CASE("launch mode: a target that exits before mapping a runtime is refused and nothing is injected",
          "[guard][inject][launch]") {
    Child        child;
    std::wstring cmd = std::wstring(L"\"") + FL_HARNESS_EXE + L"\" --hold 1";
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    REQUIRE(CreateProcessW(FL_HARNESS_EXE, cmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si,
                           &child.pi));

    ResetFake();
    g.modules = {"kernel32.dll"};    // never a runtime
    g.scanSet = {child.pi.dwProcessId};

    const Verdict v = GuardedInjectWhenReadyWithSources(child.pi.dwProcessId, FL_OVERLAY_DLL, 20000, FakeSources());
    INFO("reason " << ReasonName(v.reason) << " signal=" << v.signal);
    CHECK(v.reason == Reason::kLaunchTargetExited);
    CHECK_FALSE(v.Allowed());
}

TEST_CASE("launch mode: no presentation runtime inside the budget refuses, naming the budget's end",
          "[guard][inject][launch]") {
    Child child;
    REQUIRE(StartHarness(child));

    ResetFake();
    g.modules = {"kernel32.dll"};
    g.scanSet = {child.pi.dwProcessId};

    const ULONGLONG t0 = GetTickCount64();
    const Verdict   v = GuardedInjectWhenReadyWithSources(child.pi.dwProcessId, FL_OVERLAY_DLL, 400, FakeSources());
    const ULONGLONG waited = GetTickCount64() - t0;
    INFO("reason " << ReasonName(v.reason) << " waited " << waited << " ms");
    CHECK(v.reason == Reason::kLaunchNoPresentationRuntime);
    CHECK(waited >= 400);
    CHECK(waited < 2500);
    CHECK_FALSE(TargetHasModule(child.pi.dwProcessId, L"FrameLedger.Overlay.dll"));
}

TEST_CASE("launch mode: the poll matches no blocklist, so anti-cheat mapped beside the runtime is the FULL scan's "
          "refusal, and nothing is injected",
          "[guard][inject][launch]") {
    Child child;
    REQUIRE(StartHarness(child));

    ResetFake();
    g.modules = {"kernel32.dll"};
    g.modulesLate = {"kernel32.dll", "EasyAntiCheat_EOS.dll", "dxgi.dll", "d3d12.dll"};
    g.moduleCallsBeforeLate = 2;
    g.scanSet = {child.pi.dwProcessId};

    const Verdict v = GuardedInjectWhenReadyWithSources(child.pi.dwProcessId, FL_OVERLAY_DLL, 10000, FakeSources());
    INFO("reason " << ReasonName(v.reason) << " family=" << v.family << " signal=" << v.signal);
    CHECK(v.reason == Reason::kBlockedModule);
    CHECK(std::string(v.signal) == "EasyAntiCheat_EOS.dll");
    CHECK_FALSE(TargetHasModule(child.pi.dwProcessId, L"FrameLedger.Overlay.dll"));
}

TEST_CASE("launch mode against the real harness through the real module seam: injected once dxgi maps, and the "
          "writer says how many presents ran unhooked",
          "[guard][inject][shm][launch]") {
    // The harness creates its device at startup and presents every 8 ms; the seam is
    // the real EnumProcessModulesEx. What the writer reports afterwards is exactly the
    // number 20_OPEN_QUESTIONS §S1 said nobody had: presents completed on the chain
    // before the hook was in. Here that is however many the harness managed between
    // its first present and our injection -- a measurement, not a target.
    Child child;
    REQUIRE(StartHarness(child, L"--real --hold-presenting 12"));

    ResetFake();
    g.scanSet = {child.pi.dwProcessId};
    const ULONGLONG t0 = GetTickCount64();
    const Verdict   v =
        GuardedInjectWhenReadyWithSources(child.pi.dwProcessId, FL_OVERLAY_DLL, 10000, RealModulesAndSigner());
    const ULONGLONG waited = GetTickCount64() - t0;
    INFO("reason " << ReasonName(v.reason) << " signal=" << v.signal << " waited " << waited << " ms");
    REQUIRE(v.Allowed());
    CHECK(waited < 2000);    // the runtime was already there: one poll, then the full guard

    MappedRing r;
    REQUIRE(OpenRingFor(child.pi.dwProcessId, r));
    for (int i = 0; i < 60 && r.st->writeIndex == 0; ++i) {
        ++r.ctl->guardTicks;
        Sleep(50);
    }
    REQUIRE(r.st->writeIndex > 0);
    const uint32_t before = r.st->dxgiPresentsBeforeHook;
    INFO("presents before the first hooked present: " << before);
    CHECK(before != 0xFFFFFFFFu);    // read at the first hooked present
    CHECK(before < 5000);            // the harness ran ~1 s at ~120/s before we were in; not a bound on a title
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
        // The honesty property: a writer may claim a measurement ONLY where it
        // installed a hook capable of taking it, and may not set a value field whose
        // mask bit is clear. Derived from hooksInstalledMask -- see EntitledBy above
        // for why this is no longer a hardcoded constant compared with `!=`.
        if (!IsHonest(all[i], EntitledBy(st->hooksInstalledMask))) {
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

    // AND THE DERIVED FORM ONLY MEANS SOMETHING IF THIS WRITER REALLY IS PRESENT-ONLY.
    // EntitledBy(hooks) widens with every family, so a writer that installed everything
    // would be "honest" about any claim it made and the loop above would pass while
    // proving nothing. EQUALITY, not a bit test: an extra family bit is exactly the
    // failure this has to catch, and `& FL_HOOK_PRESENT` is blind to one.
    //
    // This harness is `--hold-presenting`, which loads no Streamline stub, so
    // FL_HOOK_PRESENT alone is the correct and only answer here.
    CHECK(st->hooksInstalledMask == static_cast<uint32_t>(fl::FL_HOOK_PRESENT));

    // THE CENSUS RAN AND SAW NO VENDOR RUNTIME, and both halves are asserted by
    // EQUALITY. This harness loads no stub, so any family bit here would be a name
    // in FL_RUNTIME_CENSUS matching a module that has nothing to do with upscaling
    // -- and a missing RAN bit would be the watchdog never taking the census at all,
    // which decodes as "nobody looked" and would silence the qualifier on every title.
    CHECK(st->runtimeCensus == static_cast<uint32_t>(fl::FL_CENSUS_RAN));

    CHECK((st->apiMask & (1u << fl::FL_API_D3D11)) != 0);
    CHECK((st->apiMask & (1u << fl::FL_API_D3D12)) == 0);

    // THE OTHER DIRECTION OF rtTier, and it is what makes the D3D12 case above
    // mean something. That case asserts the field is a legal FlRtTier value; a
    // writer that simply stored FL_RT_TIER_UNSUPPORTED unconditionally would pass
    // it. This harness presents through D3D11, so no ID3D12Device is ever
    // identified, the query never runs, and NOT_QUERIED is the only honest value.
    //
    // Same shape as the api assertion four lines up, and for the same reason:
    // without a case where the answer must be different, "we measured it" and "we
    // made it up" are indistinguishable.
    CHECK(st->rtTier == fl::FL_RT_TIER_NOT_QUERIED);

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
bool WaitForHookFamily(const fl::FlWriterState* st, uint32_t family) {
    for (int i = 0; i < 200; ++i) {
        std::atomic_ref<const uint32_t> hooks{st->hooksInstalledMask};
        if ((hooks.load(std::memory_order_acquire) & family) != 0) {
            return true;
        }
        Sleep(50);
    }
    return false;
}

bool WaitForIdentityHook(const fl::FlWriterState* st) {
    return WaitForHookFamily(st, static_cast<uint32_t>(fl::FL_HOOK_UPSCALER_IDENTITY));
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
    // BOTH FAMILIES NOW, and waiting for the second is what makes the params
    // assertions below about the PRODUCER rather than about install ordering.
    // The two hooks install independently on the 1 Hz watchdog, so there is a
    // real window where identity is live and params is not -- measured at 6
    // records out of 41 on the first run, which a floor would have papered over
    // instead of eliminating. Poll for the state you mean (HANDOFF §Traps), then
    // discard everything that came before it.
    REQUIRE(WaitForHookFamily(st, static_cast<uint32_t>(fl::FL_HOOK_UPSCALER_PARAMS)));
    CHECK((st->hooksInstalledMask & fl::FL_HOOK_UPSCALER_PARAMS) != 0u);

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
    int dishonestParams = 0;
    int wrongExtent = 0;
    int valueWithoutBit = 0;
    for (const auto& r : all) {
        if ((r.measuredMask & fl::FL_MEASURED_UPSCALER) != 0u) {
            ++measured;
        }
        if (r.upscaler == fl::FL_UPSCALER_DLSS) {
            ++identified;
        }
        if ((r.measuredMask & fl::FL_MEASURED_UPSCALER_PARAMS) != 0u) {
            ++claimedParams;
            // THE HONESTY INVARIANT, checked on every record that claims the bit
            // rather than once at the end. fl_shm.h has no in-band "not measured"
            // for upscalerQuality -- 0 is NGX MaxPerf, a real preset -- so 0 here
            // would publish "DLSS Performance" as a measurement. Sharpness is
            // 0xFF permanently: DLSSOptions::sharpness is deprecated as
            // unsupported, so there is no in-policy route to what a title applied.
            if (r.upscalerQuality == 0u || r.upscalerSharpness != 0xFFu) {
                ++dishonestParams;
            }
            // The EXACT extent the harness tagged, not merely "non-zero". A
            // writer that hardcoded a plausible render resolution fails here.
            if (r.renderW != FL_TAGGED_RENDER_W || r.renderH != FL_TAGGED_RENDER_H) {
                ++wrongExtent;
            }
        } else if (r.renderW != 0u || r.renderH != 0u) {
            // The other direction: a VALUE set while its bit is clear is the same
            // defect seen from the record instead of from the mask.
            ++valueWithoutBit;
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

    // THE PARAMS BIT NOW HAS A PRODUCER. This assertion is INVERTED rather than
    // deleted -- it read `claimedParams == 0`, which was the honest statement
    // while renderW/H had no source, and is the exact line a reviewer should see
    // change when they do.
    //
    // EQUALITY, not a floor, because the drain begins only after BOTH hooks are
    // live and the backlog is discarded there. A floor would have hidden the
    // install-ordering window rather than eliminating it.
    INFO("params claimed on " << claimedParams << " of " << all.size());
    CHECK(claimedParams == static_cast<int>(all.size()));

    // Zero tolerance on these three, unlike the floors above: there is no
    // legitimate reason for even one record to over-claim or to name a
    // resolution nobody tagged.
    CHECK(dishonestParams == 0);
    CHECK(wrongExtent == 0);
    CHECK(valueWithoutBit == 0);

    CHECK(st->faultCount == 0);

    UnmapViewOfFile(base);
    CloseHandle(mapping);
}

// The global-tag routes, injected: tag through the given harness flags, wait for
// the params family, and assert the EXACT tagged size on every record after the
// backlog. Shared by the two cases below so the assertion cannot drift between the
// export a title uses and the shape of the tag it passes.
void AssertGlobalTagRoutePublishesExactExtent(const wchar_t* harnessFlags, bool expectDlssgIdentity = false) {
    Child        child;
    std::wstring cmd =
        std::wstring(L"\"") + FL_HARNESS_EXE + L"\" --real " + harnessFlags + L" --hold-presenting-upscaled 12";
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
    REQUIRE(WaitForIdentityHook(st));
    REQUIRE(WaitForHookFamily(st, static_cast<uint32_t>(fl::FL_HOOK_UPSCALER_PARAMS)));
    fl::RingReader rd;
    REQUIRE(rd.Init(base, FL_SHM_DEFAULT_CAPACITY));
    DiscardBacklog(rd);
    const std::vector<fl::FlFrameRecord> all = DrainAtLeast(rd, 40, 8000);
    REQUIRE(all.size() > 20);
    int claimedParams = 0;
    int wrongExtent = 0;
    int identified = 0;
    int dlssgMarked = 0;
    int fsrFgMarked = 0;
    for (const auto& r : all) {
        if (r.upscaler == fl::FL_UPSCALER_DLSS) {
            ++identified;
        }
        if (r.fgMode == fl::FL_FG_DLSS_G) {
            ++dlssgMarked;
        }
        if (r.fgMode == fl::FL_FG_FSR_FG) {
            ++fsrFgMarked;
        }
        if ((r.measuredMask & fl::FL_MEASURED_UPSCALER_PARAMS) != 0u) {
            ++claimedParams;
            if (r.renderW != FL_TAGGED_RENDER_W || r.renderH != FL_TAGGED_RENDER_H) {
                ++wrongExtent;
            }
        }
    }
    INFO("identified " << identified << ", params on " << claimedParams << " of " << all.size());
    CHECK(identified > static_cast<int>(all.size()) - 3);
    // Every record once BOTH rows' family is live and the backlog is gone -- the same
    // equality the slSetTag case asserts, from the other export.
    CHECK(claimedParams == static_cast<int>(all.size()));
    CHECK(wrongExtent == 0);
    CHECK(st->faultCount == 0);

    // THE IDENTITY HALF OF FRAME GENERATION, FROM THE TAGS' TYPES (fl_shm.h §slTagCensus).
    // This fixture never evaluates kFeatureDLSS_G, so a record naming DLSS_G can only
    // have come from a HUD-less or UI tag in the list -- and without those tags NO
    // record may name it, or a scaling-input tag alone would read as frame generation.
    // The tag census must say which route carried them and must not say the local route
    // did, since this fixture tags globally only.
    // POLL FOR THE CENSUS, do not read it once. It is published by the watchdog on its
    // 1 Hz tick, while the records above were drained ~330 ms after the family went
    // live -- on CI the first read came back 0 with DLSS_G already on 41 of 41 records.
    // The state to wait for is the one every fixture here produces (a scaling-input tag
    // on a global route); the timeout keeps failing, so a census that never publishes
    // is still red rather than skipped (HANDOFF §Traps: a writer state read once).
    const auto routeBits = [st](uint32_t route) { return (st->slTagCensus >> route) & fl::FL_SL_TAG_TYPE_MASK; };
    for (int i = 0; i < 80 && ((routeBits(fl::FL_SL_TAG_ROUTE_GLOBAL) | routeBits(fl::FL_SL_TAG_ROUTE_FRAME)) &
                               fl::FL_SL_TAG_SCALING_INPUT) == 0u;
         ++i) {
        Sleep(50);
    }
    const uint32_t census = st->slTagCensus;
    const uint32_t global = (census >> fl::FL_SL_TAG_ROUTE_GLOBAL) & fl::FL_SL_TAG_TYPE_MASK;
    const uint32_t frame = (census >> fl::FL_SL_TAG_ROUTE_FRAME) & fl::FL_SL_TAG_TYPE_MASK;
    const uint32_t local = (census >> fl::FL_SL_TAG_ROUTE_LOCAL) & fl::FL_SL_TAG_TYPE_MASK;
    INFO("DLSS_G on " << dlssgMarked << " of " << all.size() << ", tag census " << census);
    CHECK(fsrFgMarked == 0);
    CHECK(local == 0u);
    CHECK(((global | frame) & fl::FL_SL_TAG_SCALING_INPUT) != 0u);
    if (expectDlssgIdentity) {
        // Every record once the family is live: the list is re-tagged before every
        // present, so every present drains the mark -- the same equality as params.
        CHECK(dlssgMarked == static_cast<int>(all.size()));
        CHECK(((global | frame) & fl::FL_SL_TAG_HUDLESS) != 0u);
        CHECK(((global | frame) & fl::FL_SL_TAG_UI_COLOR_ALPHA) != 0u);
        CHECK(((global | frame) & fl::FL_SL_TAG_DEPTH) != 0u);
    } else {
        CHECK(dlssgMarked == 0);
        CHECK(((global | frame) & fl::FL_SL_TAG_DLSSG_INPUTS) == 0u);
    }
    UnmapViewOfFile(base);
    CloseHandle(mapping);
}
TEST_CASE("a Streamline 2.8 title that tags through slSetTagForFrame still publishes the extent",
          "[guard][inject][shm][upscaler]") {
    // THE ROW THAT DID NOT EXIST ON 2026-09-04 MORNING. Dying Light: The Beast ships
    // Streamline 2.8.0, which deprecates slSetTag for slSetTagForFrame; the title
    // published DLSS identity on every batch and the params bit on none, because the
    // only tag row hooked the deprecated export. This fixture tags ONLY through the
    // frame-based export, so an extent in the record can only have come from the new
    // row -- and the exact tagged size is asserted, not "non-zero".
    AssertGlobalTagRoutePublishesExactExtent(L"--sl-tag-for-frame");
}

TEST_CASE("DLSS-G inputs tagged through Streamline name DLSS_G on the present that drained them, with no "
          "kFeatureDLSS_G evaluation",
          "[guard][inject][shm][upscaler][fg]") {
    // THE IDENTITY FIVE REAL TITLES COULD NOT GIVE. kFeatureDLSS_G is never evaluated
    // through slEvaluateFeature on Streamline 2.x (measured, five titles), so the only
    // per-frame statement a title makes about DLSS Frame Generation through an API this
    // build hooks is the HUD-less and UI tags the DLSS-G guide §5.0 requires. This
    // fixture sends that list through slSetTag and evaluates kFeatureDLSS only; the
    // record must name DLSS_G on every present, and the census must show the two inputs
    // on the global route. Its twin below sends the same list through the 2.8 export.
    AssertGlobalTagRoutePublishesExactExtent(L"--sl-tag-dlssg-inputs", /*expectDlssgIdentity=*/true);
}

TEST_CASE("the DLSS-G inputs through slSetTagForFrame name DLSS_G too", "[guard][inject][shm][upscaler][fg]") {
    AssertGlobalTagRoutePublishesExactExtent(L"--sl-tag-for-frame --sl-tag-dlssg-inputs", /*expectDlssgIdentity=*/true);
}

TEST_CASE("a title that tags the WHOLE input resource publishes the size the Resource declares",
          "[guard][inject][shm][upscaler]") {
    // The shape left after the row above landed and DL:TB still read no extent: a
    // zero extent ("use the entire resource") with the size on the Resource. Tagged
    // through the 2.8 export, as that title would. An extent in the record can only
    // have come from Resource::width/height, and it must be the exact tagged size.
    AssertGlobalTagRoutePublishesExactExtent(L"--sl-tag-for-frame --sl-tag-whole-resource");
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
    int undecoded = 0;
    int superResolution = 0;
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
        if ((r.featureFlags & fl::FL_FEAT_SL_UNDECODED) != 0u) {
            ++undecoded;
        }
        if ((r.featureFlags & fl::FL_FEAT_SL_SUPER_RESOLUTION) != 0u) {
            ++superResolution;
        }
    }
    CHECK(measured == static_cast<int>(all.size()));    // a hook ran, so the field may be read
    CHECK(unknown == static_cast<int>(all.size()));     // and what it says is "I could not tell"
    CHECK(dlss == 0);                                   // never invented
    CHECK(none == 0);                                   // and never a measured negative
    CHECK(st->faultCount == 0);

    // FL_FEAT_SL_UNDECODED, PROVEN IN THE POSITIVE DIRECTION, which it had never been.
    //
    // This fixture evaluates 0xF00D -- an id outside the four the detour decodes -- so it is
    // the one place FL_SL_SEEN_OTHER is driven through detour -> ring -> reader by a real
    // injected Overlay. Until this assertion existed the bucket had only ever been OBSERVED
    // READING ZERO: five real-title captures reported UNDECODED = 0, and that zero is
    // load-bearing, because it is what excludes a vendored sl::kFeatureDLSS_G constant not
    // matching the runtime id -- the most likely silent explanation for "frame generation is
    // never evaluated". A discrimination only ever seen reading zero is this repo's recorded
    // shape: `a != b` passes when one side is absent.
    //
    // BOTH DIRECTIONS, in one fixture. The same records must carry NO super-resolution fact,
    // because 0xF00D is not kFeatureDLSS or kFeatureNIS -- so a writer that lit both bits
    // from one condition, which is exactly how the census got contaminated once already,
    // fails here rather than reading as agreement.
    CHECK(undecoded == static_cast<int>(all.size()));
    CHECK(superResolution == 0);

    UnmapViewOfFile(base);
    CloseHandle(mapping);
}

namespace {

// What one --hold-presenting-fg run yields, drained after the identity hook is live.
struct FgObservation {
    std::uint32_t census = 0;
    std::uint32_t dxgiUnseen = 0;
    std::uint32_t dxgiSamples = 0;
    std::size_t   drained = 0;
    std::uint64_t sigma = 0;              // sum of fgEvaluations over the window
    std::uint64_t dxgiRecUnseen = 0;      // sum of dxgiUnseen over records claiming it
    std::size_t   dxgiRecClaiming = 0;    // records carrying FL_MEASURED_DXGI_PRESENTS
    std::uint32_t hooks = 0;
    std::uint32_t faults = 0;
    bool          everyRecordClaimsCounts = true;
    bool          modeIsNeverNone = true;
    bool          zeroCountRecordsStillCarryTheBits = false;
    std::size_t   withLocalTagExtent = 0;
};

// Inject into a frame-generating target and drain a post-install window.
//
// FACTORED SO BOTH K VALUES RUN THE IDENTICAL CODE. Two hand-written cases would
// let the K = 4 one acquire a tolerance the K = 1 one does not have, and the pair
// stops discriminating the moment they differ.
bool ObserveFg(int presentsPerEval, FgObservation& out) {
    Child        child;
    std::wstring cmd = std::wstring(L"\"") + FL_HARNESS_EXE + L"\" --real --presents-per-eval " +
                       std::to_wstring(presentsPerEval) + L" --hold-presenting-fg 14";
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    if (!CreateProcessW(FL_HARNESS_EXE, cmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si,
                        &child.pi)) {
        return false;
    }
    Sleep(800);
    if (WaitForSingleObject(child.pi.hProcess, 0) != WAIT_TIMEOUT) {
        return false;
    }

    ResetFake();
    g.modules = {"kernel32.dll"};
    g.scanSet = {child.pi.dwProcessId};
    if (!GuardedInjectWithSources(child.pi.dwProcessId, FL_OVERLAY_DLL, FakeSources()).Allowed()) {
        return false;
    }

    wchar_t name[128]{};
    if (_snwprintf_s(name, _TRUNCATE, L"Local\\FrameLedger.Ring.%lu", child.pi.dwProcessId) <= 0) {
        return false;
    }
    HANDLE mapping = nullptr;
    for (int i = 0; i < 100 && mapping == nullptr; ++i) {
        mapping = OpenFileMappingW(FILE_MAP_READ, FALSE, name);
        if (mapping == nullptr) {
            Sleep(50);
        }
    }
    if (mapping == nullptr) {
        return false;
    }
    const void* base = MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, 0);
    if (base == nullptr) {
        CloseHandle(mapping);
        return false;
    }

    const auto* st =
        reinterpret_cast<const fl::FlWriterState*>(static_cast<const unsigned char*>(base) + FL_SHM_WRITER_OFFSET);
    for (int i = 0; i < 100 && st->status != fl::FL_STATUS_READY; ++i) {
        Sleep(50);
    }

    // BOTH ROWS, because the count comes from the SECOND one. The watchdog installs the
    // inventory rows in order and each MinHook patch suspends every thread, so the token
    // row goes live some tens of milliseconds after the identity row -- measured
    // 2026-09-03 as 8 records at 8 ms a present. Waiting for identity alone drained
    // those 8 into the window, where they legitimately carry no FG_COUNTS bit, and the
    // assertion below read that install prefix as a writer that was wrong by 8.
    bool ok = st->status == fl::FL_STATUS_READY && WaitForIdentityHook(st) &&
              WaitForHookFamily(st, static_cast<uint32_t>(fl::FL_HOOK_FG_EVALUATIONS));
    if (ok) {
        fl::RingReader rd;
        ok = rd.Init(base, FL_SHM_DEFAULT_CAPACITY);
        if (ok) {
            // Records written before the hook went live legitimately carry no count.
            DiscardBacklog(rd);
            const std::vector<fl::FlFrameRecord> all = DrainAtLeast(rd, 60, 9000);
            out.drained = all.size();
            for (const auto& r : all) {
                out.sigma += r.fgEvaluations;
                if ((r.measuredMask & fl::FL_MEASURED_DXGI_PRESENTS) != 0u) {
                    ++out.dxgiRecClaiming;
                    out.dxgiRecUnseen += r.dxgiUnseen;
                }
                if ((r.measuredMask & fl::FL_MEASURED_FG_COUNTS) == 0u) {
                    out.everyRecordClaimsCounts = false;
                }
                if (r.fgMode == fl::FL_FG_NONE) {
                    out.modeIsNeverNone = false;
                }
                // The anti-filter property: a present that drained no evaluation must
                // still carry the bits and a zero byte, or a consumer could drop those
                // records and recover presents == sigma, i.e. fg_factor 1.0.
                if (r.fgEvaluations == 0u && (r.measuredMask & fl::FL_MEASURED_FG_COUNTS) != 0u) {
                    out.zeroCountRecordsStillCarryTheBits = true;
                }
                // The LOCAL-tag route: this fixture never calls slSetTag, so an extent
                // here can only have come from slEvaluateFeature's own `inputs`.
                if ((r.measuredMask & fl::FL_MEASURED_UPSCALER_PARAMS) != 0u && r.renderW == FL_TAGGED_RENDER_W &&
                    r.renderH == FL_TAGGED_RENDER_H) {
                    ++out.withLocalTagExtent;
                }
            }
        }
    }
    out.hooks = st->hooksInstalledMask;
    // DXGI's counter against ours: published by the watchdog on its 1 Hz tick, so poll for
    // the samples to reach the window rather than reading the words once (HANDOFF §Traps).
    for (int i = 0; i < 80 && st->dxgiPresentSamples < out.drained; ++i) {
        Sleep(50);
    }
    out.dxgiUnseen = st->dxgiPresentsUnseen;
    out.dxgiSamples = st->dxgiPresentSamples;
    out.census = st->runtimeCensus;
    out.faults = st->faultCount;

    UnmapViewOfFile(base);
    CloseHandle(mapping);
    return ok;
}

}    // namespace

TEST_CASE("application-frame tokens are COUNTED, and the count tracks the fixture's factor",
          "[guard][inject][shm][fg]") {
    // ONE EXPRESSION, TWO FIXTURES, and that pairing is the whole test.
    //
    // SINCE 2026-09-03 THE COUNT IS OF slGetNewFrameToken CALLS -- distinct tokens
    // handed to the title -- not of kFeatureDLSS_G evaluations, which five real titles
    // measured at zero. The fixture issues exactly one of each per group of K
    // presents, so the arithmetic below is unchanged; what changed is which detour
    // produces sigma, and the K = 1 control is what proves the new one is not the
    // present count wearing a different name.
    //
    // A single K = 4 run cannot fail usefully: a writer that counted PRESENTS instead
    // of tokens, or that wrote a constant, would still produce some ratio and a
    // reader would have nothing to compare it against. K = 1 is the control -- one
    // evaluation per present, the no-frame-generation shape -- and the SAME assertion
    // has to hold for both, so a writer that ignores the difference is red in one of
    // them by construction.
    //
    // THE TOLERANCE IS DERIVED, NOT TUNED. Each evaluation is followed by exactly K
    // presents, so within a drained window of sigma evaluation-carrying records the
    // span from the first to the last is (sigma-1)*K + 1 records, plus at most K-1 at
    // each end from opening and closing mid-group. That bounds |drained - K*sigma| by
    // K-1; asserting <= K leaves one record of slack and still fails a writer that is
    // wrong by a factor. Widening it past K would let K = 4 and K = 1 agree, which is
    // exactly the "fix" this note exists to forbid.
    for (const int k : {1, 4}) {
        CAPTURE(k);

        FgObservation obs;
        REQUIRE(ObserveFg(k, obs));

        CAPTURE(obs.drained);
        CAPTURE(obs.sigma);

        // A COUNT OF ZERO FAILS HERE RATHER THAN DIVIDING BY IT. If the fetch_add
        // never fires -- the total-failure case, and the one a single-sided "the bit
        // is set" assertion is green for -- sigma is 0 and this is the line that says
        // so, before any ratio is computed from it.
        REQUIRE(obs.sigma >= 8u);

        const long long drained = static_cast<long long>(obs.drained);
        const long long expected = static_cast<long long>(obs.sigma) * k;
        const long long diff = drained > expected ? drained - expected : expected - drained;
        CHECK(diff <= k);

        CHECK(obs.everyRecordClaimsCounts);
        CHECK(obs.modeIsNeverNone);
        CHECK(obs.faults == 0u);

        // EQUALITY, not a bit test. The compound family constant is the one thing no
        // gate in the tree reads the VALUE of -- hookinventory-check treats that
        // column as an opaque identifier -- so a wrong bit would publish a hook family
        // that does not exist, and `& FAMILY` is blind to exactly that.
        //
        // UPSCALER_PARAMS is in the expected set because the stub exports slSetTag as
        // well, so the watchdog installs BOTH inventory rows regardless of whether
        // this fixture ever calls the second one. Installing is what the mask records.
        CHECK(obs.hooks == static_cast<std::uint32_t>(fl::FL_HOOK_PRESENT | fl::FL_HOOK_UPSCALER_IDENTITY |
                                                      fl::FL_HOOK_UPSCALER_PARAMS | fl::FL_HOOK_FG_EVALUATIONS));

        // THE CENSUS SEES THE STUB BY NAME. The interposer stub is loaded by absolute
        // path and is still called sl.interposer.dll, so the loader answers yes for
        // it -- and for nothing in the frame-generation group, because no FG module
        // is in this process. The second check is the one that matters: it is what
        // keeps a title with Streamline loaded and no evaluation observed from being
        // printed as "no frame-generation runtime was loaded".
        // DXGI'S COUNTER AGAINST OURS, the negative control: this fixture presents only through
        // the body the inline patch covers, so DXGI must count exactly the presents the hook
        // saw -- zero unseen -- and the counter must have been read on every hooked present.
        // A real title where DXGI counts MORE is the §H5 question this word exists to answer.
        CAPTURE(obs.dxgiUnseen);
        CAPTURE(obs.dxgiSamples);
        CHECK(obs.dxgiUnseen == 0u);
        CHECK(obs.dxgiSamples >= static_cast<std::uint32_t>(obs.drained));
        // And per record, in the form the consumer's buckets read: every drained record
        // claims the bit (the chain's first present, which cannot, is in the discarded
        // backlog) and carries a zero byte.
        CAPTURE(obs.dxgiRecClaiming);
        CHECK(obs.dxgiRecUnseen == 0u);
        CHECK(obs.dxgiRecClaiming == obs.drained);
        CHECK((obs.census & fl::FL_CENSUS_RAN) != 0u);
        CHECK((obs.census & fl::FL_CENSUS_SL_INTERPOSER) != 0u);
        CHECK((obs.census & fl::FL_CENSUS_FG_FAMILIES) == 0u);

        // At K = 1 every present carries an evaluation, so there is no zero-count
        // record for the anti-filter property to be about; at K = 4 there must be.
        if (k > 1) {
            CHECK(obs.zeroCountRecordsStillCarryTheBits);
        }

        // THE LOCAL-TAG ROUTE, which had no injected coverage anywhere until this
        // fixture. FindScalingInputExtent has one production call site -- inside the
        // detour, after the feature decode -- and every other injected fixture passes
        // inputs = nullptr, so the wiring could have been deleted outright with the
        // whole suite green. This hold never calls slSetTag, so an extent in the
        // record can only have come from slEvaluateFeature's own `inputs`; and
        // because frame generation now takes a different decode arm, this is also
        // what makes "the arm must FALL THROUGH to the walk" a falsifiable claim
        // rather than a comment.
        //
        // Not every record: the params publish is gated on an evaluation having been
        // drained for that present, so at K = 4 three records in four correctly carry
        // no extent. `sigma` is the right floor, and it is already >= 8 above.
        CAPTURE(obs.withLocalTagExtent);
        CHECK(obs.withLocalTagExtent >= 8u);
    }
}

namespace {

// What one --hold-presenting-ffx run yields, drained after the AMD rows are live.
struct FfxObservation {
    std::uint32_t census = 0;
    std::size_t   drained = 0;
    std::uint64_t sigma = 0;    // sum of fgEvaluations over the window
    std::uint32_t hooks = 0;
    std::uint32_t faults = 0;
    bool          everyRecordClaimsCounts = true;
    bool          modeIsNeverNone = true;
    std::size_t   measured = 0;      // records claiming FL_MEASURED_UPSCALER
    std::size_t   identified = 0;    // records naming the EXPECTED FSR value
    std::size_t   claimedDlss = 0;
    std::size_t   claimedNone = 0;
    std::size_t   paramsRecords = 0;
    std::size_t   wrongExtent = 0;
    std::size_t   valueWithoutBit = 0;
    std::size_t   dishonestParams = 0;
    std::size_t   fsrFgRecords = 0;
};

// Inject into an ffx-api target and drain a post-install window.
//
// ONE HELPER FOR EVERY COMBINATION -- both topologies, both K values, PREPARE on and
// off -- for the reason ObserveFg gives: two hand-written cases let one acquire a
// tolerance the other does not have, and the pair stops discriminating the moment
// they differ.
bool ObserveFfx(int presentsPerEval, const wchar_t* topology, bool prepare, fl::FlUpscaler expected,
                FfxObservation& out) {
    Child        child;
    std::wstring cmd = std::wstring(L"\"") + FL_HARNESS_EXE + L"\" --real --presents-per-eval " +
                       std::to_wstring(presentsPerEval) + L" --ffx-topology " + topology +
                       (prepare ? L"" : L" --ffx-no-prepare") + L" --hold-presenting-ffx 14";
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    if (!CreateProcessW(FL_HARNESS_EXE, cmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si,
                        &child.pi)) {
        return false;
    }
    Sleep(800);
    if (WaitForSingleObject(child.pi.hProcess, 0) != WAIT_TIMEOUT) {
        return false;
    }

    ResetFake();
    g.modules = {"kernel32.dll"};
    g.scanSet = {child.pi.dwProcessId};
    if (!GuardedInjectWithSources(child.pi.dwProcessId, FL_OVERLAY_DLL, FakeSources()).Allowed()) {
        return false;
    }

    wchar_t name[128]{};
    if (_snwprintf_s(name, _TRUNCATE, L"Local\\FrameLedger.Ring.%lu", child.pi.dwProcessId) <= 0) {
        return false;
    }
    HANDLE mapping = nullptr;
    for (int i = 0; i < 100 && mapping == nullptr; ++i) {
        mapping = OpenFileMappingW(FILE_MAP_READ, FALSE, name);
        if (mapping == nullptr) {
            Sleep(50);
        }
    }
    if (mapping == nullptr) {
        return false;
    }
    const void* base = MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, 0);
    if (base == nullptr) {
        CloseHandle(mapping);
        return false;
    }

    const auto* st =
        reinterpret_cast<const fl::FlWriterState*>(static_cast<const unsigned char*>(base) + FL_SHM_WRITER_OFFSET);
    for (int i = 0; i < 100 && st->status != fl::FL_STATUS_READY; ++i) {
        Sleep(50);
    }

    // ONE ROW PUBLISHES ALL THREE FAMILIES, so the FG_EVALUATIONS wait is the same wait
    // as identity's -- kept anyway, so the assertion is about the family the count is
    // entitled by rather than about which bit happened to be set first.
    bool ok = st->status == fl::FL_STATUS_READY && WaitForIdentityHook(st) &&
              WaitForHookFamily(st, static_cast<uint32_t>(fl::FL_HOOK_FG_EVALUATIONS));
    if (ok) {
        fl::RingReader rd;
        ok = rd.Init(base, FL_SHM_DEFAULT_CAPACITY);
        if (ok) {
            DiscardBacklog(rd);
            const std::vector<fl::FlFrameRecord> all = DrainAtLeast(rd, 60, 9000);
            out.drained = all.size();
            for (const auto& r : all) {
                out.sigma += r.fgEvaluations;
                if ((r.measuredMask & fl::FL_MEASURED_FG_COUNTS) == 0u) {
                    out.everyRecordClaimsCounts = false;
                }
                if (r.fgMode == fl::FL_FG_NONE) {
                    out.modeIsNeverNone = false;
                }
                if (r.fgMode == fl::FL_FG_FSR_FG) {
                    ++out.fsrFgRecords;
                }
                if ((r.measuredMask & fl::FL_MEASURED_UPSCALER) != 0u) {
                    ++out.measured;
                }
                if (r.upscaler == expected) {
                    ++out.identified;
                }
                if (r.upscaler == fl::FL_UPSCALER_DLSS) {
                    ++out.claimedDlss;
                }
                if (r.upscaler == fl::FL_UPSCALER_NONE) {
                    ++out.claimedNone;
                }
                if ((r.measuredMask & fl::FL_MEASURED_UPSCALER_PARAMS) != 0u) {
                    ++out.paramsRecords;
                    if (r.upscalerQuality == 0u || r.upscalerSharpness != 0xFFu) {
                        ++out.dishonestParams;
                    }
                    if (r.renderW != FL_TAGGED_RENDER_W || r.renderH != FL_TAGGED_RENDER_H) {
                        ++out.wrongExtent;
                    }
                } else if (r.renderW != 0u || r.renderH != 0u) {
                    ++out.valueWithoutBit;
                }
            }
        }
    }
    out.hooks = st->hooksInstalledMask;
    out.census = st->runtimeCensus;
    out.faults = st->faultCount;

    UnmapViewOfFile(base);
    CloseHandle(mapping);
    return ok;
}

// The assertions every combination shares. Catch2's CHECK works from a helper, and
// one helper is what keeps the four runs asserting ONE expression.
void AssertFfxWindow(const FfxObservation& obs, int k) {
    CAPTURE(obs.drained);
    CAPTURE(obs.sigma);
    CAPTURE(obs.measured);
    CAPTURE(obs.identified);
    CAPTURE(obs.paramsRecords);
    CAPTURE(obs.fsrFgRecords);

    // A COUNT OF ZERO FAILS HERE RATHER THAN DIVIDING BY IT.
    REQUIRE(obs.sigma >= 8u);

    // THE SAME TOLERANCE THE TOKEN TEST DERIVES, for the same arithmetic: each
    // application frame is followed by exactly K presents. A writer counting PREPARE
    // CALLS (two per frame) reads 2x here at K = 1; a writer that also hooked the
    // loader reads 2x on the 2.x topology; a writer counting presents reads K x K.
    const long long drained = static_cast<long long>(obs.drained);
    const long long expected = static_cast<long long>(obs.sigma) * k;
    const long long diff = drained > expected ? drained - expected : expected - drained;
    CHECK(diff <= k);

    CHECK(obs.everyRecordClaimsCounts);
    CHECK(obs.modeIsNeverNone);
    CHECK(obs.faults == 0u);

    // Identity is claimed on every record once a leaf is hooked, and NAMED on the
    // presents that drained an UPSCALE -- one per application frame, so sigma of them
    // within a record of drain alignment. Never DLSS, never NONE.
    CHECK(obs.measured == obs.drained);
    CHECK(obs.identified + 1 >= obs.sigma);
    CHECK(obs.identified <= obs.sigma + 1);
    CHECK(obs.claimedDlss == 0u);
    CHECK(obs.claimedNone == 0u);

    // renderW/H come off the descriptor the harness built, so the EXACT tagged extent
    // and nothing else; quality and sharpness are 0xFF on every record that claims the
    // bit; no value leaks onto a record whose bit is clear.
    CHECK(obs.paramsRecords + 1 >= obs.sigma);
    CHECK(obs.wrongExtent == 0u);
    CHECK(obs.valueWithoutBit == 0u);
    CHECK(obs.dishonestParams == 0u);

    // FSR_FG is named on the presents that drained a FRAMEGENERATION dispatch -- one
    // per application frame at K > 1, none at all at K = 1. The K = 1 half is what
    // stops a writer from naming frame generation off the PREPARE alone.
    if (k > 1) {
        CHECK(obs.fsrFgRecords + 1 >= obs.sigma);
    } else {
        CHECK(obs.fsrFgRecords == 0u);
    }

    // EQUALITY, not a bit test, for the reason the token test gives: the compound
    // family constant is the one value no gate reads.
    CHECK(obs.hooks == static_cast<std::uint32_t>(fl::FL_HOOK_PRESENT | fl::FL_HOOK_UPSCALER_IDENTITY |
                                                  fl::FL_HOOK_UPSCALER_PARAMS | fl::FL_HOOK_FG_EVALUATIONS));
    CHECK((obs.census & fl::FL_CENSUS_RAN) != 0u);
    CHECK((obs.census & fl::FL_CENSUS_SL_INTERPOSER) == 0u);
}

}    // namespace

TEST_CASE("FSR through the SDK 2.x loader: identified, extent exact, the count tracks K, and nothing is counted twice",
          "[guard][inject][shm][ffx]") {
    // THE LOADER IS IN THE CHAIN AND IS HOOKED, AND SO ARE THE LEAVES BEHIND IT. The
    // game calls the loader's export -- measured on Dying Light: The Beast, KCD2 and
    // Wukong, whose leaf exports stayed silent under the leaf-only build -- and the
    // stub forwards the way the real one was measured to: through the leaves' direct
    // entry, not their export. So every dispatch is counted once, at the loader, and
    // the K = 1 control below reads 1.0. A loader that re-entered a leaf's export would
    // be counted at both and read 2.0 here, which is exactly the shape the row set
    // must never ship.
    for (const int k : {1, 4}) {
        CAPTURE(k);
        FfxObservation obs;
        REQUIRE(ObserveFfx(k, L"2x", /*prepare=*/true, fl::FL_UPSCALER_FSR_UNVERSIONED, obs));
        AssertFfxWindow(obs, k);

        // The census sees the two SDK 2.x leaves by name and NOT the monolith -- and
        // the frame-generation leaf is in the FG group, so a title on this shape prints
        // the WARNING shape only when nothing was measured, which here it was.
        CHECK((obs.census & fl::FL_CENSUS_AMD_FFX_UPSCALER) != 0u);
        CHECK((obs.census & fl::FL_CENSUS_AMD_FFX_FRAMEGENERATION) != 0u);
        CHECK((obs.census & fl::FL_CENSUS_AMD_FFX_DX12) == 0u);
    }
}

TEST_CASE("the SDK 1.1.x monolith is FSR3; with no PREPARE the count falls back to UPSCALE dispatches",
          "[guard][inject][shm][ffx]") {
    // TWO SHAPES A 1.1.x TITLE ACTUALLY HAS. Frame generation OFF: no PREPARE is ever
    // issued, so the application-frame count must come from the UPSCALE dispatches and
    // K = 1 must still read 1.0 -- a writer that counted only prepares reads sigma = 0
    // and fails the floor. Frame generation ON at x4: the pre-V2 PREPARE type, read
    // through the V2 layout the Overlay asserts is prefix-identical.
    for (const int k : {1, 4}) {
        CAPTURE(k);
        FfxObservation obs;
        REQUIRE(ObserveFfx(k, L"1x", /*prepare=*/k > 1, fl::FL_UPSCALER_FSR3, obs));
        AssertFfxWindow(obs, k);

        CHECK((obs.census & fl::FL_CENSUS_AMD_FFX_DX12) != 0u);
        CHECK((obs.census & fl::FL_CENSUS_AMD_FFX_UPSCALER) == 0u);
        CHECK((obs.census & fl::FL_CENSUS_AMD_FFX_FRAMEGENERATION) == 0u);
    }
}

TEST_CASE("the FSR 3.0 host facade: Fsr3 identity and the count from its upscale, alone and beside the 1.1.x "
          "monolith generating",
          "[guard][inject][shm][ffx]") {
    // THE FIFTH AMD TARGET, AND NOT A LEAF: Cyberpunk 2077 at FSR 3 upscales through
    // ffx_fsr3_x64.dll's NAMED export and generates through the 1.1.x monolith's
    // ffxDispatch, and the leaf-only build printed `upscaler: N/A` beside `FsrFg`.
    //
    // (a) The host ALONE at K = 1 with no PREPARE anywhere: the application-frame count
    // must come from the host's UPSCALE and read 1.0 -- the double-count control for the
    // row (a writer that counted the call at two addresses reads 2.0), and the proof that
    // the host's count reaches fgEvaluations at all (a row claiming IDENTITY|PARAMS alone
    // would fail the floor here). Identity is FSR3 as a fact -- the 3.0 host hosts
    // nothing else -- and the census sees the host and NOT the monolith.
    {
        FfxObservation obs;
        REQUIRE(ObserveFfx(1, L"fsr3host", /*prepare=*/false, fl::FL_UPSCALER_FSR3, obs));
        AssertFfxWindow(obs, 1);

        CHECK((obs.census & fl::FL_CENSUS_FFX_FSR3) != 0u);
        CHECK((obs.census & fl::FL_CENSUS_AMD_FFX_DX12) == 0u);
    }

    // (b) Cyberpunk's shape: the host upscales while the monolith prepares (pre-V2, twice
    // per frame) and, at K = 4, generates. Identity can only have come from the host --
    // the monolith receives no UPSCALE -- while FsrFg at K = 4 and the count from the
    // PREPARE latch come from the monolith, and the family is published once BOTH are
    // patched (a family published on the first would entitle records the other module
    // was not yet producing).
    for (const int k : {1, 4}) {
        CAPTURE(k);
        FfxObservation obs;
        REQUIRE(ObserveFfx(k, L"fsr3host+mono", /*prepare=*/k > 1, fl::FL_UPSCALER_FSR3, obs));
        AssertFfxWindow(obs, k);

        CHECK((obs.census & fl::FL_CENSUS_FFX_FSR3) != 0u);
        CHECK((obs.census & fl::FL_CENSUS_AMD_FFX_DX12) != 0u);
        CHECK((obs.census & fl::FL_CENSUS_AMD_FFX_UPSCALER) == 0u);
    }
}

TEST_CASE("the UE5 shape: the two SDK 2.x leaves called directly, no loader in the process",
          "[guard][inject][shm][ffx]") {
    // Hell Is Us and Expedition 33 -- the engine plugin compiles the MIT loader in and
    // calls the effect DLLs' exports itself. With the loader hooked as well, this is the
    // case that keeps the LEAF detours exercised: the loader case above never reaches
    // them (the forward is direct), so a leaf trampoline that stopped working would be
    // invisible there. K = 4 only: no forwarder is in this chain, so the double-count
    // control has nothing to control for, and the frame-generation shape is the one
    // that touches both leaves.
    FfxObservation obs;
    REQUIRE(ObserveFfx(4, L"ue", /*prepare=*/true, fl::FL_UPSCALER_FSR_UNVERSIONED, obs));
    AssertFfxWindow(obs, 4);

    CHECK((obs.census & fl::FL_CENSUS_AMD_FFX_UPSCALER) != 0u);
    CHECK((obs.census & fl::FL_CENSUS_AMD_FFX_FRAMEGENERATION) != 0u);
    CHECK((obs.census & fl::FL_CENSUS_AMD_FFX_DX12) == 0u);
}

namespace {

struct RtObservation {
    std::size_t   drained = 0;
    std::size_t   withAsBuild = 0;
    std::size_t   withDispatch = 0;
    std::uint64_t volume = 0;
    std::uint32_t hooks = 0;
    std::uint32_t faults = 0;
    std::uint32_t rtTier = 0;
    bool          everyRecordClaimsRt = true;
    bool          volumeOnlyWithDispatchBit = true;
};

// Inject into a target that RECORDS ray-tracing work, and drain a post-install
// window.
//
// ONE HELPER, TWO MODES, for the reason ObserveFg gives: two hand-written cases
// let one of them acquire a tolerance the other does not have, and the pair stops
// discriminating the moment they differ. The modes differ by exactly one recorded
// call -- the fixture shares its acceleration structure, its state object, its
// swapchain and its loop -- so a difference in the record is attributable to that
// call and to nothing else.
bool ObserveRt(const wchar_t* mode, RtObservation& out) {
    Child        child;
    std::wstring cmd = std::wstring(L"\"") + FL_HARNESS_EXE + L"\" --real --present-interval-ms 4 " + mode + L" 14";
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    if (!CreateProcessW(FL_HARNESS_EXE, cmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si,
                        &child.pi)) {
        return false;
    }
    Sleep(800);
    if (WaitForSingleObject(child.pi.hProcess, 0) != WAIT_TIMEOUT) {
        return false;
    }

    ResetFake();
    g.modules = {"kernel32.dll"};
    g.scanSet = {child.pi.dwProcessId};
    if (!GuardedInjectWithSources(child.pi.dwProcessId, FL_OVERLAY_DLL, FakeSources()).Allowed()) {
        return false;
    }

    wchar_t name[128]{};
    if (_snwprintf_s(name, _TRUNCATE, L"Local\\FrameLedger.Ring.%lu", child.pi.dwProcessId) <= 0) {
        return false;
    }
    HANDLE mapping = nullptr;
    for (int i = 0; i < 100 && mapping == nullptr; ++i) {
        mapping = OpenFileMappingW(FILE_MAP_READ, FALSE, name);
        if (mapping == nullptr) {
            Sleep(50);
        }
    }
    if (mapping == nullptr) {
        return false;
    }
    const void* base = MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, 0);
    if (base == nullptr) {
        CloseHandle(mapping);
        return false;
    }

    const auto* st =
        reinterpret_cast<const fl::FlWriterState*>(static_cast<const unsigned char*>(base) + FL_SHM_WRITER_OFFSET);
    for (int i = 0; i < 100 && st->status != fl::FL_STATUS_READY; ++i) {
        Sleep(50);
    }

    // POLLED, never read once. The RT hooks install on a WATCHDOG tick, and the
    // watchdog cannot install them until a D3D12 swapchain has presented at least
    // once -- that is when ResolveApi first reaches the device. So READY does not
    // imply these hooks exist, and a single read would race the mechanism under
    // test (HANDOFF section Traps).
    bool ok =
        st->status == fl::FL_STATUS_READY && WaitForHookFamily(st, static_cast<uint32_t>(fl::FL_HOOK_RT_AS_BUILD));
    if (ok) {
        fl::RingReader rd;
        ok = rd.Init(base, FL_SHM_DEFAULT_CAPACITY);
        if (ok) {
            DiscardBacklog(rd);
            const std::vector<fl::FlFrameRecord> all = DrainAtLeast(rd, 60, 9000);
            out.drained = all.size();
            for (const auto& r : all) {
                if ((r.measuredMask & fl::FL_MEASURED_RT) == 0u) {
                    out.everyRecordClaimsRt = false;
                }
                if ((r.rtFlags & fl::FL_RT_AS_BUILD_OBSERVED) != 0u) {
                    ++out.withAsBuild;
                }
                if ((r.rtFlags & fl::FL_RT_DISPATCH_OBSERVED) != 0u) {
                    ++out.withDispatch;
                } else if (r.dispatchRaysVolume != 0u) {
                    // A volume with no dispatch bit is the writer contradicting
                    // itself, and it is the shape a drain that cleared one word and
                    // not the other would produce.
                    out.volumeOnlyWithDispatchBit = false;
                }
                out.volume += r.dispatchRaysVolume;
            }
        }
    }
    out.hooks = st->hooksInstalledMask;
    out.faults = st->faultCount;
    out.rtTier = st->rtTier;

    UnmapViewOfFile(base);
    CloseHandle(mapping);
    return ok;
}

}    // namespace

TEST_CASE("the injected Overlay records ray-tracing work, and an AS-build-only title is not a negative",
          "[guard][inject][shm][rt]") {
    // TWO FIXTURES, ONE EXPRESSION, and the SECOND one is what makes 03_METRICS:226
    // falsifiable rather than a sentence in a document.
    //
    // The rayquery arm records acceleration-structure builds and never dispatches --
    // the observable signature of an inline-RayQuery title. A writer with only the
    // DispatchRays hook sees NOTHING there, and its silence is indistinguishable
    // from a real negative: rtTier is at or above CAPABLE_MIN, the mask bit is set,
    // evidence is zero, and 03_METRICS' No branch fires about a title that
    // ray-traces every frame. Only the AS-build hook separates the two, and only
    // this pair proves that it does.
    //
    // It does NOT run a RayQuery shader, and saying so matters: no shader is
    // compiled and none is needed, because the claim under test is "AS-build catches
    // a title DispatchRays misses" and the ABSENCE of a dispatch is what tests it.

    RtObservation dxr;
    REQUIRE(ObserveRt(L"--hold-presenting-dxr", dxr));

    CAPTURE(dxr.drained, dxr.withAsBuild, dxr.withDispatch, dxr.volume, dxr.hooks, dxr.rtTier);

    CHECK(dxr.faults == 0u);
    CHECK(dxr.rtTier >= static_cast<std::uint32_t>(fl::FL_RT_TIER_CAPABLE_MIN));

    // BOTH FAMILIES, separately. 03_METRICS' No branch reads RtAsBuild
    // specifically, so a compound bit would let a DispatchRays-only writer satisfy
    // the conjunct the whole branch rests on.
    CHECK((dxr.hooks & static_cast<std::uint32_t>(fl::FL_HOOK_RT_DISPATCH)) != 0u);
    CHECK((dxr.hooks & static_cast<std::uint32_t>(fl::FL_HOOK_RT_AS_BUILD)) != 0u);

    // EVERY record claims it, including the ones that saw nothing. A writer that set
    // the bit only on RT-active presents would leave a consumer computing
    // rt_frame_pct over a population of exactly the frames that had evidence, i.e.
    // 100% on every title.
    CHECK(dxr.everyRecordClaimsRt);
    CHECK(dxr.withAsBuild >= 8u);
    CHECK(dxr.withDispatch >= 8u);
    CHECK(dxr.volumeOnlyWithDispatchBit);

    // THE FIXTURE'S OWN ARITHMETIC, from the shared CMake constants rather than a
    // second copy of the numbers. One dispatch of W*H*D rays per recorded frame, so
    // the total must be an exact multiple of it -- a writer that summed something
    // else, or that dropped the depth, lands off the lattice.
    const std::uint64_t perDispatch =
        static_cast<std::uint64_t>(FL_DXR_DISPATCH_W) * FL_DXR_DISPATCH_H * FL_DXR_DISPATCH_D;
    CHECK(perDispatch == 2048u);
    CHECK(dxr.volume >= perDispatch * dxr.withDispatch);
    CHECK(dxr.volume % perDispatch == 0u);

    RtObservation rayquery;
    REQUIRE(ObserveRt(L"--hold-presenting-rayquery", rayquery));

    CAPTURE(rayquery.drained, rayquery.withAsBuild, rayquery.withDispatch, rayquery.volume);

    CHECK(rayquery.faults == 0u);
    CHECK(rayquery.everyRecordClaimsRt);
    CHECK(rayquery.withAsBuild >= 8u);

    // THE DISCRIMINATING ASSERTION. Same fixture, same device, same loop, one call
    // removed -- and the evidence the AS-build hook produces survives while the
    // dispatch evidence is gone entirely. Without this the case above passes for a
    // writer that sets both bits from either detour.
    CHECK(rayquery.withDispatch == 0u);
    CHECK(rayquery.volume == 0u);
}
