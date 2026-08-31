// Copyright (c) 2026 Jason Gripp
// Licensed under the MIT License.

#include "importsmodel.h"

#include "demangler.h"
#include "icons.h"
#include "moduledata.h"

#include <KLocalizedString>

#include <QColor>
#include <QFileInfo>
#include <QString>
#include <QVariant>

#include <cstddef>
#include <utility>
#include <vector>

CImportsModel::CImportsModel(QObject* pParent)
: QAbstractTableModel(pParent)
{
}

CImportsModel::~CImportsModel()
{
}

void CImportsModel::SetImports(std::size_t uModule, std::vector<SImportSymbol> vImports, bool bResolved)
{
	beginResetModel();

	m_uModule = uModule;
	m_vImports = std::move(vImports);
	m_bResolved = bResolved;

	endResetModel();
}

void CImportsModel::ApplyResolution(std::size_t uModule, std::vector<SImportSymbol> vImports)
{
	if (uModule != m_uModule)
		return;
	if (vImports.size() != m_vImports.size())
	{
		// Reset because the row count changed while the update was in flight.
		SetImports(uModule, std::move(vImports), true);
		return;
	}

	m_vImports = std::move(vImports);
	m_bResolved = true;

	if (m_vImports.empty())
		return;

	// dataChanged rather than a reset, so selection and scroll position survive.
	QModelIndex const topLeft = index(0, 0);
	QModelIndex const bottomRight = index(static_cast<int>(m_vImports.size()) - 1, static_cast<int>(EColumn::Count) - 1);
	Q_EMIT dataChanged(topLeft, bottomRight);
}

void CImportsModel::Clear()
{
	beginResetModel();

	m_vImports.clear();
	m_uModule = g_uInvalidIndex;
	m_bResolved = false;

	endResetModel();
}

void CImportsModel::SetDemangleEnabled(bool bEnabled)
{
	if (m_bDemangle == bEnabled)
		return;

	m_bDemangle = bEnabled;

	if (m_vImports.empty())
		return;

	QModelIndex const topLeft = index(0, static_cast<int>(EColumn::Symbol));
	QModelIndex const bottomRight = index(static_cast<int>(m_vImports.size()) - 1, static_cast<int>(EColumn::Symbol));
	Q_EMIT dataChanged(topLeft, bottomRight);
}

bool CImportsModel::IsDemangleEnabled() const
{
	return m_bDemangle;
}

std::size_t CImportsModel::ModuleIndex() const
{
	return m_uModule;
}

SImportSymbol const* CImportsModel::SymbolAt(int iRow) const
{
	if (iRow < 0 || static_cast<std::size_t>(iRow) >= m_vImports.size())
		return nullptr;

	return &m_vImports[static_cast<std::size_t>(iRow)];
}

QString CImportsModel::RawSymbolNameAt(int iRow) const
{
	SImportSymbol const* const pSymbol = SymbolAt(iRow);
	if (pSymbol == nullptr)
		return QString();

	return QString::fromStdString(pSymbol->sName);
}

QString CImportsModel::DisplaySymbolNameAt(int iRow) const
{
	SImportSymbol const* const pSymbol = SymbolAt(iRow);
	if (pSymbol == nullptr)
		return QString();

	return symbolText(*pSymbol);
}

std::size_t CImportsModel::ProviderModuleAt(int iRow) const
{
	SImportSymbol const* const pSymbol = SymbolAt(iRow);
	if (pSymbol == nullptr)
		return g_uInvalidIndex;

	return pSymbol->uProviderModule;
}

void CImportsModel::QueryCounts(std::size_t& ruOutTotal, std::size_t& ruOutUnresolved, std::size_t& ruOutWeak) const
{
	ruOutTotal = m_vImports.size();
	ruOutUnresolved = 0;
	ruOutWeak = 0;

	for (SImportSymbol const& rSymbol : m_vImports)
	{
		if (rSymbol.eStatus == ESymbolStatus::Unresolved)
			++ruOutUnresolved;
		else if (rSymbol.eStatus == ESymbolStatus::WeakUnresolved)
			++ruOutWeak;
	}
}

int CImportsModel::rowCount(QModelIndex const& rParent) const
{
	if (rParent.isValid())
		return 0;

	return static_cast<int>(m_vImports.size());
}

int CImportsModel::columnCount(QModelIndex const& rParent) const
{
	if (rParent.isValid())
		return 0;

	return static_cast<int>(EColumn::Count);
}

QVariant CImportsModel::data(QModelIndex const& rIndex, int iRole) const
{
	if (!rIndex.isValid())
		return QVariant();
	if (rIndex.row() < 0 || static_cast<std::size_t>(rIndex.row()) >= m_vImports.size())
		return QVariant();

	SImportSymbol const& rSymbol = m_vImports[static_cast<std::size_t>(rIndex.row())];
	EColumn const eColumn = static_cast<EColumn>(rIndex.column());

	switch (iRole)
	{
	case Qt::DisplayRole:
		{
			switch (eColumn)
			{
			case EColumn::Status: return QString();
			case EColumn::Symbol: return symbolText(rSymbol);
			case EColumn::Version: return QString::fromStdString(rSymbol.sVersion);
			case EColumn::Type: return SymbolTypeText(rSymbol.eType);
			case EColumn::Binding: return SymbolBindingText(rSymbol.eBinding);
			case EColumn::Provider: return providerText(rSymbol);
			default: break;
			}

			return QVariant();
		}

	case Qt::DecorationRole:
		{
			if (eColumn != EColumn::Status)
				return QVariant();
			return SymbolStatusIcon(rSymbol.eStatus);
		}

	case Qt::ForegroundRole:
		{
			QColor const color = SymbolStatusColor(rSymbol.eStatus);
			if (!color.isValid())
				return QVariant();
			return color;
		}

	case Qt::ToolTipRole:
		{
			if (eColumn == EColumn::Status)
				return SymbolStatusTooltip(rSymbol.eStatus);
			if (eColumn == EColumn::Symbol && m_bDemangle)
				return QString::fromStdString(rSymbol.sName);
			if (eColumn == EColumn::Provider)
				return QString::fromStdString(rSymbol.sProviderPath);
			if (eColumn == EColumn::Version && !rSymbol.sVersionFile.empty())
			{
				return i18n("Required from %1", QString::fromStdString(rSymbol.sVersionFile));
			}

			return QVariant();
		}

	case Qt::TextAlignmentRole:
		return QVariant(static_cast<int>(Qt::AlignLeft | Qt::AlignVCenter));

	case Qt::UserRole:
		return sortKey(rSymbol, eColumn);

	default:
		break;
	}

	return QVariant();
}

QVariant CImportsModel::headerData(int iSection, Qt::Orientation eOrientation, int iRole) const
{
	if (eOrientation != Qt::Horizontal)
		return QVariant();

	// The status heading is an icon, so its tooltip also names the column in the
	// column chooser.
	if (iRole == Qt::ToolTipRole)
	{
		if (static_cast<EColumn>(iSection) == EColumn::Status)
			return i18n("Import status");

		return QVariant();
	}

	if (iRole != Qt::DisplayRole)
		return QVariant();

	switch (static_cast<EColumn>(iSection))
	{
	case EColumn::Status: return i18n("PI");
	case EColumn::Symbol: return i18n("Symbol");
	case EColumn::Version: return i18n("Version");
	case EColumn::Type: return i18n("Type");
	case EColumn::Binding: return i18n("Binding");
	case EColumn::Provider: return i18n("Provider");
	default: break;
	}

	return QVariant();
}

Qt::ItemFlags CImportsModel::flags(QModelIndex const& rIndex) const
{
	if (!rIndex.isValid())
		return Qt::NoItemFlags;

	return Qt::ItemIsEnabled | Qt::ItemIsSelectable;
}

QString CImportsModel::symbolText(SImportSymbol const& rSymbol) const
{
	return QString::fromStdString(CDemangler::Instance().DemangleIf(rSymbol.sName, m_bDemangle));
}

QString CImportsModel::providerText(SImportSymbol const& rSymbol) const
{
	if (!rSymbol.sProviderPath.empty())
		return QFileInfo(QString::fromStdString(rSymbol.sProviderPath)).fileName();
	if (m_bResolved)
		return i18n("(unresolved)");

	return QString();
}

QVariant CImportsModel::sortKey(SImportSymbol const& rSymbol, EColumn eColumn) const
{
	switch (eColumn)
	{
	case EColumn::Status: return static_cast<int>(rSymbol.eStatus);
	case EColumn::Symbol: return symbolText(rSymbol);
	case EColumn::Version: return QString::fromStdString(rSymbol.sVersion);
	case EColumn::Type: return static_cast<int>(rSymbol.eType);
	case EColumn::Binding: return static_cast<int>(rSymbol.eBinding);
	case EColumn::Provider: return providerText(rSymbol);
	default: break;
	}

	return QVariant();
}
