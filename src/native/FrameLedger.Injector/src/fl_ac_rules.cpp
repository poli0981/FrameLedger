#include <windows.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fl_ac_rules.h>
#include <jsmn.h>

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

bool ReadStringArray(const char* json, const jsmntok_t* toks, int count, int arrayTok, char (*out)[kMaxValueLen],
                     std::size_t cap, std::size_t& outCount) noexcept {
    outCount = 0;
    if (arrayTok < 0 || toks[arrayTok].type != JSMN_ARRAY) {
        return false;
    }
    if (static_cast<std::size_t>(toks[arrayTok].size) > cap) {
        return false;
    }
    for (int i = arrayTok + 1; i < count && outCount < cap; ++i) {
        if (toks[i].parent != arrayTok) {
            continue;
        }
        if (!CopyToken(json, toks[i], out[outCount], kMaxValueLen)) {
            return false;
        }
        ++outCount;
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

// The gate has to be usable, not merely well-formed. A syntactically perfect
// file with an empty `modules` array blocks nothing.
bool IsCompleteEnoughToGate(const Rules& r) noexcept {
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

    for (const auto& req : kRequired) {
        bool found = false;
        for (std::size_t i = 0; i < r.familyCount && !found; ++i) {
            found = r.families[i].group == req.group && IEquals(r.families[i].name, req.family);
        }
        if (!found) {
            return false;
        }
    }
    return true;
}

}    // namespace

ParseResult ParseRules(const char* json, std::size_t length, Rules& out) noexcept {
    out = Rules{};
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
            if (out.familyCount >= kMaxFamilies) {
                return ParseResult::kTooLarge;
            }
            if (!ReadFamily(json, toks, count, i, group, out.families[out.familyCount])) {
                return ParseResult::kMalformed;
            }
            ++out.familyCount;
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

    if (out.familyCount == 0 || !IsCompleteEnoughToGate(out)) {
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

bool RulesFilePath(char* out, std::size_t cap) noexcept {
    if (out == nullptr || cap == 0) {
        return false;
    }
    char*  base = nullptr;
    size_t len = 0;
    if (_dupenv_s(&base, &len, "LOCALAPPDATA") != 0 || base == nullptr) {
        return false;
    }
    const int written = _snprintf_s(out, cap, _TRUNCATE, R"(%s\FrameLedger\rules\detection-rules.json)", base);
    std::free(base);
    return written > 0;
}

std::size_t ReadRulesFile(char* buffer, std::size_t cap) noexcept {
    constexpr std::size_t kFailed = static_cast<std::size_t>(-1);
    if (buffer == nullptr || cap == 0) {
        return kFailed;
    }
    char path[MAX_PATH]{};
    if (!RulesFilePath(path, sizeof(path))) {
        return kFailed;
    }

    HANDLE h = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
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
