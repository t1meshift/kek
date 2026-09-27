#ifndef PLATFORM_ASSETS_FS_H
#define PLATFORM_ASSETS_FS_H

#include "kek_asset.h"

typedef struct FS_AssetProvider {
    KEK_AssetProvider base;
    const char* root_path;
} FS_AssetProvider;

void fs_asset_provider_init(FS_AssetProvider* provider, const char* root_path);

#endif // PLATFORM_ASSETS_FS_H
