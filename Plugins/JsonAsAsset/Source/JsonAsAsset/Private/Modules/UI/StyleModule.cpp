/* Copyright JsonAsAsset Contributors 2024-2025 */

#include "Modules/UI/StyleModule.h"
#include "Styling/SlateStyleRegistry.h"
#include "Interfaces/IPluginManager.h"

#define IMAGE_BRUSH(RelativePath, ...) FSlateImageBrush(Style->RootToContentDir(RelativePath, TEXT(".png")), __VA_ARGS__)

const FVector2D Icon40x40(40, 40);
const FVector2D Icon80x80(40, 40);

TSharedRef<FSlateStyleSet> FJsonAsAssetStyle::Create() {
	TSharedRef<FSlateStyleSet> Style = MakeShareable(new FSlateStyleSet("JsonAsAssetStyle"));
	Style->SetContentRoot(IPluginManager::Get().FindPlugin("JsonAsAsset")->GetBaseDir() / TEXT("Resources"));

	/* Toolbar ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */
	Style->Set("JsonAsAsset.Toolbar.Icon", new IMAGE_BRUSH(TEXT("./Toolbar/40px"), Icon40x40));
	Style->Set("JsonAsAsset.Toolbar.Icon.Warning", new IMAGE_BRUSH(TEXT("./Toolbar/40px_Warning"), Icon40x40));

	/* AboutJsonAsAsset Widget ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */
	Style->Set("JsonAsAsset.FModel.Icon", new IMAGE_BRUSH(TEXT("./About/FModel"), Icon80x80));
	Style->Set("JsonAsAsset.Github.Icon", new IMAGE_BRUSH(TEXT("./About/Github"), Icon40x40));

	return Style;
}

TSharedPtr<FSlateStyleSet> FJsonAsAssetStyle::StyleInstance = nullptr;

void FJsonAsAssetStyle::Initialize() {
	if (!StyleInstance.IsValid()) {
		StyleInstance = Create();
		FSlateStyleRegistry::RegisterSlateStyle(*StyleInstance);
	}
}

void FJsonAsAssetStyle::Shutdown() {
	FSlateStyleRegistry::UnRegisterSlateStyle(*StyleInstance);
	ensure(StyleInstance.IsUnique());
	StyleInstance.Reset();
}

FName FJsonAsAssetStyle::GetStyleSetName() {
	static FName StyleSetName(TEXT("JsonAsAssetStyle"));
	return StyleSetName;
}

const ISlateStyle& FJsonAsAssetStyle::Get() {
	return *StyleInstance;
}

#undef IMAGE_BRUSH

void FJsonAsAssetStyle::ReloadTextures() {
	if (FSlateApplication::IsInitialized()) {
		FSlateApplication::Get().GetRenderer()->ReloadTextureResources();
	}
}