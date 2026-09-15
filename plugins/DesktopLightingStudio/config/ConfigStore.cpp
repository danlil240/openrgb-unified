/*---------------------------------------------------------*\
|| ConfigStore.cpp                                           |
||                                                           |
||   SPDX-License-Identifier: GPL-2.0-or-later               |
\*---------------------------------------------------------*/

#include "ConfigStore.h"
#include "ConfigMigration.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QSaveFile>
#include <QStringList>
#include <QTimer>

namespace studio
{

/* Whole-document size cap — a workspace with thousands of objects
   is ~1 MB; anything past this is not a lighting document. */
static constexpr qint64 MAX_FILE_BYTES = 8 * 1024 * 1024;

ConfigStore::ConfigStore(const QString& workspace_dir, QObject* parent)
    : QObject(parent)
    , dir(workspace_dir)
{
    autosave_timer = new QTimer(this);
    autosave_timer->setSingleShot(true);
    connect(autosave_timer, &QTimer::timeout,
            this, &ConfigStore::WriteAutosave);

    change_timer = new QTimer(this);
    change_timer->setSingleShot(true);
    change_timer->setInterval(150);
    connect(change_timer, &QTimer::timeout,
            this, &ConfigStore::EvaluateExternalChange);

    watcher = new QFileSystemWatcher(this);
    connect(watcher, &QFileSystemWatcher::fileChanged,
            this, &ConfigStore::OnWatcherFired);
}

bool ConfigStore::EnsureWorkspaceDir(QString* error)
{
    QDir d(dir);
    if(!d.exists() && !d.mkpath("."))
    {
        if(error)
        {
            *error = QStringLiteral("cannot create %1").arg(dir);
        }
        return false;
    }
    /* Ship the schema beside the document so hand editors have it. */
    const QString schema_dir = dir + "/schemas";
    if(!QFileInfo::exists(schema_dir))
    {
        d.mkpath("schemas");
    }
    const QString schema_dst = schema_dir + "/studio.schema.json";
    if(!QFileInfo::exists(schema_dst))
    {
        QFile::copy(QStringLiteral(":/studio/studio.schema.json"), schema_dst);
    }
    return true;
}

bool ConfigStore::DocumentExists() const
{
    return QFileInfo::exists(DocumentPath());
}

bool ConfigStore::MigrationDone() const
{
    return QFileInfo::exists(MigrationMarkerPath());
}

void ConfigStore::MarkMigrationDone()
{
    /* The marker may be written before anything else exists in the
       workspace (nothing-to-migrate path) — create the dir first. */
    EnsureWorkspaceDir();
    QFile f(MigrationMarkerPath());
    if(f.open(QIODevice::WriteOnly | QIODevice::Text))
    {
        f.write("{\"note\":\"legacy host-settings migration already handled\"}\n");
    }
}

bool ConfigStore::BackupLegacySettings(const nlohmann::json& legacy,
                                       QString* error)
{
    /* Keep the original host-settings blob verbatim before the new
       store becomes exclusive. */
    const QByteArray bytes =
        QByteArray::fromStdString(legacy.dump(2));
    return WriteAtomic(LegacyBackupPath(), bytes, error);
}

ConfigStore::MigrationResult
ConfigStore::RunLegacyMigration(const nlohmann::json& legacy, QString* detail)
{
    if(DocumentExists() || MigrationDone())
    {
        return MigrationResult::NotNeeded;
    }
    if(!HasLegacySettings(legacy))
    {
        MarkMigrationDone();   /* nothing to move — decided permanently */
        return MigrationResult::NotNeeded;
    }

    if(!EnsureWorkspaceDir(detail))
    {
        return MigrationResult::Failed;   /* transient — retried next launch */
    }

    /* The backup precedes validation AND the save: a legacy blob is
       preserved verbatim even when it proves unmigratable, and never
       migrated without a copy on disk. A failed backup is treated as
       transient — no marker, so the move is retried. */
    QString berr;
    if(!BackupLegacySettings(legacy, &berr))
    {
        if(detail)
        {
            *detail = QStringLiteral("backup failed: %1").arg(berr);
        }
        return MigrationResult::Failed;
    }

    StudioDocument migrated;
    std::vector<std::string> merrs;
    if(!MigrateLegacySettings(legacy, migrated, &merrs))
    {
        /* A blob that fails validation will fail forever — the marker
           is correct here, and the backup above keeps the original. */
        MarkMigrationDone();
        if(detail)
        {
            *detail = QString::fromStdString(
                merrs.empty() ? "unknown error" : merrs.front());
        }
        return MigrationResult::Invalid;
    }

    if(!Save(migrated, detail))
    {
        /* Disk full / AV lock / sync conflict — do NOT mark done, or
           the user's old scene is orphaned (doc absent, marker set,
           migration never retried). */
        return MigrationResult::Failed;
    }
    MarkMigrationDone();
    return MigrationResult::Migrated;
}

QByteArray ConfigStore::Serialize(const StudioDocument& doc) const
{
    /* Pretty-printed, two-space, deterministic (nlohmann orders
       object keys). */
    return QByteArray::fromStdString(ToJson(doc).dump(2)) + "\n";
}

QByteArray ConfigStore::ReadFile(const QString& path) const
{
    QFile f(path);
    if(!f.open(QIODevice::ReadOnly))
    {
        return QByteArray();
    }
    if(f.size() > MAX_FILE_BYTES)
    {
        return QByteArray();
    }
    return f.readAll();
}

bool ConfigStore::WriteAtomic(const QString& path, const QByteArray& bytes,
                              QString* error)
{
    QSaveFile f(path);
    /* No Text flag — newline translation would desync the bytes we
       remember in last_written from the bytes on disk, and our own
       save would then look like an external change. */
    if(!f.open(QIODevice::WriteOnly))
    {
        if(error)
        {
            *error = QStringLiteral("%1: %2").arg(path, f.errorString());
        }
        return false;
    }
    if(f.write(bytes) != bytes.size())
    {
        if(error)
        {
            *error = QStringLiteral("%1: %2").arg(path, f.errorString());
        }
        return false;
    }
    if(!f.commit())
    {
        if(error)
        {
            *error = QStringLiteral("%1: %2").arg(path, f.errorString());
        }
        return false;
    }
    return true;
}

bool ConfigStore::LoadFile(const QString& path, StudioDocument* out,
                           QString* error, QString* warnings) const
{
    QFile f(path);
    if(!f.open(QIODevice::ReadOnly))
    {
        if(error)
        {
            *error = QStringLiteral("%1: %2").arg(path, f.errorString());
        }
        return false;
    }
    if(f.size() > MAX_FILE_BYTES)
    {
        if(error)
        {
            *error = QStringLiteral("%1: file too large").arg(path);
        }
        return false;
    }
    const QByteArray bytes = f.readAll();

    nlohmann::json j;
    try
    {
        j = nlohmann::json::parse(bytes.constData());
    }
    catch(const std::exception& e)
    {
        if(error)
        {
            *error = QStringLiteral("%1: invalid JSON (%2)")
                         .arg(path, QString::fromUtf8(e.what()));
        }
        return false;
    }

    StudioDocument candidate;
    std::vector<std::string> errs, warns;
    if(!FromJson(j, candidate, &errs, &warns))
    {
        QStringList lines;
        for(const std::string& e : errs)
        {
            lines << QString::fromStdString(e);
        }
        if(error)
        {
            *error = QStringLiteral("%1: %2").arg(path, lines.join("; "));
        }
        return false;
    }
    if(warnings && !warns.empty())
    {
        QStringList lines;
        for(const std::string& w : warns)
        {
            lines << QString::fromStdString(w);
        }
        *warnings = lines.join("; ");
    }
    *out = candidate;
    return true;
}

bool ConfigStore::Load(StudioDocument* out, QString* error, QString* warnings)
{
    if(!LoadFile(DocumentPath(), out, error, warnings))
    {
        return false;
    }
    last_written = ReadFile(DocumentPath());
    WatchDocument();
    return true;
}

void ConfigStore::UpdateBackup()
{
    if(!DocumentExists())
    {
        return;
    }
    /* The backup is the last VALID studio.json — don't overwrite it
       with a file that no longer parses. */
    StudioDocument scratch;
    if(!LoadFile(DocumentPath(), &scratch, nullptr))
    {
        return;
    }
    /* Atomic too — a remove+copy pair could leave no backup at all
       if the process dies between the two calls. */
    WriteAtomic(BackupPath(), ReadFile(DocumentPath()), nullptr);
}

bool ConfigStore::Save(const StudioDocument& doc, QString* error)
{
    if(!EnsureWorkspaceDir(error))
    {
        return false;
    }
    const QByteArray bytes = Serialize(doc);
    UpdateBackup();
    if(!WriteAtomic(DocumentPath(), bytes, error))
    {
        return false;
    }
    last_written = bytes;
    WatchDocument();
    SetClean();
    DiscardRecovery();   /* the autosave is now stale */
    return true;
}

bool ConfigStore::SaveAs(const StudioDocument& doc, const QString& path,
                         QString* error)
{
    if(path == DocumentPath())
    {
        return Save(doc, error);
    }
    return WriteAtomic(path, Serialize(doc), error);
}

void ConfigStore::MarkDirty()
{
    if(!dirty_state)
    {
        dirty_state = true;
        emit dirtyChanged();
    }
    autosave_timer->start(autosave_ms);
}

void ConfigStore::SetClean()
{
    autosave_timer->stop();
    if(dirty_state)
    {
        dirty_state = false;
        emit dirtyChanged();
    }
}

void ConfigStore::SetSnapshotProvider(std::function<StudioDocument()> fn)
{
    snapshot_fn = std::move(fn);
}

void ConfigStore::SetAutosaveDelayMs(int ms)
{
    autosave_ms = ms;
}

void ConfigStore::FlushAutosave()
{
    WriteAutosave();
}

void ConfigStore::WriteAutosave()
{
    if(!dirty_state || !snapshot_fn)
    {
        return;
    }
    QString error;
    if(!WriteAtomic(AutosavePath(), Serialize(snapshot_fn()), &error))
    {
        emit autosaveFailed(error);
    }
}

bool ConfigStore::HasRecovery() const
{
    const QByteArray auto_bytes = ReadFile(AutosavePath());
    if(auto_bytes.isEmpty())
    {
        return false;
    }
    /* Nothing to recover when the autosave matches the document. */
    if(auto_bytes == ReadFile(DocumentPath()))
    {
        return false;
    }
    StudioDocument scratch;
    return LoadFile(AutosavePath(), &scratch, nullptr);
}

bool ConfigStore::RecoverAutosave(StudioDocument* out, QString* error)
{
    return LoadFile(AutosavePath(), out, error);
}

void ConfigStore::DiscardRecovery()
{
    QFile::remove(AutosavePath());
}

void ConfigStore::WatchDocument()
{
    if(DocumentExists() && !watcher->files().contains(DocumentPath()))
    {
        watcher->addPath(DocumentPath());
    }
}

void ConfigStore::OnWatcherFired(const QString& path)
{
    if(path != DocumentPath())
    {
        return;
    }
    /* QFileSystemWatcher may drop a watched path when the file is
       replaced by rename (QSaveFile) — re-add and debounce the
       evaluation so commit flicker doesn't read a half state. */
    WatchDocument();
    change_timer->start();
}

void ConfigStore::EvaluateExternalChange()
{
    const QByteArray cur = ReadFile(DocumentPath());
    if(!cur.isEmpty() && cur == last_written)
    {
        return;   /* our own save */
    }
    emit externalChange();
}

} /* namespace studio */
