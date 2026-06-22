# libheif 専用の vcpkg トリプレット。
#
# 動的リンク (LGPL §4(d)(0) を満たす shared library 方式) を維持しつつ、
# libheif のプラグイン動的ロードを無効化する。
#
# 動的 heif.dll はプラグインローディング機構を持ち、起動時の静的初期化で
# ビルド時のプラグインディレクトリを探索して失敗する。その結果
# LoadLibrary が ERROR_DLL_INIT_FAILED (1114) となり、heif.dll を import
# する farman.exe が 0xc0000142 で起動できなかった。
#
# ENABLE_PLUGIN_LOADING=OFF にすると libheif はリンク済みの libde265 を
# 直接使い、起動時にプラグインを探索しないので初期化が通る。HEVC デコードは
# 引き続き libde265 で行える。
#
# このトリプレットは libheif (と libheif[core] の依存 libde265) のみに使う。
# ENABLE_PLUGIN_LOADING は libde265 など他ポートには無いオプションなので、
# それらでは未使用変数として無視される (実害なし)。
set(VCPKG_TARGET_ARCHITECTURE x64)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE dynamic)
set(VCPKG_CMAKE_CONFIGURE_OPTIONS "-DENABLE_PLUGIN_LOADING=OFF")
