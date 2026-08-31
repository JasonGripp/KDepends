// Copyright (c) 2026 Jason Gripp
// Licensed under the MIT License.

#include "analysisengine.h"

#include "dependencyresolver.h"
#include "importresolver.h"
#include "ldcache.h"
#include "moduledata.h"

#include <QElapsedTimer>
#include <QMetaObject>
#include <QMutexLocker>
#include <QPointer>
#include <QRunnable>
#include <QThread>

#include <atomic>
#include <cstddef>
#include <memory>
#include <unordered_set>
#include <utility>
#include <vector>

namespace {
// Status emissions are coalesced to at most one per this many milliseconds
// per session, plus one on each busy-state transition.
constexpr qint64 g_iStatusIntervalMs = 100;
}

// Held by std::shared_ptr so in-flight tasks keep it alive even if the tab is
// gone.
struct CAnalysisEngine::SSession
{
	SessionId uId = 0;
	std::unique_ptr<CDependencyResolver> pResolver;
	std::unique_ptr<CImportResolver> pImportResolver;
	std::atomic<bool> bCancelled{false};
	std::atomic<std::size_t> uOutstanding{0};
	bool bClosureRequested = false;
	std::vector<std::size_t> vPendingImportModules;
	QString sRootPath;

	// UI-thread-only bookkeeping.
	std::unordered_set<std::size_t> setQueuedNodes;
	QElapsedTimer statusTimer;
	bool bLastBusy = false;
};

class CAnalysisTask : public QRunnable
{
public:
	enum EKind
	{
		Root,
		Expand,
		Imports,
	};

private:
	QPointer<CAnalysisEngine> m_pEngine;
	std::weak_ptr<CAnalysisEngine::SSession> m_pSession;
	SessionId m_uSession = 0;
	int m_eKind = Root;
	std::size_t m_uIndex = 0;

	SRootResult m_rootResult;
	SExpandOutcome m_expandOutcome;
	SImportResolution m_importResolution;
	bool m_bHasResult = false;

public:
	CAnalysisTask(CAnalysisEngine* pEngine, std::weak_ptr<CAnalysisEngine::SSession> pSession, SessionId uSession, int eKind, std::size_t uIndex)
	: m_pEngine(pEngine)
	, m_pSession(std::move(pSession))
	, m_uSession(uSession)
	, m_eKind(eKind)
	, m_uIndex(uIndex)
	{
		setAutoDelete(true);
	}

	void run() override
	{
		std::shared_ptr<CAnalysisEngine::SSession> const pSession = m_pSession.lock();
		if (!pSession)
			return;

		// No core-analysis function throws, but an unexpected exception must
		// never cross a thread boundary.
		try
		{
			if (!pSession->bCancelled.load())
				runKind(pSession);
		}
		catch (...)
		{
			m_bHasResult = false;
		}

		// The counter must drop before the result is posted. The delivery slot
		// treats "no unexpanded nodes and nothing outstanding" as closure
		// completion, and this task's own work is finished by now.
		pSession->uOutstanding.fetch_sub(1);

		if (m_bHasResult)
			post();

		CAnalysisEngine* const pEngine = m_pEngine.data();
		if (pEngine == nullptr)
			return;

		SessionId const uSession = m_uSession;
		QMetaObject::invokeMethod(pEngine, [pEngine, uSession]()
			{ pEngine->deliverStatus(uSession); }, Qt::QueuedConnection);
	}

private:
	void runKind(std::shared_ptr<CAnalysisEngine::SSession> const& rpSession)
	{
		switch (m_eKind)
		{
		case Root:
			{
				rpSession->pResolver->AnalyzeRoot(rpSession->sRootPath.toStdString(), m_rootResult);
				break;
			}
		case Expand:
			{
				rpSession->pResolver->ExpandNode(m_uIndex, m_expandOutcome);
				break;
			}
		case Imports:
			{
				m_importResolution.uModule = m_uIndex;

				if (rpSession->pImportResolver != nullptr)
				{
					rpSession->pImportResolver->ResolveModule(m_uIndex, m_importResolution);

					if (m_importResolution.bSuccess)
					{
						rpSession->pResolver->SetModuleImports(m_uIndex, m_importResolution.vImports);
					}
				}
				else
				{
					m_importResolution.sError = "closure is not complete yet";
				}

				break;
			}
		default:
			break;
		}

		m_bHasResult = true;
	}

	void post()
	{
		switch (m_eKind)
		{
		case Root: post(m_rootResult); break;
		case Expand: post(m_expandOutcome); break;
		case Imports: post(m_importResolution); break;
		default: break;
		}
	}

	void post(SRootResult const& rResult)
	{
		CAnalysisEngine* const pEngine = m_pEngine.data();
		if (pEngine == nullptr)
			return;

		SessionId const uSession = m_uSession;
		QMetaObject::invokeMethod(pEngine, [pEngine, uSession, rResult]()
			{ pEngine->deliverRoot(uSession, rResult); }, Qt::QueuedConnection);
	}

	void post(SExpandOutcome const& rOutcome)
	{
		CAnalysisEngine* const pEngine = m_pEngine.data();
		if (pEngine == nullptr)
			return;

		SessionId const uSession = m_uSession;
		QMetaObject::invokeMethod(pEngine, [pEngine, uSession, rOutcome]()
			{ pEngine->deliverExpansion(uSession, rOutcome); }, Qt::QueuedConnection);
	}

	void post(SImportResolution const& rResolution)
	{
		CAnalysisEngine* const pEngine = m_pEngine.data();
		if (pEngine == nullptr)
			return;

		SessionId const uSession = m_uSession;
		QMetaObject::invokeMethod(pEngine, [pEngine, uSession, rResolution]()
			{ pEngine->deliverImports(uSession, rResolution); }, Qt::QueuedConnection);
	}
};

CAnalysisEngine::CAnalysisEngine(QObject* pParent)
: QObject(pParent)
{
	qRegisterMetaType<SModuleInfo>("SModuleInfo");
	qRegisterMetaType<SChildNodeInfo>("SChildNodeInfo");
	qRegisterMetaType<SRootResult>("SRootResult");
	qRegisterMetaType<SExpandOutcome>("SExpandOutcome");
	qRegisterMetaType<SImportResolution>("SImportResolution");
	qRegisterMetaType<SModuleUpdate>("SModuleUpdate");
	qRegisterMetaType<SEngineStatus>("SEngineStatus");
	qRegisterMetaType<std::vector<SImportSymbol>>("std::vector<SImportSymbol>");
	qRegisterMetaType<SessionId>("SessionId");
	qRegisterMetaType<std::size_t>("std::size_t");

	// Not the global pool, so Qt's own users of QThreadPool::globalInstance()
	// cannot starve analysis and vice versa.
	m_pool.setMaxThreadCount(qMax(2, QThread::idealThreadCount()));
	m_pool.setExpiryTimeout(-1);

	// Warm the ld.so cache before the first analysis request.
	m_pool.start(QRunnable::create([]()
		{ CLdCache::Instance(); }));
}

CAnalysisEngine::~CAnalysisEngine()
{
	CancelAll();
	m_pool.waitForDone();
}

SessionId CAnalysisEngine::CreateSession(QString const& rsRootPath)
{
	std::shared_ptr<SSession> pSession = std::make_shared<SSession>();
	pSession->pResolver = std::make_unique<CDependencyResolver>();
	pSession->sRootPath = rsRootPath;
	pSession->statusTimer.start();

	QMutexLocker const locker(&m_mutex);

	SessionId const uSession = m_uNextSessionId++;
	pSession->uId = uSession;
	m_mapSessions.emplace(uSession, std::move(pSession));

	return uSession;
}

void CAnalysisEngine::CancelSession(SessionId uSession)
{
	std::shared_ptr<SSession> pSession;

	{
		QMutexLocker const locker(&m_mutex);

		auto const itFound = m_mapSessions.find(uSession);
		if (itFound == m_mapSessions.end())
			return;

		pSession = itFound->second;
		m_mapSessions.erase(itFound);
	}

	pSession->bCancelled.store(true);
	if (pSession->pResolver)
		pSession->pResolver->Cancel();
}

void CAnalysisEngine::CancelAll()
{
	std::unordered_map<SessionId, std::shared_ptr<SSession>> mapSessions;

	{
		QMutexLocker const locker(&m_mutex);
		mapSessions.swap(m_mapSessions);
	}

	for (auto const& rPair : mapSessions)
	{
		rPair.second->bCancelled.store(true);
		if (rPair.second->pResolver)
			rPair.second->pResolver->Cancel();
	}
}

void CAnalysisEngine::RequestRoot(SessionId uSession)
{
	std::shared_ptr<SSession> const pSession = findSession(uSession);
	if (!pSession || pSession->bCancelled.load())
		return;

	startTask(pSession, CAnalysisTask::Root, 0);
	emitStatusThrottled(uSession);
}

void CAnalysisEngine::RequestExpand(SessionId uSession, std::size_t uNode)
{
	std::shared_ptr<SSession> const pSession = findSession(uSession);
	if (!pSession || pSession->bCancelled.load())
		return;

	if (pSession->setQueuedNodes.count(uNode) != 0)
		return;

	pSession->setQueuedNodes.insert(uNode);
	startTask(pSession, CAnalysisTask::Expand, uNode);
	emitStatusThrottled(uSession);
}

void CAnalysisEngine::RequestFullClosure(SessionId uSession)
{
	std::shared_ptr<SSession> const pSession = findSession(uSession);
	if (!pSession || pSession->bCancelled.load())
		return;

	pSession->bClosureRequested = true;
	pumpClosure(pSession);
}

void CAnalysisEngine::RequestImports(SessionId uSession, std::size_t uModule)
{
	std::shared_ptr<SSession> const pSession = findSession(uSession);
	if (!pSession || pSession->bCancelled.load())
		return;

	if (pSession->pImportResolver == nullptr)
	{
		// Replayed once the closure is complete, so a selection made during
		// analysis still produces correct results.
		pSession->vPendingImportModules.push_back(uModule);
		return;
	}

	startTask(pSession, CAnalysisTask::Imports, uModule);
	emitStatusThrottled(uSession);
}

bool CAnalysisEngine::IsBusy(SessionId uSession) const
{
	std::shared_ptr<SSession> const pSession = findSession(uSession);
	if (!pSession)
		return false;

	return pSession->uOutstanding.load() != 0;
}

SEngineStatus CAnalysisEngine::QueryStatus(SessionId uSession) const
{
	SEngineStatus status;

	std::shared_ptr<SSession> const pSession = findSession(uSession);
	if (!pSession)
		return status;

	status.uOutstanding = pSession->uOutstanding.load();
	status.bBusy = status.uOutstanding != 0;
	status.uModuleCount = pSession->pResolver->ModuleCount();
	status.uNodeCount = pSession->pResolver->NodeCount();
	status.bClosureComplete = pSession->pImportResolver != nullptr;

	return status;
}

int CAnalysisEngine::MaxThreadCount() const
{
	return m_pool.maxThreadCount();
}

void CAnalysisEngine::deliverRoot(SessionId uSession, SRootResult rResult)
{
	std::shared_ptr<SSession> const pSession = findSession(uSession);
	if (!pSession || pSession->bCancelled.load())
		return;

	pSession->setQueuedNodes.erase(g_uRootNodeIndex);

	if (!rResult.bSuccess)
	{
		Q_EMIT AnalysisFailed(uSession, QString::fromStdString(rResult.sError));
		return;
	}

	Q_EMIT RootReady(uSession, rResult);
	Q_EMIT ModuleDiscovered(uSession, g_uRootNodeIndex, rResult.info);

	for (std::size_t uIndex = 0; uIndex < rResult.vNewModules.size(); ++uIndex)
	{
		Q_EMIT ModuleDiscovered(uSession, rResult.vNewModules[uIndex], rResult.vNewModuleInfos[uIndex]);
	}

	RequestFullClosure(uSession);
}

void CAnalysisEngine::deliverExpansion(SessionId uSession, SExpandOutcome rOutcome)
{
	std::shared_ptr<SSession> const pSession = findSession(uSession);
	if (!pSession || pSession->bCancelled.load())
		return;

	pSession->setQueuedNodes.erase(rOutcome.uNode);

	if (!rOutcome.bAlreadyDone)
	{
		Q_EMIT NodeExpanded(uSession, rOutcome);

		if (rOutcome.uModule != g_uInvalidIndex)
		{
			SModuleUpdate update;
			update.uModule = rOutcome.uModule;
			update.info = rOutcome.moduleInfo;
			Q_EMIT ModuleUpdated(uSession, update);
		}

		for (std::size_t uIndex = 0; uIndex < rOutcome.vNewModules.size(); ++uIndex)
		{
			Q_EMIT ModuleDiscovered(uSession, rOutcome.vNewModules[uIndex], rOutcome.vNewModuleInfos[uIndex]);
		}
	}

	pumpClosure(pSession);
}

void CAnalysisEngine::deliverImports(SessionId uSession, SImportResolution rResolution)
{
	std::shared_ptr<SSession> const pSession = findSession(uSession);
	if (!pSession || pSession->bCancelled.load())
		return;

	Q_EMIT ImportsResolved(uSession, rResolution);
}

void CAnalysisEngine::deliverStatus(SessionId uSession)
{
	emitStatusThrottled(uSession);
}

std::shared_ptr<CAnalysisEngine::SSession> CAnalysisEngine::findSession(SessionId uSession) const
{
	QMutexLocker const locker(&m_mutex);

	auto const itFound = m_mapSessions.find(uSession);
	if (itFound == m_mapSessions.end())
		return {};

	return itFound->second;
}

void CAnalysisEngine::startTask(std::shared_ptr<SSession> const& rpSession, int eKind, std::size_t uIndex)
{
	rpSession->uOutstanding.fetch_add(1);

	m_pool.start(new CAnalysisTask(this, rpSession, rpSession->uId, eKind, uIndex));
}

void CAnalysisEngine::pumpClosure(std::shared_ptr<SSession> const& rpSession)
{
	if (!rpSession || rpSession->bCancelled.load())
		return;
	if (!rpSession->bClosureRequested)
		return;

	std::vector<std::size_t> vNodes;
	rpSession->pResolver->CollectUnexpandedNodes(vNodes, static_cast<std::size_t>(MaxThreadCount()));

	std::size_t uStarted = 0;
	for (std::size_t const uNode : vNodes)
	{
		if (rpSession->setQueuedNodes.count(uNode) != 0)
			continue;

		rpSession->setQueuedNodes.insert(uNode);
		startTask(rpSession, CAnalysisTask::Expand, uNode);
		++uStarted;
	}

	if (uStarted != 0)
	{
		emitStatusThrottled(rpSession->uId);
		return;
	}

	if (!vNodes.empty())
		return;
	if (rpSession->uOutstanding.load() != 0)
		return;
	if (rpSession->pImportResolver != nullptr)
		return;

	SModuleClosure closure;
	rpSession->pResolver->ClosureSnapshot(closure);
	rpSession->pImportResolver = std::make_unique<CImportResolver>(std::move(closure));

	Q_EMIT ClosureComplete(rpSession->uId);

	std::vector<std::size_t> const vPending = std::move(rpSession->vPendingImportModules);
	rpSession->vPendingImportModules.clear();

	for (std::size_t const uModule : vPending)
	{
		startTask(rpSession, CAnalysisTask::Imports, uModule);
	}

	emitStatusThrottled(rpSession->uId);
}

void CAnalysisEngine::emitStatusThrottled(SessionId uSession)
{
	std::shared_ptr<SSession> const pSession = findSession(uSession);
	if (!pSession || pSession->bCancelled.load())
		return;

	bool const bBusy = pSession->uOutstanding.load() != 0;
	bool const bTransition = bBusy != pSession->bLastBusy;

	if (!bTransition && pSession->statusTimer.isValid() && pSession->statusTimer.elapsed() < g_iStatusIntervalMs)
		return;

	pSession->bLastBusy = bBusy;
	pSession->statusTimer.restart();

	Q_EMIT StatusChanged(uSession, QueryStatus(uSession));
}
