#pragma once

#include <QList>
#include <QModelIndex>
#include <QModelIndexList>
#include <QString>
#include <QWidget>

class QAbstractItemModel;
class QEvent;
class QLabel;
class QLineEdit;
class QMenu;
class QObject;
class QPoint;
class QResizeEvent;
class QShortcut;
class QSortFilterProxyModel;
class QTableView;
class QToolButton;

// The reusable panel used by the imports, exports and flattened-modules views:
// a table with a hidden-by-default substring filter box beneath it, plus the
// table behavior all three share (copy, select-all, sorting, per-column
// show/hide, context menu).
//
// All coordinates crossing this class' boundary are source coordinates; proxy
// indices never leak to owners.
class CFilteredTable : public QWidget
{
	Q_OBJECT

private:
	QTableView* m_pView = nullptr;
	QSortFilterProxyModel* m_pProxy = nullptr;
	QWidget* m_pFilterRow = nullptr;
	QLineEdit* m_pFilterEdit = nullptr;
	QLabel* m_pFilterLabel = nullptr;
	QToolButton* m_pFilterCloseButton = nullptr;
	QShortcut* m_pCloseFilterShortcut = nullptr;
	int m_iFilterColumn = -1;
	int m_iStretchColumn = -1;
	// The width the stretch column wants, kept apart from its actual width so a
	// run of shrinks down to the floor does not lose the excess and come back
	// too wide when the window grows again.
	int m_iStretchTargetWidth = -1;
	bool m_bAutoResize = true;
	bool m_bFirstPopulation = true;
	bool m_bAdjustingStretch = false;

public:
	explicit CFilteredTable(QWidget* pParent = nullptr);
	~CFilteredTable() override;

	bool eventFilter(QObject* pWatched, QEvent* pEvent) override;

	void SetSourceModel(QAbstractItemModel* pModel);
	QAbstractItemModel* SourceModel() const;
	QTableView* View() const;
	QSortFilterProxyModel* Proxy() const;

	void SetFilterColumn(int iColumn);
	void SetFilterPlaceholder(QString const& rsText);
	void SetFilterLabel(QString const& rsText);
	QString FilterText() const;
	void SetFilterText(QString const& rsText);
	void ClearFilter();
	void FocusFilter();

	bool IsFilterVisible() const;
	void SetFilterVisible(bool bVisible);
	void ShowFilter();

	int VisibleRowCount() const;
	QModelIndexList SelectedSourceIndexes() const;
	QModelIndexList SelectedSourceRows() const;
	QModelIndex CurrentSourceIndex() const;

	void CopySelection() const;
	void SelectAllRows();
	void ResizeColumnsToContents();
	void SetAutoResizeColumns(bool bEnabled);
	void SetStretchColumn(int iColumn);

	bool IsColumnVisible(int iColumn) const;
	void SetColumnVisible(int iColumn, bool bVisible);
	void ShowAllColumns();
	// Source column indexes, so a stored set survives a change to the order the
	// user has dragged the header sections into.
	QList<int> HiddenColumns() const;
	void SetHiddenColumns(QList<int> const& rvColumns);

Q_SIGNALS:

	void ContextMenuRequested(QMenu* pMenu, QModelIndex const& rSourceIndex);
	// Emitted only for a change the user made here; restoring a stored set is
	// silent, so pushing one panel's columns to another cannot echo back.
	void ColumnVisibilityChanged();
	void SelectionChanged(QModelIndex const& rSourceIndex);
	void Activated(QModelIndex const& rSourceIndex);
	void FilterChanged(QString const& rsText);

private Q_SLOTS:

	void filterTextChanged(QString const& rsText);
	void showContextMenu(QPoint const& rPoint);
	void showHeaderContextMenu(QPoint const& rPoint);
	void currentRowChanged(QModelIndex const& rCurrent, QModelIndex const& rPrevious);
	void rowsPopulated();
	void sectionResized(int iLogicalIndex, int iOldSize, int iNewSize);

private:
	void buildUi();
	void buildShortcuts();
	QString selectionAsText() const;
	void absorbViewportResize(QResizeEvent const* pEvent);
	void fillStretchColumn();
	void applyStretchTarget();
	void updateStretchFallback();
	int columnCount() const;
	int visibleColumnCount() const;
	void applyColumnHidden(int iColumn, bool bHidden);
	QString columnTitle(int iColumn) const;
	void populateColumnsMenu(QMenu* pMenu);
};
