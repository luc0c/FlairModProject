/* Copyright JsonAsAsset Contributors 2024-2025 */

#pragma once

#include "CoreMinimal.h"
#include "Interfaces/IPluginManager.h"
#include "Modules/Versioning.h"
#include "Utilities/Serializers/PropertyUtilities.h"
#include "Utilities/AppStyleCompatibility.h"

#if ENGINE_UE4
#include "Modules/ModuleInterface.h"
#endif

class UJsonAsAssetSettings;

class FJsonAsAssetModule : public IModuleInterface {
public:
    virtual void StartupModule() override;
    virtual void ShutdownModule() override;

    /* Execute File Dialog */
    void PluginButtonClicked();

    FJsonAsAssetVersioning Versioning;
    void CheckForUpdates();

private:
    void RegisterMenus();

    TSharedPtr<FUICommandList> PluginCommands;
    TSharedRef<SWidget> CreateToolbarDropdown();
    void CreateLocalFetchDropdown(FMenuBuilder MenuBuilder) const;
    void CreateVersioningDropdown(FMenuBuilder MenuBuilder) const;
    void CreateLastDropdown(FMenuBuilder MenuBuilder) const;

    static void SupportedAssetsDropdown(FMenuBuilder& InnerMenuBuilder, bool bIsLocalFetch = false);

    bool bActionRequired = false;
    UJsonAsAssetSettings* Settings = nullptr;

    TSharedPtr<IPlugin> Plugin;

#if ENGINE_UE4
    void AddToolbarExtension(FToolBarBuilder& Builder);
#endif
};