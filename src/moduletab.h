#pragma once

#include "analysisengine.h"
#include "moduledata.h"

#include <QByteArray>
#include <QModelIndex>
#include <QString>
#include <QWidget>

#include <cstddef>

class CDependencyTreeModel;
class CExportsModel;
class CFilteredTable;
class CImportsModel;
class CModulesModel;

class QLabel;
class QMenu;
class QShowEvent;
class QPoint;
class QSplitter;
class QTreeView;

// One opened file = one CModuleTab. It owns the tab's analysis session, its
// four models, the four-panel splitter layout, and every interaction inside
// the tab. It is the only class that connects engine signals to models.
class CModuleTab : public QWidget
{
	Q_OBJECT

private:
	CAnalysisEngine& m_rEngine;
	SessionId m_uSession = 0;
	QString m_sFilePath;

	QTreeView* m_pTreeView = nullptr;
	CDependencyTreeModel* m_pTreeModel = nullptr;
	CFilteredTable* m_pImportsTable = nullptr;
	CImportsModel* m_pImportsModel = nullptr;
	CFilteredTable* m_pExportsTable = nullptr;
	CExportsModel* m_pExportsModel = nullptr;
	CFilteredTable* m_pModulesTable = nullptr;
	CModulesModel* m_pModulesModel = nullptr;
	QLabel* m_pStatusLabel = nullptr;
	QSplitter* m_pMainSplitter = nullptr;
	QSplitter* m_pTreeSplitter = nullptr;
	QSplitter* m_pSymbolSplitter = nullptr;

	std::size_t m_uSelectedModule = g_uInvalidIndex;
	bool m_bDemangle = false;
	bool m_bBusy = false;
	bool m_bClosureComplete = false;
	// Splitter defaults are proportions of the real height, which only exists
	// once the tab has been shown; these track whether that is still pending.
	bool m_bSplitterStateRestored = false;
	bool m_bDefaultSplitterSizesApplied = false;
	QString m_sError;

public:
	CModuleTab(CAnalysisEngine& rEngine, QString const& rsFilePath, QWidget* const pParent = nullptr);
	~CModuleTab() override;

	QString FilePath() const;
	QString DisplayName() const;
	QString ToolTipText() const;
	SessionId Session() const;

	void SetDemangleEnabled(bool const bEnabled);
	bool IsBusy() const;
	void Cancel();
	// Shows — or re-focuses, when already open — the filter box of whichever
	// panel currently has focus.
	void ShowFilter();

	QByteArray SaveSplitterState() const;
	void RestoreSplitterState(QByteArray const& rState);

	void SelectModule(std::size_t const uModule);
	QString SelectedSymbolName() const;
	QString SelectedModulePath() const;
	void ExpandAll();
	void CollapseAll();
	// Public so the window-level action can reuse the same URL construction.
	void LookUpSymbolOnline(QString const& rsSymbol) const;

protected:
	void showEvent(QShowEvent* const pEvent) override;

Q_SIGNALS:

	void OpenFileRequested(QString const& rsPath);
	void BusyChanged(bool const bBusy);
	void TitleChanged();
	void StatusMessage(QString const& rsMessage);
	// The demangle setting is global; the tab only asks the window to flip it.
	void DemangleToggleRequested(bool const bEnabled);

private Q_SLOTS:

	void engineRootReady(SessionId const uSession, SRootResult const& rResult);
	void engineNodeExpanded(SessionId const uSession, SExpandOutcome const& rOutcome);
	void engineModuleDiscovered(SessionId const uSession, std::size_t const uModule, SModuleInfo const& rInfo);
	void engineModuleUpdated(SessionId const uSession, SModuleUpdate const& rUpdate);
	void engineImportsResolved(SessionId const uSession, SImportResolution const& rResolution);
	void engineClosureComplete(SessionId const uSession);
	void engineStatusChanged(SessionId const uSession, SEngineStatus const& rStatus);
	void engineAnalysisFailed(SessionId const uSession, QString const& rsMessage);

	void treeExpansionNeeded(std::size_t const uNode);
	void treeSelectionChanged(QModelIndex const& rCurrent, QModelIndex const& rPrevious);
	void modulesSelectionChanged(QModelIndex const& rSourceIndex);
	void treeContextMenu(QPoint const& rPoint);
	void modulesContextMenu(QMenu* const pMenu, QModelIndex const& rSourceIndex);
	void symbolContextMenu(QMenu* const pMenu, QModelIndex const& rSourceIndex);
	void importActivated(QModelIndex const& rSourceIndex);

private:
	void buildUi();
	void applyDefaultSplitterSizes();
	void connectEngine();
	void connectViews();
	void showModule(std::size_t const uModule);
	void updateStatusLabel();
	void copyToClipboard(QString const& rsText) const;
	void openContainingFolder(QString const& rsPath) const;
	QString symbolNameForRow(CFilteredTable const* const pTable, QModelIndex const& rSourceIndex) const;
	CFilteredTable* focusedTable() const;
	void addModuleActions(QMenu* const pMenu, QString const& rsPath);
};
