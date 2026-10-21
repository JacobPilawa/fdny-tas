-- environment/scripts/research_schema.sql
--
-- Phase 3 research-state store (see plan.md). Five tables. Principles:
--   * facts (symbols) and interpretation (hypotheses) are separate tables
--   * a hypothesis is never overwritten -- supersede it with a new row
--   * every hypothesis links to >=1 evidence row; no confidence w/o evidence
--   * confidence is an explicit column, never implied by a row existing
--   * events is append-only: the audit log of what changed and why
--
-- Apply with:  sqlite3 research.db ".read environment/scripts/research_schema.sql"
-- Idempotent (CREATE TABLE IF NOT EXISTS).

PRAGMA journal_mode = WAL;
PRAGMA foreign_keys = ON;

-- --------------------------------------------------------------------------
-- symbols: the labeled facts. One row per thing we have named/typed in
-- Ghidra (function, global, struct, enum). "What is true now."
-- --------------------------------------------------------------------------
CREATE TABLE IF NOT EXISTS symbols (
    id           INTEGER PRIMARY KEY,
    kind         TEXT NOT NULL CHECK (kind IN ('function','global','struct','enum','type')),
    address      TEXT,                       -- e.g. '0x0042c800' (null for pure types)
    name         TEXT NOT NULL,              -- current Ghidra label
    orig_name    TEXT,                       -- e.g. 'FUN_0042c800' before we touched it
    class_name   TEXT,                       -- C++ class / namespace, if any
    subsystem    TEXT,                       -- source dir: main, rescue, hose, script, ...
    source_file  TEXT,                       -- e.g. 'rescue\RescueBot.cpp' when pinned
    signature    TEXT,                       -- current decompiled signature
    summary      TEXT,                       -- one-line what-it-does
    confidence   TEXT NOT NULL DEFAULT 'low' CHECK (confidence IN ('low','medium','high')),
    status       TEXT NOT NULL DEFAULT 'named' CHECK (status IN ('stub','named','typed','documented','verified')),
    created_at   TEXT NOT NULL DEFAULT (datetime('now')),
    updated_at   TEXT NOT NULL DEFAULT (datetime('now')),
    UNIQUE (kind, address)
);
CREATE INDEX IF NOT EXISTS ix_symbols_subsystem ON symbols(subsystem);
CREATE INDEX IF NOT EXISTS ix_symbols_source_file ON symbols(source_file);

-- --------------------------------------------------------------------------
-- hypotheses: interpretive claims. Append-only in spirit -- to revise,
-- insert a new row and point the old one's superseded_by at it.
-- --------------------------------------------------------------------------
CREATE TABLE IF NOT EXISTS hypotheses (
    id            INTEGER PRIMARY KEY,
    symbol_id     INTEGER REFERENCES symbols(id),   -- nullable: may be about a subsystem
    subsystem     TEXT,
    statement     TEXT NOT NULL,
    confidence    TEXT NOT NULL DEFAULT 'low' CHECK (confidence IN ('low','medium','high')),
    status        TEXT NOT NULL DEFAULT 'open' CHECK (status IN ('open','supported','refuted','confirmed')),
    superseded_by INTEGER REFERENCES hypotheses(id),
    created_at    TEXT NOT NULL DEFAULT (datetime('now'))
);
CREATE INDEX IF NOT EXISTS ix_hypotheses_symbol ON hypotheses(symbol_id);

-- --------------------------------------------------------------------------
-- evidence: the observations backing a hypothesis (or annotating a symbol).
-- --------------------------------------------------------------------------
CREATE TABLE IF NOT EXISTS evidence (
    id            INTEGER PRIMARY KEY,
    hypothesis_id INTEGER REFERENCES hypotheses(id),
    symbol_id     INTEGER REFERENCES symbols(id),
    kind          TEXT NOT NULL CHECK (kind IN ('decompile','string','xref','import','disasm','doc','web','other')),
    ref           TEXT,                      -- address / URL / string literal / file:line
    detail        TEXT NOT NULL,
    created_at    TEXT NOT NULL DEFAULT (datetime('now'))
);
CREATE INDEX IF NOT EXISTS ix_evidence_hypothesis ON evidence(hypothesis_id);
CREATE INDEX IF NOT EXISTS ix_evidence_symbol ON evidence(symbol_id);

-- --------------------------------------------------------------------------
-- research_tasks: the work queue. Add columns (parent_task_id, ...) only
-- when a real task needs them.
-- --------------------------------------------------------------------------
CREATE TABLE IF NOT EXISTS research_tasks (
    id          INTEGER PRIMARY KEY,
    title       TEXT NOT NULL,
    subsystem   TEXT,
    status      TEXT NOT NULL DEFAULT 'todo' CHECK (status IN ('todo','doing','blocked','done')),
    priority    INTEGER NOT NULL DEFAULT 3,   -- 1 = highest
    notes       TEXT,
    created_at  TEXT NOT NULL DEFAULT (datetime('now')),
    updated_at  TEXT NOT NULL DEFAULT (datetime('now'))
);
CREATE INDEX IF NOT EXISTS ix_tasks_status ON research_tasks(status, priority);

-- --------------------------------------------------------------------------
-- events: append-only audit log. Every rename / retype / comment /
-- hypothesis / task transition writes one row here.
-- --------------------------------------------------------------------------
CREATE TABLE IF NOT EXISTS events (
    id         INTEGER PRIMARY KEY,
    ts         TEXT NOT NULL DEFAULT (datetime('now')),
    kind       TEXT NOT NULL,                -- rename | retype | comment | hypothesis | task | note | session
    symbol_id  INTEGER REFERENCES symbols(id),
    summary    TEXT NOT NULL,
    detail     TEXT
);
CREATE INDEX IF NOT EXISTS ix_events_ts ON events(ts);

-- Keep symbols.updated_at fresh.
CREATE TRIGGER IF NOT EXISTS trg_symbols_touch
AFTER UPDATE ON symbols FOR EACH ROW
BEGIN
    UPDATE symbols SET updated_at = datetime('now') WHERE id = OLD.id;
END;
