/*---------------------------------------------------------*\
|| ConfigStore.cpp                                           |
||                                                           |
||   SPDX-License-Identifier: GPL-2.0-or-later               |
\*---------------------------------------------------------*/

#include "ConfigStore.h"
#include "ConfigMigration.h"
#include "../scene/SceneJson.h"

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
    /* The file watch is dropped when studio.json is deleted or
       replaced (temp+rename editors) — only the directory watch
       lets us notice a later recreation. */
    connect(watcher, &QFileSystemWatcher::directoryChanged,
            this, &ConfigStore::OnDirectoryChanged);
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
    /* Ship the schemas beside the document so hand editors have
       them. */
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
    const QString dev_schema_dst = schema_dir + "/device.schema.json";
    if(!QFileInfo::exists(dev_schema_dst))
    {
        QFile::copy(QStringLiteral(":/studio/device.schema.json"),
                    dev_schema_dst);
    }

    /* Ship the packaged device types. Missing files only — a type
       the user edited (or deleted to force the compiled-in default)
       is never overwritten. */
    const QString preset_dir = PresetDir();
    if(!QFileInfo::exists(preset_dir))
    {
        d.mkpath("presets/devices");
    }
    const QDir bundled(QStringLiteral(":/studio/presets/devices"));
    for(const QString& name : bundled.entryList(QDir::Files))
    {
        const QString dst = preset_dir + "/" + name;
        if(!QFileInfo::exists(dst))
        {
            QFile::copy(bundled.filePath(name), dst);
        }
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
    std::vector<DevicePreset> types;
    std::vector<std::string> merrs;
    if(!MigrateLegacySettings(legacy, migrated, &types, &merrs))
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

    /* Type files land (validated) before the workspace that
       references them — a failed install leaves no half-migrated
       desk, and the marker stays unset so the move retries. */
    if(!InstallTypes(migrated, types, detail))
    {
        return MigrationResult::Failed;
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

bool ConfigStore::LoadParsed(const QString& path, StudioDocument* out,
                             std::vector<DevicePreset>* types,
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

    /*------------------------------------------------*\
    || Expanded (v1/v2) workspace — migrate through    ||
    || the scene extractor. The compact result is      ||
    || overlaid with the v2 file's own preference      ||
    || sections so theme/camera/inputs survive, then   ||
    || re-validated as a normal v3 candidate.          ||
    \*------------------------------------------------*/
    const long long version = (j.is_object()
                               && j.contains("schema_version")
                               && j["schema_version"].is_number())
        ? j["schema_version"].get<long long>()
        : -1;
    if(version >= 1 && version < STUDIO_SCHEMA_VERSION
       && j.is_object() && j.contains("scene"))
    {
        SceneDocument scene;
        std::vector<std::string> serrs;
        if(!FromJson(j["scene"], scene, &serrs))
        {
            QStringList lines;
            for(const std::string& e : serrs)
            {
                lines << QString::fromStdString(e);
            }
            if(error)
            {
                *error = QStringLiteral("%1: scene: %2")
                             .arg(path, lines.join("; "));
            }
            return false;
        }
        StudioDocument mig;
        std::vector<DevicePreset> t;
        std::vector<std::string> merrs, mwarns;
        if(!MigrateExpandedScene(scene, mig, t, &merrs, &mwarns))
        {
            QStringList lines;
            for(const std::string& e : merrs)
            {
                lines << QString::fromStdString(e);
            }
            if(error)
            {
                *error = QStringLiteral("%1: migration: %2")
                             .arg(path, lines.join("; "));
            }
            return false;
        }
        nlohmann::json wj = ToJson(mig);
        /* Preference sections: keep what the v2 file had — the
           migrated scene knows nothing about editor prefs. */
        static const char* carried[] = {
            "ui", "camera", "controls", "render", "inputs",
            "extensions",
        };
        for(const char* k : carried)
        {
            if(j.contains(k))
            {
                wj[k] = j[k];
            }
        }
        if(j.contains("name"))
        {
            wj["name"] = j["name"];
        }
        if(j.contains("output") && j["output"].is_object())
        {
            if(j["output"].contains("brightness"))
            {
                wj["output"]["brightness"] = j["output"]["brightness"];
            }
        }
        if(j.contains("effects") && j["effects"].is_object())
        {
            /* The v2 effects section wins over the scene's embedded
               effect state (same data, workspace copy is newer). */
            for(auto it = j["effects"].begin(); it != j["effects"].end(); ++it)
            {
                wj["effects"][it.key()] = it.value();
            }
        }
        /* Migration never arms live output. */
        wj["output"]["live_on_startup"] = false;

        StudioDocument candidate;
        std::vector<std::string> errs, warns;
        if(!FromJson(wj, candidate, &errs, &warns))
        {
            QStringList lines;
            for(const std::string& e : errs)
            {
                lines << QString::fromStdString(e);
            }
            if(error)
            {
                *error = QStringLiteral("%1: migrated document: %2")
                             .arg(path, lines.join("; "));
            }
            return false;
        }
        for(const std::string& w : mwarns)
        {
            warns.push_back(w);
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
        if(types != nullptr)
        {
            *types = t;
        }
        return true;
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

bool ConfigStore::LoadFile(const QString& path, StudioDocument* out,
                           QString* error, QString* warnings) const
{
    return LoadParsed(path, out, nullptr, error, warnings);
}

bool ConfigStore::InstallTypes(StudioDocument& doc,
                               std::vector<DevicePreset>& types,
                               QString* error)
{
    QDir d(dir);
    if(!QFileInfo::exists(PresetDir()) && !d.mkpath("presets/devices"))
    {
        if(error)
        {
            *error = QStringLiteral("cannot create %1").arg(PresetDir());
        }
        return false;
    }
    for(DevicePreset& p : types)
    {
        QString target = PresetDir() + "/"
                         + QString::fromStdString(p.id) + ".device.json";
        if(QFileInfo::exists(target))
        {
            /* Same content — reuse; different content — never
               overwrite a local type silently, write a variant id
               and remap the references. */
            DevicePreset existing;
            if(DevicePresetFromJsonFile(target.toStdString(), existing, nullptr)
               && ToJson(existing) == ToJson(p))
            {
                continue;
            }
            const std::string base = p.id;
            for(int n = 2;; n++)
            {
                const std::string alt = base + "-" + std::to_string(n);
                const QString alt_path = PresetDir() + "/"
                    + QString::fromStdString(alt) + ".device.json";
                if(!QFileInfo::exists(alt_path))
                {
                    p.id = alt;
                    for(auto& kv : doc.devices)
                    {
                        if(kv.second.type == base)
                        {
                            kv.second.type = alt;
                        }
                    }
                    target = alt_path;
                    break;
                }
                /* An existing -n file with identical content is a
                   reuse hit too. */
                DevicePreset other;
                if(DevicePresetFromJsonFile(alt_path.toStdString(), other, nullptr)
                   && other.id == alt)
                {
                    DevicePreset candidate = p;
                    candidate.id = alt;
                    if(ToJson(candidate) == ToJson(other))
                    {
                        p.id = alt;
                        for(auto& kv : doc.devices)
                        {
                            if(kv.second.type == base)
                            {
                                kv.second.type = alt;
                            }
                        }
                        target.clear();
                        break;
                    }
                }
            }
            if(target.isEmpty())
            {
                continue;
            }
        }
        const QByteArray bytes =
            QByteArray::fromStdString(ToJson(p).dump(2)) + "\n";
        if(!WriteAtomic(target, bytes, error))
        {
            return false;
        }
        /* Validate the file that actually landed before the
           workspace is allowed to reference it. */
        DevicePreset check;
        std::vector<std::string> verrs;
        if(!DevicePresetFromJsonFile(target.toStdString(), check, &verrs)
           || check.id != p.id)
        {
            QFile::remove(target);
            if(error)
            {
                *error = QStringLiteral("%1: written type failed"
                                        " validation: %2")
                    .arg(target, QString::fromStdString(
                             verrs.empty() ? "id mismatch" : verrs.front()));
            }
            return false;
        }
    }
    return true;
}

bool ConfigStore::Load(StudioDocument* out, QString* error, QString* warnings)
{
    std::vector<DevicePreset> types;
    if(!LoadParsed(DocumentPath(), out, &types, error, warnings))
    {
        return false;
    }
    if(!types.empty())
    {
        /* v1/v2 workspace — activate the migration transactionally:
           type files first (validated on disk), the original backed
           up, then the compact document replaces it. Any failure
           leaves studio.json untouched. */
        if(!InstallTypes(*out, types, error))
        {
            return false;
        }
        QString berr;
        if(!WriteAtomic(dir + "/studio.v2.backup.json",
                        ReadFile(DocumentPath()), &berr))
        {
            if(error)
            {
                *error = QStringLiteral("backup failed: %1").arg(berr);
            }
            return false;
        }
        if(!WriteAtomic(DocumentPath(), Serialize(*out), error))
        {
            return false;
        }
    }
    last_written = ReadFile(DocumentPath());
    ext_reported = false;
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
    ext_reported = false;
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
    /* Watch the workspace dir itself: the per-file watch vanishes
       with the file, so without this a deleted studio.json that is
       later recreated would go unwatched — and the next Save would
       silently overwrite an unwarned external change. */
    if(QFileInfo(dir).isDir() && !watcher->directories().contains(dir))
    {
        watcher->addPath(dir);
    }
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

void ConfigStore::OnDirectoryChanged(const QString& path)
{
    if(QDir(path) != QDir(dir))
    {
        return;
    }
    /* Anything in the workspace dir changed: our own QSaveFile
       churn, autosave/backup writes, external edits — and, the case
       this exists for, studio.json reappearing after a delete. The
       debounced content compare sorts signal from noise. */
    WatchDocument();
    change_timer->start();
}

void ConfigStore::EvaluateExternalChange()
{
    /* Re-arm here too: a temp+rename save can fire the watcher while
       the path briefly doesn't exist, so the file watch is only
       re-addable once the rename has landed. */
    WatchDocument();
    const QByteArray cur = ReadFile(DocumentPath());
    if(!cur.isEmpty() && cur == last_written)
    {
        ext_reported = false;   /* disk back in sync with ours —
                                   the next divergence is a new event */
        return;
    }
    if(ext_reported && cur == ext_content)
    {
        return;   /* this exact external state already reported —
                     dir-watch noise must not re-prompt the UI */
    }
    ext_reported = true;
    ext_content  = cur;
    emit externalChange();
}

} /* namespace studio */
