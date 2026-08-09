# 06 — Data model (SQLite)

`%LOCALAPPDATA%\FrameLedger\ledger.db`. Pragmas: `journal_mode=WAL`, `synchronous=NORMAL`, `foreign_keys=ON`, `busy_timeout=5000`. `Microsoft.Data.Sqlite` + Dapper; all writes in explicit transactions.

**Writer ownership:** Agent writes `sessions`, `session_segments`, `frame_blobs`, `sensor_blobs`, `hardware_snapshots`, and the hook-state columns on `games`. UI writes `games` (user-editable fields), `session_annotations`, `settings`, `legal_acceptance`. Both read everything.

## Schema (v2 — hook architecture)

```sql
CREATE TABLE schema_migrations (version INTEGER PRIMARY KEY, applied_at INTEGER NOT NULL);

CREATE TABLE games (
  id INTEGER PRIMARY KEY,
  name TEXT NOT NULL,
  exe_path TEXT NOT NULL UNIQUE,
  platform TEXT NOT NULL DEFAULT 'none',       -- steam|gog|epic|itch|none
  store_id TEXT,
  engine TEXT, engine_version TEXT,
  publisher TEXT, game_version TEXT,
  cover_path TEXT, notes TEXT,

  -- hooking state (19_SAFETY)
  hook_enabled INTEGER NOT NULL DEFAULT 0,     -- 0 = Tier 2 only; default for every new game
  hook_consent_at INTEGER,                     -- per-game informed consent timestamp
  hook_blocked_reason TEXT,                    -- set by the static AC pre-scan; non-null = toggle disabled in UI
  hook_autodisabled_reason TEXT,               -- set after repeated crashes
  hook_crash_count INTEGER NOT NULL DEFAULT 0,
  capability_flags TEXT,                       -- JSON: what the game SHIPS (dlss/dlssg/dlssd/fsr/xess) — never a measurement

  -- tri-state defaults inherited by new sessions
  rt_default TEXT NOT NULL DEFAULT 'na',
  pt_default TEXT NOT NULL DEFAULT 'na',
  rr_default TEXT NOT NULL DEFAULT 'na',

  detection_rules_version TEXT,
  field_provenance TEXT,                       -- JSON: {"engine":"detected","publisher":"user",...}
                                               -- absent or unrecognised reads as "user"
  added_at INTEGER NOT NULL, updated_at INTEGER NOT NULL
);
```

> **`field_provenance` — decided now, implemented in P2.** FR-1.3 requires
> auto-detected fields to be badged and overridable, and there was nowhere to
> record which was which. One JSON column rather than a column per field, matching
> the `capability_flags` precedent in the same table: the overridable set is small
> and fixed (`engine`, `engine_version`, `platform`, `store_id`, `publisher`,
> `game_version`, `name`, `cover_path`), and a normalised override table would mean
> a join on every library-card render.
>
> **Absent reads as `user`, deliberately.** Of the two ways to be wrong, badging a
> human's typed value as auto-detected is a lie they cannot see through; failing to
> badge a detected value merely costs a badge on something they can still edit. The
> rule it exists to support — *detection never overwrites a `user` field* — is in
> `05_DETECTION` §Caching and is enforced by `StaticDetectionResult.ShouldWrite`,
> which is tested even though nothing persists yet.
>
> No migration is written here. There is no SQLite layer at all, and §Migrations
> forbids editing an applied script — so guessing the shape into `0001_init.sql`
> before its consumers exist maximises the chance of baking in a wrong guess that
> can only be appended to.

> **The three hook-state columns have a consumer before they have a table, and two
> fields it needs are not here.** Added 2026-08-06 with `IGameConsentStore`, whose
> only adapter today is a file inside the unshipped `FrameLedger.CaptureHost`.
>
> - **A disclosure provenance.** `hook_consent_at` is a bare timestamp and cannot
>   distinguish a stamp made after FR-2.1's reviewed dialog from one made after
>   anything else — so a record could carry a consent time that nothing had ever
>   disclosed anything for. `ConsentProvenance`'s zero value means *no disclosure was
>   shown*, and it deliberately has **no FR-2.1 member**: that dialog needs reviewed
>   `Safety_*` wording in en/vi/ja and no `.resx` exists anywhere in this tree.
> - **A disclosure/wording version.** `legal_acceptance` carries `(doc, version,
>   accepted_at)` while this column carries a timestamp alone, so nothing could mark
>   existing per-game consent stale when the reviewed wording changes. It is carried
>   from the *first* record because retrofitting one means treating unversioned
>   consent as either current or stale, and both are wrong about some record.
> - **A third pre-scan state.** `hook_blocked_reason` is two-state by definition here
>   — non-null disables the toggle — while `05_DETECTION` makes the pre-scan
>   tri-state, and says explicitly that *"could not verify"* must neither disable the
>   toggle (a false refusal with no appeal) nor be cleared (a fail-open). One nullable
>   TEXT column cannot carry three states.
>
> **The file-backed record is NOT a migration source.** It lives in a build output,
> `git clean` removes it, and this section's own argument against guessing a schema
> applies just as well to inheriting one from a throwaway. When the `games` table is
> written it is written from this document.
>
> **Still unanswered, and it is an owner decision rather than a coding one:** who
> clears `hook_blocked_reason` when a re-scan comes back clean. `19_SAFETY` §A game
> already enabled can become blocked later makes a non-null value mean "toggle
> disabled" and makes re-enabling "a user action" — but if nothing clears the column
> the toggle is permanently disabled and that user action is impossible, while a
> managed component that clears it is deciding an anti-cheat fact. Nothing in this
> tree clears it today, which leaves the state unreachable rather than wrong.

```sql

CREATE TABLE hardware_snapshots (
  id INTEGER PRIMARY KEY,
  hash TEXT NOT NULL UNIQUE,                   -- sha256 of normalized fields
  cpu_name TEXT, gpu_name TEXT, gpu_driver TEXT,
  ram_gb REAL, os_build TEXT,
  display_res TEXT, display_hz REAL,
  captured_at INTEGER NOT NULL
);

CREATE TABLE sessions (
  id INTEGER PRIMARY KEY,
  game_id INTEGER NOT NULL REFERENCES games(id) ON DELETE CASCADE,
  snapshot_id INTEGER NOT NULL REFERENCES hardware_snapshots(id),
  started_at INTEGER NOT NULL, ended_at INTEGER NOT NULL, duration_s REAL NOT NULL,

  capture_tier INTEGER NOT NULL,               -- 1 = hooked, 2 = etw, 3 = none
  capture_notes TEXT,                          -- why tier degraded, late_attach, etc.
  late_attach INTEGER NOT NULL DEFAULT 0,
  telemetry_source TEXT,                       -- composite descriptor, e.g. 'l1+lhm+nvapi' (18_GPU_VENDOR_APIS)
  overlay_build_id TEXT,                       -- native DLL build that produced this data
  exit_status TEXT NOT NULL,                   -- normal|crashed|unhooked_safety|degraded|interrupted

  -- Layout v3 (2026-08-05) changed four things here, and none had shipped:
  --   * `hdr INTEGER` -> `hdr_flag`/`hdr_source`, matching the three tri-states
  --     beside it. The record's `hdr` bool became `colorSpace` and a bool has no
  --     third state; an INTEGER column would have reintroduced zero-means-two-
  --     things one layer down.
  --   * `dlss_rr` leaves the `upscaler` domain. RR is an independent axis and is
  --     already stored as `rr_flag`; it was a mutually exclusive upscaler VALUE in
  --     the record, which is the conflation v3 removed. A session where DLSS-SR
  --     and RR ran together stores `upscaler='dlss'` with `rr_flag='yes'`.
  --   * `rt_tier` + `hooks_installed_mask`: 03_METRICS' definite RT `No` needs
  --     both, and neither had a column. Without them the verdict is decided at
  --     finalize from evidence that cannot distinguish "no rays" from "no hook".
  --   * `upscaler_sharpness`, so the accessor hook's yield has somewhere to land.
  -- Free exactly once: 0001_init.sql does not exist yet (see below).

  -- presentation
  api TEXT, present_mode TEXT, swap_effect TEXT,
  hdr_flag TEXT NOT NULL DEFAULT 'na', hdr_source TEXT,  -- tri-state, like rt/rr beside it
  sync_interval_mode TEXT,                     -- observed vsync behavior

  -- upscaling / FG (measured at tier 1)
  upscaler TEXT,                               -- none|dlss|fsr2|fsr3|fsr4|xess|nis|unknown
  upscaler_quality TEXT,
  upscaler_sharpness INTEGER,                  -- percent; NULL when the API reports none
  render_w INTEGER, render_h INTEGER,          -- dominant segment
  output_w INTEGER, output_h INTEGER,
  upscale_ratio REAL,
  settings_changed_midsession INTEGER NOT NULL DEFAULT 0,
  fg_mode TEXT NOT NULL DEFAULT 'none',
  fg_source TEXT,                              -- api|presentdelta|etw|cadence|manual
  fg_factor REAL,

  -- ray tracing (measured at tier 1)
  rt_flag TEXT NOT NULL DEFAULT 'na', rt_source TEXT,   -- measured|manual|inherited
  pt_flag TEXT NOT NULL DEFAULT 'na', pt_source TEXT,
  pt_confidence REAL,                          -- heuristic score, never auto-promoted to 'yes'
  rr_flag TEXT NOT NULL DEFAULT 'na', rr_source TEXT,
  rt_frame_pct REAL, rays_per_pixel REAL, rt_pso_count INTEGER,
  rt_tier INTEGER,                             -- D3D12_RAYTRACING_TIER x10; NULL = not queried
  hooks_installed_mask INTEGER,                -- FlHookFamily union over the session
  raster_pso_count INTEGER,

  -- frame statistics
  frame_count INTEGER NOT NULL, app_frame_count INTEGER NOT NULL,
  displayed_frame_count INTEGER NOT NULL, dropped_frames INTEGER NOT NULL,
  native_fps REAL, displayed_fps REAL,
  median_fps REAL, p1_low_fps REAL, p01_low_fps REAL,
  displayed_p1_low_fps REAL,                   -- 03_METRICS §Lows: stored for the
                                               -- Displayed chart series, never the headline
  min_fps REAL, max_fps REAL, frametime_stddev_ms REAL,
  stutter_count INTEGER, stutter_time_pct REAL,
  pso_stutter_pct REAL,                        -- tier 1 only
  reflex_active INTEGER, latency_avg_us INTEGER, latency_p95_us INTEGER,

  -- data quality
  dropped_records INTEGER NOT NULL DEFAULT 0,  -- ring overflow (agent stalled)
  fault_count INTEGER NOT NULL DEFAULT 0,      -- overlay hook faults
  data_quality_warnings INTEGER NOT NULL DEFAULT 0,

  -- memory & sensors
  vram_proc_avg_mb REAL, vram_proc_max_mb REAL, vram_budget_exceeded_pct REAL,
  vram_adapter_max_mb REAL,
  avg_cpu_temp REAL, max_cpu_temp REAL,
  avg_gpu_temp REAL, max_gpu_temp REAL, max_gpu_hotspot REAL,
  avg_gpu_load REAL, avg_cpu_load REAL, avg_ram_mb REAL,
  avg_gpu_power_w REAL, throttle_pct REAL
);
CREATE INDEX ix_sessions_game_started ON sessions(game_id, started_at DESC);

-- resolution/upscaler changes within one session (03_METRICS §Upscaling)
CREATE TABLE session_segments (
  id INTEGER PRIMARY KEY,
  session_id INTEGER NOT NULL REFERENCES sessions(id) ON DELETE CASCADE,
  start_frame INTEGER NOT NULL, end_frame INTEGER NOT NULL,
  render_w INTEGER, render_h INTEGER, output_w INTEGER, output_h INTEGER,
  upscaler TEXT, upscaler_quality TEXT, fg_mode TEXT,
  native_fps REAL, displayed_fps REAL, p1_low_fps REAL
);
CREATE INDEX ix_segments_session ON session_segments(session_id, start_frame);

-- Per-frame series. Every column of the CSV export (03_METRICS §Export schema)
-- must be reconstructible from this table plus session_segments — the exporter
-- reads from here, not from the live ring.
CREATE TABLE frame_blobs (
  session_id INTEGER PRIMARY KEY REFERENCES sessions(id) ON DELETE CASCADE,
  codec TEXT NOT NULL, sample_count INTEGER NOT NULL,
  frametimes BLOB NOT NULL,        -- float32[] ms
  frame_flags BLOB NOT NULL,       -- byte[] : generated/dropped/gap bits
  rt_flags BLOB,                   -- byte[] : one per frame, all 3 bits preserved
                                   -- (asBuild | dispatchRays | rtPsoAlive). Collapsing
                                   -- these loses the inline-RayQuery distinction.
  render_res BLOB,                 -- uint16[] : TWO pairs per frame (render W/H, output W/H),
                                   -- stored only when either varies
  dispatch_rays BLOB,              -- uint32[] : ray volume per frame, tier 1 only
  pso_created BLOB,                -- uint16[] : compile COUNT per frame, not a flag
  vram_proc BLOB,                  -- uint32[] MB per frame; held 1 Hz sample, see 03_METRICS
  latency_us BLOB                  -- uint32[], tier 1 + Reflex only
);

CREATE TABLE sensor_blobs (
  session_id INTEGER NOT NULL REFERENCES sessions(id) ON DELETE CASCADE,
  series TEXT NOT NULL,            -- cpu_temp|gpu_temp|gpu_hotspot|gpu_load|gpu_power|vram_proc|vram_adapter|cpu_load|ram_mb
  hz REAL NOT NULL, codec TEXT NOT NULL, data BLOB NOT NULL,
  PRIMARY KEY (session_id, series)
);

CREATE TABLE session_annotations (
  session_id INTEGER PRIMARY KEY REFERENCES sessions(id) ON DELETE CASCADE,
  tags TEXT, notes TEXT
);

CREATE TABLE settings (key TEXT PRIMARY KEY, value TEXT NOT NULL);
CREATE TABLE legal_acceptance (doc TEXT PRIMARY KEY, version TEXT NOT NULL, accepted_at INTEGER NOT NULL);
```

## Comparison safety

Because Tier-1 and Tier-2 sessions carry different fields, every query that compares sessions **must** filter or group by `capture_tier`. The Compare view refuses to overlay mixed tiers without an explicit "compare across tiers anyway" acknowledgement, and marks the chart accordingly. Likewise `settings_changed_midsession = 1` sessions are excluded from trend lines by default (they average across a settings change and are misleading), with a toggle to include them.

## Blob encoding

`float32`/`uint16`/`uint32` little-endian arrays through `DeflateStream` (Optimal). One hour at 100 fps ≈ 360k frames ≈ 1.4 MB raw frametimes ≈ **≤ 0.7 MB stored**; flags compress far better; render-res only stored when it varies. Decode helpers in `Infrastructure.Blobs` with round-trip tests (NaN forbidden — assert).

## Retention

Default: raw blobs for the **last 20 sessions per game** (configurable N or unlimited). Aggregates and segments kept forever. Sweep at finalize + on demand (Tools → DB maintenance, which also offers `PRAGMA integrity_check`, `VACUUM`, backup).

## Migrations

Sequential embedded SQL (`Migrations/0001_init.sql`, `0002_*.sql`, …), applied at startup by whichever process opens the DB first, guarded by `schema_migrations` + a global mutex. Never edit an applied script; only append.

**`0001_init.sql` creates the schema above directly.** There is no v1 → v2
upgrade path, because there is no released v1: nothing has shipped, so no
FrameLedger database exists anywhere in the world. Writing and testing a
migration from a schema that never existed is work with no user on the other end
of it. The `schema_migrations` machinery still ships from day one — it is what
makes the *next* migration safe, and `11_UPDATER` §Versioning already requires
migrations to cover every released `MAJOR-1`. That obligation begins at the first
release, not before it.

## Hardware change markers (FR-6.3)

Snapshot captured per session, hashed, deduped. Trend queries join consecutive sessions' snapshots; differing fields emit a marker `{after_session_id, field, old, new}`. GPU driver version now comes from the vendor API (`18_GPU_VENDOR_APIS`) rather than WMI, which makes it accurate enough to be worth charting.
