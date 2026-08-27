// fl-probe-signer — measures whether the guard's SIGNER HALF can be built in policy.
//
// This is NOT the guard, and it is deliberately not a step towards one. It
// installs nothing, injects nothing, and opens no process at all — every
// subject here is a FILE PATH. Where a later revision needs a process, it must
// use PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_VM_READ and never
// CREATE_THREAD | VM_OPERATION | VM_WRITE, for the reason fl-probe-guard states:
// a probe that quietly took more rights than the guard gets would answer a
// question the guard never asks.
//
// WHY IT EXISTS. docs/20_OPEN_QUESTIONS.md §S19(b) is deferred, and its own
// deferral rationale names this probe as the next step:
//
//     "Build fl-probe-signer first, in the shape fl-probe-guard established,
//      and answer those three questions with measurements before any design is
//      fixed."
//
// The three questions are docs/spike-notes.md §1's, and answering them IS the
// acceptance criterion. This probe must PRINT answers, not merely exit 0 —
// the #86 precedent: "four printed answers, not four passing asserts".
//
//   Q1  Does WinVerifyTrust(WTD_CHOICE_FILE) recover O= from a CATALOG-signed
//       system binary? §S19(b) measured that mskeyprotect.dll — the module the
//       entry was written about — carries no embedded signature at all, so the
//       obvious implementation recovers nothing for it.
//   Q2  What does verification COST per module, against the 30 s re-scan (§S6)?
//   Q3  Does WTD_REVOKE_NONE + WTD_CACHE_ONLY_URL_RETRIEVAL emit NETWORK
//       traffic? The default WTD_REVOKE_WHOLECHAIN performs CRL/OCSP fetches,
//       which breaks CLAUDE.md rule 8's permitted-network list — and NFR-10
//       offline-first — FROM INSIDE THE HARD GATE.
//
// Q3 CAN RETIRE THE WHOLE ROUTE, and it is also the weakest measurement here.
// "No packets seen" on a networked machine proves little, so this probe reports
// a MODULE census — whether cryptnet / winhttp / wininet / dnsapi appear in our
// own process across the verifications — and says out loud that absence is
// evidence while presence would be proof, never the reverse. The strong version
// is the owner's: re-run with adapters disabled and compare verdicts. The probe
// prints that instruction rather than implying it did it.
//
// THE CANARY. Section 4 verifies this probe's own executable, which is unsigned
// by construction (CLAUDE.md rule 9: we ship unsigned with published checksums).
// It MUST come back untrusted. A signer check that trusts everything is
// indistinguishable from one that works — the fl-baseline-probe failure with a
// different subject, and that probe was retired by exactly this class of test.
//
// WHAT THIS PROBE DOES NOT DO. It does not touch the guard, and no file under
// FrameLedger.Injector changes with it. Reading a row off §S19(b)'s
// pre-committed decision table is a separate PR, and building anything is a
// third — including the row that says build nothing.

#include <windows.h>

// clang-format off
// ORDER IS LOAD-BEARING, and .clang-format's IncludeBlocks: Regroup would destroy
// it -- all of these are priority 3 and would be sorted alphabetically, which puts
// <mscat.h> first. <mscat.h> uses HCRYPTPROV, CRYPT_HASH_BLOB and
// PCCERT_STRONG_SIGN_PARA without declaring them, so <wincrypt.h> has to precede
// it. Measured: sorted alphabetically this fails with twenty errors INSIDE the
// Windows SDK and one in our own code, which reads as a broken toolchain rather
// than as an include order. Fenced rather than reordered-and-hoped, because the
// next clang-format run would silently undo it.
#include <wincrypt.h>
#include <wintrust.h>
#include <softpub.h>
#include <mscat.h>
#include <bcrypt.h>
// clang-format on

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <iterator>
#include <string>
#include <vector>

#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "wintrust.lib")
#pragma comment(lib, "crypt32.lib")

namespace {

int  g_failures = 0;
bool g_elevated = false;

void Check(bool ok, const char* what) {
    std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) {
        ++g_failures;
    }
}

void Note(const char* fmt, ...) {
    std::printf("       ");
    va_list ap;
    va_start(ap, fmt);
    std::vfprintf(stdout, fmt, ap);
    va_end(ap);
    std::printf("\n");
}

void Section(const char* title) {
    std::printf("\n== %s ==\n", title);
}

std::string Narrow(const wchar_t* w) {
    if (w == nullptr || w[0] == L'\0') {
        return {};
    }
    int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
    if (n <= 1) {
        return {};
    }
    std::string out(static_cast<size_t>(n) - 1, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w, -1, out.data(), n, nullptr, nullptr);
    return out;
}

// The failure taxonomy §S19(b)'s new fail-closed matrix row has to be written
// from. Named codes, never a bare hex value: "it returned 0x800b0100" is not a
// row anyone can review, and the whole point of the matrix is reviewability.
const char* TrustName(LONG r) {
    switch (r) {
    case ERROR_SUCCESS:
        return "ERROR_SUCCESS";
    case TRUST_E_NOSIGNATURE:
        return "TRUST_E_NOSIGNATURE";
    case TRUST_E_SUBJECT_FORM_UNKNOWN:
        return "TRUST_E_SUBJECT_FORM_UNKNOWN";
    case TRUST_E_PROVIDER_UNKNOWN:
        return "TRUST_E_PROVIDER_UNKNOWN";
    case TRUST_E_SUBJECT_NOT_TRUSTED:
        return "TRUST_E_SUBJECT_NOT_TRUSTED";
    case TRUST_E_BAD_DIGEST:
        return "TRUST_E_BAD_DIGEST";
    case TRUST_E_EXPLICIT_DISTRUST:
        return "TRUST_E_EXPLICIT_DISTRUST";
    case CERT_E_UNTRUSTEDROOT:
        return "CERT_E_UNTRUSTEDROOT";
    case CERT_E_CHAINING:
        return "CERT_E_CHAINING";
    case CERT_E_REVOKED:
        return "CERT_E_REVOKED";
    case CERT_E_REVOCATION_FAILURE:
        return "CERT_E_REVOCATION_FAILURE";
    case CERT_E_EXPIRED:
        return "CERT_E_EXPIRED";
    case CRYPT_E_SECURITY_SETTINGS:
        return "CRYPT_E_SECURITY_SETTINGS";
    case CRYPT_E_FILE_ERROR:
        return "CRYPT_E_FILE_ERROR";
    default:
        return "(unnamed)";
    }
}

struct SignerIdentity {
    bool        found = false;
    std::string organisation;    // the O= RDN, which is what signerField names
    std::string subject;         // the WHOLE subject RDN sequence
};

// §S19's signerField is "O", measured 2026-08-02 on drivers and launchers and
// never on a .NET shared-framework assembly. So this reports the whole subject
// alongside O=, and the decision table has a row for "the organisation is not
// where signerField says it is".
SignerIdentity ReadSigner(const wchar_t* path) {
    SignerIdentity id;
    HCERTSTORE     store = nullptr;
    HCRYPTMSG      msg = nullptr;
    DWORD          enc = 0, ctype = 0, fmt = 0;

    if (!CryptQueryObject(CERT_QUERY_OBJECT_FILE, path, CERT_QUERY_CONTENT_FLAG_PKCS7_SIGNED_EMBED,
                          CERT_QUERY_FORMAT_FLAG_BINARY, 0, &enc, &ctype, &fmt, &store, &msg, nullptr)) {
        return id;
    }

    DWORD need = 0;
    if (CryptMsgGetParam(msg, CMSG_SIGNER_INFO_PARAM, 0, nullptr, &need) && need > 0) {
        std::vector<BYTE> buf(need);
        if (CryptMsgGetParam(msg, CMSG_SIGNER_INFO_PARAM, 0, buf.data(), &need)) {
            auto*     si = reinterpret_cast<CMSG_SIGNER_INFO*>(buf.data());
            CERT_INFO ci{};
            ci.Issuer = si->Issuer;
            ci.SerialNumber = si->SerialNumber;
            PCCERT_CONTEXT cert = CertFindCertificateInStore(store, X509_ASN_ENCODING | PKCS_7_ASN_ENCODING, 0,
                                                             CERT_FIND_SUBJECT_CERT, &ci, nullptr);
            if (cert != nullptr) {
                wchar_t o[512]{};
                DWORD   n = CertGetNameStringW(cert, CERT_NAME_ATTR_TYPE, 0, const_cast<char*>(szOID_ORGANIZATION_NAME),
                                               o, static_cast<DWORD>(std::size(o)));
                if (n > 1) {
                    id.organisation = Narrow(o);
                }
                DWORD sn =
                    CertNameToStrW(cert->dwCertEncodingType, &cert->pCertInfo->Subject, CERT_X500_NAME_STR, nullptr, 0);
                if (sn > 1) {
                    std::vector<wchar_t> s(sn);
                    CertNameToStrW(cert->dwCertEncodingType, &cert->pCertInfo->Subject, CERT_X500_NAME_STR, s.data(),
                                   sn);
                    id.subject = Narrow(s.data());
                }
                id.found = true;
                CertFreeCertificateContext(cert);
            }
        }
    }

    if (msg != nullptr) {
        CryptMsgClose(msg);
    }
    if (store != nullptr) {
        CertCloseStore(store, 0);
    }
    return id;
}

// The EMBEDDED route. `offline` is the whole of Q3: WTD_REVOKE_NONE turns
// revocation checking off, and WTD_CACHE_ONLY_URL_RETRIEVAL forces anything that
// still wants a URL — an AIA intermediate, a CRL — to come from the local
// CryptnetUrlCache rather than the network.
LONG VerifyEmbedded(const wchar_t* path, bool offline, double* ms) {
    WINTRUST_FILE_INFO fi{};
    fi.cbStruct = sizeof(fi);
    fi.pcwszFilePath = path;

    WINTRUST_DATA wd{};
    wd.cbStruct = sizeof(wd);
    wd.dwUIChoice = WTD_UI_NONE;
    wd.fdwRevocationChecks = offline ? WTD_REVOKE_NONE : WTD_REVOKE_WHOLECHAIN;
    wd.dwUnionChoice = WTD_CHOICE_FILE;
    wd.pFile = &fi;
    wd.dwStateAction = WTD_STATEACTION_VERIFY;
    wd.dwProvFlags = WTD_SAFER_FLAG | (offline ? WTD_CACHE_ONLY_URL_RETRIEVAL : 0u);

    GUID          action = WINTRUST_ACTION_GENERIC_VERIFY_V2;
    LARGE_INTEGER f{}, a{}, b{};
    QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&a);
    LONG r = WinVerifyTrust(static_cast<HWND>(INVALID_HANDLE_VALUE), &action, &wd);
    QueryPerformanceCounter(&b);
    if (ms != nullptr && f.QuadPart != 0) {
        *ms = static_cast<double>(b.QuadPart - a.QuadPart) * 1000.0 / static_cast<double>(f.QuadPart);
    }

    wd.dwStateAction = WTD_STATEACTION_CLOSE;
    WinVerifyTrust(static_cast<HWND>(INVALID_HANDLE_VALUE), &action, &wd);
    return r;
}

// The CATALOG route. This is the CryptCATAdmin* cost §S19(b) deferred on, and
// the only route that can reach a system binary carrying no embedded signature.
// It reads %SystemRoot%\System32\CatRoot, which is local by construction.
LONG VerifyCatalog(const wchar_t* path, bool offline, double* ms, bool* foundCatalog, SignerIdentity* id) {
    *foundCatalog = false;
    HANDLE file =
        CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return CRYPT_E_FILE_ERROR;
    }

    HCATADMIN admin = nullptr;
    GUID      driverAction = DRIVER_ACTION_VERIFY;
    if (!CryptCATAdminAcquireContext2(&admin, &driverAction, BCRYPT_SHA256_ALGORITHM, nullptr, 0)) {
        CloseHandle(file);
        return static_cast<LONG>(HRESULT_FROM_WIN32(GetLastError()));
    }

    DWORD hashLen = 0;
    CryptCATAdminCalcHashFromFileHandle2(admin, file, &hashLen, nullptr, 0);
    std::vector<BYTE> hash(hashLen != 0 ? hashLen : 1u);
    LONG              result = TRUST_E_NOSIGNATURE;

    if (hashLen != 0 && CryptCATAdminCalcHashFromFileHandle2(admin, file, &hashLen, hash.data(), 0)) {
        HCATINFO cat = CryptCATAdminEnumCatalogFromHash(admin, hash.data(), hashLen, 0, nullptr);
        if (cat != nullptr) {
            CATALOG_INFO ci{};
            ci.cbStruct = sizeof(ci);
            if (CryptCATCatalogInfoFromContext(cat, &ci, 0)) {
                *foundCatalog = true;

                static const wchar_t kHex[] = L"0123456789ABCDEF";
                std::wstring         tag(static_cast<size_t>(hashLen) * 2, L'\0');
                for (DWORD i = 0; i < hashLen; ++i) {
                    tag[static_cast<size_t>(i) * 2] = kHex[hash[i] >> 4];
                    tag[static_cast<size_t>(i) * 2 + 1] = kHex[hash[i] & 0x0F];
                }

                WINTRUST_CATALOG_INFO wci{};
                wci.cbStruct = sizeof(wci);
                wci.pcwszCatalogFilePath = ci.wszCatalogFile;
                wci.pcwszMemberTag = tag.c_str();
                wci.pcwszMemberFilePath = path;
                wci.hMemberFile = file;
                wci.pbCalculatedFileHash = hash.data();
                wci.cbCalculatedFileHash = hashLen;
                wci.hCatAdmin = admin;

                WINTRUST_DATA wd{};
                wd.cbStruct = sizeof(wd);
                wd.dwUIChoice = WTD_UI_NONE;
                wd.fdwRevocationChecks = offline ? WTD_REVOKE_NONE : WTD_REVOKE_WHOLECHAIN;
                wd.dwUnionChoice = WTD_CHOICE_CATALOG;
                wd.pCatalog = &wci;
                wd.dwStateAction = WTD_STATEACTION_VERIFY;
                wd.dwProvFlags = WTD_SAFER_FLAG | (offline ? WTD_CACHE_ONLY_URL_RETRIEVAL : 0u);

                GUID          action = WINTRUST_ACTION_GENERIC_VERIFY_V2;
                LARGE_INTEGER f{}, a{}, b{};
                QueryPerformanceFrequency(&f);
                QueryPerformanceCounter(&a);
                result = WinVerifyTrust(static_cast<HWND>(INVALID_HANDLE_VALUE), &action, &wd);
                QueryPerformanceCounter(&b);
                if (ms != nullptr && f.QuadPart != 0) {
                    *ms = static_cast<double>(b.QuadPart - a.QuadPart) * 1000.0 / static_cast<double>(f.QuadPart);
                }

                if (id != nullptr && result == ERROR_SUCCESS) {
                    // The identity comes from the CATALOG, not from the member
                    // file — the member has no embedded signature, which is the
                    // entire reason this route exists.
                    *id = ReadSigner(ci.wszCatalogFile);
                }

                wd.dwStateAction = WTD_STATEACTION_CLOSE;
                WinVerifyTrust(static_cast<HWND>(INVALID_HANDLE_VALUE), &action, &wd);
            }
            CryptCATAdminReleaseCatalogContext(admin, cat, 0);
        }
    }

    CryptCATAdminReleaseContext(admin, 0);
    CloseHandle(file);
    return result;
}

// Q3's evidence, and its limit is printed beside it rather than left implied.
struct NetCensus {
    bool cryptnet = false;
    bool winhttp = false;
    bool wininet = false;
    bool dnsapi = false;

    bool Any() const { return cryptnet || winhttp || wininet || dnsapi; }
};

NetCensus TakeCensus() {
    NetCensus c;
    c.cryptnet = GetModuleHandleW(L"cryptnet.dll") != nullptr;
    c.winhttp = GetModuleHandleW(L"winhttp.dll") != nullptr;
    c.wininet = GetModuleHandleW(L"wininet.dll") != nullptr;
    c.dnsapi = GetModuleHandleW(L"dnsapi.dll") != nullptr;
    return c;
}

bool PrintCensusDelta(const NetCensus& before, const NetCensus& after) {
    bool moved = false;
    auto one = [&moved](const char* n, bool b, bool a) {
        if (a && !b) {
            std::printf("       NEWLY LOADED: %s\n", n);
            moved = true;
        }
    };
    one("cryptnet.dll", before.cryptnet, after.cryptnet);
    one("winhttp.dll", before.winhttp, after.winhttp);
    one("wininet.dll", before.wininet, after.wininet);
    one("dnsapi.dll", before.dnsapi, after.dnsapi);
    return moved;
}

std::wstring SelfPath() {
    wchar_t buf[MAX_PATH]{};
    GetModuleFileNameW(nullptr, buf, static_cast<DWORD>(std::size(buf)));
    return buf;
}

std::wstring SystemFile(const wchar_t* leaf) {
    wchar_t root[MAX_PATH]{};
    GetSystemDirectoryW(root, static_cast<DWORD>(std::size(root)));
    std::wstring p = root;
    p += L"\\";
    p += leaf;
    return p;
}

// The CI blocker itself: the assembly whose NAME contains the heuristic fragment
// `protect`. Located by walking directories — never by LoadLibrary, which would
// map a module we do not need and would make this probe's own process a worse
// subject for the network census above.
//
// MEASURED 2026-08-27, AND IT CORRECTS §S19(b)'s OWN DESCRIPTION. That entry
// calls it "a .NET shared-framework assembly". It is not: Microsoft.NETCore.App
// 10.0.11 does not contain it. It is a NuGet package assembly — version 6.0.0 —
// reached transitively from Microsoft.NET.Test.Sdk through
// System.Configuration.ConfigurationManager, and copied NEXT TO every test
// binary. That matters for more than pedantry: an assembly the framework owns
// cannot be dropped, while a package reference invites the question of whether
// it can — and here the answer is still no, because dropping it means dropping
// the test SDK. So the search order below is the NuGet cache first, since that
// is where the copy a test host actually loads comes from.
std::wstring FindProtectedData() {
    const wchar_t* kLeaf = L"System.Security.Cryptography.ProtectedData.dll";

    // 1. The NuGet cache — where the test host's copy is staged from.
    wchar_t profile[MAX_PATH]{};
    if (GetEnvironmentVariableW(L"USERPROFILE", profile, static_cast<DWORD>(std::size(profile))) != 0) {
        std::wstring pkgRoot =
            std::wstring(profile) + L"\\.nuget\\packages\\system.security.cryptography.protecteddata";
        std::wstring     pattern = pkgRoot + L"\\*";
        WIN32_FIND_DATAW fd{};
        HANDLE           h = FindFirstFileW(pattern.c_str(), &fd);
        if (h != INVALID_HANDLE_VALUE) {
            std::wstring best;
            do {
                if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0 || fd.cFileName[0] == L'.') {
                    continue;
                }
                // The runtime-specific copy is the one a Windows host loads.
                const wchar_t* kUnder[] = {L"\\runtimes\\win\\lib\\net6.0\\", L"\\lib\\net6.0\\"};
                for (const wchar_t* u : kUnder) {
                    std::wstring candidate = pkgRoot + L"\\" + fd.cFileName + u + kLeaf;
                    if (GetFileAttributesW(candidate.c_str()) != INVALID_FILE_ATTRIBUTES) {
                        best = candidate;
                    }
                }
            } while (FindNextFileW(h, &fd) != 0);
            FindClose(h);
            if (!best.empty()) {
                return best;
            }
        }
    }

    // 2. The shared framework — where §S19(b) says it lives. Kept so that a
    //    machine where that IS true still measures, and so the claim stays
    //    falsifiable rather than assumed away.
    const wchar_t* roots[] = {L"C:\\Program Files\\dotnet\\shared\\Microsoft.NETCore.App",
                              L"C:\\Program Files (x86)\\dotnet\\shared\\Microsoft.NETCore.App"};
    for (const wchar_t* root : roots) {
        std::wstring     pattern = std::wstring(root) + L"\\*";
        WIN32_FIND_DATAW fd{};
        HANDLE           h = FindFirstFileW(pattern.c_str(), &fd);
        if (h == INVALID_HANDLE_VALUE) {
            continue;
        }
        std::wstring best;
        do {
            if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0 || fd.cFileName[0] == L'.') {
                continue;
            }
            std::wstring candidate = std::wstring(root) + L"\\" + fd.cFileName + L"\\" + kLeaf;
            if (GetFileAttributesW(candidate.c_str()) != INVALID_FILE_ATTRIBUTES) {
                best = candidate;
            }
        } while (FindNextFileW(h, &fd) != 0);
        FindClose(h);
        if (!best.empty()) {
            return best;
        }
    }
    return {};
}

struct SubjectResult {
    bool           present = false;
    LONG           embedded = TRUST_E_NOSIGNATURE;
    LONG           embDefault = TRUST_E_NOSIGNATURE;
    LONG           catalog = TRUST_E_NOSIGNATURE;
    bool           haveCatalog = false;
    SignerIdentity id;
};

SubjectResult ReportSubject(const char* label, const wchar_t* path) {
    SubjectResult out;
    std::printf("\n  -- %s\n", label);
    Note("path: %s", Narrow(path).c_str());
    if (GetFileAttributesW(path) == INVALID_FILE_ATTRIBUTES) {
        Note("NOT MEASURED: this file does not exist on this machine. An unrun leg is");
        Note("unrun — do not read it as a negative result.");
        return out;
    }
    out.present = true;

    double embMs = 0.0, embDefMs = 0.0, catMs = 0.0;
    out.embedded = VerifyEmbedded(path, true, &embMs);
    out.embDefault = VerifyEmbedded(path, false, &embDefMs);

    SignerIdentity embId;
    if (out.embedded == ERROR_SUCCESS) {
        embId = ReadSigner(path);
    }

    SignerIdentity catId;
    out.catalog = VerifyCatalog(path, true, &catMs, &out.haveCatalog, &catId);

    Note("embedded, offline flags : %-28s (%7.2f ms)", TrustName(out.embedded), embMs);
    Note("embedded, DEFAULT flags : %-28s (%7.2f ms)   <- Q3's comparison", TrustName(out.embDefault), embDefMs);
    Note("catalog,  offline flags : %-28s (%7.2f ms)   catalog found: %s", TrustName(out.catalog), catMs,
         out.haveCatalog ? "yes" : "no");

    out.id = (out.embedded == ERROR_SUCCESS) ? embId : catId;
    if (out.id.found && !out.id.organisation.empty()) {
        Note("O=      : %s", out.id.organisation.c_str());
        Note("subject : %s", out.id.subject.c_str());
    } else {
        Note("O=      : (none recovered)   <- signerField \"O\" yields nothing on this subject");
    }

    if (out.embedded != ERROR_SUCCESS && out.catalog == ERROR_SUCCESS) {
        Note("Q1 HERE: the EMBEDDED route recovers NOTHING and the CATALOG route works. This");
        Note("is the shape §S19(b) predicted for mskeyprotect.dll.");
    }
    if (out.embedded != out.embDefault) {
        Note("Q3 WARNING: the offline flags CHANGED the verdict on this subject. That is a");
        Note("coverage cost, and it belongs in the decision table rather than in a footnote.");
    }
    return out;
}

}    // namespace

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;

    HANDLE tok = nullptr;
    if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &tok)) {
        TOKEN_ELEVATION e{};
        DWORD           n = 0;
        if (GetTokenInformation(tok, TokenElevation, &e, static_cast<DWORD>(sizeof(e)), &n)) {
            g_elevated = e.TokenIsElevated != 0;
        }
        CloseHandle(tok);
    }

    std::printf("fl-probe-signer — can the guard's signer half be built in policy?\n");
    std::printf("elevated: %s   (the DEFAULT Agent is UNELEVATED — ADR-9)\n", g_elevated ? "yes" : "no");
    std::printf("Read the answers, not the exit code. The decision table is §S19(b) and it\n");
    std::printf("was written BEFORE this run.\n");

    const std::wstring kernel32 = SystemFile(L"kernel32.dll");
    const std::wstring keyprot = SystemFile(L"mskeyprotect.dll");
    const std::wstring blocker = FindProtectedData();
    const std::wstring self = SelfPath();

    // Q3 IS MEASURED FIRST, AND THAT ORDER IS THE MEASUREMENT.
    //
    // The first version of this probe took one census at the top and one at the
    // bottom, with BOTH the offline and the DEFAULT (WTD_REVOKE_WHOLECHAIN)
    // verifications in between. cryptnet.dll duly appeared, and the delta could
    // not say which configuration loaded it — the default one is EXPECTED to.
    // A census that spans both arms of the comparison it is supposed to
    // discriminate cannot answer Q3 at all, and it reads like an answer.
    //
    // So the offline arm runs alone, first, in a process that has made no other
    // verification call, and its census is bracketed around exactly that.
    Section("Q3a · the OFFLINE-ONLY census, taken before any default-flag call");
    const NetCensus before = TakeCensus();
    {
        const std::wstring* subjects[] = {&kernel32, &keyprot, &blocker};
        for (const std::wstring* subject : subjects) {
            if (subject->empty() || GetFileAttributesW(subject->c_str()) == INVALID_FILE_ATTRIBUTES) {
                continue;
            }
            VerifyEmbedded(subject->c_str(), true, nullptr);
            bool           haveCat = false;
            SignerIdentity ignored;
            VerifyCatalog(subject->c_str(), true, nullptr, &haveCat, &ignored);
        }
    }
    const NetCensus afterOffline = TakeCensus();
    if (PrintCensusDelta(before, afterOffline)) {
        Note("A URL-retrieval module loaded under WTD_REVOKE_NONE +");
        Note("WTD_CACHE_ONLY_URL_RETRIEVAL. That is the flag combination the whole route");
        Note("depends on, and this is the census that can attribute it.");
    } else {
        Note("Nothing newly loaded under WTD_REVOKE_NONE + WTD_CACHE_ONLY_URL_RETRIEVAL.");
    }

    Section("Q1 · which route recovers an organisation, per subject");
    Note("§S19(b) measured that mskeyprotect.dll carries NO embedded signature at all. If");
    Note("the embedded route is empty where the catalog route is not, then the obvious");
    Note("implementation — WinVerifyTrust(WTD_CHOICE_FILE) — recovers nothing for it, and");
    Note("the CryptCATAdmin* cost that entry deferred on is real rather than avoidable.");

    ReportSubject("kernel32.dll — catalog-signed, and on every machine", kernel32.c_str());
    ReportSubject("mskeyprotect.dll — §S19(b)'s own subject", keyprot.c_str());

    SubjectResult blockerResult;
    if (blocker.empty()) {
        std::printf("\n  -- System.Security.Cryptography.ProtectedData.dll — the CI blocker\n");
        Note("NOT MEASURED: no .NET shared framework found on this machine.");
    } else {
        blockerResult =
            ReportSubject("System.Security.Cryptography.ProtectedData.dll — the CI blocker", blocker.c_str());
    }

    Section("Q2 · cost, against the 30 s re-scan (§S6)");
    if (!blockerResult.present) {
        Note("NOT MEASURED: the blocker subject is absent, and timing a different file would");
        Note("be a number about the wrong thing.");
    } else {
        double cold = 0.0;
        VerifyEmbedded(blocker.c_str(), true, &cold);
        const int kReps = 20;
        double    total = 0.0;
        for (int i = 0; i < kReps; ++i) {
            double one = 0.0;
            VerifyEmbedded(blocker.c_str(), true, &one);
            total += one;
        }
        Note("cold (first call in this process): %7.2f ms", cold);
        Note("warm (mean of %d)               : %7.2f ms", kReps, total / kReps);
        Note("A scan set of N fragment-matching modules costs about N x the warm figure,");
        Note("once every 30 s. Two things the number alone does not say: the guard's own");
        Note("scan set is usually SMALL — three real titles produced no fragment hit at all");
        Note("— and a cache ACROSS evaluations is a re-scan that did not run, so only a");
        Note("cache WITHIN one evaluation is admissible.");
    }

    Section("Q3b · and after the DEFAULT-flag calls, for contrast");
    const NetCensus after = TakeCensus();
    if (!PrintCensusDelta(afterOffline, after)) {
        Note("Nothing further loaded once the default WTD_REVOKE_WHOLECHAIN calls ran.");
    }
    Note("state now: cryptnet=%d winhttp=%d wininet=%d dnsapi=%d", after.cryptnet ? 1 : 0, after.winhttp ? 1 : 0,
         after.wininet ? 1 : 0, after.dnsapi ? 1 : 0);
    std::printf("\n");
    Note("HOW TO READ THE TWO CENSUSES. Q3a is the one that decides anything: it brackets");
    Note("the offline arm ALONE. A module appearing only in Q3b is the default arm doing");
    Note("what it is documented to do, and says nothing against the offline flags.");
    std::printf("\n");
    Note("LIMIT, STATED RATHER THAN IMPLIED. Absence of cryptnet.dll is EVIDENCE that no");
    Note("CryptoAPI URL retrieval happened. Its presence is proof that one could have —");
    Note("the converse does not hold, and this probe has no packet counter. THE STRONG");
    Note("VERSION IS THE OWNER'S: re-run with network adapters disabled and compare the");
    Note("verdicts subject by subject. A verdict that changes offline retires the rung.");

    Section("Canary — this probe's own executable must NOT verify");
    Note("We ship unsigned by policy (CLAUDE.md rule 9). A signer check that trusts this");
    Note("binary is one that trusts everything, and would be indistinguishable from one");
    Note("that works. fl-baseline-probe was retired by exactly this class of test.");
    {
        double         ms = 0.0;
        const LONG     r = VerifyEmbedded(self.c_str(), true, &ms);
        SignerIdentity id = (r == ERROR_SUCCESS) ? ReadSigner(self.c_str()) : SignerIdentity{};
        Note("self: %s", Narrow(self.c_str()).c_str());
        Note("verdict: %s", TrustName(r));
        Check(r != ERROR_SUCCESS, "our own unsigned binary is NOT trusted");
        Check(!id.found || id.organisation.empty(), "no organisation is recovered from it");
    }

    Section("The blocker, and which row it points at");
    if (!blockerResult.present) {
        std::printf("BLOCKER: NOT PRESENT\n");
        Note("There is no .NET shared framework here, so §S19(b)'s measured case cannot be");
        Note("reproduced on this machine. This is NOT evidence that the blocker is gone.");
        Note("An unrun leg is unrun — re-run where the SDK is installed, or read the CI log.");
    } else {
        std::printf("BLOCKER: FOUND\n");
        Note("embedded/offline verdict: %s", TrustName(blockerResult.embedded));
        Note("O=: \"%s\"", blockerResult.id.organisation.c_str());
        if (blockerResult.embedded == ERROR_SUCCESS && blockerResult.id.organisation == "Microsoft Corporation") {
            Note("-> G1 is a CANDIDATE: the embedded half alone, offline, recovers an");
            Note("   organisation already present in trustedSigners. Candidate, not a verdict:");
            Note("   G1 also requires Q3 clean and Q2 inside the ceiling.");
        } else if (blockerResult.embedded == TRUST_E_NOSIGNATURE ||
                   blockerResult.embedded == TRUST_E_SUBJECT_FORM_UNKNOWN) {
            Note("-> G4: catalog-signed here, so the embedded half does not fix the CI case");
            Note("   either, and the CryptCATAdmin* half is back on the table. Owner's call.");
        } else if (blockerResult.embedded == ERROR_SUCCESS) {
            Note("-> G3: it verifies, but O= is not what signerField predicts. STOP and settle");
            Note("   the field before any guard change — a signer half reading the wrong field");
            Note("   suppresses the wrong modules.");
        } else {
            Note("-> G6: not a row anyone wrote. Print it, add a row, re-run. Do not promote a");
            Note("   surprise into a design.");
        }
        std::printf("\n");
        Note("Read the row off docs/20_OPEN_QUESTIONS.md §S19(b). Do NOT assemble a decision");
        Note("out of these numbers — that table exists so the mapping cannot be made after");
        Note("the answers are known.");
    }

    std::printf("\n%s — %d failure(s)\n", g_failures == 0 ? "OK" : "FAILURES", g_failures);
    return g_failures == 0 ? 0 : 1;
}
