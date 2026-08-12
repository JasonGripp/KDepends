#pragma once

#include <QModelIndex>
#include <QModelIndexList>
#include <QString>
#include <QWidget>

class QAbstractItemModel;
class QLabel;
class QLineEdit;
class QMenu;
class QPoint;
class QShortcut;
class QSortFilterProxyModel;
class QTableView;
class QToolButton;

// The reusable panel used by the imports, exports and flattened-modules views:
// a table with a hidden-by-default substring filter box beneath it, plus the
// table behavior all three share (copy, select-all, sorting, context menu).
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
	bool m_bAutoResize = true;
	bool m_bFirstPopulation = true;

public:
	explicit CFilteredTable(QWidget* pParent = nullptr);
	~CFilteredTable() override;

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

Q_SIGNALS:

	void ContextMenuRequested(QMenu* pMenu, QModelIndex const& rSourceIndex);
	void SelectionChanged(QModelIndex const& rSourceIndex);
	void Activated(QModelIndex const& rSourceIndex);
	void FilterChanged(QString const& rsText);

private Q_SLOTS:

	void filterTextChanged(QString const& rsText);
	void showContextMenu(QPoint const& rPoint);
	void currentRowChanged(QModelIndex const& rCurrent, QModelIndex const& rPrevious);
	void rowsPopulated();

private:
	void buildUi();
	void buildShortcuts();
	QString selectionAsText() const;
};
