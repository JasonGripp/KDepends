#pragma once

#include "elfstructs.h"
#include "moduledata.h"

#include <QAbstractTableModel>
#include <QModelIndex>
#include <QString>
#include <QVariant>

#include <cstddef>
#include <vector>

// Table model for the exported (defined) dynamic symbols of the module
// currently selected in a tab. Structurally the twin of CImportsModel, but
// exports are definitions rather than resolutions: there is no provider and no
// resolution step. UI thread only.
class CExportsModel : public QAbstractTableModel
{
	Q_OBJECT

public:
	enum class EColumn : int
	{
		Status = 0,
		Symbol = 1,
		Version = 2,
		Type = 3,
		Binding = 4,
		Visibility = 5,
		Address = 6,
		Count = 7,
	};

private:
	std::vector<SExportSymbol> m_vExports;
	std::size_t m_uModule = g_uInvalidIndex;
	bool m_bDemangle = false;
	bool m_b64Bit = true;

public:
	explicit CExportsModel(QObject* const pParent = nullptr);
	~CExportsModel() override;

	void SetExports(std::size_t const uModule, std::vector<SExportSymbol> vExports, EElfClass const eClass);
	void Clear();

	void SetDemangleEnabled(bool const bEnabled);
	bool IsDemangleEnabled() const;

	std::size_t ModuleIndex() const;
	SExportSymbol const* SymbolAt(int const iRow) const;
	QString RawSymbolNameAt(int const iRow) const;
	QString DisplaySymbolNameAt(int const iRow) const;
	std::size_t ExportCount() const;

	int rowCount(QModelIndex const& rParent = QModelIndex()) const override;
	int columnCount(QModelIndex const& rParent = QModelIndex()) const override;
	QVariant data(QModelIndex const& rIndex, int iRole = Qt::DisplayRole) const override;
	QVariant headerData(int iSection, Qt::Orientation eOrientation, int iRole = Qt::DisplayRole) const override;
	Qt::ItemFlags flags(QModelIndex const& rIndex) const override;

private:
	QString symbolText(SExportSymbol const& rSymbol) const;
	QString versionText(SExportSymbol const& rSymbol) const;
	QString addressText(SExportSymbol const& rSymbol) const;
	QVariant sortKey(SExportSymbol const& rSymbol, EColumn const eColumn) const;
};
