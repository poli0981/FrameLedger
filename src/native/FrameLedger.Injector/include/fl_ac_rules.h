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

inline constexpr std::size_t kMaxFamilies = 64;
inline constexpr std::size_t kMaxValuesPerFamily = 16;
inline constexpr std::size_t kMaxValueLen = 96;
inline constexpr std::size_t kMaxFamilyNameLen = 64;
inline constexpr std::size_t kMaxPerTitle = 256;
inline constexpr std::size_t kMaxRulesBytes = 1u << 20;    // 1 MiB

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

struct Rules {
    Family      families[kMaxFamilies] = {};
    std::size_t familyCount = 0;

    char        blockedExecutables[kMaxPerTitle][kMaxValueLen] = {};
    std::size_t blockedExecutableCount = 0;

    // Store ids are strings ("steam:730") so one array covers every platform.
    char        blockedStoreIds[kMaxPerTitle][kMaxValueLen] = {};
    std::size_t blockedStoreIdCount = 0;

    // The unknown-but-suspicious heuristic. Signers are compared against the
    // certificate subject's O= field, NOT CN= — measured, because every
    // WHQL-signed binary including the NVIDIA display driver carries
    // CN='Microsoft Windows Hardware Compatibility Publisher' and a CN match
    // would make the whole driver stack read as untrusted (spike-notes.md §1).
    char        nameFragments[16][kMaxValueLen] = {};
    std::size_t nameFragmentCount = 0;
    char        trustedSigners[16][kMaxValueLen] = {};
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

// Per-title lists. Both are EMPTY in the shipped seed today, so check 3 matches
// nothing — recorded as 20_OPEN_QUESTIONS §S14 rather than left to be inferred.
[[nodiscard]] bool MatchesBlockedExecutable(const Rules& rules, const char* exeName) noexcept;
[[nodiscard]] bool MatchesBlockedStoreId(const Rules& rules, const char* storeId) noexcept;

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
