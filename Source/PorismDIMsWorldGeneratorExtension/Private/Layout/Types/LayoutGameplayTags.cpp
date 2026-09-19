// Copyright 2026 Spotted Loaf Studio

#include "Layout/Types/LayoutGameplayTags.h"

namespace LayoutGameplayTags
{
	UE_DEFINE_GAMEPLAY_TAG(FaceSolid, "Layout.Face.Solid");
	UE_DEFINE_GAMEPLAY_TAG(FaceOpen, "Layout.Face.Open");
	UE_DEFINE_GAMEPLAY_TAG(FaceEntry, "Layout.Face.Entry");
	UE_DEFINE_GAMEPLAY_TAG(FaceStair, "Layout.Face.Stair");

	UE_DEFINE_GAMEPLAY_TAG(FaceWallLeft, "Layout.Face.Wall.Left");
	UE_DEFINE_GAMEPLAY_TAG(FaceWallRight, "Layout.Face.Wall.Right");
	UE_DEFINE_GAMEPLAY_TAG(FaceWallCenter, "Layout.Face.Wall.Center");
	UE_DEFINE_GAMEPLAY_TAG(FaceWallFloorLeft, "Layout.Face.Wall.Floor.Left");
	UE_DEFINE_GAMEPLAY_TAG(FaceWallFloorRight, "Layout.Face.Wall.Floor.Right");
	UE_DEFINE_GAMEPLAY_TAG(FaceWallFloorCenter, "Layout.Face.Wall.Floor.Center");
	UE_DEFINE_GAMEPLAY_TAG(FaceWallFloorLeftRight, "Layout.Face.Wall.Floor.LeftRight");

	UE_DEFINE_GAMEPLAY_TAG(FaceWallLeftRight, "Layout.Face.Wall.LeftRight");

	UE_DEFINE_GAMEPLAY_TAG(TraversalPrimary, "Layout.Traversal.Primary");
	UE_DEFINE_GAMEPLAY_TAG(TraversalSecondary, "Layout.Traversal.Secondary");

	UE_DEFINE_GAMEPLAY_TAG(ConnectorRoad, "Layout.Connector.Road");
	UE_DEFINE_GAMEPLAY_TAG(ConnectorTrail, "Layout.Connector.Trail");
	UE_DEFINE_GAMEPLAY_TAG(ConnectorBridge, "Layout.Connector.Bridge");
	UE_DEFINE_GAMEPLAY_TAG(ConnectorTunnel, "Layout.Connector.Tunnel");

	UE_DEFINE_GAMEPLAY_TAG(InterfacePartitionSolid, "Layout.Interface.Partition.Solid");
	UE_DEFINE_GAMEPLAY_TAG(InterfacePartitionDoor, "Layout.Interface.Partition.Door");

	UE_DEFINE_GAMEPLAY_TAG(FeatureWindow, "Layout.Feature.Window");
	UE_DEFINE_GAMEPLAY_TAG(FeatureTorch, "Layout.Feature.Torch");
	UE_DEFINE_GAMEPLAY_TAG(FeatureGate, "Layout.Feature.Gate");
	UE_DEFINE_GAMEPLAY_TAG(FeatureTower, "Layout.Feature.Tower");
	UE_DEFINE_GAMEPLAY_TAG(FeatureDefense, "Layout.Feature.Defense");
	UE_DEFINE_GAMEPLAY_TAG(FeatureRoom, "Layout.Feature.Room");
	UE_DEFINE_GAMEPLAY_TAG(FeatureRoomSleeping, "Layout.Feature.Room.Sleeping");
	UE_DEFINE_GAMEPLAY_TAG(FeatureRoomStorage, "Layout.Feature.Room.Storage");

	UE_DEFINE_GAMEPLAY_TAG(ProfileHouse, "Layout.Profile.House");
	UE_DEFINE_GAMEPLAY_TAG(ProfileMarket, "Layout.Profile.Market");
}
