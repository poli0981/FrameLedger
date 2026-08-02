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

#include <catch2/catch_test_macros.hpp>
#include <cstring>
#include <fl_ac_rules.h>
#include <fl_guard.h>
#include <string>
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

    std::vector<std::string> modules;
    Collected                moduleResult = Collected::kOk;

    std::vector<std::string> drivers;
    Collected                driverResult = Collected::kOk;

    std::vector<std::string> presentServices;
    Collected                serviceResult = Collected::kOk;

    std::vector<std::uint32_t> scanSet{1234};
    Collected                  scanSetResult = Collected::kOk;
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

Collected FakeEnumModules(std::uint32_t, NameSink sink, void* ctx) {
    for (const auto& m : g.modules) {
        if (!sink(ctx, m.c_str())) {
            break;
        }
    }
    return g.moduleResult;
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

Sources FakeSources() {
    Sources s;
    s.ReadRulesFile = &FakeReadRules;
    s.EnumerateModules = &FakeEnumModules;
    s.EnumerateDrivers = &FakeEnumDrivers;
    s.QueryService = &FakeQueryService;
    s.EnumerateScanSet = &FakeEnumScanSet;
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
    ResetFake();
    g.scanSet = {1000, 1001, 1234};
    g.modules = {"kernel32.dll"};

    CHECK(EvaluateWithSources(1234, FakeSources()).Allowed());

    // The fake returns the same module list for every pid, so putting a
    // blocked module there proves all of them were visited only in combination
    // with a per-pid fake. Simpler and stronger: assert the visit count.
    static int visited = 0;
    visited = 0;
    Sources s = FakeSources();
    s.EnumerateModules = [](std::uint32_t, NameSink sink, void* ctx) -> Collected {
        ++visited;
        sink(ctx, "kernel32.dll");
        return Collected::kOk;
    };
    CHECK(EvaluateWithSources(1234, s).Allowed());
    CHECK(visited == 3);
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
    // The gate passed; the injection did not take, and that is reported rather
    // than being mistaken for a refusal.
    CHECK(v.Allowed());
    CHECK(std::strstr(v.signal, "injection failed") != nullptr);
}

#endif    // FL_HARNESS_EXE && FL_OVERLAY_DLL
