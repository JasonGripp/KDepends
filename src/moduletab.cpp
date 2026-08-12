#include "moduletab.h"

#include "analysisengine.h"
#include "demangler.h"
#include "dependencytreemodel.h"
#include "exportsmodel.h"
#include "filteredtable.h"
#include "icons.h"
#include "importsmodel.h"
#include "moduledata.h"
#include "modulesmodel.h"

#include <KIO/OpenFileManagerWindowJob>
#include <KLocalizedString>

#include <QAbstractItemView>
#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QColor>
#include <QDataStream>
#include <QDesktopServices>
#include <QFileInfo>
#include <QHeaderView>
#include <QItemSelectionModel>
#include <QKeySequence>
#include <QLabel>
#include <QLatin1Char>
#include <QList>
#include <QMenu>
#include <QPoint>
#include <QSortFilterProxyModel>
#include <QSplitter>
#include <QStringList>
#include <QTableView>
#include <QTreeView>
#include <QUrl>
#include <QUrlQuery>
#include <QVBoxLayout>

#include <cstddef>
#include <initializer_list>
#include <utility>

CModuleTab::CModuleTab(CAnalysisEngine& rEngine, QString const& rsFilePath, QWidget* pParent)
: QWidget(pParent)
, m_rEngine(rEngine)
, m_sFilePath(rsFilePath)
{
	buildUi();

	m_uSession = m_rEngine.CreateSession(m_sFilePath);

	connectEngine();
	connectViews();

	// Analysis starts as soon as the tab exists; the tab is interactive
	// immediately.
	m_rEngine.RequestRoot(m_uSession);
}

CModuleTab::~CModuleTab()
{
	Cancel();
}

void CModuleTab::buildUi()
{
	m_pTreeModel = new CDependencyTreeModel(this);
	m_pImportsModel = new CImportsModel(this);
	m_pExportsModel = new CExportsModel(this);
	m_pModulesModel = new CModulesModel(this);

	m_pTreeView = new QTreeView(this);
	m_pTreeView->setModel(m_pTreeModel);
	m_pTreeView->setHeaderHidden(false);
	m_pTreeView->setUniformRowHeights(true);
	m_pTreeView->setAlternatingRowColors(true);
	m_pTreeView->setSelectionBehavior(QAbstractItemView::SelectRows);
	m_pTreeView->setContextMenuPolicy(Qt::CustomContextMenu);
	m_pTreeView->setExpandsOnDoubleClick(true);
	m_pTreeView->header()->setStretchLastSection(true);

	m_pImportsTable = new CFilteredTable(this);
	m_pImportsTable->SetSourceModel(m_pImportsModel);
	m_pImportsTable->SetFilterPlaceholder(i18n("Filter imports"));

	m_pExportsTable = new CFilteredTable(this);
	m_pExportsTable->SetSourceModel(m_pExportsModel);
	m_pExportsTable->SetFilterPlaceholder(i18n("Filter exports"));

	m_pModulesTable = new CFilteredTable(this);
	m_pModulesTable->SetSourceModel(m_pModulesModel);
	m_pModulesTable->SetFilterPlaceholder(i18n("Filter modules"));

	m_pSymbolSplitter = new QSplitter(Qt::Vertical, this);
	m_pSymbolSplitter->setObjectName(QStringLiteral("symbolSplitter"));
	m_pSymbolSplitter->addWidget(m_pImportsTable);
	m_pSymbolSplitter->addWidget(m_pExportsTable);
	m_pSymbolSplitter->setStretchFactor(0, 1);
	m_pSymbolSplitter->setStretchFactor(1, 1);

	m_pTreeSplitter = new QSplitter(Qt::Horizontal, this);
	m_pTreeSplitter->setObjectName(QStringLiteral("treeSplitter"));
	m_pTreeSplitter->addWidget(m_pTreeView);
	m_pTreeSplitter->addWidget(m_pSymbolSplitter);
	m_pTreeSplitter->setStretchFactor(0, 1);
	m_pTreeSplitter->setStretchFactor(1, 2);

	m_pMainSplitter = new QSplitter(Qt::Vertical, this);
	m_pMainSplitter->setObjectName(QStringLiteral("mainSplitter"));
	m_pMainSplitter->addWidget(m_pTreeSplitter);
	m_pMainSplitter->addWidget(m_pModulesTable);
	m_pMainSplitter->setStretchFactor(0, 3);
	m_pMainSplitter->setStretchFactor(1, 1);

	m_pStatusLabel = new QLabel(this);
	m_pStatusLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);

	QVBoxLayout* const pLayout = new QVBoxLayout(this);
	pLayout->setContentsMargins(2, 2, 2, 2);
	pLayout->addWidget(m_pMainSplitter, 1);
	pLayout->addWidget(m_pStatusLabel);

	m_pImportsTable->View()->setColumnWidth(static_cast<int>(CImportsModel::EColumn::Status), 24);
	m_pExportsTable->View()->setColumnWidth(static_cast<int>(CExportsModel::EColumn::Status), 24);
	m_pModulesTable->View()->setColumnWidth(static_cast<int>(CModulesModel::EColumn::Status), 24);

	updateStatusLabel();
}

void CModuleTab::applyDefaultSplitterSizes()
{
	// Stretch factors alone leave the modules table at barely more than its
	// size hint, so give the outer splitter explicit sizes: a third of the
	// height to the modules table, the rest to the tree and symbol panels.
	// Proportions need the laid-out height, which is why this waits for the
	// first show rather than running in buildUi.
	int const iHeight = m_pMainSplitter->height();
	if (iHeight <= 0)
		return;

	int const iModulesHeight = iHeight / 3;

	m_pMainSplitter->setSizes({iHeight - iModulesHeight, iModulesHeight});
}

void CModuleTab::showEvent(QShowEvent* pEvent)
{
	QWidget::showEvent(pEvent);

	if (m_bDefaultSplitterSizesApplied || m_bSplitterStateRestored)
		return;

	m_bDefaultSplitterSizesApplied = true;
	applyDefaultSplitterSizes();
}

void CModuleTab::connectEngine()
{
	// The engine emits on the UI thread, so these are direct calls.
	connect(&m_rEngine, &CAnalysisEngine::RootReady, this, &CModuleTab::engineRootReady);
	connect(&m_rEngine, &CAnalysisEngine::NodeExpanded, this, &CModuleTab::engineNodeExpanded);
	connect(&m_rEngine, &CAnalysisEngine::ModuleDiscovered, this, &CModuleTab::engineModuleDiscovered);
	connect(&m_rEngine, &CAnalysisEngine::ModuleUpdated, this, &CModuleTab::engineModuleUpdated);
	connect(&m_rEngine, &CAnalysisEngine::ImportsResolved, this, &CModuleTab::engineImportsResolved);
	connect(&m_rEngine, &CAnalysisEngine::ClosureComplete, this, &CModuleTab::engineClosureComplete);
	connect(&m_rEngine, &CAnalysisEngine::StatusChanged, this, &CModuleTab::engineStatusChanged);
	connect(&m_rEngine, &CAnalysisEngine::AnalysisFailed, this, &CModuleTab::engineAnalysisFailed);
}

void CModuleTab::connectViews()
{
	connect(m_pTreeModel, &CDependencyTreeModel::ExpansionNeeded, this, &CModuleTab::treeExpansionNeeded);
	connect(m_pTreeView->selectionModel(), &QItemSelectionModel::currentChanged, this, &CModuleTab::treeSelectionChanged);
	connect(m_pTreeView, &QWidget::customContextMenuRequested, this, &CModuleTab::treeContextMenu);

	connect(m_pModulesTable, &CFilteredTable::SelectionChanged, this, &CModuleTab::modulesSelectionChanged);
	connect(m_pModulesTable, &CFilteredTable::ContextMenuRequested, this, &CModuleTab::modulesContextMenu);

	connect(m_pImportsTable, &CFilteredTable::ContextMenuRequested, this, &CModuleTab::symbolContextMenu);
	connect(m_pImportsTable, &CFilteredTable::Activated, this, &CModuleTab::importActivated);
	connect(m_pExportsTable, &CFilteredTable::ContextMenuRequested, this, &CModuleTab::symbolContextMenu);
}

QString CModuleTab::FilePath() const
{
	return m_sFilePath;
}

QString CModuleTab::DisplayName() const
{
	return QFileInfo(m_sFilePath).fileName();
}

QString CModuleTab::ToolTipText() const
{
	QStringList vLines;
	vLines.append(m_sFilePath);

	if (!m_sError.isEmpty())
	{
		vLines.append(m_sError);
		return vLines.join(QLatin1Char('\n'));
	}

	int const iModules = m_pModulesModel->ModuleCount();
	int const iMissing = m_pModulesModel->MissingCount();

	vLines.append(i18np("%1 module", "%1 modules", iModules));
	if (iMissing > 0)
		vLines.append(i18np("%1 problem module", "%1 problem modules", iMissing));

	return vLines.join(QLatin1Char('\n'));
}

SessionId CModuleTab::Session() const
{
	return m_uSession;
}

void CModuleTab::SetDemangleEnabled(bool bEnabled)
{
	m_bDemangle = bEnabled;

	m_pImportsModel->SetDemangleEnabled(bEnabled);
	m_pExportsModel->SetDemangleEnabled(bEnabled);
}

bool CModuleTab::IsBusy() const
{
	return m_bBusy;
}

void CModuleTab::Cancel()
{
	if (m_uSession == 0)
		return;

	m_rEngine.CancelSession(m_uSession);
	m_uSession = 0;
}

void CModuleTab::ShowFilter()
{
	focusedTable()->ShowFilter();
}

QByteArray CModuleTab::SaveSplitterState() const
{
	QByteArray state;
	QDataStream stream(&state, QIODevice::WriteOnly);

	stream << m_pMainSplitter->saveState();
	stream << m_pTreeSplitter->saveState();
	stream << m_pSymbolSplitter->saveState();

	return state;
}

void CModuleTab::RestoreSplitterState(QByteArray const& rState)
{
	if (rState.isEmpty())
		return;

	QByteArray stateCopy = rState;
	QDataStream stream(&stateCopy, QIODevice::ReadOnly);

	QByteArray mainState;
	QByteArray treeState;
	QByteArray symbolState;

	stream >> mainState;
	stream >> treeState;
	stream >> symbolState;

	if (stream.status() != QDataStream::Ok)
		return;

	m_pMainSplitter->restoreState(mainState);
	m_pTreeSplitter->restoreState(treeState);
	m_pSymbolSplitter->restoreState(symbolState);

	// Suppresses the first-show defaults: the saved layout wins.
	m_bSplitterStateRestored = true;
}

void CModuleTab::SelectModule(std::size_t uModule)
{
	if (uModule == g_uInvalidIndex)
		return;

	QModelIndex const treeIndex = m_pTreeModel->FirstIndexForModule(uModule);
	if (treeIndex.isValid())
	{
		m_pTreeView->setCurrentIndex(treeIndex);
		m_pTreeView->scrollTo(treeIndex);
	}

	int const iRow = m_pModulesModel->RowForModule(uModule);
	if (iRow >= 0)
	{
		QModelIndex const sourceIndex = m_pModulesModel->index(iRow, 0);
		QModelIndex const proxyIndex = m_pModulesTable->Proxy()->mapFromSource(sourceIndex);

		if (proxyIndex.isValid())
		{
			m_pModulesTable->View()->setCurrentIndex(proxyIndex);
			m_pModulesTable->View()->scrollTo(proxyIndex);
		}
	}

	showModule(uModule);
}

QString CModuleTab::SelectedSymbolName() const
{
	if (m_pExportsTable->View()->hasFocus())
	{
		return symbolNameForRow(m_pExportsTable, m_pExportsTable->CurrentSourceIndex());
	}

	return symbolNameForRow(m_pImportsTable, m_pImportsTable->CurrentSourceIndex());
}

QString CModuleTab::SelectedModulePath() const
{
	QString const sTreePath = m_pTreeModel->PathAt(m_pTreeView->currentIndex());
	if (!sTreePath.isEmpty())
		return sTreePath;

	QModelIndex const modulesIndex = m_pModulesTable->CurrentSourceIndex();
	if (modulesIndex.isValid())
		return m_pModulesModel->PathAt(modulesIndex.row());

	return QString();
}

void CModuleTab::ExpandAll()
{
	// Expanding queues the corresponding engine requests through fetchMore.
	m_pTreeView->expandAll();
}

void CModuleTab::CollapseAll()
{
	m_pTreeView->collapseAll();
}

void CModuleTab::engineRootReady(SessionId uSession, SRootResult const& rResult)
{
	if (uSession != m_uSession)
		return;

	m_sError.clear();

	m_pTreeModel->ApplyRoot(rResult);
	m_pModulesModel->AddModule(g_uRootNodeIndex, rResult.info, EModuleStatus::Root);
	m_pModulesModel->AddModules(rResult.vNewModules, rResult.vNewModuleInfos);

	QModelIndex const rootIndex = m_pTreeModel->IndexForNode(g_uRootNodeIndex);
	if (rootIndex.isValid())
	{
		m_pTreeView->expand(rootIndex);
		m_pTreeView->setCurrentIndex(rootIndex);
	}

	updateStatusLabel();
	Q_EMIT TitleChanged();
}

void CModuleTab::engineNodeExpanded(SessionId uSession, SExpandOutcome const& rOutcome)
{
	if (uSession != m_uSession)
		return;

	m_pTreeModel->ApplyExpansion(rOutcome);
	m_pModulesModel->AddModules(rOutcome.vNewModules, rOutcome.vNewModuleInfos);

	if (!rOutcome.bSuccess && rOutcome.uModule != g_uInvalidIndex)
	{
		m_pModulesModel->SetModuleStatus(rOutcome.uModule, EModuleStatus::Error, QString::fromStdString(rOutcome.sError));
	}

	updateStatusLabel();
	Q_EMIT TitleChanged();
}

void CModuleTab::engineModuleDiscovered(SessionId uSession, std::size_t uModule, SModuleInfo const& rInfo)
{
	if (uSession != m_uSession)
		return;

	EModuleStatus const eStatus = uModule == g_uRootNodeIndex ? EModuleStatus::Root : EModuleStatus::Resolved;
	m_pModulesModel->AddModule(uModule, rInfo, eStatus);
}

void CModuleTab::engineModuleUpdated(SessionId uSession, SModuleUpdate const& rUpdate)
{
	if (uSession != m_uSession)
		return;

	m_pModulesModel->UpdateModule(rUpdate.uModule, rUpdate.info);
	m_pTreeModel->ApplyModuleUpdate(rUpdate.uModule, rUpdate.info);

	// Selecting a not-yet-parsed module fills its tables when this arrives.
	if (rUpdate.uModule == m_uSelectedModule)
		showModule(rUpdate.uModule);
}

void CModuleTab::engineImportsResolved(SessionId uSession, SImportResolution const& rResolution)
{
	if (uSession != m_uSession)
		return;
	if (!rResolution.bSuccess)
		return;

	m_pImportsModel->ApplyResolution(rResolution.uModule, rResolution.vImports);

	updateStatusLabel();
}

void CModuleTab::engineClosureComplete(SessionId uSession)
{
	if (uSession != m_uSession)
		return;

	m_bClosureComplete = true;

	// Re-issue so the providers are computed against the finished closure.
	if (m_uSelectedModule != g_uInvalidIndex)
		m_rEngine.RequestImports(m_uSession, m_uSelectedModule);

	updateStatusLabel();
	Q_EMIT TitleChanged();
}

void CModuleTab::engineStatusChanged(SessionId uSession, SEngineStatus const& rStatus)
{
	if (uSession != m_uSession)
		return;

	if (rStatus.bBusy != m_bBusy)
	{
		m_bBusy = rStatus.bBusy;
		Q_EMIT BusyChanged(m_bBusy);
	}

	updateStatusLabel();
}

void CModuleTab::engineAnalysisFailed(SessionId uSession, QString const& rsMessage)
{
	if (uSession != m_uSession)
		return;

	m_sError = rsMessage;

	updateStatusLabel();
	Q_EMIT TitleChanged();
	Q_EMIT StatusMessage(rsMessage);
}

void CModuleTab::treeExpansionNeeded(std::size_t uNode)
{
	if (m_uSession == 0)
		return;

	m_rEngine.RequestExpand(m_uSession, uNode);
}

void CModuleTab::treeSelectionChanged(QModelIndex const& rCurrent, QModelIndex const& rPrevious)
{
	Q_UNUSED(rPrevious)

	showModule(m_pTreeModel->ModuleAt(rCurrent));
}

void CModuleTab::modulesSelectionChanged(QModelIndex const& rSourceIndex)
{
	if (!rSourceIndex.isValid())
		return;

	showModule(m_pModulesModel->ModuleAt(rSourceIndex.row()));
}

void CModuleTab::treeContextMenu(QPoint const& rPoint)
{
	QModelIndex const index = m_pTreeView->indexAt(rPoint);

	QMenu menu(this);

	QString const sPath = m_pTreeModel->PathAt(index);
	addModuleActions(&menu, sPath);

	std::size_t const uNode = m_pTreeModel->NodeAt(index);
	if (uNode != g_uInvalidIndex)
	{
		QString const sNeeded = m_pTreeModel->NeededNameAt(index);

		QAction* const pCopyNeeded = menu.addAction(i18n("Copy &needed name"));
		pCopyNeeded->setEnabled(!sNeeded.isEmpty());
		connect(pCopyNeeded, &QAction::triggered, this, [this, sNeeded]()
			{ copyToClipboard(sNeeded); });

		QAction* const pExpandAll = menu.addAction(i18n("&Expand all children"));
		connect(pExpandAll, &QAction::triggered, this, [this, index]()
			{ m_pTreeView->expandRecursively(index); });
	}

	menu.exec(m_pTreeView->viewport()->mapToGlobal(rPoint));
}

void CModuleTab::modulesContextMenu(QMenu* pMenu, QModelIndex const& rSourceIndex)
{
	if (pMenu == nullptr)
		return;

	QString const sPath = rSourceIndex.isValid() ? m_pModulesModel->PathAt(rSourceIndex.row()) : QString();
	addModuleActions(pMenu, sPath);
}

void CModuleTab::addModuleActions(QMenu* pMenu, QString const& rsPath)
{
	bool const bHasPath = !rsPath.isEmpty();

	QAction* const pCopyPath = pMenu->addAction(i18n("Copy &path"));
	pCopyPath->setShortcut(QKeySequence::Copy);
	pCopyPath->setEnabled(bHasPath);
	connect(pCopyPath, &QAction::triggered, this, [this, rsPath]()
		{ copyToClipboard(rsPath); });

	QAction* const pOpenTab = pMenu->addAction(i18n("&Open in new tab"));
	pOpenTab->setEnabled(bHasPath);
	connect(pOpenTab, &QAction::triggered, this, [this, rsPath]()
		{ Q_EMIT OpenFileRequested(rsPath); });

	QAction* const pOpenFolder = pMenu->addAction(i18n("Open containing &folder"));
	pOpenFolder->setEnabled(bHasPath);
	connect(pOpenFolder, &QAction::triggered, this, [this, rsPath]()
		{ openContainingFolder(rsPath); });
}

void CModuleTab::symbolContextMenu(QMenu* pMenu, QModelIndex const& rSourceIndex)
{
	if (pMenu == nullptr)
		return;

	CFilteredTable const* const pTable = qobject_cast<CFilteredTable*>(sender());
	if (pTable == nullptr)
		return;

	bool const bIsImports = pTable == m_pImportsTable;
	QString const sSymbol = symbolNameForRow(pTable, rSourceIndex);

	QAction* const pDemangle = pMenu->addAction(i18n("&Demangle C++ names"));
	pDemangle->setCheckable(true);
	pDemangle->setChecked(m_bDemangle);
	pDemangle->setShortcut(QKeySequence(Qt::Key_F10));
	connect(pDemangle, &QAction::triggered, this, [this](bool bChecked)
		{ Q_EMIT DemangleToggleRequested(bChecked); });

	QAction* const pCopyName = pMenu->addAction(i18n("Copy &symbol name"));
	pCopyName->setEnabled(!sSymbol.isEmpty());
	connect(pCopyName, &QAction::triggered, this, [this, sSymbol]()
		{ copyToClipboard(sSymbol); });

	QAction* const pLookUp = pMenu->addAction(i18n("&Look up symbol online"));
	pLookUp->setEnabled(!sSymbol.isEmpty());
	connect(pLookUp, &QAction::triggered, this, [this, sSymbol]()
		{ LookUpSymbolOnline(sSymbol); });

	if (!bIsImports)
		return;

	std::size_t const uProvider = rSourceIndex.isValid() ? m_pImportsModel->ProviderModuleAt(rSourceIndex.row()) : g_uInvalidIndex;

	QAction* const pGoToProvider = pMenu->addAction(i18n("&Go to providing module"));
	pGoToProvider->setEnabled(uProvider != g_uInvalidIndex);
	connect(pGoToProvider, &QAction::triggered, this, [this, uProvider]()
		{ SelectModule(uProvider); });
}

void CModuleTab::importActivated(QModelIndex const& rSourceIndex)
{
	if (!rSourceIndex.isValid())
		return;

	std::size_t const uProvider = m_pImportsModel->ProviderModuleAt(rSourceIndex.row());
	if (uProvider == g_uInvalidIndex)
		return;

	SelectModule(uProvider);
}

void CModuleTab::showModule(std::size_t uModule)
{
	if (uModule == g_uInvalidIndex)
	{
		m_uSelectedModule = g_uInvalidIndex;
		m_pImportsModel->Clear();
		m_pExportsModel->Clear();
		updateStatusLabel();
		return;
	}

	m_uSelectedModule = uModule;

	int const iRow = m_pModulesModel->RowForModule(uModule);
	SModuleInfo const* const pInfo = iRow >= 0 ? m_pModulesModel->InfoAt(iRow) : nullptr;

	if (pInfo == nullptr || !pInfo->bParsed)
	{
		// A placeholder shows empty panels until its ModuleUpdated arrives.
		m_pImportsModel->Clear();
		m_pExportsModel->Clear();
		updateStatusLabel();
		return;
	}

	m_pImportsModel->SetImports(uModule, pInfo->vImports, pInfo->bImportsResolved);
	m_pExportsModel->SetExports(uModule, pInfo->vExports, pInfo->eClass);

	if (!pInfo->bImportsResolved && !pInfo->vImports.empty() && m_uSession != 0)
	{
		m_rEngine.RequestImports(m_uSession, uModule);
	}

	updateStatusLabel();
}

void CModuleTab::updateStatusLabel()
{
	if (!m_sError.isEmpty())
	{
		QColor const color = ModuleStatusColor(EModuleStatus::Missing);
		QString const sColor = color.isValid() ? color.name() : QStringLiteral("red");

		m_pStatusLabel->setText(QStringLiteral("<span style=\"color:%1\">%2</span>").arg(sColor, m_sError.toHtmlEscaped()));
		return;
	}

	QStringList vParts;

	int const iModules = m_pModulesModel->ModuleCount();
	int const iMissing = m_pModulesModel->MissingCount();

	vParts.append(i18np("%1 module", "%1 modules", iModules));
	if (iMissing > 0)
		vParts.append(i18np("%1 problem module", "%1 problem modules", iMissing));

	if (m_bBusy)
		vParts.append(i18n("analyzing…"));
	else if (m_bClosureComplete)
		vParts.append(i18n("complete"));

	if (m_uSelectedModule != g_uInvalidIndex)
	{
		std::size_t uTotal = 0;
		std::size_t uUnresolved = 0;
		std::size_t uWeak = 0;
		m_pImportsModel->QueryCounts(uTotal, uUnresolved, uWeak);

		vParts.append(i18n("selected: %1 imports, %2 exports",
			QString::number(uTotal), QString::number(m_pExportsModel->ExportCount())));

		if (uUnresolved > 0)
			vParts.append(i18n("%1 unresolved", QString::number(uUnresolved)));
		if (uWeak > 0)
			vParts.append(i18n("%1 weak unresolved", QString::number(uWeak)));
	}

	m_pStatusLabel->setText(vParts.join(QStringLiteral(" — ")));
}

void CModuleTab::copyToClipboard(QString const& rsText) const
{
	if (rsText.isEmpty())
		return;

	QClipboard* const pClipboard = QApplication::clipboard();
	if (pClipboard != nullptr)
		pClipboard->setText(rsText);
}

void CModuleTab::openContainingFolder(QString const& rsPath) const
{
	if (rsPath.isEmpty())
		return;

	KIO::highlightInFileManager({QUrl::fromLocalFile(rsPath)});
}

void CModuleTab::LookUpSymbolOnline(QString const& rsSymbol) const
{
	if (rsSymbol.isEmpty())
		return;

	// The only network-touching action in the application, and it requires an
	// explicit menu choice.
	QUrl url(QStringLiteral("https://duckduckgo.com/"));

	QUrlQuery query;
	query.addQueryItem(QStringLiteral("q"), rsSymbol);
	url.setQuery(query);

	QDesktopServices::openUrl(url);
}

QString CModuleTab::symbolNameForRow(CFilteredTable const* pTable, QModelIndex const& rSourceIndex) const
{
	if (pTable == nullptr)
		return QString();
	if (!rSourceIndex.isValid())
		return QString();

	if (pTable == m_pImportsTable)
		return m_pImportsModel->DisplaySymbolNameAt(rSourceIndex.row());
	if (pTable == m_pExportsTable)
		return m_pExportsModel->DisplaySymbolNameAt(rSourceIndex.row());

	return QString();
}

CFilteredTable* CModuleTab::focusedTable() const
{
	QWidget const* const pFocus = QApplication::focusWidget();

	if (pFocus != nullptr)
	{
		for (CFilteredTable* const pTable : {m_pImportsTable, m_pExportsTable, m_pModulesTable})
		{
			if (pTable == pFocus || pTable->isAncestorOf(pFocus))
				return pTable;
		}
	}

	// The dependency tree has no filter of its own, so Ctrl+F with focus there
	// (or nowhere) falls back to the imports panel.
	return m_pImportsTable;
}
