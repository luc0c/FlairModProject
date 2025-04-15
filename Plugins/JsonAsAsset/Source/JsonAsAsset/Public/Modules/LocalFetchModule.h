﻿/* Copyright JsonAsAsset Contributors 2024-2025 */

#pragma once

#include "CoreMinimal.h"

/* Structures, Enumerations, used for Local Fetch */
#include "LocalFetchModule.generated.h"

class LocalFetchModule {
public:
	static bool LaunchLocalFetch();
	static void CloseLocalFetch();
};

USTRUCT()
struct FAesKey
{
	GENERATED_BODY()
public:
	FAesKey() {
	}

	FAesKey(FString NewGUID, FString NewKey) {
		Value = NewKey;
		Guid = NewGUID;
	}

	UPROPERTY(EditAnywhere, Config, Category = "Key", meta = (DisplayName = "Key"))
	FString Value = "0x00000000000000000000000000000000000000000000000000000000000";

	UPROPERTY(EditAnywhere, Config, Category = "Key")
	FString Guid = "00000000000000000000000000000000";
};

/*
 * Matching CUE4Parse's Unreal Versioning Enum
 * Found at: https://github.com/FabianFG/CUE4Parse/blob/master/CUE4Parse/UE4/Versions/EGame.cs
*/
UENUM()
enum ECUE4ParseVersion
{
	GAME_UE4_0,
	GAME_UE4_1,
	GAME_UE4_2,
	GAME_UE4_3,
	GAME_UE4_4,
	GAME_UE4_5,
	GAME_ArkSurvivalEvolved,
	GAME_UE4_6,
	GAME_UE4_7,
	GAME_UE4_8,
	GAME_UE4_9,
	GAME_UE4_10,
	GAME_SeaOfThieves,
	GAME_UE4_11,
	GAME_GearsOfWar4,
	GAME_UE4_12,
	GAME_UE4_13,
	GAME_StateOfDecay2,
	GAME_UE4_14,
	GAME_TEKKEN7,
	GAME_UE4_15,
	GAME_UE4_16,
	GAME_PlayerUnknownsBattlegrounds,
	GAME_TrainSimWorld2020,
	GAME_UE4_17,
	GAME_AWayOut,
	GAME_UE4_18,
	GAME_KingdomHearts3,
	GAME_FinalFantasy7Remake,
	GAME_AceCombat7,
	GAME_UE4_19,
	GAME_Paragon,
	GAME_UE4_20,
	GAME_Borderlands3,
	GAME_UE4_21,
	GAME_StarWarsJediFallenOrder,
	GAME_UE4_22,
	GAME_UE4_23,
	GAME_ApexLegendsMobile,
	GAME_UE4_24,
	GAME_UE4_25,
	GAME_UE4_25_Plus,
	GAME_RogueCompany,
	GAME_DeadIsland2,
	GAME_KenaBridgeofSpirits,
	GAME_CalabiYau,
	GAME_UE4_26,
	GAME_GTATheTrilogyDefinitiveEdition,
	GAME_ReadyOrNot,
	GAME_BladeAndSoul,
	GAME_TowerOfFantasy,
	GAME_Dauntless,
	GAME_TheDivisionResurgence,
	GAME_StarWarsJediSurvivor,
	GAME_Snowbreak,
	GAME_UE4_27,
	GAME_Splitgate,
	GAME_HYENAS,
	GAME_HogwartsLegacy,
	GAME_OutlastTrials,
	GAME_Valorant,
	GAME_Gollum,
	GAME_Grounded,
	GAME_UE4_28,

	GAME_UE5_0,
	GAME_MeetYourMaker,
	GAME_UE5_1,
	GAME_UE5_2,
	GAME_UE5_3,
	GAME_UE5_4,
	GAME_UE5_5,
	GAME_UE5_6,

	/* Change this always to the last available enum above */
	GAME_UE5_LATEST = GAME_UE5_6
};