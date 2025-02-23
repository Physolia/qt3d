// Copyright (C) 2017 Klaralvdalens Datakonsult AB (KDAB).
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR LGPL-3.0-only OR GPL-2.0-only OR GPL-3.0-only

#include "handler_p.h"
#include <Qt3DAnimation/private/managers_p.h>
#include <Qt3DAnimation/private/loadanimationclipjob_p.h>
#include <Qt3DAnimation/private/findrunningclipanimatorsjob_p.h>
#include <Qt3DAnimation/private/evaluateclipanimatorjob_p.h>
#include <Qt3DAnimation/private/buildblendtreesjob_p.h>
#include <Qt3DAnimation/private/evaluateblendclipanimatorjob_p.h>
#include <Qt3DAnimation/private/animationlogging_p.h>
#include <Qt3DAnimation/private/buildblendtreesjob_p.h>
#include <Qt3DAnimation/private/evaluateblendclipanimatorjob_p.h>
#include <Qt3DCore/private/qaspectjob_p.h>
#include <Qt3DCore/private/vector_helper_p.h>

QT_BEGIN_NAMESPACE

namespace Qt3DAnimation {
namespace Animation {

Handler::Handler()
    : m_animationClipLoaderManager(new AnimationClipLoaderManager)
    , m_clockManager(new ClockManager)
    , m_clipAnimatorManager(new ClipAnimatorManager)
    , m_blendedClipAnimatorManager(new BlendedClipAnimatorManager)
    , m_channelMappingManager(new ChannelMappingManager)
    , m_channelMapperManager(new ChannelMapperManager)
    , m_clipBlendNodeManager(new ClipBlendNodeManager)
    , m_skeletonManager(new SkeletonManager)
    , m_loadAnimationClipJob(new LoadAnimationClipJob)
    , m_findRunningClipAnimatorsJob(new FindRunningClipAnimatorsJob)
    , m_buildBlendTreesJob(new BuildBlendTreesJob)
    , m_simulationTime(0)
{
    m_loadAnimationClipJob->setHandler(this);
    m_findRunningClipAnimatorsJob->setHandler(this);
    m_buildBlendTreesJob->setHandler(this);
}

Handler::~Handler()
{
}

void Handler::setDirty(DirtyFlag flag, Qt3DCore::QNodeId nodeId)
{
    switch (flag) {
    case AnimationClipDirty: {
        QMutexLocker lock(&m_mutex);
        const auto handle = m_animationClipLoaderManager->lookupHandle(nodeId);
        if (!m_dirtyAnimationClips.contains(handle))
            m_dirtyAnimationClips.push_back(handle);
        break;
    }

    case ChannelMappingsDirty: {
        break;
    }

    case ClipAnimatorDirty: {
        QMutexLocker lock(&m_mutex);
        const auto handle = m_clipAnimatorManager->lookupHandle(nodeId);
        if (!m_dirtyClipAnimators.contains(handle))
            m_dirtyClipAnimators.push_back(handle);
        break;
    }

    case BlendedClipAnimatorDirty: {
        QMutexLocker lock(&m_mutex);
        const HBlendedClipAnimator handle = m_blendedClipAnimatorManager->lookupHandle(nodeId);
        if (!m_dirtyBlendedAnimators.contains(handle))
            m_dirtyBlendedAnimators.push_back(handle);
        break;
    }
    }
}

// Called by the Jobs
void Handler::setClipAnimatorRunning(const HClipAnimator &handle, bool running)
{
    QMutexLocker lock(&m_mutex);

    // Add clip to running set if not already present
    if (running && !m_runningClipAnimators.contains(handle)) {
        m_runningClipAnimators.push_back(handle);
        ClipAnimator *clipAnimator = m_clipAnimatorManager->data(handle);
        if (clipAnimator)
            clipAnimator->setStartTime(m_simulationTime);
    }

    // If being marked as not running, remove from set of running clips
    if (!running) {
        m_runningClipAnimators.removeAll(handle);
    }
}

// Called by the Jobs
void Handler::setBlendedClipAnimatorRunning(const HBlendedClipAnimator &handle, bool running)
{
    QMutexLocker lock(&m_mutex);

    // Add clip to running set if not already present
    if (running && !m_runningBlendedClipAnimators.contains(handle)) {
        m_runningBlendedClipAnimators.push_back(handle);
        BlendedClipAnimator *blendedClipAnimator = m_blendedClipAnimatorManager->data(handle);
        if (blendedClipAnimator)
            blendedClipAnimator->setStartTime(m_simulationTime);
    }

    // If being marked as not running, remove from set of running clips
    if (!running) {
        const auto it = std::find_if(m_runningBlendedClipAnimators.begin(),
                                     m_runningBlendedClipAnimators.end(),
                                     [handle](const HBlendedClipAnimator &h) { return h == handle; });
        if (it != m_runningBlendedClipAnimators.end())
            m_runningBlendedClipAnimators.erase(it);
    }
}

// The vectors may get outdated when the application removes/deletes an
// animator component in the meantime. Recognize this. This should be
// relatively infrequent so in most cases the vectors will not change at all.
void Handler::cleanupHandleList(QVector<HAnimationClip> *clips)
{
    for (auto it = clips->begin(); it != clips->end(); ) {
        if (!m_animationClipLoaderManager->data(*it))
            it = clips->erase(it);
        else
            ++it;
    }
}

void Handler::cleanupHandleList(QVector<HClipAnimator> *animators)
{
    for (auto it = animators->begin(); it != animators->end(); ) {
        if (!m_clipAnimatorManager->data(*it))
            it = animators->erase(it);
        else
            ++it;
    }
}

void Handler::cleanupHandleList(QVector<HBlendedClipAnimator> *animators)
{
    for (auto it = animators->begin(); it != animators->end(); ) {
        if (!m_blendedClipAnimatorManager->data(*it))
            it = animators->erase(it);
        else
            ++it;
    }
}

std::vector<Qt3DCore::QAspectJobPtr> Handler::jobsToExecute(qint64 time)
{
    // Store the simulation time so we can mark the start time of
    // animators which will allow us to calculate the local time of
    // animation clips.
    m_simulationTime = time;

    std::vector<Qt3DCore::QAspectJobPtr> jobs;

    QMutexLocker lock(&m_mutex);

    // If there are any dirty animation clips that need loading,
    // queue up a job for them
    const bool hasLoadAnimationClipJob = !m_dirtyAnimationClips.isEmpty();
    if (hasLoadAnimationClipJob) {
        qCDebug(HandlerLogic) << "Added LoadAnimationClipJob";
        cleanupHandleList(&m_dirtyAnimationClips);
        m_loadAnimationClipJob->addDirtyAnimationClips(m_dirtyAnimationClips);
        jobs.push_back(m_loadAnimationClipJob);
        m_dirtyAnimationClips.clear();
    }

    // If there are dirty clip animators, find the set that are able to
    // run. I.e. are marked as running and have animation clips and
    // channel mappings

    const bool hasFindRunningClipAnimatorsJob = !m_dirtyClipAnimators.isEmpty();
    if (hasFindRunningClipAnimatorsJob) {
        qCDebug(HandlerLogic) << "Added FindRunningClipAnimatorsJob";
        cleanupHandleList(&m_dirtyClipAnimators);
        m_findRunningClipAnimatorsJob->setDirtyClipAnimators(m_dirtyClipAnimators);
        // Only set the dependency once
        if (Q_UNLIKELY(m_findRunningClipAnimatorsJob->dependencies().empty()))
            m_findRunningClipAnimatorsJob->addDependency(m_loadAnimationClipJob);
        jobs.push_back(m_findRunningClipAnimatorsJob);
        if (hasLoadAnimationClipJob)
            m_dirtyClipAnimators.clear();
    }

    // Rebuild blending trees if a blend tree is dirty
    const bool hasBuildBlendTreesJob = !m_dirtyBlendedAnimators.isEmpty();
    if (hasBuildBlendTreesJob) {
        const QVector<HBlendedClipAnimator> dirtyBlendedAnimators = Qt3DCore::moveAndClear(m_dirtyBlendedAnimators);
        m_buildBlendTreesJob->setBlendedClipAnimators(dirtyBlendedAnimators);
        jobs.push_back(m_buildBlendTreesJob);
    }

    // TODO: Parallelise the animator evaluation and property building at a finer level

    // If there are any running ClipAnimators, evaluate them for the current
    // time and send property changes
    cleanupHandleList(&m_runningClipAnimators);
    if (!m_runningClipAnimators.isEmpty()) {
        qCDebug(HandlerLogic) << "Added EvaluateClipAnimatorJobs";

        // Ensure we have a job per clip animator
        const qsizetype oldSize = m_evaluateClipAnimatorJobs.size();
        const qsizetype newSize = m_runningClipAnimators.size();
        if (oldSize < newSize) {
            m_evaluateClipAnimatorJobs.resize(newSize);
            for (qsizetype i = oldSize; i < newSize; ++i) {
                m_evaluateClipAnimatorJobs[i] = QSharedPointer<EvaluateClipAnimatorJob>::create();
                m_evaluateClipAnimatorJobs[i]->setHandler(this);
            }
        }

        // Set each job up with an animator to process and set dependencies
        for (qsizetype i = 0; i < newSize; ++i) {
            m_evaluateClipAnimatorJobs[i]->setClipAnimator(m_runningClipAnimators[i]);
            Qt3DCore::QAspectJobPrivate::get(m_evaluateClipAnimatorJobs[i].data())->clearDependencies();
            if (hasLoadAnimationClipJob)
                m_evaluateClipAnimatorJobs[i]->addDependency(m_loadAnimationClipJob);
            if (hasFindRunningClipAnimatorsJob)
                m_evaluateClipAnimatorJobs[i]->addDependency(m_findRunningClipAnimatorsJob);
            jobs.push_back(m_evaluateClipAnimatorJobs[i]);
        }
    }

    // BlendClipAnimator execution
    cleanupHandleList(&m_runningBlendedClipAnimators);
    if (!m_runningBlendedClipAnimators.isEmpty()) {
        // Ensure we have a job per clip animator
        const qsizetype oldSize = m_evaluateBlendClipAnimatorJobs.size();
        const qsizetype newSize = m_runningBlendedClipAnimators.size();
        if (oldSize < newSize) {
            m_evaluateBlendClipAnimatorJobs.resize(newSize);
            for (qsizetype i = oldSize; i < newSize; ++i) {
                m_evaluateBlendClipAnimatorJobs[i] = QSharedPointer<EvaluateBlendClipAnimatorJob>::create();
                m_evaluateBlendClipAnimatorJobs[i]->setHandler(this);
            }
        }

        // Set each job up with an animator to process and set dependencies
        for (qsizetype i = 0; i < newSize; ++i) {
            m_evaluateBlendClipAnimatorJobs[i]->setBlendClipAnimator(m_runningBlendedClipAnimators[i]);
            Qt3DCore::QAspectJobPrivate::get(m_evaluateBlendClipAnimatorJobs[i].data())->clearDependencies();
            if (hasLoadAnimationClipJob)
                m_evaluateBlendClipAnimatorJobs[i]->addDependency(m_loadAnimationClipJob);
            if (hasBuildBlendTreesJob)
                m_evaluateBlendClipAnimatorJobs[i]->addDependency(m_buildBlendTreesJob);
            jobs.push_back(m_evaluateBlendClipAnimatorJobs[i]);
        }
    }

    return jobs;
}

} // namespace Animation
} // namespace Qt3DAnimation

QT_END_NAMESPACE
