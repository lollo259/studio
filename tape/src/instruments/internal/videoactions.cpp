/*
 * Tape (Laurence Who Studio) — video
 * SPDX-License-Identifier: GPL-3.0-only
 */
#include "videoactions.h"

#include <QFileInfo>
#include <QSettings>
#include <QStandardPaths>
#include <QStringList>

#include "framework/global/types/translatablestring.h"
#include "framework/actions/actiontypes.h"
#include "context/uicontext.h"
#include "context/shortcutcontext.h"

#include "videostore.h"

using namespace au::instruments;
using namespace muse;
using namespace muse::ui;
using namespace muse::actions;

static const ActionCode IMPORT_CODE("video-import");
static const ActionCode TOGGLE_CODE("video-window");
static const char* VIDEO_URI = "audacity://video";
static const char* KEY_DIR = "video/lastFolder";

static const UiActionList s_actions = {
    UiAction(IMPORT_CODE,
             au::context::UiCtxAny,
             au::context::CTX_ANY,
             TranslatableString("action", "Import &video…"),
             TranslatableString("action_description", "Import a film to score to: it plays in the Video window, locked to the playhead")),
    UiAction(TOGGLE_CODE,
             au::context::UiCtxAny,
             au::context::CTX_ANY,
             TranslatableString("action", "Show/hide &video window"),
             TranslatableString("action_description", "Show or hide the picture of the project")),
};

void VideoActions::init()
{
    dispatcher()->reg(this, IMPORT_CODE, this, &VideoActions::importVideo);
    dispatcher()->reg(this, TOGGLE_CODE, this, &VideoActions::toggleWindow);

    globalContext()->currentProjectChanged().onNotify(this, [this]() { onProjectChanged(); });
}

QString VideoActions::projectPath() const
{
    auto project = globalContext()->currentProject();
    return project ? project->path().toQString() : QString();
}

void VideoActions::onProjectChanged()
{
    VideoStore::instance()->clearUnsaved();

    auto project = globalContext()->currentProject();
    if (!project) {
        if (windowOpened()) {
            interactive()->close(Uri(VIDEO_URI));
        }
        return;
    }

    // a project saved for the first time takes the video it had in memory with it
    std::weak_ptr<au::project::IAudacityProject> weak = project;
    project->pathChanged().onNotify(this, [weak]() {
        if (auto p = weak.lock()) {
            VideoStore::instance()->adopt(p->path().toQString());
        }
    }, muse::async::Asyncable::Mode::SetReplace);

    // reopening a project with a picture: bring the picture back
    if (VideoStore::instance()->get(projectPath()).hasVideo()) {
        openWindow();
    }
}

bool VideoActions::windowOpened() const
{
    const RetVal<bool> r = interactive()->isOpened(Uri(VIDEO_URI));
    return r.ret && r.val;
}

void VideoActions::openWindow()
{
    if (!windowOpened()) {
        interactive()->open(UriQuery(VIDEO_URI));
    }
}

void VideoActions::toggleWindow()
{
    if (windowOpened()) {
        interactive()->close(Uri(VIDEO_URI));
    } else {
        openWindow();
    }
}

void VideoActions::importVideo()
{
    if (!globalContext()->currentProject()) {
        interactive()->info("Video", std::string("Open or create a project first, then import its video."));
        return;
    }

    QSettings settings;
    QString dir = settings.value(KEY_DIR).toString();
    if (dir.isEmpty() || !QFileInfo(dir).isDir()) {
        dir = QStandardPaths::writableLocation(QStandardPaths::MoviesLocation);
    }
    const io::path_t picked = interactive()->selectOpeningFileSync(
        "Import video",
        io::path_t(dir),
        { "Video (*.mp4 *.mov *.mkv *.avi *.m4v)", "All files (*)" });
    if (picked.empty()) {
        return;
    }
    const QString file = QFileInfo(picked.toQString()).absoluteFilePath();
    settings.setValue(KEY_DIR, QFileInfo(file).absolutePath());

    // a new picture starts at the beginning of the timeline; the frame rate stays what the project used
    VideoSettings s = VideoStore::instance()->get(projectPath());
    if (s.file != file) {
        s.offset = 0.0;
    }
    s.file = file;
    VideoStore::instance()->set(projectPath(), s);

    openWindow();

    // the film's sound as a track, through Tape's own importer (FFmpeg)
    dispatcher()->dispatch("project-import", ActionData::make_arg1<QStringList>(QStringList { file }));
}

const UiActionList& VideoActions::actionsList() const
{
    return s_actions;
}

bool VideoActions::actionEnabled(const UiAction&) const
{
    return true;
}

async::Channel<ActionCodeList> VideoActions::actionEnabledChanged() const
{
    return m_enabledChanged;
}

bool VideoActions::actionChecked(const UiAction&) const
{
    return false;
}

async::Channel<ActionCodeList> VideoActions::actionCheckedChanged() const
{
    return m_checkedChanged;
}
