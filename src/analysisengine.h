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
	explicit CAnalysisEngine(QObject* const pParent = nullptr);
	~CAnalysisEngine() override;

	SessionId CreateSession(QString const& rsRootPath);
	void CancelSession(SessionId const uSession);
	void CancelAll();

	void RequestRoot(SessionId const uSession);
	void RequestExpand(SessionId const uSession, std::size_t const uNode);
	void RequestFullClosure(SessionId const uSession);
	void RequestImports(SessionId const uSession, std::size_t const uModule);

	bool IsBusy(SessionId const uSession) const;
	SEngineStatus QueryStatus(SessionId const uSession) const;
	int MaxThreadCount() const;

Q_SIGNALS:

	void RootReady(SessionId const uSession, SRootResult const& rResult);
	void NodeExpanded(SessionId const uSession, SExpandOutcome const& rOutcome);
	void ModuleDiscovered(SessionId const uSession, std::size_t const uModule, SModuleInfo const& rInfo);
	void ModuleUpdated(SessionId const uSession, SModuleUpdate const& rUpdate);
	void ImportsResolved(SessionId const uSession, SImportResolution const& rResolution);
	void ClosureComplete(SessionId const uSession);
	void StatusChanged(SessionId const uSession, SEngineStatus const& rStatus);
	void AnalysisFailed(SessionId const uSession, QString const& rsMessage);

private Q_SLOTS:

	void deliverRoot(SessionId const uSession, SRootResult const rResult);
	void deliverExpansion(SessionId const uSession, SExpandOutcome const rOutcome);
	void deliverImports(SessionId const uSession, SImportResolution const rResolution);
	void deliverStatus(SessionId const uSession);

private:
	std::shared_ptr<SSession> findSession(SessionId const uSession) const;
	void startTask(std::shared_ptr<SSession> const& rpSession, int const eKind, std::size_t const uIndex);
	void pumpClosure(std::shared_ptr<SSession> const& rpSession);
	void emitStatusThrottled(SessionId const uSession);

private:
	friend class CAnalysisTask;
};
