// Copyright 2026 Spotted Loaf Studio
#include "ChunkWorldExtended/ChunkWorldNetworkSubsystem.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FChunkWorldNetworkEndpointTest, "PorismExtension.ChunkWorld.Network.Endpoints",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FChunkWorldNetworkEndpointTest::RunTest(const FString&)
{
	FString Failure, Address;
	int32 Port = 0;
	TestTrue(TEXT("Default Auto"), UChunkWorldNetworkSubsystem::ResolvePorts(7777, 0, 0, Port, Failure));
	TestEqual(TEXT("Default terrain port"), Port, 7877);
	TestTrue(TEXT("Custom gameplay Auto"), UChunkWorldNetworkSubsystem::ResolvePorts(9000, 0, 0, Port, Failure));
	TestEqual(TEXT("Offset follows gameplay listener"), Port, 9100);
	TestTrue(TEXT("Fixed local/public mapping"), UChunkWorldNetworkSubsystem::ResolvePorts(9000, 18777, 28777, Port, Failure));
	TestEqual(TEXT("Public mapping cannot move bind"), Port, 18777);
	TestFalse(TEXT("Public mapping cannot use Auto"), UChunkWorldNetworkSubsystem::ResolvePorts(7777, 0, 28777, Port, Failure));
	TestFalse(TEXT("Auto cannot overflow"), UChunkWorldNetworkSubsystem::ResolvePorts(65436, 0, 0, Port, Failure));
	TestFalse(TEXT("Port upper bound"), UChunkWorldNetworkSubsystem::ResolvePorts(7777, 65536, 0, Port, Failure));
	FChunkWorldNetworkEndpoint Endpoint;
	Endpoint.Protocol = TEXT("Tcp"); Endpoint.Address = TEXT("18777"); Endpoint.PublicTcpPort = 28777;
	TestTrue(TEXT("LAN endpoint resolves"), Endpoint.Resolve(TEXT("192.168.1.2"), true, Address, Failure));
	TestEqual(TEXT("LAN ignores public mapping"), Address, FString(TEXT("192.168.1.2:18777")));
	TestTrue(TEXT("Public endpoint resolves"), Endpoint.Resolve(TEXT("play.example.org"), false, Address, Failure));
	TestEqual(TEXT("Public endpoint uses mapping"), Address, FString(TEXT("play.example.org:28777")));
	for (const FString& BadPort : {TEXT("0"), TEXT("65536"), TEXT("12?x"), TEXT("-1"), TEXT("1.5")})
	{
		Endpoint.Address = BadPort;
		TestFalse(TEXT("Reject malformed terrain port"), Endpoint.Resolve(TEXT("127.0.0.1"), true, Address, Failure));
	}
	Endpoint.Address = TEXT("18777");
	TestFalse(TEXT("Reject gameplay URL injection"), Endpoint.Resolve(TEXT("host?listen"), true, Address, Failure));
	Endpoint.Protocol = TEXT("Steam");
	TestFalse(TEXT("TCP mapping cannot apply to Steam endpoint"), Endpoint.Resolve(TEXT("host"), false, Address, Failure));
	return true;
}
