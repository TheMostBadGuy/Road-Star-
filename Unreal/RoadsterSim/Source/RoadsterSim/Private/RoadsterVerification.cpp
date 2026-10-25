#include "RoadsterController.h"
#include "RoadsterPawn.h"
#include "TrackActor.h"
#include "ProceduralMeshComponent.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"

// Exercise the runtime-generated mesh, its actual collision, and persisted editable pieces.
// Opt in with -verifytracks; normal play never writes verification tracks.
bool RunRoadsterTrackVerification(ARoadsterController* Controller)
{
	ATrackActor* Track = Controller ? Controller->Track() : nullptr;
	if (!Track) return false;
	UProceduralMeshComponent* Mesh = Track->FindComponentByClass<UProceduralMeshComponent>();
	if (!Mesh) return false;
	const auto OriginalPieces = Track->Pieces;
	const FString OriginalName = Track->TrackName;
	const bool OriginalClosed = Track->bCloseLoop;
	const bool OriginalAsync = Mesh->bUseAsyncCooking;
	Mesh->bUseAsyncCooking = false; // collision must be complete before queries in this same call
	TArray<FString> Lines;
	int32 Failures = 0;
	auto Check = [&](const FString& Name, bool Pass)
	{
		Lines.Add(FString(Pass ? TEXT("PASS ") : TEXT("FAIL ")) + Name);
		if (!Pass) ++Failures;
	};
	const FString TrackDir = FPaths::ProjectSavedDir() / TEXT("Tracks");
	const FString Nonce = FGuid::NewGuid().ToString(EGuidFormats::Digits);
	for (int32 Preset = 0; Preset < ATrackActor::PresetNames().Num(); ++Preset)
	{
		Track->LoadPreset(Preset); Track->Rebuild();
		const FString Prefix = ATrackActor::PresetNames()[Preset];
		const auto Expected = Track->Pieces;
		const float ExpectedLength = Track->GetLengthM();
		bool Finite = Track->GetSamples().Num() > 30;
		int32 HitCount = 0, ProbeCount = 0, Inverted = 0, Tube = 0;
		const auto& Samples = Track->GetSamples();
		for (int32 i = 5; i < Samples.Num() - 5; i += 37)
		{
			const auto& S = Samples[i];
			Finite &= !S.P.ContainsNaN() && !S.Q.ContainsNaN() && FMath::IsFinite(S.S) && FMath::Abs(S.Q.SizeSquared() - 1.f) < 0.01f;
			const auto& Next = Samples[i + 1];
			if (S.bGap || Next.bGap) continue;
			// Probe inside a face rather than exactly on a shared vertex/edge.
			const FVector N = Track->GetActorTransform().TransformVectorNoScale(FQuat::Slerp(S.Q, Next.Q, 0.37f).GetUpVector()).GetSafeNormal();
			const FVector P = Track->GetActorTransform().TransformPosition(FMath::Lerp(S.P, Next.P, 0.37f));
			FHitResult Hit;
			FCollisionQueryParams Params(SCENE_QUERY_STAT(TrackVerification), true);
			++ProbeCount;
			if (Mesh->LineTraceComponent(Hit, P + N * 80.f, P - N * 80.f, Params) && FVector::DotProduct(Hit.ImpactNormal, N) > 0.5f) ++HitCount;
			else Lines.Add(FString::Printf(TEXT("DETAIL %s sample %d piece %d normalDot=%.3f"), *Prefix, i, S.Piece, FVector::DotProduct(Hit.ImpactNormal, N)));
			if (N.Z < -0.8f) ++Inverted;
			if (S.TubeR > 0.f) ++Tube;
		}
		Check(Prefix + TEXT(" finite normalized samples"), Finite);
		Check(FString::Printf(TEXT("%s surface collision %d/%d (inverted=%d tube=%d)"), *Prefix, HitCount, ProbeCount, Inverted, Tube), ProbeCount > 0 && HitCount >= ProbeCount * 0.98f);
		const FString Name = TEXT("_Verification_") + Nonce + FString::FromInt(Preset);
		Check(Prefix + TEXT(" saves to Tracks directory"), Track->SaveToFile(Name));
		Track->Pieces.Reset();
		const bool Loaded = Track->LoadFromFile(Name);
		bool Same = Loaded && Track->Pieces.Num() == Expected.Num() && FMath::IsNearlyEqual(Track->GetLengthM(), ExpectedLength, 0.01f);
		for (int32 i = 0; Same && i < Expected.Num(); ++i)
		{
			const auto& A = Expected[i]; const auto& B = Track->Pieces[i];
			Same &= A.Type == B.Type && A.Length == B.Length && A.Angle == B.Angle && A.Radius == B.Radius && A.Bank == B.Bank && A.Offset == B.Offset && A.Width == B.Width && A.bWalls == B.bWalls;
		}
		Check(Prefix + TEXT(" editable piece save/load round trip"), Same);
		IFileManager::Get().Delete(*(TrackDir / (Name + TEXT(".json"))));
	}
	Check(TEXT("reject traversal name"), !Track->SaveToFile(TEXT("../outside")));
	const FString BadName = TEXT("_Verification_Malformed_") + Nonce;
	FFileHelper::SaveStringToFile(TEXT("{\"name\":\"bad\",\"pieces\":[null]}"), *(TrackDir / (BadName + TEXT(".json"))));
	Check(TEXT("reject malformed file without replacing track"), !Track->LoadFromFile(BadName));
	IFileManager::Get().Delete(*(TrackDir / (BadName + TEXT(".json"))));
	Track->Pieces = OriginalPieces; Track->TrackName = OriginalName; Track->bCloseLoop = OriginalClosed;
	Track->Rebuild(); Mesh->bUseAsyncCooking = OriginalAsync;
	if (Controller->Car()) Controller->Car()->ResetToTrack(true);
	Lines.Add(FString::Printf(TEXT("RESULT: %d failures"), Failures));
	const FString Report = FString::Join(Lines, TEXT("\n"));
	FFileHelper::SaveStringToFile(Report, *(FPaths::ProjectSavedDir() / TEXT("TrackVerification.txt")));
	UE_LOG(LogTemp, Display, TEXT("ROADSTER_TRACK_VERIFICATION\n%s"), *Report);
	return Failures == 0;
}
