/*---------------------------------------------------------*\
|| ConfigStore.h                                             |
||                                                           |
||   Qt file-system store for studio.json — the            |
||   authoritative workspace. Owns atomic saves            |
||   (QSaveFile), the last-valid backup, debounced         |
||   autosave/recovery, one-time legacy-settings           |
||   migration markers, and external-change detection      |
||   (QFileSystemWatcher + content compare).               |
||                                                           |
||   The store never decides what the document means — it  |
||   only validates into candidates (config/StudioConfig)  |
||   and moves bytes. SceneBridge owns the active state.   |
||                                                           |
||   SPDX-License-Identifier: GPL-2.0-or-later               |
\*---------------------------------------------------------*/

#pragma once

#include "StudioConfig.h"

#include <QObject>
#include <QByteArray>
#include <QString>

#include <functional>

class QFileSystemWatcher;
class QTimer;

namespace studio
{

class ConfigStore : public QObject
{
    Q_OBJECT

public:
    explicit ConfigStore(const QString& workspace_dir, QObject* parent = nullptr);

    QString WorkspaceDir()  const { return dir; }
    QString DocumentPath()  const { return dir + "/studio.json"; }
    QString BackupPath()    const { return dir + "/studio.backup.json"; }
    QString AutosavePath()  const { return dir + "/studio.autosave.json"; }
    QString LegacyBackupPath() const { return dir + "/legacy-settings.backup.json"; }
    QString MigrationMarkerPath() const { return dir + "/migration.done"; }

    /* Creates the workspace dir (+ schemas/ with a copy of the
       bundled studio.schema.json) if needed. */
    bool EnsureWorkspaceDir(QString* error = nullptr);

    bool DocumentExists() const;

    /* One-time migration bookkeeping. The marker file means "we
       already looked" — deleting studio.json (or saving an
       intentionally empty scene) must not restart migration. */
    bool MigrationDone() const;
    void MarkMigrationDone();
    bool BackupLegacySettings(const nlohmann::json& legacy, QString* error = nullptr);

    enum class MigrationResult
    {
        NotNeeded,   /* already migrated, doc exists, or nothing to move */
        Migrated,    /* migrated doc saved to studio.json; marker written */
        Invalid,     /* blob failed validation — permanent; marker written */
        Failed       /* write failed — transient; marker NOT written so
                        the next launch retries instead of orphaning the
                        old scene */
    };
    /* Runs the whole legacy-settings -> studio.json move with the
       correct ordering: backup the original blob first, validate the
       candidate, then save; the marker is written only when nothing
       needed migrating or the outcome is permanent (landed / invalid
       blob). Never writes the marker after a failed save. */
    MigrationResult RunLegacyMigration(const nlohmann::json& legacy,
                                       QString* detail = nullptr);

    /* Parse + validate studio.json into `out`. On failure `out` is
       untouched and `error` carries the field messages. A missing
       file is an error here — callers check DocumentExists first. */
    bool Load(StudioDocument* out, QString* error = nullptr,
              QString* warnings = nullptr);
    bool LoadFile(const QString& path, StudioDocument* out,
                  QString* error = nullptr, QString* warnings = nullptr) const;

    /* Atomic save: last-valid backup first, then QSaveFile commit.
       On failure returns false with `error` set and the previous
       file is left in place. */
    bool Save(const StudioDocument& doc, QString* error = nullptr);
    bool SaveAs(const StudioDocument& doc, const QString& path,
                QString* error = nullptr);

    /* Dirty tracking + debounced autosave. The provider is invoked
       on this object's thread when the autosave timer fires. */
    bool dirty() const { return dirty_state; }
    void MarkDirty();
    void SetClean();
    void SetSnapshotProvider(std::function<StudioDocument()> fn);
    void SetAutosaveDelayMs(int ms);
    /* Write the autosave immediately if dirty — bridge teardown so
       a clean shutdown still leaves recoverable edits. */
    void FlushAutosave();

    /* Recovery file (studio.autosave.json): present when it parses
       as a valid document AND differs from the on-disk studio.json. */
    bool HasRecovery() const;
    bool RecoverAutosave(StudioDocument* out, QString* error = nullptr);
    void DiscardRecovery();

    /* Begin watching studio.json for external edits. */
    void WatchDocument();

signals:
    void dirtyChanged();
    /* studio.json changed on disk and it wasn't our write. */
    void externalChange();
    void autosaveFailed(const QString& msg);

private:
    QByteArray Serialize(const StudioDocument& doc) const;
    QByteArray ReadFile(const QString& path) const;
    bool       WriteAtomic(const QString& path, const QByteArray& bytes,
                           QString* error);
    /* Copy the current studio.json to studio.backup.json — only if
       it still parses as a valid document (backup = last valid). */
    void       UpdateBackup();
    void       OnWatcherFired(const QString& path);
    void       OnDirectoryChanged(const QString& path);
    void       EvaluateExternalChange();
    void       WriteAutosave();

    QString                 dir;
    QFileSystemWatcher*     watcher       = nullptr;
    QTimer*                 autosave_timer = nullptr;
    /* Rename/commit produces a burst of watcher events — the file
       can briefly vanish mid-commit, so evaluation is debounced. */
    QTimer*                 change_timer  = nullptr;
    std::function<StudioDocument()> snapshot_fn;
    QByteArray              last_written;   /* content we last wrote/read */
    /* Directory watching makes every workspace write land in the
       debounced evaluator — remember the on-disk content already
       reported so unchanged state doesn't re-signal externalChange
       (e.g. autosave churn while the file is missing/different). */
    QByteArray              ext_content;
    bool                    ext_reported  = false;
    bool                    dirty_state   = false;
    int                     autosave_ms   = 2000;
};

} /* namespace studio */
