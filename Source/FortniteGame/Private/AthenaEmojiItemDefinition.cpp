#include "AthenaEmojiItemDefinition.h"
#include "GameplayTagContainer.h"
#include "GameplayTagsManager.h"

void UAthenaEmojiItemDefinition::ConfigureParticleSystem(UParticleSystemComponent* ParticleSystem, TSoftObjectPtr<UTexture2D> OverrideImage) const {
}

UAthenaEmojiItemDefinition::UAthenaEmojiItemDefinition(const FObjectInitializer& ObjectInitializer) 
    : Super(ObjectInitializer) {
    FrameIndex = 0;
    FrameCount = 0;
    BaseMaterial = NULL;
    LifetimeIntroSeconds = 1;
    LifetimeMidSeconds = 1;
    LifetimeOutroSeconds = 1;
    GeneratedMaterial = NULL;
    ItemType = EFortItemType::AthenaDance;
    bMovingEmote = true;
    DisplayName = FText::FromString("Emoticon");
}

