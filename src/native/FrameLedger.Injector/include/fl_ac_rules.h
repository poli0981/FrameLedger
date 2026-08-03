// The anti-cheat blocklist, as data (docs/19_SAFETY §Blocklist seed).
//
// Fixed-capacity throughout. Nothing here allocates, and every bound is a
// refusal rather than a truncation: a rules file that does not fit is a rules
// file we do not understand, and 19_SAFETY has no "understood most of it" state.

#ifndef FL_AC_RULES_H
#define FL_AC_RULES_H

#include <cstddef>
#include <cstdint>

namespace fl::guard {

enum class MatchKind : std::uint8_t { kExact = 0, kPrefix };

// Which evidence group an entry belongs to. Group membership is load-bearing,
// not decoration: Riot Vanguard in `drivers` is the machine-wide gate, and the
// same family moved to `modules` would satisfy a group-agnostic check while
// that gate silently lost its only entry (tools/rules-validate.ps1 enforces the
// same thing on the data side).
enum class Group : std::uint8_t { kModules = 0, kDrivers, kDirectories, kServices, kFiles, kGroupCount };

// EVERY CAP BELOW IS ALSO A SCHEMA BOUND. rules/detection-rules.schema.json must
// not accept a file this parser then refuses: exceeding any of these is not a
// rejected entry, it is ParseResult::kMalformed for the WHOLE FILE, which means
// the guard refuses every title on the machine. Rules ship as updatable data
// pushed to every client, so that is a fleet-wide outage published by a CI-green
// edit. tools/rules-validate.ps1 reads these constants back out of this header
// rather than restating them, and ctest fl_rules_budget generates its boundary
// cases from them, so the two cannot drift again.
//
// Note the off-by-one: CopyToken reserves a byte for the NUL and rejects at
// `len >= cap`, so kMaxValueLen 96 admits 95 characters, not 96.
inline constexpr std::size_t kMaxFamilies = 64;
inline constexpr std::size_t kMaxValuesPerFamily = 16;
inline constexpr std::size_t kMaxValueLen = 96;
inline constexpr std::size_t kMaxFamilyNameLen = 64;
inline constexpr std::size_t kMaxNameFragments = 16;
inline constexpr std::size_t kMaxTrustedSigners = 16;
inline constexpr std::size_t kMaxRulesBytes = 1u << 20;    // 1 MiB

// Per-title rules (check 3). These are OBJECTS in the schema, not bare strings:
// 19_SAFETY requires the UI to name the check that fired and why, and a bare exe
// name carries neither. kMaxTitleRules x kMaxValuesPerTitleRule = 512 blockable
// names per array, more than the 256 the previous flat cap allowed.
inline constexpr std::size_t kMaxTitleRules = 64;
inline constexpr std::size_t kMaxValuesPerTitleRule = 8;
inline constexpr std::size_t kMaxReasonLen = 128;

// jsmn tokenises the WHOLE buffer before FindMember locates `anticheat` — it has
// no skip mode — so every engine, platform and capability rule in the file
// consumes the hard gate's parse budget, and overflow is JSMN_ERROR_NOMEM ->
// kTooLarge -> refuse everything. The coupling is structural, not an
// implementation choice.
//
// Measured 2026-08-03 on the shipped seed: 9,128 bytes, 475 tokens, of which 275
// (58%) are $comment/engines/platforms/capabilities this parser never reads.
//
// The budget is deliberately HALF the capacity. Crossing it fails the build
// while there is still room to act, forcing a considered choice — raise
// kMaxTokens and pay the BSS, or split the file — rather than discovering the
// wall as a machine-wide refusal in the field.
inline constexpr std::size_t kMaxTokens = 8192;
inline constexpr std::size_t kRulesTokenBudget = kMaxTokens / 2;

// The shortest prefix we will honour. A 1-3 character prefix matches a large
// share of ordinary Windows DLLs; over-matching fails CLOSED, so it cannot get
// anyone banned, but it refuses every title on the machine — which is how a
// user ends up hunting for the override CLAUDE.md rule 2 says does not exist.
inline constexpr std::size_t kMinPrefixLen = 4;

struct Family {
    char        name[kMaxFamilyNameLen] = {};
    Group       group = Group::kModules;
    MatchKind   match = MatchKind::kExact;
    char        values[kMaxValuesPerFamily][kMaxValueLen] = {};
    std::size_t valueCount = 0;
};

// One per-title entry (check 3): which title, and what to tell the user.
//
// `reason` is not decoration. 19_SAFETY requires the refusal to name the check
// that fired, and check 3 is the only one whose signal — an executable name —
// explains nothing on its own: "BlockedExecutable: game.exe" tells a user
// nothing they can act on, where "this title is a competitive online game"
// does.
struct TitleRule {
    char        family[kMaxFamilyNameLen] = {};
    char        reason[kMaxReasonLen] = {};
    MatchKind   match = MatchKind::kExact;
    char        values[kMaxValuesPerTitleRule][kMaxValueLen] = {};
    std::size_t valueCount = 0;
};

struct Rules {
    Family      families[kMaxFamilies] = {};
    std::size_t familyCount = 0;

    TitleRule   blockedExecutables[kMaxTitleRules] = {};
    std::size_t blockedExecutableCount = 0;

    // The schema carries `store` and `id` separately; the parser composes them
    // into the single joined form the matcher compares ("steam:730"), so one
    // array covers every platform and the caller needs no per-store branch.
    TitleRule   blockedStoreIds[kMaxTitleRules] = {};
    std::size_t blockedStoreIdCount = 0;

    // The unknown-but-suspicious heuristic. Signers are compared against the
    // certificate subject's O= field, NOT CN= — measured, because every
    // WHQL-signed binary including the NVIDIA display driver carries
    // CN='Microsoft Windows Hardware Compatibility Publisher' and a CN match
    // would make the whole driver stack read as untrusted (spike-notes.md §1).
    char        nameFragments[kMaxNameFragments][kMaxValueLen] = {};
    std::size_t nameFragmentCount = 0;
    char        trustedSigners[kMaxTrustedSigners][kMaxValueLen] = {};
    std::size_t trustedSignerCount = 0;
};

enum class ParseResult : std::uint8_t {
    kOk = 0,
    kMalformed,     // not JSON, or not the shape we require
    kTooLarge,      // exceeded one of the caps above
    kIncomplete,    // parsed, but a required family/group is absent
};

// Parse, then verify the result is USABLE AS A GATE. A syntactically valid
// rules file with an empty `modules` array parses fine and blocks nothing, so
// completeness is checked here rather than being left to whoever wrote the file
// (the same required-family floor tools/rules-validate.ps1 applies in CI —
// deliberately duplicated, because CI is not in the loop at injection time).
[[nodiscard]] ParseResult ParseRules(const char* json, std::size_t length, Rules& out) noexcept;

// Case-insensitive match of one observed name against the blocklist, scoped to
// a group. Returns the matching family, or nullptr.
//
// `observed` is a base name for modules/services/files and a full native path
// for drivers; drivers therefore match on the path's leaf.
[[nodiscard]] const Family* MatchName(const Rules& rules, Group group, const char* observed) noexcept;

// Per-title lists (check 3). Return the matching rule so the caller can report
// the family and the reason, or nullptr.
//
// TWO SEPARATE REASONS THESE MATCH NOTHING TODAY, and §S14 records only the
// first: both arrays are empty in the shipped seed, AND neither function has a
// single call site anywhere in the tree — check 3 is UNWIRED, not merely
// unpopulated, so filling the data would change nothing. `storeId` is the
// joined form ("steam:730"); an unresolvable identity must reach the caller as
// unknown, never as a clean miss (§S14's second question, still open).
[[nodiscard]] const TitleRule* MatchesBlockedExecutable(const Rules& rules, const char* exeName) noexcept;
[[nodiscard]] const TitleRule* MatchesBlockedStoreId(const Rules& rules, const char* storeId) noexcept;

// True if `moduleName` contains a suspicious fragment. The caller pairs this
// with a signer check; a fragment alone never refuses (19_SAFETY: "name
// fragment AND not signed by a known vendor").
[[nodiscard]] bool HasSuspiciousFragment(const Rules& rules, const char* moduleName) noexcept;
[[nodiscard]] bool IsTrustedSigner(const Rules& rules, const char* signerOrganisation) noexcept;

// The ONE location rules are read from, shared by the injection guard and the
// Vulkan layer.
//
// Not a parameter anywhere (§S3: letting a caller name the rules path is a
// documented override of the hard gate), and not duplicated as a literal in two
// files either — the layer reading a different file from the guard would be a
// second blocklist by accident, which is the same defect as a second matcher.
//
// Writes `%LOCALAPPDATA%\FrameLedger\rules\detection-rules.json` into `out`.
// Returns false if it does not fit or LOCALAPPDATA is unavailable.
[[nodiscard]] bool RulesFilePath(char* out, std::size_t cap) noexcept;

// Read the rules file into a caller-owned buffer. Returns bytes read, or
// SIZE_MAX on any failure — absent, unreadable, or larger than `cap`.
[[nodiscard]] std::size_t ReadRulesFile(char* buffer, std::size_t cap) noexcept;

}    // namespace fl::guard

#endif    // FL_AC_RULES_H
