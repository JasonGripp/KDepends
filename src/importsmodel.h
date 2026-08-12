// Copyright (c) 2026 Jason Gripp
// Licensed under the MIT License.

#pragma once

#include "moduledata.h"

#include <QAbstractTableModel>
#include <QModelIndex>
#include <QString>
#include <QVariant>

#include <cstddef>
#include <vector>

// Table model for the imports of the module currently selected in a tab. Holds
// its own copy of the import list and re-renders when the demangle toggle
// flips or when resolution results arrive. UI thread only.
class CImportsModel : public QAbstractTableModel
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
		Provider = 5,
		Count = 6,
	};

private:
	std::vector<SImportSymbol> m_vImports;
	std::size_t m_uModule = g_uInvalidIndex;
	bool m_bDemangle = false;
	bool m_bResolved = false;

public:
	explicit CImportsModel(QObject* pParent = nullptr);
	~CImportsModel() override;

	void SetImports(std::size_t uModule, std::vector<SImportSymbol> vImports, bool bResolved);
	void ApplyResolution(std::size_t uModule, std::vector<SImportSymbol> vImports);
	void Clear();

	void SetDemangleEnabled(bool bEnabled);
	bool IsDemangleEnabled() const;

	std::size_t ModuleIndex() const;
	SImportSymbol const* SymbolAt(int iRow) const;
	QString RawSymbolNameAt(int iRow) const;
	QString DisplaySymbolNameAt(int iRow) const;
	std::size_t ProviderModuleAt(int iRow) const;
	void QueryCounts(std::size_t& ruOutTotal, std::size_t& ruOutUnresolved, std::size_t& ruOutWeak) const;

	int rowCount(QModelIndex const& rParent = QModelIndex()) const override;
	int columnCount(QModelIndex const& rParent = QModelIndex()) const override;
	QVariant data(QModelIndex const& rIndex, int iRole = Qt::DisplayRole) const override;
	QVariant headerData(int iSection, Qt::Orientation eOrientation, int iRole = Qt::DisplayRole) const override;
	Qt::ItemFlags flags(QModelIndex const& rIndex) const override;

private:
	QString symbolText(SImportSymbol const& rSymbol) const;
	QString providerText(SImportSymbol const& rSymbol) const;
	QVariant sortKey(SImportSymbol const& rSymbol, EColumn eColumn) const;
};
