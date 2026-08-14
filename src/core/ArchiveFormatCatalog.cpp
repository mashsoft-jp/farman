#include "ArchiveFormatCatalog.h"

#include "ArchiveDispatcher.h"
#include "settings/Settings.h"
#include "utils/ArchivePath.h"

#include <QCoreApplication>

namespace Farman {
namespace ArchiveFormatCatalog {

namespace {

QString name(const char* text) {
  return QCoreApplication::translate("ArchiveFormatCatalog", text);
}

using Enc = ArchiveFormatInfo::Encryption;

// 作成もできるコンテナ形式 (v0.9.9 までと同じ 5 形式)。
ArchiveFormatInfo makeCreatable(const QString& id, const QString& displayName,
                                const QStringList& patterns,
                                bool supportsLevel, Enc encryption) {
  ArchiveFormatInfo f;
  f.id                       = id;
  f.displayName              = displayName;
  f.defaultPatterns          = patterns;
  f.source                   = ArchiveFormatInfo::Source::Builtin;
  f.defaultEnabled           = true;
  f.canCreate                = true;
  f.supportsCompressionLevel = supportsLevel;
  f.encryption               = encryption;
  f.supportsFilenameEncoding = true;
  return f;
}

// 読取のみのコンテナ形式。libarchive は読めるはずだが farman としての動作確認と
// 各 OS のコーデック同梱保証が済んでいないため、既定では無効にしておく。
ArchiveFormatInfo makeReadOnly(const QString& id, const QString& displayName,
                               const QStringList& patterns,
                               Enc encryption = Enc::None,
                               bool supportsFilenameEncoding = false) {
  ArchiveFormatInfo f;
  f.id                       = id;
  f.displayName              = displayName;
  f.defaultPatterns          = patterns;
  f.source                   = ArchiveFormatInfo::Source::Builtin;
  f.defaultEnabled           = false;
  f.canCreate                = false;
  f.supportsCompressionLevel = false;
  f.encryption               = encryption;
  f.supportsFilenameEncoding = supportsFilenameEncoding;
  return f;
}

// 中身 1 エントリの単一ファイル圧縮 (tar を伴わない .gz / .xz など)。
ArchiveFormatInfo makeSingleFile(const QString& id, const QString& displayName,
                                 const QStringList& patterns) {
  ArchiveFormatInfo f = makeReadOnly(id, displayName, patterns);
  f.singleFileCompression = true;
  return f;
}

QList<ArchiveFormatInfo> buildBuiltinFormats() {
  QList<ArchiveFormatInfo> list;

  // ── 作成もできる形式 ──────────────────────────
  list << makeCreatable(QStringLiteral("zip"), name("ZIP"),
                        {QStringLiteral("*.zip")}, true, Enc::ReadWrite);
  list << makeCreatable(QStringLiteral("tar"), name("TAR (uncompressed)"),
                        {QStringLiteral("*.tar")}, false, Enc::None);
  list << makeCreatable(QStringLiteral("tar.gz"), name("TAR + gzip"),
                        {QStringLiteral("*.tar.gz"), QStringLiteral("*.tgz")},
                        true, Enc::None);
  list << makeCreatable(QStringLiteral("tar.bz2"), name("TAR + bzip2"),
                        {QStringLiteral("*.tar.bz2"), QStringLiteral("*.tbz2")},
                        true, Enc::None);
  list << makeCreatable(QStringLiteral("tar.xz"), name("TAR + xz"),
                        {QStringLiteral("*.tar.xz"), QStringLiteral("*.txz")},
                        true, Enc::None);

  // ── 読取のみのコンテナ形式 ────────────────────
  list << makeReadOnly(QStringLiteral("tar.zst"), name("TAR + Zstandard"),
                       {QStringLiteral("*.tar.zst"), QStringLiteral("*.tzst")});
  list << makeReadOnly(QStringLiteral("tar.lz4"), name("TAR + LZ4"),
                       {QStringLiteral("*.tar.lz4")});
  list << makeReadOnly(QStringLiteral("tar.lzma"), name("TAR + LZMA"),
                       {QStringLiteral("*.tar.lzma"), QStringLiteral("*.tlz")});
  list << makeReadOnly(QStringLiteral("tar.lz"), name("TAR + lzip"),
                       {QStringLiteral("*.tar.lz")});
  list << makeReadOnly(QStringLiteral("tar.Z"), name("TAR + compress"),
                       {QStringLiteral("*.tar.Z"), QStringLiteral("*.taz")});
  list << makeReadOnly(QStringLiteral("7z"), name("7-Zip"),
                       {QStringLiteral("*.7z")}, Enc::ReadOnly, true);
  list << makeReadOnly(QStringLiteral("rar"), name("RAR"),
                       {QStringLiteral("*.rar")}, Enc::ReadOnly, true);
  list << makeReadOnly(QStringLiteral("iso"), name("ISO9660 disc image"),
                       {QStringLiteral("*.iso")});
  list << makeReadOnly(QStringLiteral("cab"), name("Microsoft Cabinet"),
                       {QStringLiteral("*.cab")}, Enc::None, true);
  list << makeReadOnly(QStringLiteral("xar"), name("xar / pkg"),
                       {QStringLiteral("*.xar"), QStringLiteral("*.pkg")});
  list << makeReadOnly(QStringLiteral("cpio"), name("cpio"),
                       {QStringLiteral("*.cpio")});
  list << makeReadOnly(QStringLiteral("ar"), name("ar / Debian package"),
                       {QStringLiteral("*.ar"), QStringLiteral("*.a"),
                        QStringLiteral("*.deb")});

  // ── 単一ファイル圧縮 ─────────────────────────
  // tar 併用形 (*.tar.gz 等) より後に置くが、照合は「最長のパターン優先」で
  // 決めるのでカタログ順には依存しない。
  list << makeSingleFile(QStringLiteral("gz"),   name("gzip"),      {QStringLiteral("*.gz")});
  list << makeSingleFile(QStringLiteral("bz2"),  name("bzip2"),     {QStringLiteral("*.bz2")});
  list << makeSingleFile(QStringLiteral("xz"),   name("xz"),        {QStringLiteral("*.xz")});
  list << makeSingleFile(QStringLiteral("lzma"), name("LZMA"),      {QStringLiteral("*.lzma")});
  list << makeSingleFile(QStringLiteral("zst"),  name("Zstandard"), {QStringLiteral("*.zst")});
  list << makeSingleFile(QStringLiteral("lz4"),  name("LZ4"),       {QStringLiteral("*.lz4")});
  list << makeSingleFile(QStringLiteral("lz"),   name("lzip"),      {QStringLiteral("*.lz")});
  list << makeSingleFile(QStringLiteral("Z"),    name("compress"),  {QStringLiteral("*.Z")});
  list << makeSingleFile(QStringLiteral("lzo"),  name("LZO"),       {QStringLiteral("*.lzo")});

  return list;
}

}  // namespace

const QList<ArchiveFormatInfo>& builtinFormats() {
  static const QList<ArchiveFormatInfo> formats = buildBuiltinFormats();
  return formats;
}

const ArchiveFormatInfo* findBuiltin(const QString& id) {
  for (const ArchiveFormatInfo& f : builtinFormats()) {
    if (f.id.compare(id, Qt::CaseInsensitive) == 0) return &f;
  }
  return nullptr;
}

QList<ArchiveFormatInfo> allFormats() {
  QList<ArchiveFormatInfo> list = builtinFormats();

  // プラグイン形式を同じ一覧に合流させる。ロードに失敗したプラグインは形式と
  // しては存在しないので載せない (状況は設定 → プラグインの一覧側で見せる)。
  for (const ArchivePluginRecord& rec : ArchiveDispatcher::instance().pluginRecords()) {
    if (!rec.loaded || rec.pluginId.isEmpty()) continue;

    ArchiveFormatInfo f;
    f.id          = rec.pluginId;
    f.pluginId    = rec.pluginId;
    f.displayName = rec.pluginName.isEmpty() ? rec.pluginId : rec.pluginName;
    f.source      = ArchiveFormatInfo::Source::Plugin;
    // プラグインは「ロードされている = 読める」ことが確認済みなので既定で有効。
    // 個別の有効 / 無効はプラグイン側の仕組み (disabledArchivePlugins) と共通。
    f.defaultEnabled           = true;
    f.canCreate                = false;
    f.supportsCompressionLevel = false;
    f.encryption               = ArchiveFormatInfo::Encryption::ReadOnly;
    f.supportsFilenameEncoding = true;

    // IArchivePlugin::supportedExtensions() は先頭ドット無しの小文字拡張子を
    // 返す規約 ({"lzh", "lha"})。カタログはファイル名 glob で持つので変換する。
    for (const QString& ext : rec.supportedExtensions) {
      QString e = ext.trimmed();
      if (e.isEmpty()) continue;
      if (e.startsWith(QLatin1Char('.'))) e = e.mid(1);
      f.defaultPatterns << (QStringLiteral("*.") + e);
    }
    if (f.defaultPatterns.isEmpty()) continue;

    list << f;
  }

  return list;
}

QList<ResolvedArchiveFormat> resolvedFormats() {
  const Settings& settings = Settings::instance();
  const QMap<QString, ArchiveFormatOverride> overrides = settings.archiveFormatOverrides();

  QList<ResolvedArchiveFormat> resolved;
  for (const ArchiveFormatInfo& info : allFormats()) {
    const ArchiveFormatOverride ov = overrides.value(info.id);

    ResolvedArchiveFormat r;
    r.info     = info;
    r.patterns = ov.patterns.value_or(info.defaultPatterns);

    if (info.source == ArchiveFormatInfo::Source::Plugin) {
      // プラグイン形式の有効 / 無効は既存の disabledArchivePlugins に一本化する
      // (設定 → プラグインの一覧と食い違わせない)。
      r.enabled = !settings.isArchivePluginDisabled(info.pluginId);
    } else {
      r.enabled = ov.enabled.value_or(info.defaultEnabled);
    }

    r.compressionLevel = ov.compressionLevel.value_or(-1);
    r.encryption       = ov.encryption.value_or(QString());
    r.filenameEncoding = ov.filenameEncoding.value_or(QString());

    resolved << r;
  }
  return resolved;
}

ResolvedArchiveFormat resolvedFormat(const QString& id, bool* found) {
  for (const ResolvedArchiveFormat& r : resolvedFormats()) {
    if (r.info.id.compare(id, Qt::CaseInsensitive) == 0) {
      if (found) *found = true;
      return r;
    }
  }
  if (found) *found = false;
  return {};
}

QStringList activePatterns() {
  QStringList patterns;
  for (const ResolvedArchiveFormat& r : resolvedFormats()) {
    if (!r.enabled) continue;
    for (const QString& p : r.patterns) {
      const QString trimmed = p.trimmed();
      if (!trimmed.isEmpty() && !patterns.contains(trimmed, Qt::CaseInsensitive)) {
        patterns << trimmed;
      }
    }
  }
  return patterns;
}

void applyToArchivePath() {
  ArchivePath::setArchivePatterns(activePatterns());
}

}  // namespace ArchiveFormatCatalog
}  // namespace Farman
