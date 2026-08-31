// Copyright (c) 2026 Jason Gripp
// Licensed under the MIT License.

#pragma once

#include "analysisengine.h"
#include "moduletab.h"

#include <KXmlGuiWindow>

#include <QByteArray>
#include <QSize>
#include <QString>
#include <QStringList>

#include <memory>

class CModuleTab;

class KRecentFilesAction;
class KToggleAction;

class QAction;
class QCloseEvent;
class QDragEnterEvent;
class QDropEvent;
class QLabel;
class QTabWidget;
class QUrl;

// Owns application-wide actions, tabs, and persistent settings.
// Analysis remains in CAnalysisEngine and CModuleTab.
class CMainWindow : public KXmlGuiWindow
{
	Q_OBJECT

private:
	std::unique_ptr<CAnalysisEngine> m_pEngine;
	QTabWidget* m_pTabWidget = nullptr;
	KRecentFilesAction* m_pRecentFilesAction = nullptr;
	KToggleAction* m_pDemangleAction = nullptr;
	QAction* m_pCloseTabAction = nullptr;
	QAction* m_pReanalyzeAction = nullptr;
	QAction* m_pCopyPathAction = nullptr;
	QAction* m_pLookUpAction = nullptr;
	QAction* m_pExpandAllAction = nullptr;
	QAction* m_pCollapseAllAction = nullptr;
	QAction* m_pShowFilterAction = nullptr;
	QLabel* m_pStatusLabel = nullptr;
	bool m_bDemangle = true;
	QString m_sLastOpenDirectory;
	QByteArray m_splitterState;
	SColumnVisibility m_columnVisibility;
	SColumnWidths m_columnWidths;

public:
	explicit CMainWindow(QWidget* pParent = nullptr);
	~CMainWindow() override;

	void OpenFile(QString const& rsPath);
	void OpenFiles(QStringList const& rsPaths);

	int TabCount() const;
	bool IsDemangleEnabled() const;

	QSize sizeHint() const override;

protected:
	void dragEnterEvent(QDragEnterEvent* pEvent) override;
	void dropEvent(QDropEvent* pEvent) override;
	void closeEvent(QCloseEvent* pEvent) override;

private Q_SLOTS:

	void openFileDialog();
	void openRecentFile(QUrl const& rUrl);
	void closeCurrentTab();
	void closeTab(int iIndex);
	void currentTabChanged(int iIndex);
	void reanalyzeCurrentTab();
	void demangleToggled(bool bEnabled);
	void tabOpenFileRequested(QString const& rsPath);
	void tabBusyChanged(bool bBusy);
	void tabTitleChanged();
	void tabStatusMessage(QString const& rsMessage);
	void tabColumnVisibilityChanged(SColumnVisibility const& rVisibility);
	void lookUpSelectedSymbol();
	void copyCurrentPath();

private:
	void setupActions();
	void setupTabWidget();
	void readSettings();
	void writeSettings();
	void writeColumnVisibility();

	CModuleTab* currentTab() const;
	CModuleTab* tabAt(int iIndex) const;
	CModuleTab* findTabForPath(QString const& rsPath) const;

	void connectTab(CModuleTab* pTab);
	void updateActionStates();
	void updateWindowTitle();
};
