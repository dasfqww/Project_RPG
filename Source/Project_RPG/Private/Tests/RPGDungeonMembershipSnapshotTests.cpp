#if WITH_DEV_AUTOMATION_TESTS
#include "Online/RPGDungeonMembershipSnapshot.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGDungeonMembershipSnapshotCodecTest,
	"ProjectRPG.Online.DungeonMembershipSnapshotCodec",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRPGDungeonMembershipSnapshotCodecTest::RunTest(const FString& Parameters)
{
	const FString Session = TEXT("00000000-0000-0000-0000-000000000001");
	const FString Character = TEXT("00000000-0000-0000-0000-000000000002");
	const FString Steam = TEXT("76561198000000001");
	const FString Server = TEXT("membership-fixture-server");
	const FString Member = TEXT("{\"characterId\":\"00000000-0000-0000-0000-000000000003\",\"steamId\":\"76561198000000002\",\"leaseExpiresAt\":\"2026-10-10T01:00:00Z\"}");
	const FString Receipt = TEXT("{\"characterId\":\"00000000-0000-0000-0000-000000000002\",\"steamId\":\"76561198000000001\",\"abandonedAt\":\"2026-10-10T00:00:00+00:00\",\"sessionStateAtAbandonment\":\"InProgress\",\"rewardExcluded\":true}");
	const FString ExpelledCharacter = TEXT("00000000-0000-0000-0000-000000000005");
	const FString ExpelledSteam = TEXT("76561198000000003");
	const FString Expulsion = FString::Printf(TEXT("{\"characterId\":\"%s\",\"steamId\":\"%s\",\"disconnectedAt\":\"2026-10-10T00:00:00Z\",\"expiredAt\":\"2026-10-10T00:10:00Z\",\"processedAt\":\"2026-10-10T00:10:01Z\",\"rewardExcluded\":true}"), *ExpelledCharacter, *ExpelledSteam);
	const FString Body = FString::Printf(TEXT("{\"dungeonSessionId\":\"%s\",\"serverId\":\"%s\",\"state\":\"InProgress\",\"updatedAt\":\"2026-10-10T00:00:00Z\",\"expiresAt\":\"2026-10-10T01:00:00Z\",\"members\":[%s],\"abandonments\":[%s]}"),
		*Session, *Server, *Member, *Receipt);
	FRPGDungeonMembershipSnapshot Snapshot;
	TestTrue(TEXT("Matching assigned snapshot accepted"), FRPGDungeonMembershipSnapshotCodec::Decode(Body, 200, Session, Server, Snapshot));
	TestTrue(TEXT("Exact session/server/character/account receipt proves opt-out"), Snapshot.IsExplicitlyAbandoned(Session, Server, Character, Steam));
	TestFalse(TEXT("Other session cannot reuse evidence"), Snapshot.IsExplicitlyAbandoned(TEXT("00000000-0000-0000-0000-000000000004"), Server, Character, Steam));
	TestFalse(TEXT("Other server cannot reuse evidence"), Snapshot.IsExplicitlyAbandoned(Session, TEXT("other-server"), Character, Steam));
	TestFalse(TEXT("Other account cannot reuse evidence"), Snapshot.IsExplicitlyAbandoned(Session, Server, Character, TEXT("76561198000000002")));
	TestFalse(TEXT("Member not explicitly abandoned"), Snapshot.IsExplicitlyAbandoned(Session, Server, TEXT("00000000-0000-0000-0000-000000000003"), TEXT("76561198000000002")));
	const FString WithExpulsion = Body.LeftChop(1) + FString::Printf(TEXT(",\"disconnectionExpulsions\":[%s]}"), *Expulsion);
	const FString ExpulsionArray = FString::Printf(TEXT("\"disconnectionExpulsions\":[%s]"), *Expulsion);
	TestTrue(TEXT("Additive expiry receipts accepted"), FRPGDungeonMembershipSnapshotCodec::Decode(WithExpulsion, 200, Session, Server, Snapshot));
	TestTrue(TEXT("Exact expiry receipt proves disconnect removal"), Snapshot.IsDisconnectedExpelled(Session, Server, ExpelledCharacter, ExpelledSteam));
	TestFalse(TEXT("Expiry is not explicit abandonment"), Snapshot.IsExplicitlyAbandoned(Session, Server, ExpelledCharacter, ExpelledSteam));
	TestFalse(TEXT("Other server cannot reuse expiry"), Snapshot.IsDisconnectedExpelled(Session, TEXT("foreign"), ExpelledCharacter, ExpelledSteam));
	TestFalse(TEXT("Other account cannot reuse expiry"), Snapshot.IsDisconnectedExpelled(Session, Server, ExpelledCharacter, Steam));
	TestTrue(TEXT("Closed snapshot can return historical expiry"), FRPGDungeonMembershipSnapshotCodec::Decode(
		WithExpulsion.Replace(TEXT("\"state\":\"InProgress\""), TEXT("\"state\":\"Closed\"")), 200, Session, Server, Snapshot));
	TestTrue(TEXT("Closed expiry remains scoped evidence"), Snapshot.IsDisconnectedExpelled(Session, Server, ExpelledCharacter, ExpelledSteam));
	auto Reject = [&](const FString& InvalidBody, int32 Status = 200)
	{
		Snapshot.DungeonSessionId = Session; Snapshot.Abandonments.AddDefaulted(); Snapshot.DisconnectionExpulsions.AddDefaulted();
		TestFalse(TEXT("Invalid input never produces evidence"), FRPGDungeonMembershipSnapshotCodec::Decode(InvalidBody, Status, Session, Server, Snapshot));
		TestTrue(TEXT("Failure clears old output"), Snapshot.DungeonSessionId.IsEmpty() && Snapshot.Abandonments.IsEmpty()
			&& Snapshot.Members.IsEmpty() && Snapshot.DisconnectionExpulsions.IsEmpty());
	};
	Reject(WithExpulsion.Replace(*ExpulsionArray, TEXT("\"disconnectionExpulsions\":null")));
	Reject(WithExpulsion.Replace(*Expulsion, *(Expulsion + TEXT(",") + Expulsion)));
	Reject(WithExpulsion.Replace(*ExpelledCharacter, *Character));
	Reject(WithExpulsion.Replace(TEXT("\"expiredAt\":\"2026-10-10T00:10:00Z\""), TEXT("\"expiredAt\":\"2026-10-10T00:09:59Z\"")));
	Reject(WithExpulsion.Replace(TEXT("\"processedAt\":\"2026-10-10T00:10:01Z\""), TEXT("\"processedAt\":\"2026-10-10T00:09:59Z\"")));
	Reject(WithExpulsion.Replace(TEXT("\"processedAt\":\"2026-10-10T00:10:01Z\""), TEXT("\"processedAt\":\"2026-10-10T00:10:01+09:00\"")));
	Reject(WithExpulsion.Replace(*Expulsion, *Expulsion.Replace(TEXT("\"rewardExcluded\":true"), TEXT("\"rewardExcluded\":false"))));
	for (int32 Status : { 0, 202, 204, 401, 403, 404, 409, 429, 500 }) Reject(Body, Status);
	Reject(TEXT("{}")); Reject(TEXT("[]")); Reject(TEXT("null")); Reject(TEXT("{"));
	Reject(Body.Replace(*Server, TEXT("other-server")));
	Reject(Body.Replace(*Session, TEXT("00000000-0000-0000-0000-000000000004")));
	Reject(Body.Replace(TEXT("\"state\":\"InProgress\""), TEXT("\"state\":\"inprogress\"")));
	Reject(Body.Replace(TEXT("\"state\":\"InProgress\""), TEXT("\"state\":\"Waiting\"")));
	Reject(Body.Replace(TEXT("\"rewardExcluded\":true"), TEXT("\"rewardExcluded\":false")));
	Reject(Body.Replace(TEXT("\"rewardExcluded\":true"), TEXT("\"rewardExcluded\":\"true\"")));
	Reject(Body.Replace(TEXT("\"rewardExcluded\":true"), TEXT("\"rewardExcluded\":1")));
	Reject(Body.Replace(TEXT("\"sessionStateAtAbandonment\":\"InProgress\""), TEXT("\"sessionStateAtAbandonment\":\"Cleared\"")));
	Reject(Body.Replace(*Steam, TEXT("invalid-steam")));
	Reject(Body.Replace(*Character, TEXT("00000000-0000-0000-0000-000000000000")));
	Reject(Body.Replace(TEXT("2026-10-10T00:00:00+00:00"), TEXT("2026-10-10T00:00:00+09:00")));
	Reject(Body.Replace(TEXT("2026-10-10T01:00:00Z"), TEXT("not-a-time")));
	Reject(Body.Replace(TEXT("\"abandonments\":["), TEXT("\"missingAbandonments\":[")));
	Reject(Body.Replace(*Receipt, *(Receipt + TEXT(",") + Receipt)));
	Reject(Body.Replace(*Member, *(Member + TEXT(",") + Member)));
	Reject(Body.Replace(TEXT("00000000-0000-0000-0000-000000000003"), *Character));
	Reject(Body.Replace(*Member, TEXT("null")));
	Reject(Body.Replace(*Receipt, TEXT("true")));
	Reject(FString::ChrN(64 * 1024 + 1, TEXT(' ')) + Body);
	FString ManyReceipts;
	for (int32 Index = 0; Index < 129; ++Index) { if (Index) ManyReceipts += TEXT(","); ManyReceipts += Receipt; }
	Reject(Body.Replace(*Receipt, *ManyReceipts));
	const FString Empty = Body.Replace(*Member, TEXT("")).Replace(*Receipt, TEXT(""));
	TestTrue(TEXT("Empty snapshot is valid data"), FRPGDungeonMembershipSnapshotCodec::Decode(Empty, 200, Session, Server, Snapshot));
	TestFalse(TEXT("Missing membership is not explicit opt-out"), Snapshot.IsExplicitlyAbandoned(Session, Server, Character, Steam));
	TestTrue(TEXT("Closed snapshot retains historical evidence"), FRPGDungeonMembershipSnapshotCodec::Decode(
		Body.Replace(TEXT("\"state\":\"InProgress\""), TEXT("\"state\":\"Closed\"")), 200, Session, Server, Snapshot));
	TestTrue(TEXT("Closed receipt usable for exact old session only"), Snapshot.IsExplicitlyAbandoned(Session, Server, Character, Steam));
	const FString PresentedMember = Member.LeftChop(1)
		+ TEXT(",\"characterName\":\"  Hero  \",\"disconnectedAt\":\"2026-10-10T00:00:00Z\",\"reconnectExpiresAt\":\"2026-10-10T00:10:00Z\",\"connectionGeneration\":\"9223372036854775807\"}");
	const FString PresentedBody = Body.Replace(*Member, *PresentedMember).LeftChop(1) + TEXT(",\"observedAt\":\"2026-10-10T00:03:00Z\"}");
	if (!TestTrue(TEXT("Additive presentation does not change required membership decoding"),
		FRPGDungeonMembershipSnapshotCodec::Decode(PresentedBody, 200, Session, Server, Snapshot))) return false;
	TestTrue(TEXT("Original immutable evidence is preserved with presentation"), Snapshot.IsExplicitlyAbandoned(Session, Server, Character, Steam));
	if (!TestEqual(TEXT("Presented member count"), Snapshot.Members.Num(), 1)) return false;
	TestTrue(TEXT("Valid disconnect metadata is available to the read-only HUD"), Snapshot.Members[0].bHasConnectionPresentation);
	TestEqual(TEXT("Presentation name is sanitized"), Snapshot.Members[0].CharacterName, FString(TEXT("Hero")));
	TestEqual(TEXT("String generation keeps exact int64 precision"), Snapshot.Members[0].ConnectionGeneration, MAX_int64);
	TestFalse(TEXT("Older senders do not invent a distinct disconnect generation"), Snapshot.Members[0].bHasDisconnectedGeneration);
	const FString WithDisconnectedGeneration = PresentedBody.Replace(TEXT("\"connectionGeneration\":\"9223372036854775807\""),
		TEXT("\"connectionGeneration\":\"9223372036854775807\",\"disconnectedGeneration\":\"9223372036854775806\""));
	if (!TestTrue(TEXT("A newer consumed ticket can coexist with an older disconnect"),
		FRPGDungeonMembershipSnapshotCodec::Decode(WithDisconnectedGeneration, 200, Session, Server, Snapshot))) return false;
	TestTrue(TEXT("Disconnect generation is available independently"), Snapshot.Members[0].bHasDisconnectedGeneration);
	TestEqual(TEXT("Disconnect generation preserves its own precision"), Snapshot.Members[0].DisconnectedGeneration, MAX_int64 - 1);
	for (const FString BadPresentation : {
		PresentedBody.Replace(TEXT("\"connectionGeneration\":\"9223372036854775807\""), TEXT("\"connectionGeneration\":9223372036854775807")),
		PresentedBody.Replace(TEXT("\"connectionGeneration\":\"9223372036854775807\""), TEXT("\"connectionGeneration\":\"9223372036854775808\"")),
		PresentedBody.Replace(TEXT("\"reconnectExpiresAt\":\"2026-10-10T00:10:00Z\""), TEXT("\"reconnectExpiresAt\":true")),
		PresentedBody.Replace(TEXT("2026-10-10T00:10:00Z"), TEXT("2026-10-10T00:09:00Z")),
		WithDisconnectedGeneration.Replace(TEXT("\"disconnectedGeneration\":\"9223372036854775806\""), TEXT("\"disconnectedGeneration\":9223372036854775806")),
		WithDisconnectedGeneration.Replace(TEXT("\"connectionGeneration\":\"9223372036854775807\""), TEXT("\"connectionGeneration\":\"1\"")),
		WithDisconnectedGeneration.Replace(TEXT("\"disconnectedGeneration\":\"9223372036854775806\""), TEXT("\"disconnectedGeneration\":\"9223372036854775808\"")),
		PresentedBody.Replace(TEXT("\"reconnectExpiresAt\":\"2026-10-10T00:10:00Z\""), TEXT("\"reconnectExpiresAt\":null"))})
	{
		if (!TestTrue(TEXT("Malformed optional metadata cannot suppress existing authority evidence"),
			FRPGDungeonMembershipSnapshotCodec::Decode(BadPresentation, 200, Session, Server, Snapshot))) return false;
		TestTrue(TEXT("Opt-out evidence still accepted"), Snapshot.IsExplicitlyAbandoned(Session, Server, Character, Steam));
		TestFalse(TEXT("Only malformed presentation is discarded"), Snapshot.Members[0].bHasConnectionPresentation);
	}
	return true;
}
#endif
