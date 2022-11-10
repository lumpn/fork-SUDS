﻿#include "SUDSScript.h"

#include "SUDS.h"
#include "SUDSScriptNode.h"
#include "SUDSScriptNodeText.h"
#include "EditorFramework/AssetImportData.h"

void USUDSScript::StartImport(TArray<USUDSScriptNode*>** ppNodes,
                              TArray<USUDSScriptNode*>** ppHeaderNodes,
                              TMap<FName, int>** ppLabelList,
                              TMap<FName, int>** ppHeaderLabelList,
                              TArray<FString>** ppSpeakerList)
{
	*ppNodes = &Nodes;
	*ppHeaderNodes = &HeaderNodes;
	*ppLabelList = &LabelList;
	*ppHeaderLabelList = &HeaderLabelList;
	*ppSpeakerList = &Speakers;
}

USUDSScriptNode* USUDSScript::GetNextNode(USUDSScriptNode* Node) const
{
	switch (Node->GetEdgeCount())
	{
	case 0:
		return nullptr;
	case 1:
		return Node->GetEdge(0)->GetTargetNode().Get();
	default:
		UE_LOG(LogSUDS, Error, TEXT("Called GetNextNode on a node with more than one edge"));
		return nullptr;
	}
	
}

void USUDSScript::FinishImport()
{
	// As an optimisation, make all text nodes pre-scan their follow-on nodes for choice nodes
	// We can actually have intermediate nodes, for example set nodes which run for all choices that are placed
	// between the text and the first choice. Resolve whether they exist now
	for (auto Node : Nodes)
	{
		if (Node->GetNodeType() == ESUDSScriptNodeType::Text)
		{
			auto NextNode = GetNextNode(Node);
			while (NextNode &&
				NextNode->GetNodeType() == ESUDSScriptNodeType::SetVariable) // We only skip over set right now
			{
				NextNode = GetNextNode(NextNode);
			}

			if (NextNode && NextNode->GetNodeType() == ESUDSScriptNodeType::Choice)
			{
				auto TextNode = Cast<USUDSScriptNodeText>(Node);
				TextNode->NotifyHasChoices();
			}
		}
	}
	
}

USUDSScriptNode* USUDSScript::GetHeaderNode() const
{
	if (HeaderNodes.Num() > 0)
		return HeaderNodes[0];

	return nullptr;
}

USUDSScriptNode* USUDSScript::GetFirstNode() const
{
	if (Nodes.Num() > 0)
		return Nodes[0];

	return nullptr;
}

USUDSScriptNode* USUDSScript::GetNodeByLabel(const FName& Label) const
{
	if (const int* pIdx = LabelList.Find(Label))
	{
		return Nodes[*pIdx];
	}

	return nullptr;
	
}
#if WITH_EDITORONLY_DATA

void USUDSScript::PostInitProperties()
{
	if (!HasAnyFlags(RF_ClassDefaultObject))
	{
		AssetImportData = NewObject<UAssetImportData>(this, TEXT("AssetImportData"));
	}
	Super::PostInitProperties();
}

void USUDSScript::GetAssetRegistryTags(TArray<FAssetRegistryTag>& OutTags) const
{
	if (AssetImportData)
	{
		OutTags.Add( FAssetRegistryTag(SourceFileTagName(), AssetImportData->GetSourceData().ToJson(), FAssetRegistryTag::TT_Hidden) );
	}

	Super::GetAssetRegistryTags(OutTags);
}
void USUDSScript::Serialize(FArchive& Ar)
{
	Super::Serialize(Ar);

	if (Ar.IsLoading() && Ar.UEVer() < VER_UE4_ASSET_IMPORT_DATA_AS_JSON && !AssetImportData)
	{
		// AssetImportData should always be valid
		AssetImportData = NewObject<UAssetImportData>(this, TEXT("AssetImportData"));
	}
}
#endif
