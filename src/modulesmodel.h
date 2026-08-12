#pragma once

#include "moduledata.h"

#include <QAbstractTableModel>
#include <QModelIndex>
#include <QString>
#include <QVariant>

#include <cstddef>
#include <unordered_map>
#include <vector>

// Table model for the flattened list of every unique module in a tab's
// dependency closure. Unlike the symbol models it is populated incrementally:
// a row appears as soon as a module is discovered (with only its path known)
// and its metadata columns fill in when that module is parsed. UI thread only.
class CModulesModel : public QAbstractTableModel
{
	Q_OBJECT

public:
	enum class EColumn : int
	{
		Status = 0,
		Name = 1,
		Path = 2,
		Machine = 3,
		Class = 4,
		Type = 5,
		Size = 6,
		Soname = 7,
		EntryPoint = 8,
		Interpreter = 9,
		RunPath = 10,
		Stripped = 11,
		Count = 12,
	};

private:
	struct SModuleRow
	{
		std::size_t uModule = g_uInvalidIndex;
		SModuleInfo info;
		EModuleStatus eStatus = EModuleStatus::Unknown;
		bool bComplete = false;
		QString sError;
	};

private:
	std::vector<SModuleRow> m_vRows;
	std::unordered_map<std::size_t, int> m_mapModuleToRow;

public:
	explicit CModulesModel(QObject* const pParent = nullptr);
	~CModulesModel() override;

	void Reset();
	void AddModule(std::size_t const uModule, SModuleInfo const& rInfo, EModuleStatus const eStatus);
	void AddModules(std::vector<std::size_t> const& rvModules, std::vector<SModuleInfo> const& rvInfos);
	void UpdateModule(std::size_t const uModule, SModuleInfo const& rInfo);
	void SetModuleStatus(std::size_t const uModule, EModuleStatus const eStatus, QString const& rsError);

	int RowForModule(std::size_t const uModule) const;
	std::size_t ModuleAt(int const iRow) const;
	QString PathAt(int const iRow) const;
	SModuleInfo const* InfoAt(int const iRow) const;
	int ModuleCount() const;
	int MissingCount() const;

	int rowCount(QModelIndex const& rParent = QModelIndex()) const override;
	int columnCount(QModelIndex const& rParent = QModelIndex()) const override;
	QVariant data(QModelIndex const& rIndex, int iRole = Qt::DisplayRole) const override;
	QVariant headerData(int iSection, Qt::Orientation eOrientation, int iRole = Qt::DisplayRole) const override;
	Qt::ItemFlags flags(QModelIndex const& rIndex) const override;

private:
	QVariant sortKey(SModuleRow const& rRow, EColumn const eColumn) const;
	QString runPathText(SModuleInfo const& rInfo) const;
	QString rowTooltip(SModuleRow const& rRow) const;
};
