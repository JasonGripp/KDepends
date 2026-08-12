#include "filteredtable.h"

#include <KLocalizedString>

#include <QAbstractItemModel>
#include <QAbstractItemView>
#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QIcon>
#include <QItemSelectionModel>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QModelIndex>
#include <QModelIndexList>
#include <QPoint>
#include <QRegularExpression>
#include <QShortcut>
#include <QSortFilterProxyModel>
#include <QStringList>
#include <QTableView>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWidget>

#include <algorithm>

namespace {
// Floor for the auto-fit column cap, so a very narrow panel still shows
// usable columns.
constexpr int g_iMinimumAutoColumnWidth = 120;
} //namespace

CFilteredTable::CFilteredTable(QWidget* const pParent)
: QWidget(pParent)
{
	buildUi();
	buildShortcuts();
}

CFilteredTable::~CFilteredTable()
{
}

void CFilteredTable::buildUi()
{
	m_pProxy = new QSortFilterProxyModel(this);
	m_pProxy->setFilterCaseSensitivity(Qt::CaseInsensitive);
	m_pProxy->setFilterKeyColumn(m_iFilterColumn);
	m_pProxy->setSortCaseSensitivity(Qt::CaseInsensitive);
	// The source models expose a per-column sort key under Qt::UserRole so
	// numeric columns sort numerically rather than lexicographically.
	m_pProxy->setSortRole(Qt::UserRole);
	m_pProxy->setDynamicSortFilter(true);

	m_pView = new QTableView(this);
	m_pView->setModel(m_pProxy);
	m_pView->setSelectionBehavior(QAbstractItemView::SelectRows);
	m_pView->setSelectionMode(QAbstractItemView::ExtendedSelection);
	m_pView->setSortingEnabled(true);
	// Explicit, because the header's default sort indicator order is not
	// guaranteed and an unspecified initial order makes the first column's
	// grouping look arbitrary.
	m_pView->sortByColumn(0, Qt::AscendingOrder);
	m_pView->setAlternatingRowColors(true);
	m_pView->setWordWrap(false);
	m_pView->setContextMenuPolicy(Qt::CustomContextMenu);
	m_pView->verticalHeader()->setVisible(false);
	m_pView->verticalHeader()->setDefaultSectionSize(QFontMetrics(m_pView->font()).height() + 6);
	m_pView->horizontalHeader()->setSectionsMovable(true);
	m_pView->horizontalHeader()->setStretchLastSection(true);

	// The filter row lives in its own widget so it can be shown and hidden as a
	// unit; it stays hidden until the pane's Ctrl+F asks for it.
	m_pFilterRow = new QWidget(this);

	m_pFilterLabel = new QLabel(i18n("Search:"), m_pFilterRow);

	m_pFilterEdit = new QLineEdit(m_pFilterRow);

	m_pFilterCloseButton = new QToolButton(m_pFilterRow);
	m_pFilterCloseButton->setIcon(QIcon::fromTheme(QStringLiteral("dialog-close")));
	m_pFilterCloseButton->setAutoRaise(true);
	m_pFilterCloseButton->setToolTip(i18n("Clear and hide the filter"));

	QHBoxLayout* const pFilterLayout = new QHBoxLayout(m_pFilterRow);
	pFilterLayout->setContentsMargins(0, 0, 0, 0);
	pFilterLayout->addWidget(m_pFilterLabel);
	pFilterLayout->addWidget(m_pFilterEdit, 1);
	pFilterLayout->addWidget(m_pFilterCloseButton);

	m_pFilterRow->setVisible(false);

	QVBoxLayout* const pLayout = new QVBoxLayout(this);
	pLayout->setContentsMargins(0, 0, 0, 0);
	pLayout->addWidget(m_pView, 1);
	pLayout->addWidget(m_pFilterRow);

	connect(m_pFilterEdit, &QLineEdit::textChanged, this, &CFilteredTable::filterTextChanged);
	connect(m_pFilterCloseButton, &QToolButton::clicked, this, [this]()
		{ SetFilterVisible(false); });
	connect(m_pView, &QWidget::customContextMenuRequested, this, &CFilteredTable::showContextMenu);
	connect(m_pView->selectionModel(), &QItemSelectionModel::currentRowChanged, this, &CFilteredTable::currentRowChanged);

	connect(m_pView, &QAbstractItemView::activated, this, [this](QModelIndex const& rIndex)
		{ Q_EMIT Activated(m_pProxy->mapToSource(rIndex)); });
}

void CFilteredTable::buildShortcuts()
{
	// Widget-scoped so two panels in the same tab do not create ambiguous
	// shortcuts.
	QShortcut* const pCopy = new QShortcut(QKeySequence::Copy, this);
	pCopy->setContext(Qt::WidgetWithChildrenShortcut);
	connect(pCopy, &QShortcut::activated, this, &CFilteredTable::CopySelection);

	QShortcut* const pSelectAll = new QShortcut(QKeySequence::SelectAll, this);
	pSelectAll->setContext(Qt::WidgetWithChildrenShortcut);
	connect(pSelectAll, &QShortcut::activated, this, &CFilteredTable::SelectAllRows);

	// Ctrl+F is deliberately absent here: the window-level Find action owns it
	// and routes it to whichever panel has focus, so a panel-scoped shortcut
	// would only make it ambiguous.
	//
	// Escape is scoped to the whole panel, not just the box, so it closes an
	// open filter whether the user is still typing or back in the table. It is
	// disabled while the filter is hidden, so a disabled shortcut does not
	// swallow Escape from anything else.
	m_pCloseFilterShortcut = new QShortcut(QKeySequence(Qt::Key_Escape), this);
	m_pCloseFilterShortcut->setContext(Qt::WidgetWithChildrenShortcut);
	m_pCloseFilterShortcut->setEnabled(false);
	connect(m_pCloseFilterShortcut, &QShortcut::activated, this, [this]()
		{ SetFilterVisible(false); });
}

void CFilteredTable::SetSourceModel(QAbstractItemModel* const pModel)
{
	QAbstractItemModel* const pPrevious = m_pProxy->sourceModel();
	if (pPrevious != nullptr)
		pPrevious->disconnect(this);

	m_pProxy->setSourceModel(pModel);
	m_pFilterEdit->clear();
	m_bFirstPopulation = true;

	if (pModel != nullptr)
	{
		connect(pModel, &QAbstractItemModel::modelReset, this, &CFilteredTable::rowsPopulated);
		connect(pModel, &QAbstractItemModel::rowsInserted, this, &CFilteredTable::rowsPopulated);
		rowsPopulated();
	}

	// The selection model is replaced whenever the proxy's source changes.
	connect(m_pView->selectionModel(), &QItemSelectionModel::currentRowChanged, this, &CFilteredTable::currentRowChanged, Qt::UniqueConnection);
}

QAbstractItemModel* CFilteredTable::SourceModel() const
{
	return m_pProxy->sourceModel();
}

QTableView* CFilteredTable::View() const
{
	return m_pView;
}

QSortFilterProxyModel* CFilteredTable::Proxy() const
{
	return m_pProxy;
}

void CFilteredTable::SetFilterColumn(int const iColumn)
{
	m_iFilterColumn = iColumn;
	m_pProxy->setFilterKeyColumn(iColumn);
}

void CFilteredTable::SetFilterPlaceholder(QString const& rsText)
{
	m_pFilterEdit->setPlaceholderText(rsText);
}

void CFilteredTable::SetFilterLabel(QString const& rsText)
{
	m_pFilterLabel->setText(rsText);
}

QString CFilteredTable::FilterText() const
{
	return m_pFilterEdit->text();
}

void CFilteredTable::SetFilterText(QString const& rsText)
{
	m_pFilterEdit->setText(rsText);
}

void CFilteredTable::ClearFilter()
{
	m_pFilterEdit->clear();
}

void CFilteredTable::FocusFilter()
{
	m_pFilterEdit->setFocus();
	m_pFilterEdit->selectAll();
}

bool CFilteredTable::IsFilterVisible() const
{
	// Relative to this widget, so a panel sitting in a background tab still
	// reports its own filter state correctly.
	return m_pFilterRow->isVisibleTo(this);
}

void CFilteredTable::SetFilterVisible(bool const bVisible)
{
	m_pCloseFilterShortcut->setEnabled(bVisible);

	if (bVisible)
	{
		m_pFilterRow->setVisible(true);
		FocusFilter();
		return;
	}

	// A hidden filter must never keep rows filtered out of sight, so closing it
	// always clears it and hands focus back to the table.
	m_pFilterRow->setVisible(false);
	ClearFilter();
	m_pView->setFocus();
}

void CFilteredTable::ShowFilter()
{
	// Deliberately not a toggle: on an already-open filter this just puts the
	// cursor back in the box. Escape and the close button are what shut it.
	SetFilterVisible(true);
}

int CFilteredTable::VisibleRowCount() const
{
	return m_pProxy->rowCount();
}

QModelIndexList CFilteredTable::SelectedSourceIndexes() const
{
	QModelIndexList vIndexes;

	QItemSelectionModel const* const pSelection = m_pView->selectionModel();
	if (pSelection == nullptr)
		return vIndexes;

	for (QModelIndex const& rIndex : pSelection->selectedIndexes())
	{
		vIndexes.append(m_pProxy->mapToSource(rIndex));
	}

	return vIndexes;
}

QModelIndexList CFilteredTable::SelectedSourceRows() const
{
	QModelIndexList vIndexes;

	QItemSelectionModel const* const pSelection = m_pView->selectionModel();
	if (pSelection == nullptr)
		return vIndexes;

	for (QModelIndex const& rIndex : pSelection->selectedRows(0))
	{
		vIndexes.append(m_pProxy->mapToSource(rIndex));
	}

	return vIndexes;
}

QModelIndex CFilteredTable::CurrentSourceIndex() const
{
	QItemSelectionModel const* const pSelection = m_pView->selectionModel();
	if (pSelection == nullptr)
		return QModelIndex();

	return m_pProxy->mapToSource(pSelection->currentIndex());
}

void CFilteredTable::CopySelection() const
{
	QString const sText = selectionAsText();
	if (sText.isEmpty())
		return;

	QClipboard* const pClipboard = QApplication::clipboard();
	if (pClipboard != nullptr)
		pClipboard->setText(sText);
}

void CFilteredTable::SelectAllRows()
{
	m_pView->selectAll();
}

void CFilteredTable::ResizeColumnsToContents()
{
	m_pView->resizeColumnsToContents();

	// A single deeply templated demangled C++ name can be thousands of pixels
	// wide and would push every other column off-screen, so no column is
	// allowed to claim more than a third of the viewport on an auto-fit.
	int const iMaxWidth = qMax(g_iMinimumAutoColumnWidth, m_pView->viewport()->width() / 3);

	for (int iColumn = 0; iColumn < m_pProxy->columnCount(); ++iColumn)
	{
		if (m_pView->columnWidth(iColumn) > iMaxWidth)
			m_pView->setColumnWidth(iColumn, iMaxWidth);
	}
}

void CFilteredTable::SetAutoResizeColumns(bool const bEnabled)
{
	m_bAutoResize = bEnabled;
}

void CFilteredTable::filterTextChanged(QString const& rsText)
{
	// Escaped so a user typing '.' or '*' filters literally.
	m_pProxy->setFilterRegularExpression(QRegularExpression(QRegularExpression::escape(rsText),
		QRegularExpression::CaseInsensitiveOption));

	Q_EMIT FilterChanged(rsText);
}

void CFilteredTable::showContextMenu(QPoint const& rPoint)
{
	QModelIndex const proxyIndex = m_pView->indexAt(rPoint);
	QModelIndex const sourceIndex = m_pProxy->mapToSource(proxyIndex);

	QMenu menu(this);

	QAction* const pCopy = menu.addAction(i18n("&Copy"));
	pCopy->setShortcut(QKeySequence::Copy);
	connect(pCopy, &QAction::triggered, this, &CFilteredTable::CopySelection);

	QAction* const pSelectAll = menu.addAction(i18n("Select &All"));
	pSelectAll->setShortcut(QKeySequence::SelectAll);
	connect(pSelectAll, &QAction::triggered, this, &CFilteredTable::SelectAllRows);

	menu.addSeparator();

	Q_EMIT ContextMenuRequested(&menu, sourceIndex);

	menu.exec(m_pView->viewport()->mapToGlobal(rPoint));
}

void CFilteredTable::currentRowChanged(QModelIndex const& rCurrent, QModelIndex const& rPrevious)
{
	Q_UNUSED(rPrevious)

	Q_EMIT SelectionChanged(m_pProxy->mapToSource(rCurrent));
}

void CFilteredTable::rowsPopulated()
{
	if (!m_bAutoResize)
		return;
	if (!m_bFirstPopulation)
		return;
	if (m_pProxy->rowCount() == 0)
		return;

	m_bFirstPopulation = false;
	ResizeColumnsToContents();
}

QString CFilteredTable::selectionAsText() const
{
	QItemSelectionModel const* const pSelection = m_pView->selectionModel();
	if (pSelection == nullptr)
		return QString();

	QModelIndexList vRows = pSelection->selectedRows(0);
	if (vRows.isEmpty())
		return QString();

	std::sort(vRows.begin(), vRows.end(), [](QModelIndex const& rLeft, QModelIndex const& rRight)
		{ return rLeft.row() < rRight.row(); });

	int const iColumnCount = m_pProxy->columnCount();

	QStringList vLines;
	vLines.reserve(vRows.size());

	for (QModelIndex const& rRow : vRows)
	{
		QStringList vCells;
		vCells.reserve(iColumnCount);

		for (int iColumn = 0; iColumn < iColumnCount; ++iColumn)
		{
			if (m_pView->isColumnHidden(iColumn))
				continue;

			QModelIndex const cellIndex = m_pProxy->index(rRow.row(), iColumn, rRow.parent());
			vCells.append(cellIndex.data(Qt::DisplayRole).toString());
		}

		vLines.append(vCells.join(QLatin1Char('\t')));
	}

	return vLines.join(QLatin1Char('\n'));
}
