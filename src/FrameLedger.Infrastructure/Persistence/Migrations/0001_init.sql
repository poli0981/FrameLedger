-- 0001_init.sql — the v2 schema of docs/06_DATA_MODEL.md, created directly (there is no v1 to
-- migrate from: nothing has shipped). Applied once by MigrationRunner under schema_migrations.
--
-- NEVER EDIT THIS SCRIPT ONCE APPLIED ANYWHERE. Append 0002_*.sql instead.
--
-- Conventions this file states once, because the document did not:
--   * every *_at / *_ms INTEGER is unix-ms UTC. QPC never leaves the capture pipeline except
--     sessions.qpc_epoch / qpc_frequency, which are the time base the blobs are aligned to.
--   * every tri-state TEXT column defaults to 'na', never to 'none' or 'no' — `none` is the one
--     negative 03_METRICS lets a consumer aggregate, and a DEFAULT of it would reinstate at the
--     storage layer the affirmative negative the writer and the consumer both refuse.
--   * NULL in a measured column means "not measured" (N/A), never 0.

CREATE TABLE schema_migrations (
  version    INTEGER PRIMARY KEY,
  applied_at INTEGER NOT NULL
);

CREATE TABLE games (
  id INTEGER PRIMARY KEY,
  name TEXT NOT NULL,
  -- COLLATE NOCASE: consent matches OrdinalIgnoreCase on the normalised full path and nothing
  -- else (Domain.Consent.ExecutableFingerprint), and the UNIQUE index must agree with the lookup.
  exe_path TEXT NOT NULL UNIQUE COLLATE NOCASE,
  platform TEXT NOT NULL DEFAULT 'none',       -- steam|gog|epic|itch|none
  store_id TEXT,
  engine TEXT, engine_version TEXT,
  publisher TEXT, game_version TEXT,
  cover_path TEXT, notes TEXT,

  -- the ExecutableFingerprint beside the path: what detects that the binary changed under a consent
  exe_size_bytes INTEGER,
  exe_mtime_ms INTEGER,

  -- hooking state (19_SAFETY)
  hook_enabled INTEGER NOT NULL DEFAULT 0,     -- 0 = Tier 2, i.e. nothing measured; default for every new game
  hook_consent_at INTEGER,                     -- per-game informed consent timestamp, stamped by the Agent
  -- ConsentProvenance by NAME (never its number); the default means no disclosure was shown
  hook_consent_provenance TEXT NOT NULL DEFAULT 'NotRecorded',
  hook_consent_disclosure_version TEXT NOT NULL DEFAULT '',
  hook_blocked_reason TEXT,                    -- set by a real guard verdict; non-null = toggle disabled in UI.
                                               -- NOTHING in P2 clears it (06_DATA_MODEL: the owner's question)
  -- the static pre-scan's THIRD state, which one nullable TEXT could not carry (05_DETECTION):
  -- 'unverified' is "could not verify" — neither a block nor a pass, and it must not disable the toggle
  hook_prescan_state TEXT NOT NULL DEFAULT 'not_run'
    CHECK (hook_prescan_state IN ('not_run', 'clean', 'blocked', 'unverified')),
  hook_autodisabled_reason TEXT,               -- set after repeated crashes
  hook_autodisabled_at INTEGER,
  hook_crash_count INTEGER NOT NULL DEFAULT 0,
  hook_last_injected_at INTEGER,
  capability_flags TEXT,                       -- JSON: what the game SHIPS (dlss/dlssg/dlssd/fsr/xess) — never a measurement

  -- tri-state defaults inherited by new sessions
  rt_default TEXT NOT NULL DEFAULT 'na',
  pt_default TEXT NOT NULL DEFAULT 'na',
  rr_default TEXT NOT NULL DEFAULT 'na',

  detection_rules_version TEXT,
  field_provenance TEXT,                       -- JSON: {"engine":"detected","publisher":"user",...}; absent reads as "user"
  added_at INTEGER NOT NULL, updated_at INTEGER NOT NULL
);

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
  -- the identity the .partial file, the pipe and the recovery path all key on (20_OPEN_QUESTIONS §G)
  session_guid TEXT NOT NULL UNIQUE,
  game_id INTEGER NOT NULL REFERENCES games(id) ON DELETE CASCADE,
  snapshot_id INTEGER NOT NULL REFERENCES hardware_snapshots(id),
  started_at INTEGER NOT NULL, ended_at INTEGER NOT NULL, duration_s REAL NOT NULL,
  -- the time base every blob and sensor series is aligned to (03_METRICS §Export, §Sensor aggregates)
  qpc_epoch INTEGER NOT NULL, qpc_frequency INTEGER NOT NULL,

  capture_tier INTEGER NOT NULL CHECK (capture_tier IN (1, 2)),   -- 1 = hooked, 2 = not hooked
  capture_mode TEXT NOT NULL CHECK (capture_mode IN ('attach', 'launch')),
  capture_notes TEXT,                          -- why tier degraded, late_attach, the fine reason + guard signal
  late_attach INTEGER NOT NULL DEFAULT 0,
  telemetry_source TEXT,                       -- composite descriptor, e.g. 'l1+lhm+nvapi' (18_GPU_VENDOR_APIS)
  overlay_build_id TEXT,                       -- native DLL build that produced this data
  exit_status TEXT NOT NULL
    CHECK (exit_status IN ('normal', 'crashed', 'unhooked_safety', 'degraded', 'interrupted')),

  -- the drain's own accounting (04_CAPTURE §Ring draining)
  drain_ticks INTEGER, foreground_ticks INTEGER,
  records_before_attach INTEGER,
  dxgi_presents_before_hook INTEGER,           -- launch mode's cost meter; NULL = the hook never saw a present
  gap_count INTEGER NOT NULL DEFAULT 0,
  guard_ticks_published INTEGER,
  launch_wait_ms INTEGER,

  -- presentation
  api TEXT, present_mode TEXT, swap_effect TEXT,
  hdr_flag TEXT NOT NULL DEFAULT 'na', hdr_source TEXT,
  sync_interval_mode TEXT,

  -- upscaling / FG (measured at tier 1)
  upscaler TEXT,                               -- none|dlss|fsr2|fsr3|fsr4|xess|nis|unknown
  upscaler_quality TEXT,
  upscaler_sharpness INTEGER,                  -- percent; NULL when the API reports none
  upscaler_driver_reported TEXT,               -- the driver's word (03_METRICS §Upscaling, driver-reported rung)
  render_w INTEGER, render_h INTEGER,          -- dominant segment
  output_w INTEGER, output_h INTEGER,
  upscale_ratio REAL,
  settings_changed_midsession INTEGER NOT NULL DEFAULT 0,
  fg_mode TEXT NOT NULL DEFAULT 'na',          -- 'na', NOT 'none' (see the header)
  -- NULL = not measured; 'none' is the counted negative (03_METRICS rung 4); 'manual' is the P3 override
  fg_source TEXT CHECK (fg_source IS NULL OR fg_source IN ('api', 'cadence', 'none', 'manual')),
  fg_factor REAL,
  fg_driver_reported TEXT,
  fg_runtime_census INTEGER,                   -- FlWriterState.runtimeCensus, raw; NULL = the writer predates the field
  fg_none_withheld_reason TEXT,                -- why a counted 1.0 was NOT published as none (§H5)
  presented_fps REAL,                          -- the one number that stands alone, with its qualifier
  presented_qualifier TEXT
    CHECK (presented_qualifier IS NULL OR presented_qualifier IN ('census_not_run', 'no_fg_runtime', 'fg_runtime_loaded', 'none_withheld')),
  dxgi_unseen_total INTEGER, dxgi_present_samples INTEGER,
  displayed_counted_by TEXT CHECK (displayed_counted_by IS NULL OR displayed_counted_by IN ('hook', 'dxgi')),
  sl_tag_census INTEGER, sl_interposer_version TEXT,
  runtime_modules TEXT,                        -- JSON: name -> file version, as the census snapshot saw them
  executable_markers TEXT,                     -- JSON: the vendor SDK strings the executable file carries
  ngx_driver_words TEXT,                       -- JSON: the driver's raw NGX words, as probed

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
  displayed_p1_low_fps REAL,
  min_fps REAL, max_fps REAL, frametime_stddev_ms REAL,
  stutter_count INTEGER, stutter_time_pct REAL,
  pso_stutter_pct REAL,
  reflex_active INTEGER, latency_avg_us INTEGER, latency_p95_us INTEGER,

  -- data quality
  dropped_records INTEGER NOT NULL DEFAULT 0,  -- ring overflow (agent stalled)
  fault_count INTEGER NOT NULL DEFAULT 0,      -- overlay hook faults
  data_quality_warnings INTEGER NOT NULL DEFAULT 0,
  writer_status_at_end INTEGER, early_stop_family INTEGER, loader_signals INTEGER,

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
  swapchain_id INTEGER NOT NULL,               -- 0 = the undifferentiated stream
  start_frame INTEGER NOT NULL, end_frame INTEGER NOT NULL,
  render_w INTEGER, render_h INTEGER, output_w INTEGER, output_h INTEGER,
  upscaler TEXT, upscaler_quality TEXT, fg_mode TEXT,
  native_fps REAL, displayed_fps REAL, p1_low_fps REAL
);
CREATE INDEX ix_segments_session ON session_segments(session_id, start_frame);

-- Per-frame series. Every column of the CSV export (03_METRICS §Export schema) must be
-- reconstructible from this table plus session_segments.
CREATE TABLE frame_blobs (
  session_id INTEGER PRIMARY KEY REFERENCES sessions(id) ON DELETE CASCADE,
  codec TEXT NOT NULL, sample_count INTEGER NOT NULL,
  frametimes BLOB NOT NULL,        -- float32[] ms: EVERY raw interval, gap-adjacent ones included; the
                                   -- frame_flags gap bit is what excludes them from the statistics, so
                                   -- qpc_ms in the export stays reconstructible
  frame_flags BLOB NOT NULL,       -- byte[] : generated/dropped/gap bits
  frame_index BLOB,                -- uint32[] the writer's process-wide present counter
  swapchain_ids BLOB,              -- uint32[] only when the session held more than one stream
  rt_flags BLOB,                   -- byte[] : all 3 bits preserved (asBuildObserved|dispatchObserved|psoCreatedEver)
  render_res BLOB,                 -- uint16[] : TWO pairs per frame (render W/H, output W/H), only when either varies
  dispatch_rays BLOB,              -- uint32[] : ray volume per frame, tier 1 only
  pso_created BLOB,                -- uint16[] : compile COUNT per frame, not a flag
  vram_proc BLOB,                  -- uint32[] MB per frame; held 1 Hz sample
  latency_us BLOB                  -- uint32[], tier 1 + Reflex only
);

CREATE TABLE sensor_blobs (
  session_id INTEGER NOT NULL REFERENCES sessions(id) ON DELETE CASCADE,
  series TEXT NOT NULL,            -- t_ms|cpu_temp|gpu_temp|gpu_hotspot|gpu_load|gpu_power|vram_proc|vram_adapter|cpu_load|ram_mb
  hz REAL NOT NULL, codec TEXT NOT NULL, data BLOB NOT NULL,
  PRIMARY KEY (session_id, series)
);

CREATE TABLE session_annotations (
  session_id INTEGER PRIMARY KEY REFERENCES sessions(id) ON DELETE CASCADE,
  tags TEXT, notes TEXT
);

CREATE TABLE settings (key TEXT PRIMARY KEY, value TEXT NOT NULL);
CREATE TABLE legal_acceptance (doc TEXT PRIMARY KEY, version TEXT NOT NULL, accepted_at INTEGER NOT NULL);
