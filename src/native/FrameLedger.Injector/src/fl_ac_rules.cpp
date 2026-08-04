// windows.h stays in its own block above the sorted group: clang-format orders
// the rest alphabetically, and knownfolders/objbase/shlobj_core all require it
// to have been seen first.
#include <windows.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <fl_ac_rules.h>
#include <jsmn.h>
#include <knownfolders.h>
#include <objbase.h>
#include <shlobj_core.h>

// Its own block, below the sorted one, because it needs Family/Group/MatchKind
// from fl_ac_rules.h and clang-format sorts each block independently.
// Generated at build time from rules/detection-rules.json — see
// tools/gen-ac-floor.ps1 and src/native/CMakeLists.txt §the floor.
#include <fl_ac_floor.generated.h>

namespace fl::guard {
namespace {

// kMaxTokens lives in the header now: tools/rules-validate.ps1 reads it back out
// to apply the same budget without building anything, and a private copy here
// would be the second number that drifts from the first.

bool IEquals(const char* a, const char* b) noexcept {
    return _stricmp(a, b) == 0;
}

bool IStartsWith(const char* text, const char* prefix) noexcept {
    const std::size_t n = std::strlen(prefix);
    return _strnicmp(text, prefix, n) == 0;
}

bool IContains(const char* haystack, const char* needle) noexcept {
    const std::size_t hn = std::strlen(haystack);
    const std::size_t nn = std::strlen(needle);
    if (nn == 0 || nn > hn) {
        return false;
    }
    for (std::size_t i = 0; i + nn <= hn; ++i) {
        if (_strnicmp(haystack + i, needle, nn) == 0) {
            return true;
        }
    }
    return false;
}

// Copy a jsmn token's text into a fixed buffer. Returns false if it does not
// fit — a value we cannot hold is a value we cannot match, and silently
// truncating it would produce a SHORTER string that matches MORE.
bool CopyToken(const char* json, const jsmntok_t& tok, char* out, std::size_t cap) noexcept {
    if (tok.start < 0 || tok.end < tok.start) {
        return false;
    }
    const std::size_t len = static_cast<std::size_t>(tok.end - tok.start);
    if (len >= cap) {
        return false;
    }
    std::memcpy(out, json + tok.start, len);
    out[len] = '\0';
    return true;
}

bool TokenIs(const char* json, const jsmntok_t& tok, const char* literal) noexcept {
    const std::size_t len = static_cast<std::size_t>(tok.end - tok.start);
    return std::strlen(literal) == len && std::strncmp(json + tok.start, literal, len) == 0;
}

// Index of the value token for `key` inside the object at `objIndex`, or -1.
int FindMember(const char* json, const jsmntok_t* toks, int count, int objIndex, const char* key) noexcept {
    if (objIndex < 0 || objIndex >= count || toks[objIndex].type != JSMN_OBJECT) {
        return -1;
    }
    // Direct children only. JSMN_PARENT_LINKS makes this a single pass and
    // stops a nested object's key from being mistaken for one of ours.
    for (int i = objIndex + 1; i < count; ++i) {
        if (toks[i].parent != objIndex) {
            continue;
        }
        if (toks[i].type == JSMN_STRING && TokenIs(json, toks[i], key) && i + 1 < count) {
            return i + 1;
        }
    }
    return -1;
}

Group GroupFromName(const char* name, bool& ok) noexcept {
    ok = true;
    if (IEquals(name, "modules"))
        return Group::kModules;
    if (IEquals(name, "drivers"))
        return Group::kDrivers;
    if (IEquals(name, "directories"))
        return Group::kDirectories;
    if (IEquals(name, "services"))
        return Group::kServices;
    if (IEquals(name, "files"))
        return Group::kFiles;
    ok = false;
    return Group::kModules;
}

// Read one { family, match?, values[] } entry.
bool ReadFamily(const char* json, const jsmntok_t* toks, int count, int objIndex, Group group, Family& out) noexcept {
    const int nameTok = FindMember(json, toks, count, objIndex, "family");
    if (nameTok < 0 || !CopyToken(json, toks[nameTok], out.name, kMaxFamilyNameLen)) {
        return false;
    }
    out.group = group;

    // `match` is absent for name-only groups (directories/services/files),
    // which are exact by nature.
    out.match = MatchKind::kExact;
    const int matchTok = FindMember(json, toks, count, objIndex, "match");
    if (matchTok >= 0) {
        char buf[16] = {};
        if (!CopyToken(json, toks[matchTok], buf, sizeof(buf))) {
            return false;
        }
        if (IEquals(buf, "prefix")) {
            out.match = MatchKind::kPrefix;
        } else if (!IEquals(buf, "exact")) {
            return false;    // an unknown match kind is not a match kind we can honour
        }
    }

    const int valuesTok = FindMember(json, toks, count, objIndex, "values");
    if (valuesTok < 0 || toks[valuesTok].type != JSMN_ARRAY || toks[valuesTok].size <= 0) {
        return false;    // a family with no values matches nothing: refuse the file
    }
    if (static_cast<std::size_t>(toks[valuesTok].size) > kMaxValuesPerFamily) {
        return false;
    }

    out.valueCount = 0;
    for (int i = valuesTok + 1; i < count; ++i) {
        if (toks[i].parent != valuesTok) {
            continue;
        }
        if (!CopyToken(json, toks[i], out.values[out.valueCount], kMaxValueLen)) {
            return false;
        }
        const std::size_t len = std::strlen(out.values[out.valueCount]);
        if (len == 0) {
            return false;    // a blank value would match everything
        }
        // The prefix floor, enforced HERE and not only in CI: rules ship as
        // updatable data, so the file the guard reads at injection time may
        // never have been through tools/rules-validate.ps1.
        if (out.match == MatchKind::kPrefix && len < kMinPrefixLen) {
            return false;
        }
        if (++out.valueCount >= kMaxValuesPerFamily) {
            break;
        }
    }
    return out.valueCount > 0;
}

// Copy a NUL-terminated literal into a fixed buffer, refusing rather than
// truncating. Used for the generated floor, whose entries the schema already
// bounds — but a generator bug must not produce a SHORTER token that matches
// MORE, which is the same rule CopyToken follows.
bool CopyLiteral(const char* text, char (&out)[kMaxValueLen]) noexcept {
    if (text == nullptr) {
        return false;
    }
    const std::size_t len = std::strlen(text);
    if (len == 0 || len >= kMaxValueLen) {
        return false;
    }
    std::memcpy(out, text, len + 1);
    return true;
}

// APPENDS from the caller's current count rather than resetting it, because the
// floor is already in the array by the time the file is read (§S21). Duplicates
// are skipped case-insensitively: the floor is generated FROM the shipped seed,
// so an unmodified file repeats every entry, and storing both would spend the cap
// twice over — a seed with 9 fragments would then need 18 of kMaxNameFragments 16
// and refuse the whole file.
bool ReadStringArray(const char* json, const jsmntok_t* toks, int count, int arrayTok, char (*out)[kMaxValueLen],
                     std::size_t cap, std::size_t& outCount) noexcept {
    if (arrayTok < 0 || toks[arrayTok].type != JSMN_ARRAY) {
        return false;
    }
    for (int i = arrayTok + 1; i < count; ++i) {
        if (toks[i].parent != arrayTok) {
            continue;
        }
        char buf[kMaxValueLen] = {};
        if (!CopyToken(json, toks[i], buf, kMaxValueLen)) {
            return false;
        }
        bool already = false;
        for (std::size_t j = 0; j < outCount && !already; ++j) {
            already = IEquals(out[j], buf);
        }
        if (already) {
            continue;
        }
        if (outCount >= cap) {
            return false;    // genuinely more distinct entries than we can hold
        }
        std::memcpy(out[outCount], buf, kMaxValueLen);
        ++outCount;
    }
    return true;
}

// True when `candidate` is already in the floor, entry for entry. The floor is
// generated from the shipped seed, so an unmodified rules file repeats all of it;
// appending both copies would spend kMaxFamilies twice and refuse any seed larger
// than half the cap. A family that differs in ANY respect is not a duplicate and
// is kept — matching then sees both, which is what "data may extend" means.
bool SameFamily(const Family& a, const Family& b) noexcept {
    if (a.group != b.group || a.match != b.match || a.valueCount != b.valueCount || !IEquals(a.name, b.name)) {
        return false;
    }
    for (std::size_t i = 0; i < a.valueCount; ++i) {
        if (!IEquals(a.values[i], b.values[i])) {
            return false;
        }
    }
    return true;
}

// Read one blockedExecutables entry: { family, match, values[], reason }.
//
// The schema has always required all four (acBlockedExecutable), while this
// parser read the array as bare strings — so the FIRST entry anyone added would
// either overflow kMaxValueLen with its JSON text and refuse the whole file, or
// fit and be stored as an unmatchable blob of punctuation. Both empty arrays are
// the only reason that never happened.
bool ReadTitleRule(const char* json, const jsmntok_t* toks, int count, int objIndex, TitleRule& out) noexcept {
    if (objIndex < 0 || objIndex >= count || toks[objIndex].type != JSMN_OBJECT) {
        return false;
    }
    const int famTok = FindMember(json, toks, count, objIndex, "family");
    if (famTok < 0 || !CopyToken(json, toks[famTok], out.family, kMaxFamilyNameLen)) {
        return false;
    }
    const int reasonTok = FindMember(json, toks, count, objIndex, "reason");
    if (reasonTok < 0 || !CopyToken(json, toks[reasonTok], out.reason, kMaxReasonLen)) {
        return false;
    }

    const int matchTok = FindMember(json, toks, count, objIndex, "match");
    if (matchTok < 0) {
        return false;    // required here, unlike the name-only groups
    }
    char matchBuf[16] = {};
    if (!CopyToken(json, toks[matchTok], matchBuf, sizeof(matchBuf))) {
        return false;
    }
    if (IEquals(matchBuf, "prefix")) {
        out.match = MatchKind::kPrefix;
    } else if (IEquals(matchBuf, "exact")) {
        out.match = MatchKind::kExact;
    } else {
        return false;
    }

    const int valuesTok = FindMember(json, toks, count, objIndex, "values");
    if (valuesTok < 0 || toks[valuesTok].type != JSMN_ARRAY || toks[valuesTok].size <= 0) {
        return false;
    }
    if (static_cast<std::size_t>(toks[valuesTok].size) > kMaxValuesPerTitleRule) {
        return false;
    }

    out.valueCount = 0;
    for (int i = valuesTok + 1; i < count; ++i) {
        if (toks[i].parent != valuesTok) {
            continue;
        }
        if (!CopyToken(json, toks[i], out.values[out.valueCount], kMaxValueLen)) {
            return false;
        }
        const std::size_t len = std::strlen(out.values[out.valueCount]);
        if (len == 0) {
            return false;    // a blank value would match every title
        }
        if (out.match == MatchKind::kPrefix && len < kMinPrefixLen) {
            return false;    // the same floor ReadFamily applies, for the same reason
        }
        if (++out.valueCount >= kMaxValuesPerTitleRule) {
            break;
        }
    }
    return out.valueCount > 0;
}

// Read one blockedStoreIds entry: { store, id, family, reason }.
//
// The schema keeps store and id apart so it can constrain each (a Steam appid is
// digits); the matcher wants the joined form fl_ac_rules.h promises. Composing
// here means exactly one place knows the separator.
bool ReadStoreRule(const char* json, const jsmntok_t* toks, int count, int objIndex, TitleRule& out) noexcept {
    if (objIndex < 0 || objIndex >= count || toks[objIndex].type != JSMN_OBJECT) {
        return false;
    }
    const int famTok = FindMember(json, toks, count, objIndex, "family");
    if (famTok < 0 || !CopyToken(json, toks[famTok], out.family, kMaxFamilyNameLen)) {
        return false;
    }
    const int reasonTok = FindMember(json, toks, count, objIndex, "reason");
    if (reasonTok < 0 || !CopyToken(json, toks[reasonTok], out.reason, kMaxReasonLen)) {
        return false;
    }

    char      store[kMaxValueLen] = {};
    char      id[kMaxValueLen] = {};
    const int storeTok = FindMember(json, toks, count, objIndex, "store");
    const int idTok = FindMember(json, toks, count, objIndex, "id");
    if (storeTok < 0 || !CopyToken(json, toks[storeTok], store, sizeof(store))) {
        return false;
    }
    if (idTok < 0 || !CopyToken(json, toks[idTok], id, sizeof(id))) {
        return false;
    }
    if (store[0] == '\0' || id[0] == '\0') {
        return false;
    }

    // _snprintf_s truncates on overflow; a truncated store id is a SHORTER
    // string that matches MORE titles, so refuse rather than store it.
    const int written = _snprintf_s(out.values[0], kMaxValueLen, _TRUNCATE, "%s:%s", store, id);
    if (written < 0) {
        return false;
    }
    out.match = MatchKind::kExact;    // a store id is an identity, never a prefix
    out.valueCount = 1;
    return true;
}

using TitleReader = bool (*)(const char*, const jsmntok_t*, int, int, TitleRule&) noexcept;

bool ReadTitleArray(const char* json, const jsmntok_t* toks, int count, int arrayTok, TitleReader read, TitleRule* out,
                    std::size_t& outCount) noexcept {
    outCount = 0;
    if (arrayTok < 0 || toks[arrayTok].type != JSMN_ARRAY) {
        return false;
    }
    if (static_cast<std::size_t>(toks[arrayTok].size) > kMaxTitleRules) {
        return false;
    }
    for (int i = arrayTok + 1; i < count && outCount < kMaxTitleRules; ++i) {
        if (toks[i].parent != arrayTok) {
            continue;
        }
        if (!read(json, toks, count, i, out[outCount])) {
            return false;
        }
        ++outCount;
    }
    return true;
}

// What the FILE supplied, recorded as it is read.
//
// The gate has to be usable, not merely well-formed, and a syntactically perfect
// file with an empty `modules` array blocks nothing — so completeness is checked
// here rather than left to whoever wrote the file.
//
// It is asked of what the file OFFERED, never of what ended up in `Rules`. Two
// reasons, and both have already bitten a version of this code: the floor
// satisfies the check by construction, so scanning the merged set would make this
// a gate that cannot fail; and the floor is generated FROM the shipped seed, so
// an unmodified file is entirely duplicate and stores nothing, which would make
// the correct seed report kIncomplete.
struct SuppliedFamilies {
    struct Required {
        const char* family;
        Group       group;
    };
    // Family AND group. See the comment on Group.
    static constexpr Required kRequired[] = {
        {"Easy Anti-Cheat", Group::kModules},
        {"BattlEye", Group::kModules},
        {"Riot Vanguard", Group::kDrivers},
    };
    static constexpr std::size_t kRequiredCount = sizeof(kRequired) / sizeof(kRequired[0]);

    bool seen[kRequiredCount] = {};
    bool any = false;

    void Note(const Family& f) noexcept {
        any = true;
        for (std::size_t i = 0; i < kRequiredCount; ++i) {
            if (f.group == kRequired[i].group && IEquals(f.name, kRequired[i].family)) {
                seen[i] = true;
            }
        }
    }

    [[nodiscard]] bool Complete() const noexcept {
        if (!any) {
            return false;
        }
        for (std::size_t i = 0; i < kRequiredCount; ++i) {
            if (!seen[i]) {
                return false;
            }
        }
        return true;
    }
};

}    // namespace

const Family* FloorFamilies(std::size_t& count) noexcept {
    count = sizeof(generated::kFloorFamilies) / sizeof(generated::kFloorFamilies[0]);
    return generated::kFloorFamilies;
}

const char* const* FloorFragments(std::size_t& count) noexcept {
    count = sizeof(generated::kFloorFragments) / sizeof(generated::kFloorFragments[0]);
    return generated::kFloorFragments;
}

ParseResult ParseRules(const char* json, std::size_t length, Rules& out) noexcept {
    out = Rules{};

    // §S21. The floor goes in FIRST, before a byte of the file is read, and
    // nothing below ever rewrites or removes these entries — the file's families
    // are appended after them. That ordering is the mechanism: there is no merge
    // step for a crafted file to win, and no code path that can shrink the set.
    std::size_t   floorCount = 0;
    const Family* floor = FloorFamilies(floorCount);
    for (std::size_t i = 0; i < floorCount && out.familyCount < kMaxFamilies; ++i) {
        out.families[out.familyCount++] = floor[i];
    }
    const std::size_t dataFirst = out.familyCount;

    // The fuzzy tier is floored too. §S19(d) records that a rules file with no
    // `heuristic` block parses kOk and the tier silently stops existing, and
    // proposed fixing it with a new ParseResult cause — which its own text says
    // would make kRulesIncomplete's signal a lie and would drive layer.cpp to
    // machine-wide inert passthrough. A floor needs neither: the tier cannot stop
    // existing, because the data never supplied it.
    //
    // trustedSigners is deliberately NOT floored. It is an ALLOW-widening list,
    // so "data may only add" has the wrong polarity there — forcing entries in
    // would be forcing suppressions in.
    std::size_t        fragCount = 0;
    const char* const* frags = FloorFragments(fragCount);
    for (std::size_t i = 0; i < fragCount && out.nameFragmentCount < kMaxNameFragments; ++i) {
        if (CopyLiteral(frags[i], out.nameFragments[out.nameFragmentCount])) {
            ++out.nameFragmentCount;
        }
    }
    if (json == nullptr || length == 0) {
        return ParseResult::kMalformed;
    }
    if (length > kMaxRulesBytes) {
        return ParseResult::kTooLarge;
    }

    static jsmntok_t toks[kMaxTokens];
    jsmn_parser      parser;
    jsmn_init(&parser);
    const int count = jsmn_parse(&parser, json, length, toks, static_cast<unsigned>(kMaxTokens));
    if (count < 1 || toks[0].type != JSMN_OBJECT) {
        // JSMN_ERROR_NOMEM, _INVAL and _PART all land here. All three mean the
        // same thing to a gate: we do not know what this file says.
        return (count == JSMN_ERROR_NOMEM) ? ParseResult::kTooLarge : ParseResult::kMalformed;
    }

    const int acTok = FindMember(json, toks, count, 0, "anticheat");
    if (acTok < 0 || toks[acTok].type != JSMN_OBJECT) {
        return ParseResult::kMalformed;
    }

    SuppliedFamilies supplied;

    static constexpr const char* kGroupNames[] = {"modules", "drivers", "directories", "services", "files"};
    for (const char* groupName : kGroupNames) {
        bool        ok = false;
        const Group group = GroupFromName(groupName, ok);
        if (!ok) {
            return ParseResult::kMalformed;
        }
        const int arrTok = FindMember(json, toks, count, acTok, groupName);
        if (arrTok < 0 || toks[arrTok].type != JSMN_ARRAY) {
            continue;    // absent group: completeness is judged below, not here
        }
        for (int i = arrTok + 1; i < count; ++i) {
            if (toks[i].parent != arrTok || toks[i].type != JSMN_OBJECT) {
                continue;
            }
            Family fam;
            if (!ReadFamily(json, toks, count, i, group, fam)) {
                return ParseResult::kMalformed;
            }

            // Completeness is judged on what the FILE supplied, so it is recorded
            // here — before the duplicate check. The floor is generated from the
            // shipped seed, so an unmodified file duplicates all of it and stores
            // nothing; judging completeness by what was STORED would then report
            // the correct seed as incomplete.
            supplied.Note(fam);

            bool duplicate = false;
            for (std::size_t f = 0; f < dataFirst && !duplicate; ++f) {
                duplicate = SameFamily(out.families[f], fam);
            }
            if (duplicate) {
                continue;
            }
            if (out.familyCount >= kMaxFamilies) {
                return ParseResult::kTooLarge;
            }
            out.families[out.familyCount++] = fam;
        }
    }

    // Per-title lists may legitimately be empty (§S14), but must be present and
    // must be arrays if they exist at all. Both are arrays of OBJECTS — see
    // ReadTitleRule for what reading them as strings would have done.
    const int execTok = FindMember(json, toks, count, acTok, "blockedExecutables");
    if (execTok >= 0 && !ReadTitleArray(json, toks, count, execTok, &ReadTitleRule, out.blockedExecutables,
                                        out.blockedExecutableCount)) {
        return ParseResult::kMalformed;
    }
    const int storeTok = FindMember(json, toks, count, acTok, "blockedStoreIds");
    if (storeTok >= 0 &&
        !ReadTitleArray(json, toks, count, storeTok, &ReadStoreRule, out.blockedStoreIds, out.blockedStoreIdCount)) {
        return ParseResult::kMalformed;
    }

    const int heurTok = FindMember(json, toks, count, acTok, "heuristic");
    if (heurTok >= 0 && toks[heurTok].type == JSMN_OBJECT) {
        const int fragTok = FindMember(json, toks, count, heurTok, "nameFragments");
        if (fragTok >= 0 &&
            !ReadStringArray(json, toks, count, fragTok, out.nameFragments, kMaxNameFragments, out.nameFragmentCount)) {
            return ParseResult::kMalformed;
        }
        const int signTok = FindMember(json, toks, count, heurTok, "trustedSigners");
        if (signTok >= 0 && !ReadStringArray(json, toks, count, signTok, out.trustedSigners, kMaxTrustedSigners,
                                             out.trustedSignerCount)) {
            return ParseResult::kMalformed;
        }
    }

    // Asked of the FILE, never of the merged set (see SuppliedFamilies). A file
    // that supplies no families at all is still kIncomplete even though the floor
    // would gate perfectly well without it: shipping an empty blocklist is a
    // mistake worth surfacing, and the floor is a backstop, not a substitute.
    if (!supplied.Complete()) {
        return ParseResult::kIncomplete;
    }
    return ParseResult::kOk;
}

const Family* MatchName(const Rules& rules, Group group, const char* observed) noexcept {
    if (observed == nullptr || observed[0] == '\0') {
        return nullptr;
    }

    // Drivers arrive as native paths (\SystemRoot\system32\drivers\vgk.sys);
    // match on the leaf.
    const char* leaf = observed;
    if (group == Group::kDrivers) {
        for (const char* p = observed; *p != '\0'; ++p) {
            if (*p == '\\' || *p == '/') {
                leaf = p + 1;
            }
        }
    }

    for (std::size_t i = 0; i < rules.familyCount; ++i) {
        const Family& f = rules.families[i];
        if (f.group != group) {
            continue;
        }
        for (std::size_t v = 0; v < f.valueCount; ++v) {
            const bool hit =
                (f.match == MatchKind::kPrefix) ? IStartsWith(leaf, f.values[v]) : IEquals(leaf, f.values[v]);
            if (hit) {
                return &f;
            }
        }
    }
    return nullptr;
}

const TitleRule* MatchesBlockedExecutable(const Rules& rules, const char* exeName) noexcept {
    if (exeName == nullptr || exeName[0] == '\0') {
        return nullptr;
    }
    for (std::size_t i = 0; i < rules.blockedExecutableCount; ++i) {
        const TitleRule& r = rules.blockedExecutables[i];
        for (std::size_t v = 0; v < r.valueCount; ++v) {
            const bool hit =
                (r.match == MatchKind::kPrefix) ? IStartsWith(exeName, r.values[v]) : IEquals(exeName, r.values[v]);
            if (hit) {
                return &r;
            }
        }
    }
    return nullptr;
}

const TitleRule* MatchesBlockedStoreId(const Rules& rules, const char* storeId) noexcept {
    if (storeId == nullptr || storeId[0] == '\0') {
        return nullptr;
    }
    for (std::size_t i = 0; i < rules.blockedStoreIdCount; ++i) {
        const TitleRule& r = rules.blockedStoreIds[i];
        // Always exact: ReadStoreRule composes one joined value and forces
        // kExact, because a store id is an identity and a prefix over identities
        // would block "steam:7300" for an entry naming "steam:730".
        for (std::size_t v = 0; v < r.valueCount; ++v) {
            if (IEquals(storeId, r.values[v])) {
                return &r;
            }
        }
    }
    return nullptr;
}

bool HasSuspiciousFragment(const Rules& rules, const char* moduleName) noexcept {
    if (moduleName == nullptr) {
        return false;
    }
    for (std::size_t i = 0; i < rules.nameFragmentCount; ++i) {
        if (IContains(moduleName, rules.nameFragments[i])) {
            return true;
        }
    }
    return false;
}

bool IsTrustedSigner(const Rules& rules, const char* signerOrganisation) noexcept {
    // A signature that is absent, invalid, or simply could not be checked is
    // NOT trusted. The caller passes nullptr for all three, and they all land
    // here as false — which refuses. That direction is deliberate.
    if (signerOrganisation == nullptr || signerOrganisation[0] == '\0') {
        return false;
    }
    for (std::size_t i = 0; i < rules.trustedSignerCount; ++i) {
        // Full comparison, never a prefix or substring: a substring signer rule
        // is a forgery surface, because "NVIDIA Corporation Ltd" would match
        // "NVIDIA Corporation".
        if (IEquals(signerOrganisation, rules.trustedSigners[i])) {
            return true;
        }
    }
    return false;
}

// --- The single rules location ---------------------------------------------
//
// Lives here, beside the matcher, so the injection guard and the Vulkan layer
// cannot end up reading different files. A second rules path would be a second
// blocklist by accident — the same defect as a second matcher, with none of the
// visibility.

bool LocalAppDataDir(wchar_t* out, std::size_t cap) noexcept {
    if (out == nullptr || cap == 0) {
        return false;
    }
    out[0] = L'\0';

    PWSTR         folder = nullptr;
    const HRESULT hr = SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_DEFAULT, nullptr, &folder);
    if (FAILED(hr) || folder == nullptr) {
        CoTaskMemFree(folder);    // documented as safe on nullptr
        return false;
    }

    const bool fits = wcslen(folder) < cap;
    if (fits) {
        wcscpy_s(out, cap, folder);
    }
    CoTaskMemFree(folder);
    return fits;    // truncation would name a different directory — refuse instead
}

bool RulesFilePath(wchar_t* out, std::size_t cap) noexcept {
    if (out == nullptr || cap == 0) {
        return false;
    }
    out[0] = L'\0';

    wchar_t base[kMaxRulesPathLen]{};
    if (!LocalAppDataDir(base, kMaxRulesPathLen)) {
        return false;
    }
    const int written = _snwprintf_s(out, cap, _TRUNCATE, LR"(%s\FrameLedger\rules\detection-rules.json)", base);
    return written > 0;    // _TRUNCATE reports -1 rather than truncating silently
}

std::size_t ReadRulesFile(char* buffer, std::size_t cap) noexcept {
    constexpr std::size_t kFailed = static_cast<std::size_t>(-1);
    if (buffer == nullptr || cap == 0) {
        return kFailed;
    }
    wchar_t path[kMaxRulesPathLen]{};
    if (!RulesFilePath(path, kMaxRulesPathLen)) {
        return kFailed;
    }

    // FILE_SHARE_DELETE matters as much as the other two, and it is the half that
    // is easy to leave out. §S20's delivery path must replace this file
    // atomically rather than rewriting it in place, or a reader can see it
    // half-written and refuse every title mid-session.
    //
    // **The primitive is ReplaceFileW, and this comment used to prescribe the
    // wrong one.** It said MoveFileExW(MOVEFILE_REPLACE_EXISTING), reasoning that
    // delete sharing is what lets a rename proceed against a live reader.
    // Measured 2026-08-04 against a handle opened exactly as below:
    //
    //     MoveFileExW(MOVEFILE_REPLACE_EXISTING)  ERROR_ACCESS_DENIED (5)
    //     ReplaceFileW, backup file named         succeeds
    //
    // Delete sharing is NECESSARY and nowhere near sufficient. Naming the wrong
    // call here would have sent whoever implements §S20 to a primitive that fails
    // on exactly the machines where the guard is busy — and its error goes
    // nowhere, so the update would simply not happen.
    //
    // The guard and the Vulkan layer previously disagreed here (READ vs
    // READ|WRITE) about what fl_ac_rules.h calls "the ONE location": by that
    // comment's own logic, a second blocklist by accident.
    HANDLE h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        return kFailed;
    }
    LARGE_INTEGER size{};
    if (!GetFileSizeEx(h, &size) || size.QuadPart <= 0 || static_cast<std::size_t>(size.QuadPart) >= cap) {
        CloseHandle(h);
        return kFailed;
    }
    DWORD      read = 0;
    const BOOL ok = ReadFile(h, buffer, static_cast<DWORD>(size.QuadPart), &read, nullptr);
    CloseHandle(h);
    return ok ? read : kFailed;
}

}    // namespace fl::guard
