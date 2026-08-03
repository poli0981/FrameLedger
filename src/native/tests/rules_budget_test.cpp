// The rules file that SHIPS, measured against the parser that has to read it.
//
// guard_test.cpp proves the matcher works — but every one of its cases parses an
// inline GoodRulesJson(), so nothing in this repository proved that
// rules/detection-rules.json, the file we actually publish, parses in the guard
// at all. It does not have to: the schema was looser than the parser in eight
// separate bounds, and exceeding any of them is ParseResult::kMalformed for the
// WHOLE FILE, which the guard turns into "refuse every title on this machine".
// Rules ship as updatable data pushed to every client, so a CI-green edit could
// have taken the product out in the field.
//
// So this file does two things:
//
//   1. Parses the real seed and asserts kOk. That is the load-bearing test.
//   2. GENERATES its boundary cases from the constants in fl_ac_rules.h rather
//      than hand-copying numbers, so the schema, the parser and this test cannot
//      drift apart a second time. If someone raises a cap, the accept case here
//      follows automatically and the reject case moves with it.
//
// The budget assertion has ~8x headroom today and will not fire for a long time.
// That is stated rather than dressed up: its value now is the seed parse.

#include <catch2/catch_test_macros.hpp>
#include <cstdio>
#include <fl_ac_rules.h>
#include <jsmn.h>
#include <string>
#include <vector>

using namespace fl::guard;

namespace {

// FAILS rather than skips when the seed is unreadable.
//
// fl-probe-vklayer deliberately skips when no rules file is installed, because
// it reads from the product's one location and cannot invent one. This test
// reads the file in the repository, by absolute path, at compile time — if that
// is missing something is wrong with the checkout, and "skipped" would be a gate
// that quietly stops guarding the thing it exists to guard.
std::string ReadSeed() {
    std::FILE* f = nullptr;
    REQUIRE(fopen_s(&f, FL_SEED_RULES, "rb") == 0);
    REQUIRE(f != nullptr);
    std::string out;
    char        buf[4096];
    std::size_t n = 0;
    while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0) {
        out.append(buf, n);
    }
    std::fclose(f);
    REQUIRE(!out.empty());
    return out;
}

// The true token count, measured with a scratch array far larger than the
// guard's, so we learn the real number rather than just "it did not fit".
int CountTokens(const std::string& text) {
    std::vector<jsmntok_t> scratch(200000);
    jsmn_parser            p;
    jsmn_init(&p);
    return jsmn_parse(&p, text.c_str(), static_cast<unsigned>(text.size()), scratch.data(),
                      static_cast<unsigned>(scratch.size()));
}

std::string Repeat(char c, std::size_t n) {
    return std::string(n, c);
}

// A rules document that is complete enough to gate, with holes for the piece
// under test. The three required families (19_SAFETY's floor) are always here,
// so a boundary case fails for the reason it is testing and not because
// IsCompleteEnoughToGate rejected it.
struct Doc {
    std::string extraModules;    // extra entries inside modules[]
    std::string blockedExecutables = "[]";
    std::string blockedStoreIds = "[]";
    std::string nameFragments = R"(["anticheat", "antitamper", "gameguard", "guard", "protect"])";
    std::string trustedSigners = R"(["Microsoft Corporation"])";
};

std::string Build(const Doc& d) {
    return std::string(R"({
      "schemaVersion": 2,
      "anticheat": {
        "modules": [
          { "family": "Easy Anti-Cheat", "match": "prefix", "values": ["EasyAntiCheat"] },
          { "family": "BattlEye", "match": "prefix", "values": ["BEClient"] })") +
           d.extraModules + R"(
        ],
        "drivers": [ { "family": "Riot Vanguard", "match": "exact", "values": ["vgk.sys"] } ],
        "directories": [ { "family": "Easy Anti-Cheat", "values": ["EasyAntiCheat"] } ],
        "services": [ { "family": "Riot Vanguard", "values": ["vgc"] } ],
        "files": [ { "family": "Xigncode3", "values": ["x3.xem"] } ],
        "blockedExecutables": )" +
           d.blockedExecutables + R"(,
        "blockedStoreIds": )" +
           d.blockedStoreIds + R"(,
        "heuristic": {
          "signerField": "O",
          "nameFragments": )" +
           d.nameFragments + R"(,
          "trustedSigners": )" +
           d.trustedSigners + R"(,
          "action": "warn_and_refuse"
        }
      }
    })";
}

ParseResult ParseDoc(const std::string& json, Rules& out) {
    return ParseRules(json.c_str(), json.size(), out);
}

// N extra module families, each with one exact value, names kept distinct.
std::string ExtraFamilies(std::size_t n) {
    std::string s;
    for (std::size_t i = 0; i < n; ++i) {
        s += ",\n          { \"family\": \"Filler " + std::to_string(i) + "\", \"match\": \"exact\", \"values\": [\"f" +
             std::to_string(i) + ".sys\"] }";
    }
    return s;
}

// One module family carrying `n` exact values.
std::string FamilyWithValues(std::size_t n) {
    std::string s = ",\n          { \"family\": \"Wide\", \"match\": \"exact\", \"values\": [";
    for (std::size_t i = 0; i < n; ++i) {
        s += (i ? ", " : "");
        s += "\"v" + std::to_string(i) + ".sys\"";
    }
    return s + "] }";
}

}    // namespace

// ===========================================================================
// The seed. This is why the file exists.
// ===========================================================================
TEST_CASE("the rules file that SHIPS parses in the guard", "[rules][seed]") {
    const std::string seed = ReadSeed();

    Rules             rules;
    const ParseResult r = ParseRules(seed.c_str(), seed.size(), rules);

    // Not just "not malformed": kIncomplete would mean the seed lost a required
    // family, which is a shipping blocker of its own.
    REQUIRE(r == ParseResult::kOk);
    CHECK(rules.familyCount > 0);
    CHECK(rules.familyCount <= kMaxFamilies);

    std::printf("[seed] %zu bytes, %zu families of %zu\n", seed.size(), rules.familyCount, kMaxFamilies);
}

TEST_CASE("the shipped seed is inside the guard's parse budget", "[rules][seed][budget]") {
    const std::string seed = ReadSeed();
    const int         tokens = CountTokens(seed);

    REQUIRE(tokens > 0);    // a negative return is a jsmn error code

    // Printed on EVERY run, not only on failure. The whole hazard here is a
    // number nobody looks at until it is already too late, and the budget is
    // deliberately half the capacity so that this line is visible while there
    // is still room to act.
    std::printf("[seed] %d tokens of budget %zu (capacity %zu) - %zu spare\n", tokens, kRulesTokenBudget, kMaxTokens,
                kRulesTokenBudget - static_cast<std::size_t>(tokens));

    CHECK(static_cast<std::size_t>(tokens) <= kRulesTokenBudget);
}

// ===========================================================================
// Boundary cases, generated from the header constants.
//
// Each asserts BOTH directions. A cap test that only proves the reject case
// would pass against a parser that rejects everything.
// ===========================================================================
TEST_CASE("a family holds exactly kMaxValuesPerFamily values, and not one more", "[rules][bounds]") {
    Rules rules;
    Doc   ok;
    ok.extraModules = FamilyWithValues(kMaxValuesPerFamily);
    CHECK(ParseDoc(Build(ok), rules) == ParseResult::kOk);

    Doc over;
    over.extraModules = FamilyWithValues(kMaxValuesPerFamily + 1);
    CHECK(ParseDoc(Build(over), rules) == ParseResult::kMalformed);
}

TEST_CASE("a value holds kMaxValueLen-1 characters, and not kMaxValueLen", "[rules][bounds]") {
    // CopyToken reserves a byte for the NUL and rejects at `len >= cap`, so the
    // schema's maxLength must be kMaxValueLen - 1. It said 128 against a cap of
    // 96, which is where this off-by-one was hiding.
    Rules rules;

    Doc ok;
    ok.extraModules = ",\n          { \"family\": \"Long\", \"match\": \"exact\", \"values\": [\"" +
                      Repeat('a', kMaxValueLen - 1) + "\"] }";
    CHECK(ParseDoc(Build(ok), rules) == ParseResult::kOk);

    Doc over;
    over.extraModules = ",\n          { \"family\": \"Long\", \"match\": \"exact\", \"values\": [\"" +
                        Repeat('a', kMaxValueLen) + "\"] }";
    CHECK(ParseDoc(Build(over), rules) == ParseResult::kMalformed);
}

TEST_CASE("a family name holds kMaxFamilyNameLen-1 characters, and not kMaxFamilyNameLen", "[rules][bounds]") {
    Rules rules;

    Doc ok;
    ok.extraModules = ",\n          { \"family\": \"" + Repeat('F', kMaxFamilyNameLen - 1) +
                      "\", \"match\": \"exact\", \"values\": [\"x.sys\"] }";
    CHECK(ParseDoc(Build(ok), rules) == ParseResult::kOk);

    Doc over;
    over.extraModules = ",\n          { \"family\": \"" + Repeat('F', kMaxFamilyNameLen) +
                        "\", \"match\": \"exact\", \"values\": [\"x.sys\"] }";
    CHECK(ParseDoc(Build(over), rules) == ParseResult::kMalformed);
}

TEST_CASE("the file holds exactly kMaxFamilies families, and not one more", "[rules][bounds]") {
    // The floor already contributes three (EAC, BattlEye, Vanguard) plus one
    // each in directories/services/files, so count from what Build() emits.
    constexpr std::size_t kBaseFamilies = 6;
    Rules                 rules;

    Doc ok;
    ok.extraModules = ExtraFamilies(kMaxFamilies - kBaseFamilies);
    REQUIRE(ParseDoc(Build(ok), rules) == ParseResult::kOk);
    CHECK(rules.familyCount == kMaxFamilies);

    // One past the cap is kTooLarge, not kMalformed — a distinct reason,
    // because "this file is bigger than we can hold" and "this file is not the
    // shape we require" are different problems for whoever has to fix them.
    Doc over;
    over.extraModules = ExtraFamilies(kMaxFamilies - kBaseFamilies + 1);
    CHECK(ParseDoc(Build(over), rules) == ParseResult::kTooLarge);
}

TEST_CASE("a prefix holds kMinPrefixLen characters, and not one fewer", "[rules][bounds]") {
    Rules rules;

    Doc ok;
    ok.extraModules = ",\n          { \"family\": \"Short\", \"match\": \"prefix\", \"values\": [\"" +
                      Repeat('p', kMinPrefixLen) + "\"] }";
    CHECK(ParseDoc(Build(ok), rules) == ParseResult::kOk);

    // The schema said 3 while the parser and tools/rules-validate.ps1 both said
    // 4, so a 3-character prefix validated in CI and then refused every title.
    Doc under;
    under.extraModules = ",\n          { \"family\": \"Short\", \"match\": \"prefix\", \"values\": [\"" +
                         Repeat('p', kMinPrefixLen - 1) + "\"] }";
    CHECK(ParseDoc(Build(under), rules) == ParseResult::kMalformed);
}

TEST_CASE("a heuristic array holds its cap, and not one more", "[rules][bounds]") {
    Rules rules;

    auto list = [](const char* stem, std::size_t n) {
        std::string s = "[";
        for (std::size_t i = 0; i < n; ++i) {
            s += (i ? ", " : "");
            s += std::string("\"") + stem + std::to_string(i) + "\"";
        }
        return s + "]";
    };

    Doc okFrags;
    okFrags.nameFragments = list("frag", kMaxNameFragments);
    CHECK(ParseDoc(Build(okFrags), rules) == ParseResult::kOk);

    Doc overFrags;
    overFrags.nameFragments = list("frag", kMaxNameFragments + 1);
    CHECK(ParseDoc(Build(overFrags), rules) == ParseResult::kMalformed);

    Doc okSigners;
    okSigners.trustedSigners = list("Signer", kMaxTrustedSigners);
    CHECK(ParseDoc(Build(okSigners), rules) == ParseResult::kOk);

    // An ALLOWLIST that can refuse the whole file is doubly perverse: the array
    // whose job is to suppress false refusals was itself a way to cause them.
    Doc overSigners;
    overSigners.trustedSigners = list("Signer", kMaxTrustedSigners + 1);
    CHECK(ParseDoc(Build(overSigners), rules) == ParseResult::kMalformed);
}

// ===========================================================================
// The shape reconciliation. These are the cases that did not exist before,
// because nothing ever put an entry in either array.
// ===========================================================================
TEST_CASE("blockedExecutables entries are objects, and carry family and reason", "[rules][shape]") {
    Rules rules;
    Doc   d;
    d.blockedExecutables =
        R"([{ "family": "Example Online", "match": "exact", "values": ["ranked.exe"], "reason": "competitive online title" }])";
    REQUIRE(ParseDoc(Build(d), rules) == ParseResult::kOk);
    REQUIRE(rules.blockedExecutableCount == 1);

    const TitleRule* hit = MatchesBlockedExecutable(rules, "RANKED.EXE");
    REQUIRE(hit != nullptr);
    CHECK(std::string(hit->family) == "Example Online");
    CHECK(std::string(hit->reason) == "competitive online title");

    // The negative direction, so the positive one means something.
    CHECK(MatchesBlockedExecutable(rules, "singleplayer.exe") == nullptr);
    CHECK(MatchesBlockedExecutable(rules, "") == nullptr);
    CHECK(MatchesBlockedExecutable(rules, nullptr) == nullptr);
}

TEST_CASE("a bare string in blockedExecutables is REFUSED, not silently stored", "[rules][shape][failclosed]") {
    // This is the shape the parser used to accept. It matched nothing, because
    // an exe name is not a rule — so had anyone ever populated the array, the
    // check would have looked configured and blocked nothing.
    Rules rules;
    Doc   d;
    d.blockedExecutables = R"(["ranked.exe"])";
    CHECK(ParseDoc(Build(d), rules) == ParseResult::kMalformed);
}

TEST_CASE("a blockedExecutables entry holds kMaxValuesPerTitleRule values, and not one more", "[rules][bounds]") {
    Rules rules;

    auto entry = [](std::size_t n) {
        std::string s = R"([{ "family": "Example", "match": "exact", "reason": "why", "values": [)";
        for (std::size_t i = 0; i < n; ++i) {
            s += (i ? ", " : "");
            s += "\"e" + std::to_string(i) + ".exe\"";
        }
        return s + "] }]";
    };

    Doc ok;
    ok.blockedExecutables = entry(kMaxValuesPerTitleRule);
    CHECK(ParseDoc(Build(ok), rules) == ParseResult::kOk);

    Doc over;
    over.blockedExecutables = entry(kMaxValuesPerTitleRule + 1);
    CHECK(ParseDoc(Build(over), rules) == ParseResult::kMalformed);
}

TEST_CASE("blockedStoreIds compose store and id into the joined form", "[rules][shape]") {
    Rules rules;
    Doc   d;
    d.blockedStoreIds =
        R"([{ "store": "steam", "id": "730", "family": "Valve VAC", "reason": "VAC-protected online title" }])";
    REQUIRE(ParseDoc(Build(d), rules) == ParseResult::kOk);
    REQUIRE(rules.blockedStoreIdCount == 1);

    // fl_ac_rules.h promises the joined form; the schema keeps the parts apart
    // so it can constrain each. Exactly one place knows the separator.
    const TitleRule* hit = MatchesBlockedStoreId(rules, "steam:730");
    REQUIRE(hit != nullptr);
    CHECK(std::string(hit->family) == "Valve VAC");

    // A store id is an identity, never a prefix: an entry for steam:730 must
    // not take out steam:7300.
    CHECK(MatchesBlockedStoreId(rules, "steam:7300") == nullptr);
    CHECK(MatchesBlockedStoreId(rules, "gog:730") == nullptr);
    CHECK(MatchesBlockedStoreId(rules, "730") == nullptr);
}

TEST_CASE("a per-title entry missing its reason is REFUSED", "[rules][shape][failclosed]") {
    // 19_SAFETY requires the refusal to name the check that fired and why, and
    // check 3's signal — an executable name — explains nothing on its own.
    Rules rules;
    Doc   d;
    d.blockedExecutables = R"([{ "family": "Example", "match": "exact", "values": ["ranked.exe"] }])";
    CHECK(ParseDoc(Build(d), rules) == ParseResult::kMalformed);
}
