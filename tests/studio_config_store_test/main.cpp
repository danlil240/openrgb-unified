/*---------------------------------------------------------*\
|| studio_config_store_test                                  |
||                                                           |
||   ConfigStore against a temp workspace dir: atomic save, |
||   last-valid backup, debounced autosave, recovery,       |
||   external-change detection, migration markers.          |
||                                                           |
||   SPDX-License-Identifier: GPL-2.0-or-later               |
\*---------------------------------------------------------*/

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>

#include "config/ConfigStore.h"
#include "config/ConfigMigration.h"
#include "scene/DefaultDesk.h"
#include "scene/SceneJson.h"
#include "scene/SceneResolver.h"
#include "presets/PresetRegistry.h"

#include <cstdio>
#include <functional>

static int failures = 0;
static int checks   = 0;

#define CHECK(cond, name)                                              \
    do {                                                               \
        ++checks;                                                      \
        if(!(cond)) { ++failures; std::printf("FAIL: %s\n", name); }   \
    } while(0)

static bool WaitFor(const std::function<bool()>& cond, int ms = 4000)
{
    for(int i = 0; i < ms / 10; i++)
    {
        QCoreApplication::processEvents();
        if(cond())
        {
            return true;
        }
        QThread::msleep(10);
    }
    return cond();
}

static QByteArray ReadAll(const QString& path)
{
    QFile f(path);
    if(!f.open(QIODevice::ReadOnly))
    {
        return QByteArray();
    }
    return f.readAll();
}

static studio::StudioDocument DocA()
{
    studio::StudioDocument w = studio::BuildDefaultWorkspace();
    w.meta.name = "Store fixture";
    w.brightness = 0.7f;
    w.inputs.audio = true;
    w.inputs.sens_pct = 175;
    w.object_colors["pc_case/front"] = 0xA0B0C0u;
    return w;
}

static void TestSaveLoadBackup()
{
    using namespace studio;
    QTemporaryDir tmp;
    CHECK(tmp.isValid(), "store: temp dir");
    ConfigStore store(tmp.path());

    CHECK(store.EnsureWorkspaceDir(), "store: mkdir");
    CHECK(QFileInfo::exists(store.WorkspaceDir() + "/schemas/studio.schema.json"),
          "store: bundled schema copied");

    StudioDocument a = DocA();
    QString err;
    CHECK(store.Save(a, &err), "store: save");
    CHECK(QFileInfo::exists(store.DocumentPath()), "store: studio.json exists");

    const QByteArray bytes_a = ReadAll(store.DocumentPath());
    /* pretty-printed two-space JSON */
    CHECK(bytes_a.contains("\n  \"schema_version\": 3"), "store: 2-space pretty print");
    {
        /* Compact v3: instances + shared sections only — no inline
           entities, generated emitters or embedded definitions.
           (colors.emitters is a legit v3 section — per-emitter
           paint, not generated layout.) */
        const nlohmann::json dj =
            nlohmann::json::parse(bytes_a.constData());
        bool compact = dj.contains("devices")
                       && !dj.contains("scene")
                       && !dj.contains("entities")
                       && !dj.contains("definitions");
        for(const auto& kv : dj["devices"].items())
        {
            compact = compact && !kv.value().contains("emitters")
                      && !kv.value().contains("entities")
                      && !kv.value().contains("geometry");
        }
        CHECK(compact, "store: compact instances only");
    }
    CHECK(bytes_a.contains("\"#"), "store: hex colors on disk");

    /* A second save snapshots the previous file as last-valid backup. */
    a.brightness = 0.4f;
    CHECK(store.Save(a, &err), "store: second save");
    CHECK(ReadAll(store.BackupPath()) == bytes_a, "store: backup holds last valid");
    CHECK(!store.dirty(), "store: clean after save");

    StudioDocument back;
    CHECK(store.Load(&back, &err), "store: load");
    CHECK(back.meta.name == "Store fixture"
          && back.devices.size() == a.devices.size()
          && back.inputs.audio && back.inputs.sens_pct == 175,
          "store: load round-trips workspace");

    /* Load failure leaves the candidate untouched. */
    QFile f(store.DocumentPath());
    f.open(QIODevice::WriteOnly | QIODevice::Text);
    f.write("{ not json !!");
    f.close();
    StudioDocument sentinel = DocA();
    sentinel.meta.name = "untouched";
    CHECK(!store.Load(&sentinel, &err) && !err.isEmpty()
          && sentinel.meta.name == "untouched",
          "store: corrupt file rejected, candidate untouched");
    /* ... and the last valid document survives in the backup. */
    StudioDocument rescued;
    CHECK(store.LoadFile(store.BackupPath(), &rescued, &err)
          && rescued.meta.name == "Store fixture",
          "store: backup is last valid");
}

static void TestSaveFailure()
{
    using namespace studio;
    QTemporaryDir tmp;
    ConfigStore store(tmp.path());

    StudioDocument a = DocA();
    QString err;
    /* Parent dir does not exist and cannot be created (illegal name). */
    CHECK(!store.SaveAs(a, tmp.path() + "/no/such/dir/deep/doc.json", &err)
          && !err.isEmpty(),
          "store: save failure reports error");
    CHECK(!QFileInfo::exists(store.DocumentPath()),
          "store: failed save leaves no file");
}

static void TestAutosaveRecovery()
{
    using namespace studio;
    QTemporaryDir tmp;
    ConfigStore store(tmp.path());
    store.EnsureWorkspaceDir();

    StudioDocument saved = DocA();
    QString err;
    CHECK(store.Save(saved, &err), "autosave: initial save");

    /* Provider serves the edited state. */
    StudioDocument edited = DocA();
    edited.meta.name = "Edited desk";
    edited.brightness = 0.2f;
    store.SetSnapshotProvider([&edited]() { return edited; });
    store.SetAutosaveDelayMs(30);

    store.MarkDirty();
    CHECK(store.dirty(), "autosave: dirty flag");
    CHECK(WaitFor([&]() { return QFileInfo::exists(store.AutosavePath()); }),
          "autosave: debounced write landed");
    CHECK(store.dirty(), "autosave: still dirty (unsaved)");

    /* Recovery only when the autosave differs from disk. */
    CHECK(store.HasRecovery(), "autosave: recovery detected");
    StudioDocument rec;
    CHECK(store.RecoverAutosave(&rec, &err) && rec.meta.name == "Edited desk",
          "autosave: recovery restores edited doc");
    store.DiscardRecovery();
    CHECK(!store.HasRecovery(), "autosave: recovery discarded");

    /* Successful save clears the dirty flag and drops the autosave. */
    store.MarkDirty();
    store.Save(edited, &err);
    CHECK(!store.dirty() && !QFileInfo::exists(store.AutosavePath()),
          "autosave: save clears recovery file");

    /* Crash case: autosave without a studio.json still recovers. */
    QFile::remove(store.DocumentPath());
    store.MarkDirty();
    CHECK(WaitFor([&]() { return QFileInfo::exists(store.AutosavePath()); }),
          "autosave: writes without main doc");
    CHECK(store.HasRecovery(), "autosave: recovery without main doc");
}

static void TestExternalChange()
{
    using namespace studio;
    QTemporaryDir tmp;
    ConfigStore store(tmp.path());
    store.EnsureWorkspaceDir();

    int ext_count = 0;
    QObject::connect(&store, &ConfigStore::externalChange,
                     &store, [&ext_count]() { ++ext_count; });

    StudioDocument a = DocA();
    QString err;
    CHECK(store.Save(a, &err), "ext: save");
    /* Let our own save's watcher events settle — they must be
       suppressed by the content compare. */
    WaitFor([]() { return false; }, 500);
    CHECK(ext_count == 0, "ext: own save not flagged");

    /* External edit: write a different valid document directly. */
    StudioDocument b = DocA();
    b.meta.name = "External edit";
    QFile f(store.DocumentPath());
    f.open(QIODevice::WriteOnly | QIODevice::Text);
    f.write(QByteArray::fromStdString(ToJson(b).dump(2)));
    f.close();
    CHECK(WaitFor([&ext_count]() { return ext_count == 1; }),
          "ext: external edit detected");

    /* Deleting the file is also an external change. */
    QFile::remove(store.DocumentPath());
    WaitFor([]() { return false; }, 400);   /* debounce window */
    /* re-add path after deletion happens inside the store; give the
       watcher a moment then check the signal fired again */
    CHECK(WaitFor([&ext_count]() { return ext_count >= 2; }),
          "ext: file deletion detected");

    /* Regression: the file watch is dropped on delete. Without the
       directory watch re-arming it, a recreated studio.json would go
       unwatched and the next Save would silently overwrite an
       unwarned external change. */
    const int after_delete = ext_count;
    {
        StudioDocument c = DocA();
        c.meta.name = "Recreated externally";
        QFile rf(store.DocumentPath());
        rf.open(QIODevice::WriteOnly | QIODevice::Text);
        rf.write(QByteArray::fromStdString(ToJson(c).dump(2)));
        rf.close();
    }
    CHECK(WaitFor([&]() { return ext_count > after_delete; }),
          "ext: file recreation detected");

    /* ... and the re-armed file watch keeps working for later
       external edits, not just the recreation event itself. */
    const int after_recreate = ext_count;
    {
        StudioDocument d = DocA();
        d.meta.name = "Edited after recreate";
        QFile ef(store.DocumentPath());
        ef.open(QIODevice::WriteOnly | QIODevice::Text);
        ef.write(QByteArray::fromStdString(ToJson(d).dump(2)));
        ef.close();
    }
    CHECK(WaitFor([&]() { return ext_count > after_recreate; }),
          "ext: edits after recreate still detected");
}

static void TestV2FileMigration()
{
    using namespace studio;
    QTemporaryDir tmp;
    ConfigStore store(tmp.path());
    store.EnsureWorkspaceDir();

    /* An expanded v2 workspace — what older builds saved. */
    const SceneDocument scene = BuildDefaultDesk();
    const nlohmann::json v2 = {
        {"schema_version", 2},
        {"name", "Old expanded desk"},
        {"scene", ToJson(scene)},
        {"inputs", {{"audio", true}, {"sens_pct", 140}}},
    };
    {
        QFile f(store.DocumentPath());
        f.open(QIODevice::WriteOnly | QIODevice::Text);
        f.write(QByteArray::fromStdString(v2.dump(2)));
        f.close();
    }

    StudioDocument w;
    QString err, warns;
    CHECK(store.Load(&w, &err, &warns), "v2: load migrates in place");
    if(!err.isEmpty())
    {
        std::printf("  (load error: %s)\n", err.toUtf8().constData());
    }
    CHECK(QFileInfo::exists(store.WorkspaceDir() + "/studio.v2.backup.json"),
          "v2: original backed up");
    CHECK(!QDir(store.PresetDir()).entryList(QStringList("*.device.json"),
                                            QDir::Files).isEmpty(),
          "v2: extracted types installed");
    CHECK(w.devices.size() == scene.objects.size(),
          "v2: every object became an instance");
    CHECK(w.meta.name == "Old expanded desk"
          && w.inputs.audio && w.inputs.sens_pct == 140,
          "v2: workspace sections carried over");
    CHECK(!w.meta.live_on_startup,
          "v2: migration never arms live output");

    /* The on-disk file is compact v3 — no expanded content
       (colors.emitters is a legit v3 section; what must be gone
       is the v2 "scene" blob and inline definitions). */
    const QByteArray bytes = ReadAll(store.DocumentPath());
    const nlohmann::json dj = nlohmann::json::parse(bytes.constData());
    CHECK(dj.value("schema_version", 0) == 3
          && !dj.contains("scene") && !dj.contains("definitions")
          && dj.contains("devices"),
          "v2: studio.json rewritten compact");

    /* The activation contract: a registry loaded AFTER Load (which
       is when the extracted type files exist) resolves the migrated
       workspace — the order SceneBridge::LoadWorkspace must use. */
    PresetRegistry reg;
    std::vector<std::string> lerrs;
    reg.LoadDirectory(store.PresetDir().toStdString(), &lerrs);
    SceneDocument resolved;
    std::vector<std::string> rerrs;
    CHECK(ResolveScene(w, reg, resolved, &rerrs),
          "v2: migrated workspace resolves against fresh registry");
    if(!rerrs.empty())
    {
        std::printf("  (resolve: %s)\n", rerrs.front().c_str());
    }
}

static void TestMigrationMarkers()
{
    using namespace studio;
    QTemporaryDir tmp;
    ConfigStore store(tmp.path());
    store.EnsureWorkspaceDir();

    CHECK(!store.MigrationDone(), "migration: not done initially");
    QString err;
    nlohmann::json legacy = {{"scene", {{"version", 2}, {"objects", nlohmann::json::array()}}},
                             {"inputs", {{"audio", true}}}};
    CHECK(store.BackupLegacySettings(legacy, &err),
          "migration: original settings backed up");
    CHECK(QFileInfo::exists(store.LegacyBackupPath()), "migration: backup file");
    store.MarkMigrationDone();
    CHECK(store.MigrationDone(), "migration: marker written");
}

static void TestMigrationRetry()
{
    using namespace studio;
    QTemporaryDir tmp;

    const nlohmann::json legacy = {
        {"scene", {{"version", 2},
                   {"name", "Old desk"},
                   {"objects", nlohmann::json::array()}}},
        {"inputs", {{"audio", true}, {"sens_pct", 150}}},
    };

    /* Transient failure: a file squatting on the workspace dir name
       makes every write fail (disk full / sync lock analogue). The
       marker must NOT be written — the next launch retries. */
    const QString blocked_dir = tmp.path() + "/ws";
    {
        QFile block(blocked_dir);
        block.open(QIODevice::WriteOnly);
        block.write("x");
        block.close();
    }
    {
        ConfigStore bad(blocked_dir);
        QString detail;
        CHECK(bad.RunLegacyMigration(legacy, &detail)
                  == ConfigStore::MigrationResult::Failed,
              "retry: failed save reports Failed");
        CHECK(!bad.MigrationDone(),
              "retry: no marker after failed save");
        CHECK(!bad.DocumentExists(),
              "retry: no doc after failed save");
    }

    /* "Next launch": the blockage clears, migration lands, and the
       original blob was backed up before anything else. */
    QFile::remove(blocked_dir);
    {
        ConfigStore good(blocked_dir);
        QString detail;
        CHECK(good.RunLegacyMigration(legacy, &detail)
                  == ConfigStore::MigrationResult::Migrated,
              "retry: retry migrates");
        CHECK(good.MigrationDone(), "retry: marker after success");
        CHECK(good.DocumentExists(), "retry: studio.json written");
        CHECK(QFileInfo::exists(good.LegacyBackupPath()),
              "retry: legacy backup written");

        StudioDocument w;
        QString err;
        CHECK(good.Load(&w, &err) && w.meta.name == "Old desk"
              && w.inputs.audio && w.inputs.sens_pct == 150,
              "retry: migrated doc carries scene+inputs");
        CHECK(!w.meta.live_on_startup,
              "retry: migration never enables live output");

        /* Once the marker exists, deleting studio.json does not
           resurrect the old blob — an intentional empty scene stays. */
        QFile::remove(good.DocumentPath());
        CHECK(good.RunLegacyMigration(legacy, &detail)
                  == ConfigStore::MigrationResult::NotNeeded,
              "retry: marker prevents re-run");
    }

    /* A permanently-invalid blob still marks done — but only after
       its verbatim backup landed. */
    {
        ConfigStore inv(tmp.path() + "/ws_invalid");
        const nlohmann::json bad_legacy = {
            {"scene", {{"version", 99}, {"objects", nlohmann::json::array()}}},
        };
        QString detail;
        CHECK(inv.RunLegacyMigration(bad_legacy, &detail)
                  == ConfigStore::MigrationResult::Invalid,
              "retry: invalid blob reports Invalid");
        CHECK(inv.MigrationDone(),
              "retry: invalid blob marks done (permanent)");
        CHECK(QFileInfo::exists(inv.LegacyBackupPath()),
              "retry: invalid blob still backed up");
    }

    /* Nothing to migrate: marker written so an intentional empty
       scene never restarts migration. */
    {
        ConfigStore empty(tmp.path() + "/ws_empty");
        const nlohmann::json nada = nlohmann::json::object();
        QString detail;
        CHECK(empty.RunLegacyMigration(nada, &detail)
                  == ConfigStore::MigrationResult::NotNeeded,
              "retry: empty blob needs nothing");
        CHECK(empty.MigrationDone(),
              "retry: empty blob still marks done");
    }
}

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);

    TestSaveLoadBackup();
    TestSaveFailure();
    TestAutosaveRecovery();
    TestExternalChange();
    TestV2FileMigration();
    TestMigrationMarkers();
    TestMigrationRetry();

    std::printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
