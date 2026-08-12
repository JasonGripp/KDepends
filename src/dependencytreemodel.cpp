// Copyright (c) 2026 Jason Gripp
// Licensed under the MIT License.

#include "dependencytreemodel.h"

#include "analysisengine.h"
#include "icons.h"
#include "moduledata.h"

#include <KLocalizedString>

#include <QColor>
#include <QFont>
#include <QLatin1String>
#include <QStringList>
#include <QVariant>

#include <algorithm>
#include <cstddef>
#include <vector>

CDependencyTreeModel::CDependencyTreeModel(QObject* pParent)
: QAbstractItemModel(pParent)
{
}

CDependencyTreeModel::~CDependencyTreeModel()
{
}

void CDependencyTreeModel::Reset()
{
	beginResetModel();

	m_vNodes.clear();
	m_bHasRoot = false;
	m_sRootPath.clear();

	endResetModel();
}

void CDependencyTreeModel::ApplyRoot(SRootResult const& rResult)
{
	beginResetModel();

	m_vNodes.clear();
	m_sRootPath = QString::fromStdString(rResult.sRootPath);

	m_vNodes.resize(1);

	SMirrorNode& rRoot = m_vNodes[g_uRootNodeIndex];
	rRoot.uParent = g_uInvalidIndex;
	rRoot.uModule = g_uRootNodeIndex;
	rRoot.uRowInParent = 0;
	rRoot.sResolvedPath = m_sRootPath;
	rRoot.sSoname = QString::fromStdString(rResult.info.sSoname);
	rRoot.eStatus = EModuleStatus::Root;
	rRoot.bLeaf = rResult.info.vNeeded.empty();
	rRoot.bExpanded = true;
	rRoot.bRequested = true;
	rRoot.bValid = true;

	m_bHasRoot = true;

	appendChildren(g_uRootNodeIndex, rResult.vChildren);

	endResetModel();

	Q_EMIT RootApplied();
}

void CDependencyTreeModel::ApplyExpansion(SExpandOutcome const& rOutcome)
{
	std::size_t const uNode = rOutcome.uNode;

	if (uNode >= m_vNodes.size())
		return;
	if (!m_vNodes[uNode].bValid)
		return;
	if (m_vNodes[uNode].bExpanded)
		return;

	if (!rOutcome.bSuccess)
	{
		SMirrorNode& rNode = m_vNodes[uNode];

		if (rNode.eStatus != EModuleStatus::Root)
			rNode.eStatus = EModuleStatus::Error;
		rNode.sError = QString::fromStdString(rOutcome.sError);
		rNode.bLeaf = true;
		rNode.bExpanded = true;

		QModelIndex const nodeIndex = IndexForNode(uNode);
		if (nodeIndex.isValid())
			Q_EMIT dataChanged(nodeIndex, nodeIndex);

		return;
	}

	QModelIndex const parentIndex = IndexForNode(uNode);
	int const iFirst = static_cast<int>(m_vNodes[uNode].vChildren.size());
	int const iLast = iFirst + static_cast<int>(rOutcome.vChildren.size()) - 1;

	if (iLast >= iFirst)
	{
		beginInsertRows(parentIndex, iFirst, iLast);
		appendChildren(uNode, rOutcome.vChildren);
		m_vNodes[uNode].bExpanded = true;
		endInsertRows();
	}
	else
	{
		m_vNodes[uNode].bExpanded = true;
		m_vNodes[uNode].bLeaf = true;
	}

	if (parentIndex.isValid())
		Q_EMIT dataChanged(parentIndex, parentIndex);
}

void CDependencyTreeModel::ApplyModuleUpdate(std::size_t uModule, SModuleInfo const& rInfo)
{
	QString const sSoname = QString::fromStdString(rInfo.sSoname);

	for (std::size_t uIndex = 0; uIndex < m_vNodes.size(); ++uIndex)
	{
		SMirrorNode& rNode = m_vNodes[uIndex];
		if (!rNode.bValid)
			continue;
		if (rNode.uModule != uModule)
			continue;

		rNode.sSoname = sSoname;

		// A resolved module with no needed names will never gain children.
		if (rNode.eStatus == EModuleStatus::Resolved && rInfo.vNeeded.empty() && !rNode.bExpanded)
			rNode.bLeaf = true;

		QModelIndex const nodeIndex = IndexForNode(uIndex);
		if (nodeIndex.isValid())
			Q_EMIT dataChanged(nodeIndex, nodeIndex);
	}
}

std::size_t CDependencyTreeModel::NodeAt(QModelIndex const& rIndex) const
{
	return nodeFromIndex(rIndex);
}

std::size_t CDependencyTreeModel::ModuleAt(QModelIndex const& rIndex) const
{
	std::size_t const uNode = nodeFromIndex(rIndex);
	if (uNode == g_uInvalidIndex)
		return g_uInvalidIndex;

	return m_vNodes[uNode].uModule;
}

QString CDependencyTreeModel::PathAt(QModelIndex const& rIndex) const
{
	std::size_t const uNode = nodeFromIndex(rIndex);
	if (uNode == g_uInvalidIndex)
		return QString();

	return m_vNodes[uNode].sResolvedPath;
}

QString CDependencyTreeModel::NeededNameAt(QModelIndex const& rIndex) const
{
	std::size_t const uNode = nodeFromIndex(rIndex);
	if (uNode == g_uInvalidIndex)
		return QString();

	return m_vNodes[uNode].sNeededName;
}

QModelIndex CDependencyTreeModel::IndexForNode(std::size_t uNode) const
{
	if (uNode >= m_vNodes.size())
		return QModelIndex();
	if (!m_vNodes[uNode].bValid)
		return QModelIndex();

	return createIndex(m_vNodes[uNode].uRowInParent, 0, static_cast<quintptr>(uNode));
}

QModelIndex CDependencyTreeModel::FirstIndexForModule(std::size_t uModule) const
{
	if (uModule == g_uInvalidIndex)
		return QModelIndex();

	for (std::size_t uIndex = 0; uIndex < m_vNodes.size(); ++uIndex)
	{
		SMirrorNode const& rNode = m_vNodes[uIndex];
		if (!rNode.bValid)
			continue;
		if (rNode.uModule != uModule)
			continue;
		if (rNode.eStatus == EModuleStatus::Duplicate)
			continue;

		return IndexForNode(uIndex);
	}

	return QModelIndex();
}

bool CDependencyTreeModel::HasRoot() const
{
	return m_bHasRoot;
}

std::size_t CDependencyTreeModel::NodeCount() const
{
	return m_vNodes.size();
}

QModelIndex CDependencyTreeModel::index(int iRow, int iColumn, QModelIndex const& rParent) const
{
	if (!hasIndex(iRow, iColumn, rParent))
		return QModelIndex();

	if (!rParent.isValid())
	{
		if (!m_bHasRoot || iRow != 0)
			return QModelIndex();
		return createIndex(0, iColumn, static_cast<quintptr>(g_uRootNodeIndex));
	}

	std::size_t const uParent = nodeFromIndex(rParent);
	if (uParent == g_uInvalidIndex)
		return QModelIndex();

	std::vector<std::size_t> const& rvChildren = m_vNodes[uParent].vChildren;
	if (iRow < 0 || static_cast<std::size_t>(iRow) >= rvChildren.size())
		return QModelIndex();

	return createIndex(iRow, iColumn, static_cast<quintptr>(rvChildren[iRow]));
}

QModelIndex CDependencyTreeModel::parent(QModelIndex const& rIndex) const
{
	std::size_t const uNode = nodeFromIndex(rIndex);
	if (uNode == g_uInvalidIndex)
		return QModelIndex();

	std::size_t const uParent = m_vNodes[uNode].uParent;
	if (uParent == g_uInvalidIndex)
		return QModelIndex();
	if (uParent >= m_vNodes.size())
		return QModelIndex();

	return createIndex(m_vNodes[uParent].uRowInParent, 0, static_cast<quintptr>(uParent));
}

int CDependencyTreeModel::rowCount(QModelIndex const& rParent) const
{
	if (!rParent.isValid())
		return m_bHasRoot ? 1 : 0;
	if (rParent.column() != 0)
		return 0;

	std::size_t const uNode = nodeFromIndex(rParent);
	if (uNode == g_uInvalidIndex)
		return 0;

	return static_cast<int>(m_vNodes[uNode].vChildren.size());
}

int CDependencyTreeModel::columnCount(QModelIndex const& rParent) const
{
	Q_UNUSED(rParent)
	return 1;
}

QVariant CDependencyTreeModel::data(QModelIndex const& rIndex, int iRole) const
{
	std::size_t const uNode = nodeFromIndex(rIndex);
	if (uNode == g_uInvalidIndex)
		return QVariant();

	SMirrorNode const& rNode = m_vNodes[uNode];

	switch (iRole)
	{
	case Qt::DisplayRole:
		return nodeText(rNode);

	case Qt::DecorationRole:
		return ModuleStatusIcon(rNode.eStatus);

	case Qt::ForegroundRole:
		{
			QColor const color = ModuleStatusColor(rNode.eStatus);
			if (!color.isValid())
				return QVariant();
			return color;
		}

	case Qt::ToolTipRole:
		return nodeTooltip(rNode);

	case Qt::FontRole:
		{
			if (rNode.eStatus != EModuleStatus::Root)
				return QVariant();

			QFont font;
			font.setBold(true);
			return font;
		}

	case Qt::UserRole:
		return QVariant::fromValue<qulonglong>(static_cast<qulonglong>(uNode));

	case Qt::UserRole + 1:
		return QVariant::fromValue<qulonglong>(static_cast<qulonglong>(rNode.uModule));

	default:
		break;
	}

	return QVariant();
}

QVariant CDependencyTreeModel::headerData(int iSection, Qt::Orientation eOrientation, int iRole) const
{
	if (eOrientation != Qt::Horizontal)
		return QVariant();
	if (iRole != Qt::DisplayRole)
		return QVariant();
	if (iSection != 0)
		return QVariant();

	return i18n("Dependency");
}

bool CDependencyTreeModel::hasChildren(QModelIndex const& rParent) const
{
	if (!rParent.isValid())
		return m_bHasRoot;

	std::size_t const uNode = nodeFromIndex(rParent);
	if (uNode == g_uInvalidIndex)
		return false;

	SMirrorNode const& rNode = m_vNodes[uNode];
	if (!rNode.vChildren.empty())
		return true;

	// Shows the expander arrow before the children exist.
	return !rNode.bLeaf && !rNode.bExpanded;
}

bool CDependencyTreeModel::canFetchMore(QModelIndex const& rParent) const
{
	if (!rParent.isValid())
		return false;

	std::size_t const uNode = nodeFromIndex(rParent);
	if (uNode == g_uInvalidIndex)
		return false;

	SMirrorNode const& rNode = m_vNodes[uNode];
	return !rNode.bLeaf && !rNode.bExpanded && !rNode.bRequested;
}

void CDependencyTreeModel::fetchMore(QModelIndex const& rParent)
{
	std::size_t const uNode = nodeFromIndex(rParent);
	if (uNode == g_uInvalidIndex)
		return;

	SMirrorNode& rNode = m_vNodes[uNode];
	if (rNode.bLeaf || rNode.bExpanded || rNode.bRequested)
		return;

	rNode.bRequested = true;

	Q_EMIT ExpansionNeeded(uNode);
}

Qt::ItemFlags CDependencyTreeModel::flags(QModelIndex const& rIndex) const
{
	if (!rIndex.isValid())
		return Qt::NoItemFlags;

	return Qt::ItemIsEnabled | Qt::ItemIsSelectable;
}

std::size_t CDependencyTreeModel::nodeFromIndex(QModelIndex const& rIndex) const
{
	if (!rIndex.isValid())
		return g_uInvalidIndex;

	std::size_t const uNode = static_cast<std::size_t>(rIndex.internalId());
	if (uNode >= m_vNodes.size())
		return g_uInvalidIndex;
	if (!m_vNodes[uNode].bValid)
		return g_uInvalidIndex;

	return uNode;
}

void CDependencyTreeModel::appendChildren(std::size_t uParent, std::vector<SChildNodeInfo> const& rvChildren)
{
	if (uParent >= m_vNodes.size())
		return;

	for (SChildNodeInfo const& rChild : rvChildren)
	{
		if (rChild.uNode == g_uInvalidIndex)
			continue;

		// Tolerates out-of-order payloads by growing over the gap.
		if (rChild.uNode >= m_vNodes.size())
			m_vNodes.resize(rChild.uNode + 1);

		SMirrorNode& rNode = m_vNodes[rChild.uNode];
		rNode.uParent = uParent;
		rNode.uModule = rChild.uModule;
		rNode.sNeededName = QString::fromStdString(rChild.sNeededName);
		rNode.sResolvedPath = QString::fromStdString(rChild.sResolvedPath);
		rNode.eStatus = rChild.eStatus;
		rNode.sError = QString::fromStdString(rChild.sError);
		rNode.bLeaf = rChild.bLeaf;
		rNode.bExpanded = rChild.bLeaf;
		rNode.bRequested = rChild.bLeaf;
		rNode.bValid = true;

		std::vector<std::size_t>& rvParentChildren = m_vNodes[uParent].vChildren;
		rNode.uRowInParent = static_cast<int>(rvParentChildren.size());
		rvParentChildren.push_back(rChild.uNode);
	}
}

QString CDependencyTreeModel::nodeText(SMirrorNode const& rNode) const
{
	if (rNode.eStatus == EModuleStatus::Root)
		return rNode.sResolvedPath;

	QString const sTarget = rNode.sResolvedPath.isEmpty() ? i18n("(not found)") : rNode.sResolvedPath;

	return rNode.sNeededName + QLatin1String(" -> ") + sTarget;
}

QString CDependencyTreeModel::nodeTooltip(SMirrorNode const& rNode) const
{
	QStringList vLines;

	if (!rNode.sResolvedPath.isEmpty())
		vLines.append(rNode.sResolvedPath);
	if (!rNode.sSoname.isEmpty())
		vLines.append(i18n("SONAME: %1", rNode.sSoname));

	vLines.append(ModuleStatusTooltip(rNode.eStatus));

	if (rNode.eStatus == EModuleStatus::Duplicate)
		vLines.append(i18n("Already loaded elsewhere in this tree."));
	if (!rNode.sError.isEmpty())
		vLines.append(rNode.sError);

	return vLines.join(QLatin1Char('\n'));
}
