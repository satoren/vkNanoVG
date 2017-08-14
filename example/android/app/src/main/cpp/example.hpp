#pragma once

bool InitExample(android_app *app);

void DeleteExample(void);

bool IsExampleReady(void);

bool ExampleDrawFrame(void);

#include <fstream>
#include <unistd.h>
#include <sys/stat.h>
#include <android_native_app_glue.h>

static void copyAssetFile(AAsset *asset, const std::string &dest) {
  std::ofstream ofs(dest, std::ostream::binary);
  char buffer[512];
  while (off64_t rsize = AAsset_read(asset, buffer, 512)) {
    ofs.write(buffer, rsize);
  }
}

static bool copyFromAssets(AAssetManager *assetm, const std::string &assetsdir, const std::string &destdir) {
  mkdir(destdir.c_str(), 0777);
  AAssetDir *dir = AAssetManager_openDir(assetm, assetsdir.c_str());

  while (const char *filename = AAssetDir_getNextFileName(dir)) {
    std::string srcpath = filename;
    if (!assetsdir.empty()) {
      srcpath = assetsdir + "/" + srcpath;
    }
    AAsset *asset = AAssetManager_open(assetm, srcpath.c_str(), AASSET_MODE_STREAMING);
    copyAssetFile(asset, destdir + "/" + filename);
    AAsset_close(asset);
  }
  AAssetDir_close(dir);
  return false;
}
