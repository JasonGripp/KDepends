// Copyright (c) 2026 Jason Gripp
// Licensed under the MIT License.

#pragma once

#include "analysisengine.h"
#include "moduledata.h"

#include <QAbstractItemModel>
#include <QModelIndex>
#include <QString>
#include <QVariant>

#include <cstddef>
#include <vector>

// UI-thread mirror of a dependency tree, built only from engine payloads.
// It never reads live analysis state and therefore needs no resolver locks.
class CDependencyTreeModel : public QAbstractItemModel
{
	Q_OBJECT

private:
	struct SMirrorNode
	{
		std::size_t uParent = g_uInvalidIndex;
		std::size_t uModule = g_uInvalidIndex;
		int uRowInParent = 0;
		QString sNeededName;
		QString sResolvedPath;
		EModuleStatus eStatus = EModuleStatus::Unknown;
		QString sError;
		// Filled by ApplyModuleUpdate once the module has really been parsed.
		QString sSoname;
		std::vector<std::size_t> vChildren;
		bool bLeaf = false;
		bool bExpanded = false;
		bool bRequested = false;
		// False for storage created before an earlier node index arrives.
		// Invalid entries are never rendered.
		bool bValid = false;
	};

private:
	std::vector<SMirrorNode> m_vNodes;
	bool m_bHasRoot = false;
	QString m_sRootPath;

public:
	explicit CDependencyTreeModel(QObject* pParent = nullptr);
	~CDependencyTreeModel() override;

	void Reset();
	void ApplyRoot(SRootResult const& rResult);
	void ApplyExpansion(SExpandOutcome const& rOutcome);
	void ApplyModuleUpdate(std::size_t uModule, SModuleInfo const& rInfo);

	std::size_t NodeAt(QModelIndex const& rIndex) const;
	std::size_t ModuleAt(QModelIndex const& rIndex) const;
	QString PathAt(QModelIndex const& rIndex) const;
	QString NeededNameAt(QModelIndex const& rIndex) const;
	QModelIndex IndexForNode(std::size_t uNode) const;
	QModelIndex FirstIndexForModule(std::size_t uModule) const;
	bool HasRoot() const;
	std::size_t NodeCount() const;

	QModelIndex index(int iRow, int iColumn, QModelIndex const& rParent = QModelIndex()) const override;
	QModelIndex parent(QModelIndex const& rIndex) const override;
	int rowCount(QModelIndex const& rParent = QModelIndex()) const override;
	int columnCount(QModelIndex const& rParent = QModelIndex()) const override;
	QVariant data(QModelIndex const& rIndex, int iRole = Qt::DisplayRole) const override;
	QVariant headerData(int iSection, Qt::Orientation eOrientation, int iRole = Qt::DisplayRole) const override;
	bool hasChildren(QModelIndex const& rParent = QModelIndex()) const override;
	bool canFetchMore(QModelIndex const& rParent) const override;
	void fetchMore(QModelIndex const& rParent) override;
	Qt::ItemFlags flags(QModelIndex const& rIndex) const override;

Q_SIGNALS:

	void ExpansionNeeded(std::size_t uNode);
	void RootApplied();

private:
	std::size_t nodeFromIndex(QModelIndex const& rIndex) const;
	void appendChildren(std::size_t uParent, std::vector<SChildNodeInfo> const& rvChildren);
	QString nodeText(SMirrorNode const& rNode) const;
	QString nodeTooltip(SMirrorNode const& rNode) const;
};
