/*
 * SPDX-License-Identifier: GPL-3.0-only
 * Muse (Laurence Who Studio) - offline stand-ins for the MuseScore.com / Audio.com services.
 * Every call answers "not supported" without touching the network.
 */
#include "offlinecloudstubs.h"

using namespace muse;
using namespace muse::cloud;

namespace {
Ret offline()
{
    return make_ret(Ret::Code::NotSupported);
}

ProgressPtr offlineProgress()
{
    auto progress = std::make_shared<Progress>();
    ProgressResult res;
    res.ret = offline();
    progress->finish(res);
    return progress;
}

template<typename T>
async::Promise<RetVal<T> > offlineRetVal()
{
    return async::make_promise<RetVal<T> >([](auto resolve) {
        RetVal<T> rv;
        rv.ret = offline();
        return resolve(rv);
    });
}

async::Promise<Ret> offlineRet()
{
    return async::make_promise<Ret>([](auto resolve) {
        return resolve(offline());
    });
}
}

// convert
async::Promise<RetVal<ConvertConfig> > MuseScoreComConvertServiceStub::fetchConfig() { return offlineRetVal<ConvertConfig>(); }
ProgressPtr MuseScoreComConvertServiceStub::uploadToConvert(const ConvertUploadDataPtr&) { return offlineProgress(); }
async::Promise<RetVal<ConvertQueueList> > MuseScoreComConvertServiceStub::fetchQueue() { return offlineRetVal<ConvertQueueList>(); }
async::Promise<RetVal<ConvertResult> > MuseScoreComConvertServiceStub::submitReview(ConvertType, int, ReviewRating, const QString&)
{
    return offlineRetVal<ConvertResult>();
}

async::Promise<Ret> MuseScoreComConvertServiceStub::submitReviewComment(ConvertType, int, const QString&) { return offlineRet(); }
async::Promise<Ret> MuseScoreComConvertServiceStub::deleteConversion(ConvertType, int) { return offlineRet(); }

// musescore.com
MuseScoreComServiceStub::MuseScoreComServiceStub(IAuthorizationServicePtr auth)
    : m_auth(std::move(auth)), m_convert(std::make_shared<MuseScoreComConvertServiceStub>()) {}

IAuthorizationServicePtr MuseScoreComServiceStub::authorization() { return m_auth; }
IMuseScoreComConvertServicePtr MuseScoreComServiceStub::convert() { return m_convert; }
QUrl MuseScoreComServiceStub::scoreManagerUrl() const { return QUrl(); }
ProgressPtr MuseScoreComServiceStub::uploadScore(DevicePtr, const QString&, Visibility, const QUrl&, int) { return offlineProgress(); }
ProgressPtr MuseScoreComServiceStub::uploadAudio(DevicePtr, const QString&, const QUrl&) { return offlineProgress(); }
async::Promise<RetVal<ScoreInfo> > MuseScoreComServiceStub::downloadScoreInfo(const QUrl&) { return offlineRetVal<ScoreInfo>(); }
async::Promise<RetVal<ScoreInfo> > MuseScoreComServiceStub::downloadScoreInfo(int) { return offlineRetVal<ScoreInfo>(); }
async::Promise<ScoresList> MuseScoreComServiceStub::downloadScoresList(int, int)
{
    return async::make_promise<ScoresList>([](auto resolve) {
        return resolve(ScoresList());
    });
}

ProgressPtr MuseScoreComServiceStub::downloadScore(int, DevicePtr, const QString&, const QString&) { return offlineProgress(); }
async::Promise<Ret> MuseScoreComServiceStub::deleteScore(int) { return offlineRet(); }

// audio.com
AudioComServiceStub::AudioComServiceStub(IAuthorizationServicePtr auth)
    : m_auth(std::move(auth)) {}

IAuthorizationServicePtr AudioComServiceStub::authorization() { return m_auth; }
QUrl AudioComServiceStub::projectManagerUrl() const { return QUrl(); }
ProgressPtr AudioComServiceStub::uploadAudio(DevicePtr, const QString&, const QString&, const QUrl&, Visibility, bool)
{
    return offlineProgress();
}

CloudInfo AudioComServiceStub::cloudInfo() const { return CloudInfo(); }
