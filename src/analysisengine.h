#pragma once

#include "dependencyresolver.h"
#include "importresolver.h"
#include "moduledata.h"

#include <QMetaType>
#include <QMutex>
#include <QObject>
#include <QString>
#include <QThreadPool>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <unordered_map>
#include <vector>

// Opaque per-tab handle; 0 is never a valid session.
using SessionId = std::uint64_t;

// Delivered when a previously discovered placeholder module gains its full
// metadata.
struct SModuleUpdate
{
	std::size_t uModule = g_uInvalidIndex;
	SModuleInfo info;
};

// Delivered whenever a session's progress changes.
struct SEngineStatus
{
	bool bBusy = false;
	std::size_t uModuleCount = 0;
	std::size_t uNodeCount = 0;
	std::size_t uOutstanding = 0;
	bool bClosureComplete = false;
};

Q_DECLARE_METATYPE(SModuleInfo)
Q_DECLARE_METATYPE(SChildNodeInfo)
Q_DECLARE_METATYPE(SRootResult)
Q_DECLARE_METATYPE(SExpandOutcome)
Q_DECLARE_METATYPE(SImportResolution)
Q_DECLARE_METATYPE(SModuleUpdate)
Q_DECLARE_METATYPE(SEngineStatus)
Q_DECLARE_METATYPE(std::vector<SImportSymbol>)

// The concurrency bridge, and the only place the core analysis layer meets Qt.
// The UI thread never parses a file, never touches the disk, and never reads a
// live closure: it only calls the request methods below and receives copies
// via queued signals.
//
// Qt does not constrain signal or slot names, so signals and public slots use
// the project's PascalCase convention; private slots use camelCase like other
// private member functions.
class CAnalysisEngine : public QObject
{
	Q_OBJECT

private:
	struct SSession;

private:
	QThreadPool m_pool;
	mutable QMutex m_mutex;
	std::unordered_map<SessionId, std::shared_ptr<SSession>> m_mapSessions;
	SessionId m_uNextSessionId = 1;

public:
	explicit CAnalysisEngine(QObject* pParent = nullptr);
	~CAnalysisEngine() override;

	SessionId CreateSession(QString const& rsRootPath);
	void CancelSession(SessionId uSession);
	void CancelAll();

	void RequestRoot(SessionId uSession);
	void RequestExpand(SessionId uSession, std::size_t uNode);
	void RequestFullClosure(SessionId uSession);
	void RequestImports(SessionId uSession, std::size_t uModule);

	bool IsBusy(SessionId uSession) const;
	SEngineStatus QueryStatus(SessionId uSession) const;
	int MaxThreadCount() const;

Q_SIGNALS:

	void RootReady(SessionId uSession, SRootResult const& rResult);
	void NodeExpanded(SessionId uSession, SExpandOutcome const& rOutcome);
	void ModuleDiscovered(SessionId uSession, std::size_t uModule, SModuleInfo const& rInfo);
	void ModuleUpdated(SessionId uSession, SModuleUpdate const& rUpdate);
	void ImportsResolved(SessionId uSession, SImportResolution const& rResolution);
	void ClosureComplete(SessionId uSession);
	void StatusChanged(SessionId uSession, SEngineStatus const& rStatus);
	void AnalysisFailed(SessionId uSession, QString const& rsMessage);

private Q_SLOTS:

	void deliverRoot(SessionId uSession, SRootResult rResult);
	void deliverExpansion(SessionId uSession, SExpandOutcome rOutcome);
	void deliverImports(SessionId uSession, SImportResolution rResolution);
	void deliverStatus(SessionId uSession);

private:
	std::shared_ptr<SSession> findSession(SessionId uSession) const;
	void startTask(std::shared_ptr<SSession> const& rpSession, int eKind, std::size_t uIndex);
	void pumpClosure(std::shared_ptr<SSession> const& rpSession);
	void emitStatusThrottled(SessionId uSession);

private:
	friend class CAnalysisTask;
};
