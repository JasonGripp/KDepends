// Copyright (c) 2026 Jason Gripp
// Licensed under the MIT License.

#include "filteredtable.h"

#include <KLocalizedString>

#include <QAbstractItemModel>
#include <QAbstractItemView>
#include <QAbstractSlider>
#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QEvent>
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
#include <QObject>
#include <QPoint>
#include <QRegularExpression>
#include <QResizeEvent>
#include <QScrollBar>
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

// Floor for the stretch column, so squeezing the panel leaves it readable and
// hands the overflow to a horizontal scrollbar instead.
constexpr int g_iMinimumStretchColumnWidth = 80;
}

CFilteredTable::CFilteredTable(QWidget* pParent)
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
	// Choose a deterministic initial order instead of relying on Qt's default.
	m_pView->sortByColumn(0, Qt::AscendingOrder);
	m_pView->setAlternatingRowColors(true);
	m_pView->setWordWrap(false);
	// Per-pixel scrolling keeps wide symbols and paths readable.
	m_pView->setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);
	m_pView->setContextMenuPolicy(Qt::CustomContextMenu);
	m_pView->verticalHeader()->setVisible(false);
	m_pView->verticalHeader()->setDefaultSectionSize(QFontMetrics(m_pView->font()).height() + 6);
	m_pView->horizontalHeader()->setSectionsMovable(true);
	m_pView->horizontalHeader()->setStretchLastSection(true);
	// The header is where users look for a column chooser, so it gets the same
	// menu the cell menu carries as a submenu.
	m_pView->horizontalHeader()->setContextMenuPolicy(Qt::CustomContextMenu);

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
	connect(m_pView->horizontalHeader(), &QWidget::customContextMenuRequested, this, &CFilteredTable::showHeaderContextMenu);
	connect(m_pView->selectionModel(), &QItemSelectionModel::currentRowChanged, this, &CFilteredTable::currentRowChanged);

	connect(m_pView, &QAbstractItemView::activated, this, [this](QModelIndex const& rIndex)
		{ Q_EMIT Activated(m_pProxy->mapToSource(rIndex)); });
	connect(m_pView->horizontalHeader(), &QHeaderView::sectionResized, this, &CFilteredTable::sectionResized);
	// The view recomputes the pixel-mode step from the header's default section
	// size whenever its geometry changes, which puts the column-sized jump
	// straight back. Reapplying it on every range change preserves pixel steps.
	connect(m_pView->horizontalScrollBar(), &QAbstractSlider::rangeChanged, this, &CFilteredTable::horizontalScrollRangeChanged);
	horizontalScrollRangeChanged();

	// Track the viewport because its width already accounts for a vertical
	// scrollbar appearing or disappearing.
	m_pView->viewport()->installEventFilter(this);
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

	// The window-level Find action routes Ctrl+F to the focused panel.
	// Escape covers the panel but is disabled while the filter is hidden.
	m_pCloseFilterShortcut = new QShortcut(QKeySequence(Qt::Key_Escape), this);
	m_pCloseFilterShortcut->setContext(Qt::WidgetWithChildrenShortcut);
	m_pCloseFilterShortcut->setEnabled(false);
	connect(m_pCloseFilterShortcut, &QShortcut::activated, this, [this]()
		{ SetFilterVisible(false); });
}

bool CFilteredTable::eventFilter(QObject* pWatched, QEvent* pEvent)
{
	if (pWatched == m_pView->viewport() && pEvent->type() == QEvent::Resize)
		absorbViewportResize(static_cast<QResizeEvent const*>(pEvent));

	return QWidget::eventFilter(pWatched, pEvent);
}

void CFilteredTable::SetSourceModel(QAbstractItemModel* pModel)
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

void CFilteredTable::SetFilterColumn(int iColumn)
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

void CFilteredTable::SetFilterVisible(bool bVisible)
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
	// This is not a toggle. On an already-open filter it puts the
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

	for (int iColumn = 0; iColumn < columnCount(); ++iColumn)
	{
		// The stretch column is sized from what the others leave over, so the
		// cap would only fight with that.
		if (iColumn == m_iStretchColumn)
			continue;
		if (m_pView->columnWidth(iColumn) > iMaxWidth)
			m_pView->setColumnWidth(iColumn, iMaxWidth);
	}

	fillStretchColumn();
}

void CFilteredTable::SetAutoResizeColumns(bool bEnabled)
{
	m_bAutoResize = bEnabled;
}

void CFilteredTable::SetStretchColumn(int iColumn)
{
	m_iStretchColumn = iColumn;
	m_iStretchTargetWidth = -1;

	updateStretchFallback();
	fillStretchColumn();
}

void CFilteredTable::SetStretchLastColumn(bool bEnabled)
{
	m_bStretchLastColumn = bEnabled;

	updateStretchFallback();
}

bool CFilteredTable::IsColumnVisible(int iColumn) const
{
	if (iColumn < 0 || iColumn >= columnCount())
		return false;

	return !m_pView->isColumnHidden(iColumn);
}

void CFilteredTable::SetColumnVisible(int iColumn, bool bVisible)
{
	if (iColumn < 0 || iColumn >= columnCount())
		return;
	if (m_pView->isColumnHidden(iColumn) != bVisible)
		return;
	// An all-hidden table shows nothing to right-click on, so there would be no
	// way back. Keep the last visible column.
	if (!bVisible && visibleColumnCount() <= 1)
		return;

	applyColumnHidden(iColumn, !bVisible);

	Q_EMIT ColumnVisibilityChanged();
}

void CFilteredTable::ShowAllColumns()
{
	if (visibleColumnCount() == columnCount())
		return;

	for (int iColumn = 0; iColumn < columnCount(); ++iColumn)
	{
		applyColumnHidden(iColumn, false);
	}

	Q_EMIT ColumnVisibilityChanged();
}

QList<int> CFilteredTable::HiddenColumns() const
{
	QList<int> vHidden;

	for (int iColumn = 0; iColumn < columnCount(); ++iColumn)
	{
		if (m_pView->isColumnHidden(iColumn))
			vHidden.append(iColumn);
	}

	return vHidden;
}

void CFilteredTable::SetHiddenColumns(QList<int> const& rvColumns)
{
	int const iColumnCount = columnCount();

	for (int iColumn = 0; iColumn < iColumnCount; ++iColumn)
	{
		bool const bHide = rvColumns.contains(iColumn);

		// Preserve one visible column even when the stored configuration was edited.
		if (bHide && !m_pView->isColumnHidden(iColumn) && visibleColumnCount() <= 1)
			continue;

		applyColumnHidden(iColumn, bHide);
	}
}

QList<int> CFilteredTable::ColumnWidths() const
{
	int const iColumnCount = columnCount();

	QList<int> vWidths;
	vWidths.reserve(iColumnCount);

	for (int iColumn = 0; iColumn < iColumnCount; ++iColumn)
	{
		// A hidden section's size reads as zero. The header keeps its real width
		// internally for when it comes back, and the hidden set is stored
		// separately, so there is nothing to preserve here.
		vWidths.append(m_pView->columnWidth(iColumn));
	}

	return vWidths;
}

void CFilteredTable::SetColumnWidths(QList<int> const& rvWidths)
{
	if (rvWidths.isEmpty())
		return;

	int const iColumnCount = columnCount();

	for (int iColumn = 0; iColumn < iColumnCount; ++iColumn)
	{
		// Past the end of the stored list is a column that did not exist when it
		// was written, and zero represents a hidden column. Both keep the width the
		// view gives them.
		int const iWidth = iColumn < rvWidths.size() ? rvWidths.at(iColumn) : 0;
		if (iWidth <= 0)
			continue;

		m_pView->setColumnWidth(iColumn, iWidth);
	}

	// Do not let the first-population auto-fit overwrite stored widths. The
	// viewport resize will fit the stretch column around the restored widths.
	m_bFirstPopulation = false;
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

	populateColumnsMenu(menu.addMenu(i18n("Colu&mns")));

	menu.addSeparator();

	Q_EMIT ContextMenuRequested(&menu, sourceIndex);

	menu.exec(m_pView->viewport()->mapToGlobal(rPoint));
}

void CFilteredTable::showHeaderContextMenu(QPoint const& rPoint)
{
	QMenu menu(this);
	populateColumnsMenu(&menu);

	menu.exec(m_pView->horizontalHeader()->mapToGlobal(rPoint));
}

void CFilteredTable::populateColumnsMenu(QMenu* pMenu)
{
	if (pMenu == nullptr)
		return;

	int const iColumnCount = columnCount();
	int const iVisible = visibleColumnCount();

	for (int iColumn = 0; iColumn < iColumnCount; ++iColumn)
	{
		bool const bVisible = !m_pView->isColumnHidden(iColumn);

		QAction* const pAction = pMenu->addAction(columnTitle(iColumn));
		pAction->setCheckable(true);
		pAction->setChecked(bVisible);
		// Greyed out rather than silently ignored, so the one column that cannot
		// be switched off says so.
		pAction->setEnabled(!bVisible || iVisible > 1);
		connect(pAction, &QAction::toggled, this, [this, iColumn](bool bChecked)
			{ SetColumnVisible(iColumn, bChecked); });
	}

	pMenu->addSeparator();

	QAction* const pShowAll = pMenu->addAction(i18n("Show All Columns"));
	pShowAll->setEnabled(iVisible < iColumnCount);
	connect(pShowAll, &QAction::triggered, this, &CFilteredTable::ShowAllColumns);

	QAction* const pResize = pMenu->addAction(i18n("Fit Columns to Contents"));
	connect(pResize, &QAction::triggered, this, &CFilteredTable::ResizeColumnsToContents);
}

int CFilteredTable::columnCount() const
{
	// Use the source model's count because a stored column set is applied
	// while the table is still empty, and the proxy has no column mapping to
	// report until it has rows. Columns map one to one, so the count is the same
	// once there are any.
	QAbstractItemModel const* const pModel = m_pProxy->sourceModel();
	if (pModel == nullptr)
		return 0;

	return pModel->columnCount();
}

int CFilteredTable::visibleColumnCount() const
{
	int iVisible = 0;

	for (int iColumn = 0; iColumn < columnCount(); ++iColumn)
	{
		if (!m_pView->isColumnHidden(iColumn))
			++iVisible;
	}

	return iVisible;
}

void CFilteredTable::applyColumnHidden(int iColumn, bool bHidden)
{
	if (m_pView->isColumnHidden(iColumn) != bHidden)
		m_pView->setColumnHidden(iColumn, bHidden);

	// A column that was hidden before the view ever sized it comes back at zero
	// width, which reads as still hidden.
	if (!bHidden && m_pView->columnWidth(iColumn) <= 0)
		m_pView->resizeColumnToContents(iColumn);

	updateStretchFallback();
	fillStretchColumn();
}

QString CFilteredTable::columnTitle(int iColumn) const
{
	// The header text of an icon-only column is a single letter or empty, which
	// names nothing in a menu. Those columns carry a readable name in their
	// header tooltip, so it wins where there is one.
	QString const sToolTip = m_pProxy->headerData(iColumn, Qt::Horizontal, Qt::ToolTipRole).toString();
	if (!sToolTip.isEmpty())
		return sToolTip;

	QString const sTitle = m_pProxy->headerData(iColumn, Qt::Horizontal, Qt::DisplayRole).toString();
	if (!sTitle.isEmpty())
		return sTitle;

	return i18nc("name for a table column with no header text", "Column %1", iColumn + 1);
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

void CFilteredTable::horizontalScrollRangeChanged()
{
	// A text-height step, so a wheel notch or an arrow click moves a readable
	// amount and follows the font's DPI the way the row height does.
	m_pView->horizontalScrollBar()->setSingleStep(QFontMetrics(m_pView->font()).height());
}

void CFilteredTable::sectionResized(int iLogicalIndex, int iOldSize, int iNewSize)
{
	Q_UNUSED(iOldSize)

	if (iLogicalIndex != m_iStretchColumn)
		return;
	// Only a resize we did not cause is the user stating a new preferred width.
	if (m_bAdjustingStretch)
		return;

	m_iStretchTargetWidth = iNewSize;
}

void CFilteredTable::absorbViewportResize(QResizeEvent const* pEvent)
{
	if (m_iStretchColumn < 0)
		return;
	if (m_bAdjustingStretch)
		return;
	if (m_pView->isColumnHidden(m_iStretchColumn))
		return;

	int const iOldWidth = pEvent->oldSize().width();
	int const iNewWidth = pEvent->size().width();

	// The first layout has no previous width to grow from, so fit the column to
	// what is left instead of shifting it by a meaningless delta.
	if (iOldWidth <= 0)
	{
		fillStretchColumn();
		return;
	}

	int const iDelta = iNewWidth - iOldWidth;
	if (iDelta == 0)
		return;

	if (m_iStretchTargetWidth < 0)
		m_iStretchTargetWidth = m_pView->columnWidth(m_iStretchColumn);

	m_iStretchTargetWidth += iDelta;
	applyStretchTarget();
}

void CFilteredTable::fillStretchColumn()
{
	if (m_iStretchColumn < 0)
		return;
	if (m_pView->isColumnHidden(m_iStretchColumn))
		return;

	int iUsed = 0;

	for (int iColumn = 0; iColumn < columnCount(); ++iColumn)
	{
		if (iColumn == m_iStretchColumn)
			continue;
		if (m_pView->isColumnHidden(iColumn))
			continue;

		iUsed += m_pView->columnWidth(iColumn);
	}

	m_iStretchTargetWidth = m_pView->viewport()->width() - iUsed;
	applyStretchTarget();
}

void CFilteredTable::applyStretchTarget()
{
	int const iWidth = qMax(g_iMinimumStretchColumnWidth, m_iStretchTargetWidth);

	// Widening the column can raise a horizontal scrollbar, which resizes the
	// viewport again. The guard prevents recursive resize handling.
	m_bAdjustingStretch = true;
	m_pView->setColumnWidth(m_iStretchColumn, iWidth);
	m_bAdjustingStretch = false;
}

void CFilteredTable::updateStretchFallback()
{
	// Enable Qt's last-section fallback only when no designated stretch column
	// is visible and the panel has not opted out of stretching.
	bool const bStretchColumnUsable = m_iStretchColumn >= 0 && !m_pView->isColumnHidden(m_iStretchColumn);

	m_pView->horizontalHeader()->setStretchLastSection(m_bStretchLastColumn && !bStretchColumnUsable);
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

	int const iColumnCount = columnCount();

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
