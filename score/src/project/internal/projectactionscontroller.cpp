/*
 * SPDX-License-Identifier: GPL-3.0-only
 * MuseScore-Studio-CLA-applies
 *
 * MuseScore Studio
 * Music Composition & Notation
 *
 * Copyright (C) 2021 MuseScore Limited and others
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#include "projectactionscontroller.h"

#include <QBuffer>
#include <QFileInfo>
#include <QTemporaryFile>
#include <QUrl>
#include <QUrlQuery>
#include <QTimer>
#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QCoreApplication>
#include <QDateTime>
#include <QProcess>
#include <QLocalSocket>
#include <QSettings>
#include <QStandardPaths>
#include <qobject.h>

#include "async/async.h"
#include "defer.h"
#include "types/ret.h"
#include "rcommand/commandtypes.h"

#include "notation/imasternotation.h"
#include "notation/iexcerptnotation.h"
#include "notation/inotationelements.h"
#include "notation/inotationinteraction.h"
#include "notation/inotationselection.h"

#include "inotationproject.h"
#include "engraving/dom/masterscore.h"
#include "io/fileinfo.h"
#include "types/projecturis.h"

#include "../projectcommands.h"
#include "appshell/appshellcommands.h"

#include "log.h"

using namespace mu;
using namespace mu::project;
using namespace mu::notation;
using namespace muse;

void ProjectActionsController::init()
{
    auto d = commandDispatcher();

    d->onRequest(this, PROJECT_NEW_COMMAND, [this]() { return newProject(); });
    d->onRequest(this, PROJECT_OPEN_COMMAND, [this](const rcommand::Params& params) { return openProject(params); });
    d->onRequest(this, PROJECT_CLOSE_COMMAND, [this]() { return closeProject(); });

    d->onRequest(this, PROJECT_SAVE_COMMAND, [this]() { return runAsync(saveProject(SaveMode::Save)); });
    d->onRequest(this, PROJECT_SAVE_AS_COMMAND, [this]() { return runAsync(saveProject(SaveMode::SaveAs)); });
    d->onRequest(this, PROJECT_SAVE_A_COPY_COMMAND, [this]() { return runAsync(saveProject(SaveMode::SaveCopy)); });
    d->onRequest(this, PROJECT_SAVE_SELECTION_COMMAND, [this]() {
        return runAsync(saveProject(SaveMode::SaveSelection, SaveLocationType::Local));
    });
    d->onRequest(this, PROJECT_SAVE_TO_CLOUD_COMMAND, [this]() { return runAsync(saveProject(SaveMode::Save, SaveLocationType::Cloud)); });
    d->onRequest(this, PROJECT_SAVE_AT_COMMAND, [this](const rcommand::Params& params) { return runAsync(saveProjectAt(params)); });

    d->onRequest(this, PROJECT_PUBLISH_COMMAND, [this]() { return runAsync(publish()); });
    d->onRequest(this, PROJECT_SHARE_AUDIO_COMMAND, [this]() { return runAsync(sharedAudio()); });

    d->onRequest(this, PROJECT_EXPORT_COMMAND, [this]() { return exportScore(); });
    d->onRequest(this, PROJECT_EXPORT_STAGE_COMMAND, [this]() { return exportForStage(); });
    d->onRequest(this, PROJECT_SEND_TO_TAPE_COMMAND, [this]() { return sendToTape(); });
    d->onRequest(this, PROJECT_CONVERT_TO_SCORE_COMMAND, [this]() { return convertFileToScore(); });

    d->onRequest(this, PROJECT_PRINT_COMMAND, [this]() { return printScore(); });
    d->onRequest(this, PROJECT_CLEAR_RECENT_COMMAND, [this]() { return clearRecentScores(); });
    d->onRequest(this, PROJECT_CONTINUE_LAST_SESSION_COMMAND, [this]() { return continueLastSession(); });
    d->onRequest(this, PROJECT_PROPERTIES_COMMAND, [this]() { return openProjectProperties(); });

    saveProjectScenario()->busyChanged().onNotify(this, [this]() {
        m_busyChanged.notify();
    });

    openProjectScenario()->busyChanged().onNotify(this, [this]() {
        m_busyChanged.notify();
    });

    closeProjectScenario()->busyChanged().onNotify(this, [this]() {
        m_busyChanged.notify();
    });

    // listen changes
    projectSession()->projectChanged().onNotify(this, [this]() {
        auto project = projectSession()->project();
        if (project) {
            project->needSaveChanged().onNotify(this, [this]() {
                m_needSaveChanged.notify();
            });
        }

        auto notation = projectSession()->currentNotation();
        if (notation) {
            notation->interaction()->selectionChanged().onNotify(this, [this]() {
                m_hasSelectionChanged.notify();
            });
        }
    });
}

bool ProjectActionsController::hasProject() const
{
    return currentNotationProject() != nullptr;
}

muse::async::Notification ProjectActionsController::hasProjectChanged() const
{
    return projectSession()->projectChanged();
}

bool ProjectActionsController::needSave() const
{
    return currentNotationProject() ? currentNotationProject()->isNeedSave() : false;
}

muse::async::Notification ProjectActionsController::needSaveChanged() const
{
    return m_needSaveChanged;
}

bool ProjectActionsController::isBusy(BusyStatus status) const
{
    return m_busyStatuses.contains(status)
           || saveProjectScenario()->isBusy(status)
           || openProjectScenario()->isBusy(status)
           || closeProjectScenario()->isBusy(status);
}

void ProjectActionsController::setBusy(BusyStatus status, bool isBusy)
{
    bool wasBusy = m_busyStatuses.contains(status);
    if (wasBusy == isBusy) {
        return;
    }

    if (isBusy) {
        m_busyStatuses.insert(status);
    } else {
        m_busyStatuses.erase(status);
    }

    m_busyChanged.notify();
}

muse::async::Notification ProjectActionsController::busyChanged() const
{
    return m_busyChanged;
}

bool ProjectActionsController::hasSelection() const
{
    return currentNotationSelection() ? !currentNotationSelection()->isNone() : false;
}

muse::async::Notification ProjectActionsController::hasSelectionChanged() const
{
    return m_hasSelectionChanged;
}

INotationProjectPtr ProjectActionsController::currentNotationProject() const
{
    return projectSession()->project();
}

IMasterNotationPtr ProjectActionsController::currentMasterNotation() const
{
    return currentNotationProject() ? currentNotationProject()->masterNotation() : nullptr;
}

INotationPtr ProjectActionsController::currentNotation() const
{
    return currentMasterNotation() ? currentMasterNotation()->notation() : nullptr;
}

INotationInteractionPtr ProjectActionsController::currentInteraction() const
{
    return currentNotation() ? currentNotation()->interaction() : nullptr;
}

INotationSelectionPtr ProjectActionsController::currentNotationSelection() const
{
    return currentNotation() ? currentInteraction()->selection() : nullptr;
}

muse::Ret ProjectActionsController::openPageIfNeed(muse::Uri pageUri)
{
    if (!interactive()->isOpened(pageUri).val) {
        interactive()->open(pageUri);
    }
    return make_ret(Ret::Code::Ok);
}

muse::Ret ProjectActionsController::openProject(const muse::io::path_t& path, const QString& displayNameOverride)
{
    return runAsync(openProjectScenario()->openProject(path, displayNameOverride));
}

muse::Ret ProjectActionsController::openProject(const muse::rcommand::Params& params)
{
    return runAsync(openProjectScenario()->openProject(params));
}

bool ProjectActionsController::isProjectOpened(const muse::io::path_t& scorePath) const
{
    auto project = projectSession()->project();
    if (!project) {
        return false;
    }

    LOGD() << "project->path: " << project->path() << ", check path: " << scorePath;
    if (project->path() == scorePath) {
        return true;
    }

    return false;
}

bool ProjectActionsController::isAnyProjectOpened() const
{
    auto project = projectSession()->project();
    if (project) {
        return true;
    }
    return false;
}

muse::Ret ProjectActionsController::newProject()
{
    //! NOTE This method is synchronous,
    //! but inside `multiwindowsProvider` there can be an event loop
    //! to wait for the responses from other instances, accordingly,
    //! the events (like user click) can be executed and this method can be called several times,
    //! before the end of the current call.
    //! So we ignore all subsequent calls until the current one completes.
    if (isBusy(BusyStatus::Opening)) {
        return make_ret(Ret::Code::Busy);
    }
    setBusy(BusyStatus::Opening, true);

    DEFER {
        setBusy(BusyStatus::Opening, false);
    };

    if (projectSession()->project()) {
        if (multiwindowsProvider()->isHasWindowWithoutProject()) {
            multiwindowsProvider()->activateWindowWithoutProject({ QString::fromStdString(PROJECT_NEW_COMMAND.toString()) });
            return make_ok();
        }
        QStringList args;
        args << "--session-type" << "start-with-new";
        multiwindowsProvider()->openNewWindow(args);
        return make_ok();
    }

    auto promise = interactive()->open(NEW_SCORE_URI);
    promise.onResolve(this, [this](const Val&) {
        Ret ret = openProjectScenario()->finishOpening();

        if (!ret) {
            LOGE() << ret.toString();
        }
    });

    return make_ok();
}

muse::Ret ProjectActionsController::closeProject()
{
    bool anyInstanceWithoutProject = multiwindowsProvider()->isHasWindowWithoutProject();

    return runAsync(closeProjectScenario()->closeOpenedProject(true)
                    .then<Ret>(this, [this, anyInstanceWithoutProject](const Ret& ret, auto resolve) {
        if (ret && anyInstanceWithoutProject) {
            //! NOTE: we need to call `quit` in the next event loop due to controlling the lifecycle of this method
            async::Async::call(this, [this]() {
                commandDispatcher()->dispatch(appshell::APP_QUIT_COMMAND, { { "all_instances", muse::Val(false) } });
            });
            multiwindowsProvider()->activateWindowWithoutProject();
        }

        return resolve(ret);
    }));
}

//! Commands report that the flow has started; its outcome is shown to the user by the scenario itself
muse::Ret ProjectActionsController::runAsync(async::Promise<Ret> flow)
{
    flow.onResolve(this, [](const Ret& ret) {
        if (!ret) {
            LOGD() << ret.toString();
        }
    });

    return make_ok();
}

async::Promise<Ret> ProjectActionsController::saveProject(SaveMode saveMode, SaveLocationType saveLocationType, bool force)
{
    return saveProjectScenario()->saveProject(saveMode, saveLocationType, force);
}

async::Promise<Ret> ProjectActionsController::publish()
{
    return saveProjectScenario()->publish();
}

async::Promise<Ret> ProjectActionsController::sharedAudio()
{
    return saveProjectScenario()->shareAudio();
}

async::Promise<Ret> ProjectActionsController::saveProjectAt(const muse::rcommand::Params& params)
{
    return saveProjectScenario()->saveProjectAt(params);
}

muse::Ret ProjectActionsController::convertFileToScore()
{
    convertFileToScoreScenario()->convertFiles();
    return make_ok();
}

muse::Ret ProjectActionsController::clearRecentScores()
{
    recentFilesController()->clearRecentFiles();
    return make_ok();
}

muse::Ret ProjectActionsController::continueLastSession()
{
    const RecentFilesList& recentScorePaths = recentFilesController()->recentFilesList();

    if (recentScorePaths.empty()) {
        Ret ret = openPageIfNeed(HOME_PAGE_URI);
        if (!ret) {
            LOGE() << ret.toString();
        }
        return ret;
    }

    muse::io::path_t lastScorePath = recentScorePaths.front().path;
    return openProject(lastScorePath);
}

muse::Ret ProjectActionsController::exportScore()
{
    static const Uri EXPORT_URI("musescore://project/export");
    if (!interactive()->isOpened(EXPORT_URI).val) {
        interactive()->open(EXPORT_URI);
    }
    return make_ok();
}

// Muse: one folder with everything the 3D stage app needs to put the parts on the
// music stands and turn their pages in time with the audio.
//   stage.json            manifest (parts, page images, timing files, audio)
//   pages-*.png           every page of the full score and of each part
//   timing*.mpos          bar positions on the pages + the time (ms) each bar is played
//   audio.wav             the piece as Muse plays it
muse::Ret ProjectActionsController::exportForStage()
{
    INotationProjectPtr project = currentNotationProject();
    IMasterNotationPtr master = currentMasterNotation();
    if (!project || !master) {
        return make_ret(Ret::Code::InternalError);
    }

    io::path_t parent = interactive()->selectDirectory(muse::trc("project/export", "Choose where to save the stage export"),
                                                       io::dirpath(project->path()));
    if (parent.empty()) {
        return make_ret(Ret::Code::Cancel);
    }

    QString title = io::completeBasename(project->path()).toQString();
    if (title.isEmpty()) {
        title = "Score";
    }
    const QString folder = parent.toQString() + "/" + title + " - stage";
    QDir().mkpath(folder);

    INotationPtrList notations { master->notation() };
    for (const IExcerptNotationPtr& excerpt : master->potentialExcerpts()) {
        notations.push_back(excerpt->notation());
    }

    const io::path_t pagesBase = folder + "/pages.png";
    const io::path_t timingBase = folder + "/timing.mpos";
    const io::path_t audioPath = folder + "/audio.wav";

    if (!exportProjectScenario()->exportScores(notations, pagesBase, INotationWriter::UnitType::PER_PAGE)) {
        return make_ret(Ret::Code::Cancel);
    }
    exportProjectScenario()->exportScores(notations, timingBase, INotationWriter::UnitType::PER_PART);
    bool audioOk = exportProjectScenario()->exportScores({ master->notation() }, audioPath, INotationWriter::UnitType::PER_PART,
                                                         /*openDestinationFolderOnExport*/ true);

    // manifest: the same file names the export scenario just wrote
    QJsonArray parts;
    for (size_t i = 0; i < notations.size(); ++i) {
        const INotationPtr& n = notations[i];
        bool isMain = i == 0;
        QString suffix = isMain ? QString() : "-" + io::escapeFileName(n->name()).toQString();
        size_t pageCount = n->elements()->msScore()->pages().size();
        QJsonArray pages;
        for (size_t p = 0; p < pageCount; ++p) {
            pages.append(QString("pages%1-%2.png").arg(suffix).arg(p + 1));
        }
        QJsonObject part;
        part["name"] = isMain ? QString("Full score") : n->name();
        part["pages"] = pages;
        part["timing"] = QString("timing%1.mpos").arg(suffix);
        parts.append(part);
    }
    QJsonObject root;
    root["format"] = "muse-stage-1";
    root["title"] = title;
    root["audio"] = audioOk ? QString("audio.wav") : QString();
    root["parts"] = parts;

    QFile manifest(folder + "/stage.json");
    if (manifest.open(QIODevice::WriteOnly)) {
        manifest.write(QJsonDocument(root).toJson());
        manifest.close();
    }

    return make_ok();
}

// Studio: Score -> Tape.
// Every instrument is rendered on its own (the same playback as the score, other instruments
// silent) into Documents/Studio/From Score/<title> <time>/, then Tape opens with one track per
// instrument, all starting at bar 1. If Tape is already open the files arrive in that window.
QString ProjectActionsController::findTape()
{
    QSettings settings("LaurenceWho", "Studio");
    const QString saved = settings.value("tapePath").toString();
    if (!saved.isEmpty() && QFileInfo::exists(saved)) {
        return saved;
    }

    // Studio folder: Studio/Score/bin/<score>.exe and Studio/Tape/bin/Tape.exe
    const QDir bin(QCoreApplication::applicationDirPath());
    const QStringList candidates = {
        bin.filePath("../../Tape/bin/Tape.exe"),
        bin.filePath("../../Tape/bin/Audacity4.exe"),
        bin.filePath("../../Tape/bin/audacity4"),
        bin.filePath("../../Tape/bin/Tape"),
    };
    for (const QString& c : candidates) {
        if (QFileInfo(c).isFile()) {
            return QFileInfo(c).canonicalFilePath();
        }
    }

    io::path_t picked = interactive()->selectOpeningFileSync(muse::trc("project", "Where is Tape? Pick Tape.exe (or Audacity4.exe)"),
                                                             io::path_t(QStandardPaths::writableLocation(QStandardPaths::DownloadLocation)),
                                                             { "Tape (*.exe)", "All files (*)" });
    if (picked.empty()) {
        return QString();
    }
    settings.setValue("tapePath", picked.toQString());
    return picked.toQString();
}

muse::Ret ProjectActionsController::sendToTape()
{
    INotationProjectPtr project = currentNotationProject();
    IMasterNotationPtr master = currentMasterNotation();
    if (!project || !master) {
        return make_ret(Ret::Code::InternalError);
    }

    const QString tape = findTape();
    if (tape.isEmpty()) {
        return make_ret(Ret::Code::Cancel);
    }

    QString title = io::completeBasename(project->path()).toQString();
    if (title.isEmpty()) {
        title = master->notation()->name().isEmpty() ? QString("Score") : master->notation()->name();
    }
    title = io::escapeFileName(io::path_t(title)).toQString();

    const QString docs = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    const QString folder = docs + "/Studio/From Score/" + title + " " + QDateTime::currentDateTime().toString("yyyy-MM-dd HH.mm.ss");
    QDir().mkpath(folder);

    // one notation per instrument: the parts already in the score, plus a part for every
    // instrument that has none yet
    INotationPtrList notations;
    for (const IExcerptNotationPtr& excerpt : master->excerpts()) {
        notations.push_back(excerpt->notation());
    }
    for (const IExcerptNotationPtr& excerpt : master->potentialExcerpts()) {
        notations.push_back(excerpt->notation());
    }
    if (notations.empty()) {
        notations.push_back(master->notation());
    }

    const io::path_t base = folder + "/" + title + ".wav";
    if (!exportProjectScenario()->exportScores(notations, base, INotationWriter::UnitType::PER_PART)) {
        return make_ret(Ret::Code::Cancel);
    }

    // the whole score as MIDI too, so the notes travel with the sound
    exportProjectScenario()->exportScores({ master->notation() }, folder + "/" + title + ".mid", INotationWriter::UnitType::PER_PART);

    QStringList files;
    QJsonArray tracks;
    for (const QFileInfo& fi : QDir(folder).entryInfoList({ "*.wav" }, QDir::Files, QDir::Name)) {
        files << QDir::toNativeSeparators(fi.absoluteFilePath());
        tracks.append(fi.fileName());
    }
    if (files.isEmpty()) {
        interactive()->error(muse::trc("project", "Nothing to send to Tape"),
                             muse::trc("project", "The score made no sound. Check that the instruments aren't muted."));
        return make_ret(Ret::Code::InternalError);
    }

    // a small note for Tape (and for later: Tape -> Score, live sync)
    QJsonObject link;
    link["format"] = "studio-link-1";
    link["score"] = QDir::toNativeSeparators(project->path().toQString());
    link["title"] = title;
    link["tracks"] = tracks;
    link["midi"] = title + ".mid";
    QFile linkFile(folder + "/studio-link.json");
    if (linkFile.open(QIODevice::WriteOnly)) {
        linkFile.write(QJsonDocument(link).toJson());
        linkFile.close();
    }

    // inside the Studio window: turn to the Tape page
    if (qEnvironmentVariableIsSet("STUDIO_EMBEDDED")) {
        QLocalSocket s;
        s.connectToServer("laurencewho-studio-link");
        if (s.waitForConnected(200)) {
            s.write("{\"cmd\":\"show\",\"page\":\"tape\",\"from\":\"score-project\"}\n");
            s.flush();
            s.waitForBytesWritten(200);
            s.disconnectFromServer();
        }
    }

    if (!QProcess::startDetached(tape, files, QFileInfo(tape).absolutePath())) {
        QSettings("LaurenceWho", "Studio").remove("tapePath");
        interactive()->error(muse::trc("project", "Tape didn't start"),
                             muse::trc("project", "The files are ready in Documents\\Studio\\From Score. Try Send to Tape again and pick Tape."));
        return make_ret(Ret::Code::InternalError);
    }

    return make_ok();
}

muse::Ret ProjectActionsController::printScore()
{
    INotationPtr notation = projectSession()->currentNotation();
    if (!notation) {
        return make_ret(Ret::Code::InternalError);
    }

    printProvider()->printNotation(notation);
    return make_ok();
}

muse::Ret ProjectActionsController::openProjectProperties()
{
    interactive()->open(PROJECT_PROPERTIES_URI);
    return make_ok();
}
