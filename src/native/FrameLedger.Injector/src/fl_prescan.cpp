#include <cstring>
#include <cwchar>
#include <fl_prescan.h>

namespace fl::guard {
namespace {

// Mirrors the Refuse/Allow pair in fl_guard.cpp. Duplicated rather than shared
// because exporting them would put verdict construction on the public surface,
// and a Verdict a caller can mint is one step from a clearance a caller can
// mint (§S13(b)).
Verdict Refused(Reason r, const char* family, const char* signal) noexcept {
    Verdict v;
    v.reason = r;
    if (family != nullptr) {
        strncpy_s(v.family, sizeof(v.family), family, _TRUNCATE);
    }
    if (signal != nullptr) {
        strncpy_s(v.signal, sizeof(v.signal), signal, _TRUNCATE);
    }
    return v;
}

Verdict Passed() noexcept {
    Verdict v;
    v.reason = Reason::kAllow;
    return v;
}

// What the sink accumulates. No allocation: the walk reports names one at a
// time and we keep only the first hit.
struct ScanState {
    const Rules*  rules = nullptr;
    const Family* hit = nullptr;
    bool          hitWasDirectory = false;
    char          signal[260] = {};
};

bool EntrySink(void* ctx, const char* name, bool isDirectory) noexcept {
    auto* st = static_cast<ScanState*>(ctx);
    if (st == nullptr || st->rules == nullptr || name == nullptr || name[0] == '\0') {
        // A nameless entry is one we could not inspect. Keep walking — the
        // enumerator reports that as kIncomplete, which refuses on its own; if
        // we stopped here we would refuse with the wrong reason.
        return true;
    }

    // Directories and files are matched against DIFFERENT groups. Group
    // membership is load-bearing (fl_ac_rules.h): `EasyAntiCheat` as a directory
    // and `x3.xem` as a file are separate signals, and collapsing them would let
    // a data edit move one gate into the other without anything noticing.
    const Group   group = isDirectory ? Group::kDirectories : Group::kFiles;
    const Family* f = MatchName(*st->rules, group, name);
    if (f == nullptr) {
        return true;
    }

    st->hit = f;
    st->hitWasDirectory = isDirectory;
    strncpy_s(st->signal, sizeof(st->signal), name, _TRUNCATE);
    return false;    // stop: we have the answer
}

Verdict ScanWith(const Sources& s, const Rules& rules, const wchar_t* dir) noexcept {
    if (s.EnumerateDirEntries == nullptr) {
        return Refused(Reason::kPreScanFailed, nullptr, "no directory source");
    }
    if (dir == nullptr || dir[0] == L'\0') {
        return Refused(Reason::kPreScanFailed, nullptr, "no game directory to scan");
    }

    ScanState st;
    st.rules = &rules;

    const Collected c = s.EnumerateDirEntries(dir, &EntrySink, &st);

    // The hit is reported even if the walk then reported a problem: a directory
    // we positively identified is a stronger signal than an incomplete listing,
    // and both refuse anyway.
    if (st.hit != nullptr) {
        return Refused(st.hitWasDirectory ? Reason::kAntiCheatDirectory : Reason::kAntiCheatFile, st.hit->name,
                       st.signal);
    }

    // EVERY uncertainty is a refusal, and each has its own text because "the
    // directory is gone" and "there were more entries than we will look at" are
    // different problems for whoever has to fix them.
    if (c == Collected::kFailed) {
        return Refused(Reason::kPreScanFailed, nullptr, "the game directory could not be listed");
    }
    if (c == Collected::kIncomplete) {
        return Refused(Reason::kPreScanFailed, nullptr,
                       "the game directory listing was truncated, unreadable, or crossed a reparse point");
    }
    return Passed();
}

// Case-insensitive comparison of one path segment.
bool SegmentIs(const wchar_t* begin, const wchar_t* end, const wchar_t* literal) noexcept {
    const std::size_t n = static_cast<std::size_t>(end - begin);
    return wcslen(literal) == n && _wcsnicmp(begin, literal, n) == 0;
}

}    // namespace

bool ResolveInstallRoot(const wchar_t* exeDir, wchar_t* out, std::size_t cap) noexcept {
    if (exeDir == nullptr || out == nullptr || cap == 0) {
        return false;
    }

    // Split into segments without allocating.
    constexpr std::size_t kMaxSegments = 64;
    const wchar_t*        starts[kMaxSegments] = {};
    const wchar_t*        ends[kMaxSegments] = {};
    std::size_t           count = 0;

    const wchar_t* p = exeDir;
    while (*p != L'\0' && count < kMaxSegments) {
        while (*p == L'\\' || *p == L'/') {
            ++p;
        }
        if (*p == L'\0') {
            break;
        }
        starts[count] = p;
        while (*p != L'\0' && *p != L'\\' && *p != L'/') {
            ++p;
        }
        ends[count] = p;
        ++count;
    }

    // Boundaries whose CHILD is the install root. Hardcoded, per the header.
    struct Boundary {
        const wchar_t* seg[2];
        std::size_t    len;
    };
    static const Boundary kBoundaries[] = {
        {{L"steamapps", L"common"}, 2},
        {{L"GOG Galaxy", L"Games"}, 2},
        {{L"Epic Games", nullptr}, 1},
    };

    for (const Boundary& b : kBoundaries) {
        for (std::size_t i = 0; i + b.len < count; ++i) {
            bool hit = true;
            for (std::size_t k = 0; k < b.len && hit; ++k) {
                hit = SegmentIs(starts[i + k], ends[i + k], b.seg[k]);
            }
            if (!hit) {
                continue;
            }
            // Root ends at the segment AFTER the boundary.
            const wchar_t* rootEnd = ends[i + b.len];
            const auto     n = static_cast<std::size_t>(rootEnd - exeDir);
            if (n + 1 > cap) {
                return false;    // a truncated path is a path to somewhere else
            }
            std::wmemcpy(out, exeDir, n);
            out[n] = L'\0';
            return true;
        }
    }

    // No boundary recognised: the executable's own directory, unchanged.
    if (wcslen(exeDir) + 1 > cap) {
        return false;
    }
    wcscpy_s(out, cap, exeDir);
    return true;
}

Verdict CheckStaticPreScan(const Sources& s, const Rules& rules, std::uint32_t targetPid) noexcept {
    if (s.ImageDirectory == nullptr) {
        return Refused(Reason::kPreScanFailed, nullptr, "no image-path source");
    }

    wchar_t dir[kMaxPreScanPathLen] = {};
    if (s.ImageDirectory(targetPid, dir, kMaxPreScanPathLen) != Collected::kOk || dir[0] == L'\0') {
        // We could not find out where the game lives, so check 4 did not run.
        // 19_SAFETY has no "three of four checks passed" state.
        return Refused(Reason::kPreScanFailed, nullptr, "could not establish the target's directory");
    }
    return ScanWith(s, rules, dir);
}

Verdict StaticPreScan(const wchar_t* gameDirectory) noexcept {
    const Sources s = SystemSources();
    if (s.ReadRulesFile == nullptr) {
        return Refused(Reason::kRulesUnreadable, nullptr, "no rules source");
    }

    // Static rather than stack for the same reason LoadRules does it: this is
    // ~1 MiB of text and the guard is not re-entrant.
    static char       buffer[kMaxRulesBytes];
    const std::size_t n = s.ReadRulesFile(buffer, sizeof(buffer));
    if (n == static_cast<std::size_t>(-1) || n == 0) {
        return Refused(Reason::kRulesUnreadable, nullptr, "rules file could not be read");
    }

    static Rules rules;
    rules = Rules{};
    switch (ParseRules(buffer, n, rules)) {
    case ParseResult::kOk:
        break;
    case ParseResult::kIncomplete:
        return Refused(Reason::kRulesIncomplete, nullptr, "a required anti-cheat family is missing");
    case ParseResult::kTooLarge:
        return Refused(Reason::kRulesMalformed, nullptr, "rules file exceeds the parser's bounds");
    case ParseResult::kMalformed:
    default:
        return Refused(Reason::kRulesMalformed, nullptr, "rules file is not the shape the guard requires");
    }
    return ScanWith(s, rules, gameDirectory);
}

#ifdef FL_GUARD_TESTABLE
Verdict StaticPreScanWithSources(const wchar_t* gameDirectory, const Sources& sources) noexcept {
    if (sources.ReadRulesFile == nullptr) {
        return Refused(Reason::kRulesUnreadable, nullptr, "no rules source");
    }
    static char       buffer[kMaxRulesBytes];
    const std::size_t n = sources.ReadRulesFile(buffer, sizeof(buffer));
    if (n == static_cast<std::size_t>(-1) || n == 0) {
        return Refused(Reason::kRulesUnreadable, nullptr, "rules file could not be read");
    }

    static Rules rules;
    rules = Rules{};
    switch (ParseRules(buffer, n, rules)) {
    case ParseResult::kOk:
        break;
    case ParseResult::kIncomplete:
        return Refused(Reason::kRulesIncomplete, nullptr, "a required anti-cheat family is missing");
    case ParseResult::kTooLarge:
        return Refused(Reason::kRulesMalformed, nullptr, "rules file exceeds the parser's bounds");
    case ParseResult::kMalformed:
    default:
        return Refused(Reason::kRulesMalformed, nullptr, "rules file is not the shape the guard requires");
    }
    return ScanWith(sources, rules, gameDirectory);
}
#endif

}    // namespace fl::guard
