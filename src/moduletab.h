// Copyright (c) 2026 Jason Gripp
// Licensed under the MIT License.

#pragma once

#include "analysisengine.h"
#include "moduledata.h"

#include <QByteArray>
#include <QList>
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

// Hidden source-column indexes for all three tables. The window shares this
// setting between tabs and stores it in the application configuration.
struct SColumnVisibility
{
	QList<int> vHiddenImports;
	QList<int> vHiddenExports;
	QList<int> vHiddenModules;
};

// Widths for all three tables in source-column order. The window shares this
// layout between tabs and stores it in the application state.
struct SColumnWidths
{
	QList<int> vImports;
	QList<int> vExports;
	QList<int> vModules;
};

// Owns one file's analysis session, models, layout, and tab-local interactions.
// This is the only class that connects engine signals to models.
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
	// once the tab has been shown. These track whether that is still pending.
	bool m_bSplitterStateRestored = false;
	bool m_bDefaultSplitterSizesApplied = false;
	QString m_sError;

public:
	CModuleTab(CAnalysisEngine& rEngine, QString const& rsFilePath, QWidget* pParent = nullptr);
	~CModuleTab() override;

	QString FilePath() const;
	QString DisplayName() const;
	QString ToolTipText() const;
	SessionId Session() const;

	void SetDemangleEnabled(bool bEnabled);
	bool IsBusy() const;
	void Cancel();
	// Shows the focused panel's filter box, or refocuses it when already open.
	void ShowFilter();

	QByteArray SaveSplitterState() const;
	void RestoreSplitterState(QByteArray const& rState);

	SColumnVisibility ColumnVisibility() const;
	void SetColumnVisibility(SColumnVisibility const& rVisibility);

	SColumnWidths ColumnWidths() const;
	void SetColumnWidths(SColumnWidths const& rWidths);

	void SelectModule(std::size_t uModule);
	QString SelectedSymbolName() const;
	QString SelectedModulePath() const;
	void ExpandAll();
	void CollapseAll();
	// Public so the window-level action can reuse the same URL construction.
	void LookUpSymbolOnline(QString const& rsSymbol) const;

protected:
	void showEvent(QShowEvent* pEvent) override;

Q_SIGNALS:

	void OpenFileRequested(QString const& rsPath);
	void BusyChanged(bool bBusy);
	void TitleChanged();
	void StatusMessage(QString const& rsMessage);
	// The demangle setting is global. The tab asks the window to change it.
	void DemangleToggleRequested(bool bEnabled);
	// Hidden columns are also global. The window propagates and stores changes.
	void ColumnVisibilityChanged(SColumnVisibility const& rVisibility);

private Q_SLOTS:

	void engineRootReady(SessionId uSession, SRootResult const& rResult);
	void engineNodeExpanded(SessionId uSession, SExpandOutcome const& rOutcome);
	void engineModuleDiscovered(SessionId uSession, std::size_t uModule, SModuleInfo const& rInfo);
	void engineModuleUpdated(SessionId uSession, SModuleUpdate const& rUpdate);
	void engineImportsResolved(SessionId uSession, SImportResolution const& rResolution);
	void engineClosureComplete(SessionId uSession);
	void engineStatusChanged(SessionId uSession, SEngineStatus const& rStatus);
	void engineAnalysisFailed(SessionId uSession, QString const& rsMessage);

	void treeExpansionNeeded(std::size_t uNode);
	void treeSelectionChanged(QModelIndex const& rCurrent, QModelIndex const& rPrevious);
	void modulesSelectionChanged(QModelIndex const& rSourceIndex);
	void treeContextMenu(QPoint const& rPoint);
	void modulesContextMenu(QMenu* pMenu, QModelIndex const& rSourceIndex);
	void symbolContextMenu(QMenu* pMenu, QModelIndex const& rSourceIndex);
	void importActivated(QModelIndex const& rSourceIndex);
	void tableColumnVisibilityChanged();

private:
	void buildUi();
	void applyDefaultSplitterSizes();
	void connectEngine();
	void connectViews();
	void showModule(std::size_t uModule);
	void updateStatusLabel();
	void copyToClipboard(QString const& rsText) const;
	void openContainingFolder(QString const& rsPath) const;
	QString symbolNameForRow(CFilteredTable const* pTable, QModelIndex const& rSourceIndex) const;
	CFilteredTable* focusedTable() const;
	void addModuleActions(QMenu* pMenu, QString const& rsPath);
};
