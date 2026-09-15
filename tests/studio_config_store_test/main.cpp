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
    studio::StudioDocument w;
    w.meta.name = "Store fixture";
    w.scene     = studio::BuildDefaultDesk();
    w.scene.brightness = 0.7f;
    w.inputs.audio = true;
    w.inputs.sens_pct = 175;
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
    CHECK(bytes_a.contains("\n  \"schema_version\": 2"), "store: 2-space pretty print");
    CHECK(bytes_a.contains("\"#"), "store: hex colors on disk");

    /* A second save snapshots the previous file as last-valid backup. */
    a.scene.brightness = 0.4f;
    CHECK(store.Save(a, &err), "store: second save");
    CHECK(ReadAll(store.BackupPath()) == bytes_a, "store: backup holds last valid");
    CHECK(!store.dirty(), "store: clean after save");

    StudioDocument back;
    CHECK(store.Load(&back, &err), "store: load");
    CHECK(back.meta.name == "Store fixture"
          && back.scene.objects.size() == a.scene.objects.size()
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
    edited.scene.brightness = 0.2f;
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
    TestMigrationMarkers();
    TestMigrationRetry();

    std::printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
