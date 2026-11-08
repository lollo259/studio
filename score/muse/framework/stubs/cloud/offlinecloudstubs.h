/*
 * SPDX-License-Identifier: GPL-3.0-only
 * Muse (Laurence Who Studio) - offline stand-ins for the MuseScore.com / Audio.com services.
 * Based on MuseScore Studio, Copyright (C) MuseScore Limited and others.
 */
#pragma once

#include "cloud/musescorecom/imusescorecomservice.h"
#include "cloud/audiocom/iaudiocomservice.h"

namespace muse::cloud {
class MuseScoreComConvertServiceStub : public IMuseScoreComConvertService
{
public:
    async::Promise<RetVal<ConvertConfig> > fetchConfig() override;
    ProgressPtr uploadToConvert(const ConvertUploadDataPtr& data) override;
    async::Promise<RetVal<ConvertQueueList> > fetchQueue() override;
    async::Promise<RetVal<ConvertResult> > submitReview(ConvertType type, int id, ReviewRating review,
                                                        const QString& comment = QString()) override;
    async::Promise<Ret> submitReviewComment(ConvertType type, int id, const QString& comment) override;
    async::Promise<Ret> deleteConversion(ConvertType type, int id) override;
};

class MuseScoreComServiceStub : public IMuseScoreComService
{
public:
    MuseScoreComServiceStub(IAuthorizationServicePtr auth);

    IAuthorizationServicePtr authorization() override;
    IMuseScoreComConvertServicePtr convert() override;
    QUrl scoreManagerUrl() const override;
    ProgressPtr uploadScore(DevicePtr scoreData, const QString& title, Visibility visibility = Visibility::Private,
                            const QUrl& sourceUrl = QUrl(), int revisionId = 0) override;
    ProgressPtr uploadAudio(DevicePtr audioData, const QString& audioFormat, const QUrl& sourceUrl) override;
    async::Promise<RetVal<ScoreInfo> > downloadScoreInfo(const QUrl& sourceUrl) override;
    async::Promise<RetVal<ScoreInfo> > downloadScoreInfo(int scoreId) override;
    async::Promise<ScoresList> downloadScoresList(int scoresPerBatch, int batchNumber) override;
    ProgressPtr downloadScore(int scoreId, DevicePtr scoreData, const QString& hash = QString(),
                              const QString& secret = QString()) override;
    async::Promise<Ret> deleteScore(int scoreId) override;

private:
    IAuthorizationServicePtr m_auth;
    IMuseScoreComConvertServicePtr m_convert;
};

class AudioComServiceStub : public IAudioComService
{
public:
    AudioComServiceStub(IAuthorizationServicePtr auth);

    IAuthorizationServicePtr authorization() override;
    QUrl projectManagerUrl() const override;
    ProgressPtr uploadAudio(DevicePtr audioData, const QString& audioFormat, const QString& title, const QUrl& existingUrl,
                            Visibility visibility = Visibility::Private, bool replaceExisting = false) override;
    CloudInfo cloudInfo() const override;

private:
    IAuthorizationServicePtr m_auth;
};
}
