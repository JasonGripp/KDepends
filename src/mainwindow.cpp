#include "mainwindow.h"

#include "analysisengine.h"
#include "icons.h"
#include "moduletab.h"

#include <KActionCollection>
#include <KConfigGroup>
#include <KLocalizedString>
#include <KRecentFilesAction>
#include <KSharedConfig>
#include <KStandardAction>
#include <KToggleAction>
#include <KXMLGUIFactory>

#include <QAction>
#include <QApplication>
#include <QByteArray>
#include <QClipboard>
#include <QCloseEvent>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QKeySequence>
#include <QLabel>
#include <QList>
#include <QMimeData>
#include <QScreen>
#include <QSize>
#include <QStackedWidget>
#include <QStatusBar>
#include <QTabWidget>
#include <QUrl>

#include <memory>

namespace {
// Group names in two different files. The config file (kdependsrc) holds
// only what the user deliberately chose; everything the app merely observed
// — layout, history — goes to the state file (kdependsstaterc), where a
// config backup will not carry it along.
constexpr char const* g_pcConfigGroup = "General";
constexpr char const* g_pcStateGroup = "General";
constexpr char const* g_pcRecentFilesGroup = "RecentFiles";

QString CanonicalOrOriginal(QString const& rsPath)
{
	QFileInfo const info(rsPath);

	QString const sCanonical = info.canonicalFilePath();
	if (!sCanonical.isEmpty())
		return sCanonical;

	return info.absoluteFilePath();
}
} //namespace

CMainWindow::CMainWindow(QWidget* pParent)
: KXmlGuiWindow(pParent)
{
	// Deliberately unparented: the unique_ptr owns it, and it must outlive
	// every tab (each tab holds a reference to it).
	m_pEngine = std::make_unique<CAnalysisEngine>();

	// KMainWindow's WA_DeleteOnClose is deliberately left on: that self-delete
	// is what drives the last-window-closed quit. Clearing it leaves a hidden
	// window and an event loop that never returns, so the process survives its
	// own close. `main` heap-allocates this window to match.

	setWindowIcon(ApplicationIcon());

	setupTabWidget();
	setupActions();

	m_pStatusLabel = new QLabel(this);
	statusBar()->addPermanentWidget(m_pStatusLabel);

	setupGUI(Default, QStringLiteral("kdependsui.rc"));
	setAutoSaveSettings(QStringLiteral("MainWindow"));

	readSettings();

	setAcceptDrops(true);

	updateActionStates();
	updateWindowTitle();
}

CMainWindow::~CMainWindow()
{
	writeSettings();

	if (m_pEngine)
		m_pEngine->CancelAll();

	// Tabs hold a reference to the engine, so they must be destroyed before it.
	// Qt would otherwise destroy them with the rest of the widget tree, which
	// happens after this object's members are gone.
	while (m_pTabWidget->count() > 0)
	{
		QWidget* const pTab = m_pTabWidget->widget(0);
		m_pTabWidget->removeTab(0);
		delete pTab;
	}
}

void CMainWindow::setupTabWidget()
{
	QStackedWidget* const pStack = new QStackedWidget(this);
	pStack->setObjectName(QStringLiteral("centralStack"));

	QLabel* const pPlaceholder = new QLabel(i18n("Open a file to begin."), pStack);
	pPlaceholder->setAlignment(Qt::AlignCenter);
	pPlaceholder->setEnabled(false);

	m_pTabWidget = new QTabWidget(pStack);
	m_pTabWidget->setTabsClosable(true);
	m_pTabWidget->setMovable(true);
	m_pTabWidget->setDocumentMode(true);

	pStack->addWidget(pPlaceholder);
	pStack->addWidget(m_pTabWidget);

	setCentralWidget(pStack);

	auto const UpdatePage = [pStack, this]()
	{
		pStack->setCurrentIndex(m_pTabWidget->count() > 0 ? 1 : 0);
	};

	UpdatePage();

	connect(m_pTabWidget, &QTabWidget::tabCloseRequested, this, &CMainWindow::closeTab);
	connect(m_pTabWidget, &QTabWidget::currentChanged, this, &CMainWindow::currentTabChanged);
	connect(m_pTabWidget, &QTabWidget::currentChanged, this, UpdatePage);
}

void CMainWindow::setupActions()
{
	KActionCollection* const pActions = actionCollection();

	KStandardAction::open(this, &CMainWindow::openFileDialog, pActions);
	KStandardAction::quit(qApp, &QApplication::closeAllWindows, pActions);
	KStandardAction::keyBindings(guiFactory(), &KXMLGUIFactory::showConfigureShortcutsDialog, pActions);
	KStandardAction::configureToolbars(this, &KXmlGuiWindow::configureToolbars, pActions);

	m_pRecentFilesAction = KStandardAction::openRecent(this, &CMainWindow::openRecentFile, pActions);

	m_pReanalyzeAction = pActions->addAction(QStringLiteral("reanalyze"));
	m_pReanalyzeAction->setText(i18n("Re-&analyze"));
	m_pReanalyzeAction->setIcon(QIcon::fromTheme(QStringLiteral("view-refresh")));
	pActions->setDefaultShortcut(m_pReanalyzeAction, QKeySequence(Qt::Key_F5));
	connect(m_pReanalyzeAction, &QAction::triggered, this, &CMainWindow::reanalyzeCurrentTab);

	m_pCloseTabAction = pActions->addAction(QStringLiteral("close_tab"));
	m_pCloseTabAction->setText(i18n("&Close Tab"));
	m_pCloseTabAction->setIcon(QIcon::fromTheme(QStringLiteral("tab-close")));
	pActions->setDefaultShortcut(m_pCloseTabAction, QKeySequence::Close);
	connect(m_pCloseTabAction, &QAction::triggered, this, &CMainWindow::closeCurrentTab);

	m_pExpandAllAction = pActions->addAction(QStringLiteral("expand_all"));
	m_pExpandAllAction->setText(i18n("&Expand All"));
	connect(m_pExpandAllAction, &QAction::triggered, this, [this]()
		{
		CModuleTab * const pTab = currentTab();
		if (pTab != nullptr) pTab->ExpandAll(); });

	m_pCollapseAllAction = pActions->addAction(QStringLiteral("collapse_all"));
	m_pCollapseAllAction->setText(i18n("&Collapse All"));
	connect(m_pCollapseAllAction, &QAction::triggered, this, [this]()
		{
		CModuleTab * const pTab = currentTab();
		if (pTab != nullptr) pTab->CollapseAll(); });

	m_pShowFilterAction = KStandardAction::find(this, [this]()
		{
		CModuleTab * const pTab = currentTab();
		if (pTab != nullptr) pTab->ShowFilter(); }, pActions);

	m_pCopyPathAction = pActions->addAction(QStringLiteral("copy_path"));
	m_pCopyPathAction->setText(i18n("Copy &Path"));
	m_pCopyPathAction->setIcon(QIcon::fromTheme(QStringLiteral("edit-copy")));
	connect(m_pCopyPathAction, &QAction::triggered, this, &CMainWindow::copyCurrentPath);

	m_pLookUpAction = pActions->addAction(QStringLiteral("look_up_symbol"));
	m_pLookUpAction->setText(i18n("&Look Up Symbol Online"));
	m_pLookUpAction->setIcon(QIcon::fromTheme(QStringLiteral("internet-web-browser")));
	connect(m_pLookUpAction, &QAction::triggered, this, &CMainWindow::lookUpSelectedSymbol);

	m_pDemangleAction = new KToggleAction(i18n("&Demangle C++ Names"), this);
	pActions->addAction(QStringLiteral("demangle_cpp"), m_pDemangleAction);
	pActions->setDefaultShortcut(m_pDemangleAction, QKeySequence(Qt::Key_F10));
	m_pDemangleAction->setChecked(m_bDemangle);
	connect(m_pDemangleAction, &QAction::toggled, this, &CMainWindow::demangleToggled);
}

void CMainWindow::OpenFile(QString const& rsPath)
{
	if (rsPath.isEmpty())
		return;

	QFileInfo const info(rsPath);

	if (!info.exists() || !info.isFile())
	{
		statusBar()->showMessage(i18n("Cannot open %1: not a regular file.", rsPath), 5000);

		if (m_pRecentFilesAction != nullptr)
			m_pRecentFilesAction->removeUrl(QUrl::fromLocalFile(rsPath));
		return;
	}

	QString const sPath = CanonicalOrOriginal(rsPath);

	CModuleTab* const pExisting = findTabForPath(sPath);
	if (pExisting != nullptr)
	{
		m_pTabWidget->setCurrentWidget(pExisting);
		return;
	}

	CModuleTab* const pTab = new CModuleTab(*m_pEngine, sPath, m_pTabWidget);
	pTab->SetDemangleEnabled(m_bDemangle);
	pTab->RestoreSplitterState(m_splitterState);
	pTab->SetColumnVisibility(m_columnVisibility);

	connectTab(pTab);

	int const iIndex = m_pTabWidget->addTab(pTab, ModuleStatusIcon(EModuleStatus::Root), pTab->DisplayName());
	m_pTabWidget->setTabToolTip(iIndex, pTab->ToolTipText());
	m_pTabWidget->setCurrentIndex(iIndex);

	if (m_pRecentFilesAction != nullptr)
		m_pRecentFilesAction->addUrl(QUrl::fromLocalFile(sPath));

	m_sLastOpenDirectory = info.absolutePath();

	updateActionStates();
	updateWindowTitle();
}

void CMainWindow::OpenFiles(QStringList const& rsPaths)
{
	if (rsPaths.isEmpty())
		return;

	int const iFirstIndex = m_pTabWidget->count();

	for (QString const& rsPath : rsPaths)
		OpenFile(rsPath);

	if (iFirstIndex < m_pTabWidget->count())
		m_pTabWidget->setCurrentIndex(iFirstIndex);
}

int CMainWindow::TabCount() const
{
	return m_pTabWidget->count();
}

bool CMainWindow::IsDemangleEnabled() const
{
	return m_bDemangle;
}

QSize CMainWindow::sizeHint() const
{
	// KMainWindow takes the first-launch size from this hint, before the state
	// config holds any geometry (it ignores a resize() in the constructor). The
	// inherited hint is the empty placeholder page plus chrome — a couple of
	// hundred pixels — so give a size suited to a tree beside two tables,
	// capped to keep it on the screen the window opens on.
	QSize const preferred{1280, 800};

	QScreen const* const pScreen = screen();
	if (pScreen == nullptr)
		return preferred;

	return preferred.boundedTo(pScreen->availableGeometry().size() * 0.9);
}

void CMainWindow::dragEnterEvent(QDragEnterEvent* pEvent)
{
	if (pEvent == nullptr)
		return;

	QMimeData const* const pMimeData = pEvent->mimeData();
	if (pMimeData == nullptr || !pMimeData->hasUrls())
		return;

	for (QUrl const& rUrl : pMimeData->urls())
	{
		if (!rUrl.isLocalFile())
			continue;

		pEvent->acceptProposedAction();
		return;
	}
}

void CMainWindow::dropEvent(QDropEvent* pEvent)
{
	if (pEvent == nullptr)
		return;

	QMimeData const* const pMimeData = pEvent->mimeData();
	if (pMimeData == nullptr || !pMimeData->hasUrls())
		return;

	QStringList vPaths;
	for (QUrl const& rUrl : pMimeData->urls())
	{
		if (!rUrl.isLocalFile())
			continue;
		vPaths.append(rUrl.toLocalFile());
	}

	if (vPaths.isEmpty())
		return;

	pEvent->acceptProposedAction();
	OpenFiles(vPaths);
}

void CMainWindow::closeEvent(QCloseEvent* pEvent)
{
	writeSettings();

	// Analysis is read-only, so there is never anything to save and never a
	// confirmation prompt.
	if (m_pEngine)
		m_pEngine->CancelAll();

	KXmlGuiWindow::closeEvent(pEvent);
}

void CMainWindow::openFileDialog()
{
	QStringList const vPaths = QFileDialog::getOpenFileNames(this, i18n("Open Executable or Library"),
		m_sLastOpenDirectory,
		i18n("Executables and libraries (*.so *.so.* *);;All files (*)"));

	OpenFiles(vPaths);
}

void CMainWindow::openRecentFile(QUrl const& rUrl)
{
	if (!rUrl.isLocalFile())
	{
		statusBar()->showMessage(i18n("Only local files can be inspected."), 5000);
		return;
	}

	OpenFile(rUrl.toLocalFile());
}

void CMainWindow::closeCurrentTab()
{
	closeTab(m_pTabWidget->currentIndex());
}

void CMainWindow::closeTab(int iIndex)
{
	CModuleTab* const pTab = tabAt(iIndex);
	if (pTab == nullptr)
		return;

	m_pTabWidget->removeTab(iIndex);

	// Deleted immediately rather than deferred: the tab holds a reference to
	// the engine, and a deferred delete could outlive it. The sender is the
	// tab widget, not the tab, so this is not a delete-during-own-signal.
	delete pTab;

	updateActionStates();
	updateWindowTitle();
}

void CMainWindow::currentTabChanged(int iIndex)
{
	Q_UNUSED(iIndex)

	CModuleTab const* const pTab = currentTab();
	m_pStatusLabel->setText(pTab != nullptr && pTab->IsBusy() ? i18n("Analyzing…") : QString());

	updateActionStates();
	updateWindowTitle();
}

void CMainWindow::reanalyzeCurrentTab()
{
	CModuleTab* const pTab = currentTab();
	if (pTab == nullptr)
		return;

	QString const sPath = pTab->FilePath();
	int const iIndex = m_pTabWidget->currentIndex();

	m_splitterState = pTab->SaveSplitterState();

	closeTab(iIndex);
	OpenFile(sPath);
}

void CMainWindow::demangleToggled(bool bEnabled)
{
	m_bDemangle = bEnabled;

	for (int iIndex = 0; iIndex < m_pTabWidget->count(); ++iIndex)
	{
		CModuleTab* const pTab = tabAt(iIndex);
		if (pTab != nullptr)
			pTab->SetDemangleEnabled(bEnabled);
	}

	KConfigGroup group(KSharedConfig::openConfig(), QLatin1String(g_pcConfigGroup));
	group.writeEntry("DemangleCppNames", m_bDemangle);
	group.sync();
}

void CMainWindow::tabOpenFileRequested(QString const& rsPath)
{
	OpenFile(rsPath);
}

void CMainWindow::tabBusyChanged(bool bBusy)
{
	CModuleTab const* const pSender = qobject_cast<CModuleTab*>(sender());
	if (pSender == nullptr)
		return;
	if (pSender != currentTab())
		return;

	m_pStatusLabel->setText(bBusy ? i18n("Analyzing…") : QString());
}

void CMainWindow::tabTitleChanged()
{
	CModuleTab* const pSender = qobject_cast<CModuleTab*>(sender());
	if (pSender == nullptr)
		return;

	int const iIndex = m_pTabWidget->indexOf(pSender);
	if (iIndex < 0)
		return;

	m_pTabWidget->setTabText(iIndex, pSender->DisplayName());
	m_pTabWidget->setTabToolTip(iIndex, pSender->ToolTipText());
}

void CMainWindow::tabStatusMessage(QString const& rsMessage)
{
	if (sender() != currentTab())
		return;

	statusBar()->showMessage(rsMessage, 8000);
}

void CMainWindow::tabColumnVisibilityChanged(SColumnVisibility const& rVisibility)
{
	m_columnVisibility = rVisibility;

	// Hiding a column is a statement about the panel, not about one file, so
	// every open tab follows. The originating tab is skipped because it is
	// already in this state, and re-applying it there is what would echo.
	QObject const* const pSender = sender();

	for (int iIndex = 0; iIndex < m_pTabWidget->count(); ++iIndex)
	{
		CModuleTab* const pTab = tabAt(iIndex);
		if (pTab == nullptr || pTab == pSender)
			continue;

		pTab->SetColumnVisibility(m_columnVisibility);
	}

	// Written straight away, like the demangle toggle: it is a deliberate
	// choice, and waiting for shutdown would lose it to a crash.
	writeColumnVisibility();
}

void CMainWindow::lookUpSelectedSymbol()
{
	CModuleTab* const pTab = currentTab();
	if (pTab == nullptr)
		return;

	QString const sSymbol = pTab->SelectedSymbolName();
	if (sSymbol.isEmpty())
	{
		statusBar()->showMessage(i18n("Select a symbol first."), 5000);
		return;
	}

	// The tab owns the browser hand-off; asking it keeps the URL construction
	// in one place.
	pTab->LookUpSymbolOnline(sSymbol);
}

void CMainWindow::copyCurrentPath()
{
	CModuleTab* const pTab = currentTab();
	if (pTab == nullptr)
		return;

	QString const sPath = pTab->SelectedModulePath();
	if (sPath.isEmpty())
	{
		statusBar()->showMessage(i18n("Select a module first."), 5000);
		return;
	}

	QClipboard* const pClipboard = QApplication::clipboard();
	if (pClipboard != nullptr)
		pClipboard->setText(sPath);
}

void CMainWindow::readSettings()
{
	KConfigGroup group(KSharedConfig::openConfig(), QLatin1String(g_pcConfigGroup));
	KConfigGroup stateGroup(KSharedConfig::openStateConfig(), QLatin1String(g_pcStateGroup));

	m_bDemangle = group.readEntry("DemangleCppNames", true);
	m_sLastOpenDirectory = stateGroup.readEntry("LastOpenDirectory", QString());
	m_splitterState = stateGroup.readEntry("SplitterState", QByteArray());

	m_columnVisibility.vHiddenImports = group.readEntry("HiddenImportColumns", QList<int>());
	m_columnVisibility.vHiddenExports = group.readEntry("HiddenExportColumns", QList<int>());
	m_columnVisibility.vHiddenModules = group.readEntry("HiddenModuleColumns", QList<int>());

	if (m_pDemangleAction != nullptr)
		m_pDemangleAction->setChecked(m_bDemangle);

	if (m_pRecentFilesAction != nullptr)
	{
		KConfigGroup recentGroup(KSharedConfig::openStateConfig(), QLatin1String(g_pcRecentFilesGroup));
		m_pRecentFilesAction->loadEntries(recentGroup);
	}
}

void CMainWindow::writeSettings()
{
	KConfigGroup group(KSharedConfig::openConfig(), QLatin1String(g_pcConfigGroup));
	KConfigGroup stateGroup(KSharedConfig::openStateConfig(), QLatin1String(g_pcStateGroup));

	group.writeEntry("DemangleCppNames", m_bDemangle);
	stateGroup.writeEntry("LastOpenDirectory", m_sLastOpenDirectory);

	CModuleTab const* const pTab = currentTab();
	if (pTab != nullptr)
		m_splitterState = pTab->SaveSplitterState();

	stateGroup.writeEntry("SplitterState", m_splitterState);

	if (m_pRecentFilesAction != nullptr)
	{
		KConfigGroup recentGroup(KSharedConfig::openStateConfig(), QLatin1String(g_pcRecentFilesGroup));
		m_pRecentFilesAction->saveEntries(recentGroup);
	}

	group.sync();
	stateGroup.sync();

	writeColumnVisibility();
}

void CMainWindow::writeColumnVisibility()
{
	// The config file rather than the state file: unlike the splitter layout,
	// a hidden column is only ever the result of the user picking it off a menu.
	KConfigGroup group(KSharedConfig::openConfig(), QLatin1String(g_pcConfigGroup));

	group.writeEntry("HiddenImportColumns", m_columnVisibility.vHiddenImports);
	group.writeEntry("HiddenExportColumns", m_columnVisibility.vHiddenExports);
	group.writeEntry("HiddenModuleColumns", m_columnVisibility.vHiddenModules);

	group.sync();
}

CModuleTab* CMainWindow::currentTab() const
{
	return tabAt(m_pTabWidget->currentIndex());
}

CModuleTab* CMainWindow::tabAt(int iIndex) const
{
	if (iIndex < 0 || iIndex >= m_pTabWidget->count())
		return nullptr;

	return qobject_cast<CModuleTab*>(m_pTabWidget->widget(iIndex));
}

CModuleTab* CMainWindow::findTabForPath(QString const& rsPath) const
{
	for (int iIndex = 0; iIndex < m_pTabWidget->count(); ++iIndex)
	{
		CModuleTab* const pTab = tabAt(iIndex);
		if (pTab == nullptr)
			continue;
		if (pTab->FilePath() != rsPath)
			continue;

		return pTab;
	}

	return nullptr;
}

void CMainWindow::connectTab(CModuleTab* pTab)
{
	connect(pTab, &CModuleTab::OpenFileRequested, this, &CMainWindow::tabOpenFileRequested);
	connect(pTab, &CModuleTab::BusyChanged, this, &CMainWindow::tabBusyChanged);
	connect(pTab, &CModuleTab::TitleChanged, this, &CMainWindow::tabTitleChanged);
	connect(pTab, &CModuleTab::StatusMessage, this, &CMainWindow::tabStatusMessage);
	connect(pTab, &CModuleTab::ColumnVisibilityChanged, this, &CMainWindow::tabColumnVisibilityChanged);
	connect(pTab, &CModuleTab::DemangleToggleRequested, this, [this](bool bEnabled)
		{ m_pDemangleAction->setChecked(bEnabled); });
}

void CMainWindow::updateActionStates()
{
	bool const bHasTab = currentTab() != nullptr;

	m_pCloseTabAction->setEnabled(bHasTab);
	m_pReanalyzeAction->setEnabled(bHasTab);
	m_pExpandAllAction->setEnabled(bHasTab);
	m_pCollapseAllAction->setEnabled(bHasTab);
	m_pCopyPathAction->setEnabled(bHasTab);
	m_pLookUpAction->setEnabled(bHasTab);

	if (m_pShowFilterAction != nullptr)
		m_pShowFilterAction->setEnabled(bHasTab);
}

void CMainWindow::updateWindowTitle()
{
	CModuleTab const* const pTab = currentTab();

	if (pTab == nullptr)
	{
		setCaption(QString());
		return;
	}

	setCaption(pTab->DisplayName());
}
