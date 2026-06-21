#include "HeifImagePlugin.h"

#include <QIODevice>
#include <QImage>

#include <libheif/heif.h>

#include <cstring>
#include <cstdint>

namespace {
// libheif 1.13 以降は decoder がプラグイン化されており、heif_init() を呼ぶと
// 確実にロードされる。古いバージョン (例: Ubuntu 22.04 の 1.12) には無いので
// バージョンガードする。refcount 管理なので read ごとに init/deinit して良い。
#if defined(LIBHEIF_NUMERIC_VERSION) && LIBHEIF_NUMERIC_VERSION >= 0x010D0000
constexpr bool kHasHeifInit = true;
#else
constexpr bool kHasHeifInit = false;
#endif
} // namespace

bool HeifHandler::canReadFrom(QIODevice* device) {
  if (!device) return false;
  const QByteArray header = device->peek(12);
  if (header.size() < 12) return false;
  // ISO BMFF: 先頭の box が "....ftyp"。bytes[4..8] == "ftyp"。
  if (header.mid(4, 4) != QByteArrayLiteral("ftyp")) return false;
  // major brand (bytes[8..12]) が HEIF 系か。
  const QByteArray brand = header.mid(8, 4);
  static const char* const kBrands[] = {
    "heic", "heix", "heim", "heis", "hevc", "hevx", "mif1", "msf1", "heif",
  };
  for (const char* b : kBrands) {
    if (brand == b) return true;
  }
  return false;
}

bool HeifHandler::canRead() const {
  if (canReadFrom(device())) {
    setFormat("heif");
    return true;
  }
  return false;
}

bool HeifHandler::read(QImage* image) {
  if (!image || !device()) return false;
  const QByteArray data = device()->readAll();
  if (data.isEmpty()) return false;

  if (kHasHeifInit) {
#if defined(LIBHEIF_NUMERIC_VERSION) && LIBHEIF_NUMERIC_VERSION >= 0x010D0000
    heif_init(nullptr);
#endif
  }

  heif_context* ctx = heif_context_alloc();
  heif_image_handle* handle = nullptr;
  heif_image* himg = nullptr;
  bool ok = false;

  do {
    heif_error err = heif_context_read_from_memory_without_copy(
      ctx, data.constData(), static_cast<size_t>(data.size()), nullptr);
    if (err.code != heif_error_Ok) break;

    err = heif_context_get_primary_image_handle(ctx, &handle);
    if (err.code != heif_error_Ok || !handle) break;

    const bool hasAlpha = heif_image_handle_has_alpha_channel(handle);
    const heif_chroma chroma = hasAlpha ? heif_chroma_interleaved_RGBA
                                        : heif_chroma_interleaved_RGB;
    err = heif_decode_image(handle, &himg, heif_colorspace_RGB, chroma, nullptr);
    if (err.code != heif_error_Ok || !himg) break;

    const int w = heif_image_get_width(himg, heif_channel_interleaved);
    const int h = heif_image_get_height(himg, heif_channel_interleaved);
    if (w <= 0 || h <= 0) break;

    int stride = 0;
    const uint8_t* pixels =
      heif_image_get_plane_readonly(himg, heif_channel_interleaved, &stride);
    if (!pixels) break;

    QImage result(w, h, hasAlpha ? QImage::Format_RGBA8888
                                 : QImage::Format_RGB888);
    if (result.isNull()) break;

    // libheif の stride は行末パディングを含みうる。QImage 側の行へ実バイト数
    // だけコピーする (QImage の bytesPerLine も 4 バイト境界に揃うため)。
    const qsizetype rowBytes = static_cast<qsizetype>(w) * (hasAlpha ? 4 : 3);
    for (int y = 0; y < h; ++y) {
      std::memcpy(result.scanLine(y),
                  pixels + static_cast<qsizetype>(y) * stride,
                  static_cast<size_t>(rowBytes));
    }
    *image = result;
    ok = true;
  } while (false);

  if (himg) heif_image_release(himg);
  if (handle) heif_image_handle_release(handle);
  heif_context_free(ctx);

  if (kHasHeifInit) {
#if defined(LIBHEIF_NUMERIC_VERSION) && LIBHEIF_NUMERIC_VERSION >= 0x010D0000
    heif_deinit();
#endif
  }
  return ok;
}

QImageIOPlugin::Capabilities HeifPlugin::capabilities(
  QIODevice* device, const QByteArray& format) const {
  if (format == "heif" || format == "heic") {
    return Capabilities(CanRead);
  }
  if (!format.isEmpty()) {
    return {};
  }
  if (device && device->isReadable() && HeifHandler::canReadFrom(device)) {
    return Capabilities(CanRead);
  }
  return {};
}

QImageIOHandler* HeifPlugin::create(QIODevice* device,
                                    const QByteArray& format) const {
  auto* handler = new HeifHandler();
  handler->setDevice(device);
  handler->setFormat(format.isEmpty() ? QByteArray("heif") : format);
  return handler;
}
