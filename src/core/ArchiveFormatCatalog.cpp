#include "ArchiveFormatCatalog.h"

#include "ArchiveDispatcher.h"
#include "ArchiveEntryName.h"
#include "settings/Settings.h"
#include "utils/ArchivePath.h"
#include "utils/MediaMatchers.h"

#include <QFileInfo>

#include <QCoreApplication>

namespace Farman {
namespace ArchiveFormatCatalog {

namespace {

// 表示名は呼び出し側で QT_TRANSLATE_NOOP("ArchiveFormatCatalog", ...) と
// マークしておき (lupdate はマクロを見て抽出する)、実際の翻訳はここで行う。
// ラッパ関数越しに translate すると lupdate が文言を拾えないため、抽出用の
// マークと翻訳の実行を分けている。
QString name(const char* text) {
  return QCoreApplication::translate("ArchiveFormatCatalog", text);
}

using Enc = ArchiveFormatInfo::Encryption;

// 作成もできるコンテナ形式 (v0.9.9 までと同じ 5 形式)。
ArchiveFormatInfo makeCreatable(const QString& id, const char* displayName,
                                const QStringList& patterns,
                                bool supportsLevel, Enc encryption) {
  ArchiveFormatInfo f;
  f.id                       = id;
  f.displayName              = name(displayName);
  f.defaultPatterns          = patterns;
  f.source                   = ArchiveFormatInfo::Source::Builtin;
  f.defaultEnabled           = true;
  f.canCreate                = true;
  f.supportsCompressionLevel = supportsLevel;
  f.encryption               = encryption;
  f.supportsFilenameEncoding = true;
  if (encryption == Enc::ReadWrite) {
    // 旧式 ZipCrypto は脆弱なので既定は AES-256 (v0.9.9 までの決め打ちと同じ)。
    f.defaultEncryption = QStringLiteral("aes256");
  }
  return f;
}

// 読取のみのコンテナ形式。
//
// 既定で有効にしてよいかは「3 OS の配布ビルドの libarchive がそのコーデックを
// 内蔵しているか」で決まる。内蔵していないと、一覧には出るのに開けない形式が
// できてしまう。調査 (2026-09-05) の結果、ここに載せた形式はすべて内蔵されて
// いるので既定で有効にしている:
//   - ISO9660 / CAB / cpio / ar / RAR / 7-Zip の各リーダは libarchive 内蔵
//   - xar は libxml2 / expat 依存だが 3 OS とも同梱済み
//   - zstd / lz4 / lzma / lzip / compress の各フィルタも 3 OS とも同梱済み
//
// LZO (*.lzo) は**対応外**としてカタログに載せない (2026-09-05 決定)。配布する
// 3 OS のビルドはいずれも libarchive に LZO が入っておらず、libarchive は外部
// lzop コマンドの起動にフォールバックして失敗する:
//   - macOS   : Homebrew の libarchive は liblzo2 を非リンク
//                (bsdtar --lzop が "Can't launch external program: lzop")
//   - Windows : vcpkg の libarchive 既定 features に lzo が無い
//   - Linux   : ubuntu-22.04 の libarchive13 の依存に liblzo2 が無い
// 一覧に出しても開けない形式が並ぶだけなので、既定 OFF で残すのではなく外す。
ArchiveFormatInfo makeReadOnly(const QString& id, const char* displayName,
                               const QStringList& patterns,
                               Enc encryption = Enc::None,
                               bool supportsFilenameEncoding = false) {
  ArchiveFormatInfo f;
  f.id                       = id;
  f.displayName              = name(displayName);
  f.defaultPatterns          = patterns;
  f.source                   = ArchiveFormatInfo::Source::Builtin;
  f.defaultEnabled           = true;
  f.canCreate                = false;
  f.supportsCompressionLevel = false;
  f.encryption               = encryption;
  f.supportsFilenameEncoding = supportsFilenameEncoding;
  return f;
}

// 中身 1 エントリの単一ファイル圧縮 (tar を伴わない .gz / .xz など)。
ArchiveFormatInfo makeSingleFile(const QString& id, const char* displayName,
                                 const QStringList& patterns) {
  ArchiveFormatInfo f = makeReadOnly(id, displayName, patterns);
  f.singleFileCompression = true;
  return f;
}

QList<ArchiveFormatInfo> buildBuiltinFormats() {
  QList<ArchiveFormatInfo> list;

  // ── 作成もできる形式 ──────────────────────────
  list << makeCreatable(QStringLiteral("zip"), QT_TRANSLATE_NOOP("ArchiveFormatCatalog", "ZIP"),
                        {QStringLiteral("*.zip")}, true, Enc::ReadWrite);
  list << makeCreatable(QStringLiteral("tar"), QT_TRANSLATE_NOOP("ArchiveFormatCatalog", "TAR (uncompressed)"),
                        {QStringLiteral("*.tar")}, false, Enc::None);
  list << makeCreatable(QStringLiteral("tar.gz"), QT_TRANSLATE_NOOP("ArchiveFormatCatalog", "TAR + gzip"),
                        {QStringLiteral("*.tar.gz"), QStringLiteral("*.tgz")},
                        true, Enc::None);
  list << makeCreatable(QStringLiteral("tar.bz2"), QT_TRANSLATE_NOOP("ArchiveFormatCatalog", "TAR + bzip2"),
                        {QStringLiteral("*.tar.bz2"), QStringLiteral("*.tbz2")},
                        true, Enc::None);
  list << makeCreatable(QStringLiteral("tar.xz"), QT_TRANSLATE_NOOP("ArchiveFormatCatalog", "TAR + xz"),
                        {QStringLiteral("*.tar.xz"), QStringLiteral("*.txz")},
                        true, Enc::None);

  // ── 読取のみのコンテナ形式 ────────────────────
  list << makeReadOnly(QStringLiteral("tar.zst"), QT_TRANSLATE_NOOP("ArchiveFormatCatalog", "TAR + Zstandard"),
                       {QStringLiteral("*.tar.zst"), QStringLiteral("*.tzst")});
  list << makeReadOnly(QStringLiteral("tar.lz4"), QT_TRANSLATE_NOOP("ArchiveFormatCatalog", "TAR + LZ4"),
                       {QStringLiteral("*.tar.lz4")});
  list << makeReadOnly(QStringLiteral("tar.lzma"), QT_TRANSLATE_NOOP("ArchiveFormatCatalog", "TAR + LZMA"),
                       {QStringLiteral("*.tar.lzma"), QStringLiteral("*.tlz")});
  list << makeReadOnly(QStringLiteral("tar.lz"), QT_TRANSLATE_NOOP("ArchiveFormatCatalog", "TAR + lzip"),
                       {QStringLiteral("*.tar.lz")});
  list << makeReadOnly(QStringLiteral("tar.Z"), QT_TRANSLATE_NOOP("ArchiveFormatCatalog", "TAR + compress"),
                       {QStringLiteral("*.tar.Z"), QStringLiteral("*.taz")});
  list << makeReadOnly(QStringLiteral("7z"), QT_TRANSLATE_NOOP("ArchiveFormatCatalog", "7-Zip"),
                       {QStringLiteral("*.7z")}, Enc::ReadOnly, true);
  list << makeReadOnly(QStringLiteral("rar"), QT_TRANSLATE_NOOP("ArchiveFormatCatalog", "RAR"),
                       {QStringLiteral("*.rar")}, Enc::ReadOnly, true);
  list << makeReadOnly(QStringLiteral("iso"), QT_TRANSLATE_NOOP("ArchiveFormatCatalog", "ISO9660 disc image"),
                       {QStringLiteral("*.iso")});
  list << makeReadOnly(QStringLiteral("cab"), QT_TRANSLATE_NOOP("ArchiveFormatCatalog", "Microsoft Cabinet"),
                       {QStringLiteral("*.cab")}, Enc::None, true);
  list << makeReadOnly(QStringLiteral("xar"), QT_TRANSLATE_NOOP("ArchiveFormatCatalog", "xar / pkg"),
                       {QStringLiteral("*.xar"), QStringLiteral("*.pkg")});
  list << makeReadOnly(QStringLiteral("cpio"), QT_TRANSLATE_NOOP("ArchiveFormatCatalog", "cpio"),
                       {QStringLiteral("*.cpio")});
  list << makeReadOnly(QStringLiteral("ar"), QT_TRANSLATE_NOOP("ArchiveFormatCatalog", "ar / Debian package"),
                       {QStringLiteral("*.ar"), QStringLiteral("*.a"),
                        QStringLiteral("*.deb")});

  // ── 単一ファイル圧縮 ─────────────────────────
  // tar 併用形 (*.tar.gz 等) より後に置くが、照合は「最長のパターン優先」で
  // 決めるのでカタログ順には依存しない。
  list << makeSingleFile(QStringLiteral("gz"),   QT_TRANSLATE_NOOP("ArchiveFormatCatalog", "gzip"),      {QStringLiteral("*.gz")});
  list << makeSingleFile(QStringLiteral("bz2"),  QT_TRANSLATE_NOOP("ArchiveFormatCatalog", "bzip2"),     {QStringLiteral("*.bz2")});
  list << makeSingleFile(QStringLiteral("xz"),   QT_TRANSLATE_NOOP("ArchiveFormatCatalog", "xz"),        {QStringLiteral("*.xz")});
  list << makeSingleFile(QStringLiteral("lzma"), QT_TRANSLATE_NOOP("ArchiveFormatCatalog", "LZMA"),      {QStringLiteral("*.lzma")});
  list << makeSingleFile(QStringLiteral("zst"),  QT_TRANSLATE_NOOP("ArchiveFormatCatalog", "Zstandard"), {QStringLiteral("*.zst")});
  list << makeSingleFile(QStringLiteral("lz4"),  QT_TRANSLATE_NOOP("ArchiveFormatCatalog", "LZ4"),       {QStringLiteral("*.lz4")});
  list << makeSingleFile(QStringLiteral("lz"),   QT_TRANSLATE_NOOP("ArchiveFormatCatalog", "lzip"),      {QStringLiteral("*.lz")});
  list << makeSingleFile(QStringLiteral("Z"),    QT_TRANSLATE_NOOP("ArchiveFormatCatalog", "compress"),  {QStringLiteral("*.Z")});

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

  // プラグイン形式を同じ一覧に合流させる。ロードできなかったもの (ユーザーが
  // 無効化した / 外部プラグイン読込みが OFF / ロード失敗) も載せる。載せないと
  // 一度無効にしたプラグインが一覧から消えて再有効化できなくなるし、ロード
  // エラーにも気づけない。実際の状態は pluginRecord 側で見せる。
  for (const ArchivePluginRecord& rec : ArchiveDispatcher::instance().pluginRecords()) {
    if (rec.pluginId.isEmpty()) continue;  // ID 不明では設定に紐付けられない

    ArchiveFormatInfo f;
    f.id           = rec.pluginId;
    f.pluginId     = rec.pluginId;
    f.pluginRecord = rec;
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
    // 拡張子が取れないほど早い段階で失敗したプラグインもあるが、その場合でも
    // 一覧には出す (パターンが無いので認識には寄与しない)。

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
    r.encryption       = ov.encryption.value_or(info.defaultEncryption);
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

bool isSingleFileCompression(const QString& fileName) {
  const QString name = QFileInfo(fileName).fileName();
  if (name.isEmpty()) return false;

  const QList<ResolvedArchiveFormat> formats = resolvedFormats();

  // コンテナ形式が先。単一ファイル圧縮のパターンは短い接尾辞なので、
  // コンテナ形式のファイル名にも当たってしまう ("x.tar.gz" は gzip の
  // "*.gz" にも一致する)。単一ファイル圧縮と判定すると読み取り側が raw を
  // 有効にし、エントリ名を「圧縮拡張子を剥がした 1 個の名前」に置き換えて
  // しまうので、tar の中身が全部 1 エントリに潰れる。コンテナ形式に一致する
  // 名前は、単一ファイル圧縮ではないと先に決めてしまう。
  for (const ResolvedArchiveFormat& r : formats) {
    if (!r.enabled || r.info.singleFileCompression) continue;
    if (MediaMatchers::fileNameMatches(r.patterns, name)) return false;
  }

  for (const ResolvedArchiveFormat& r : formats) {
    if (!r.enabled || !r.info.singleFileCompression) continue;
    if (MediaMatchers::fileNameMatches(r.patterns, name)) return true;
  }
  return false;
}

void applyToArchivePath() {
  ArchivePath::setArchivePatterns(activePatterns());

  // エントリ名の文字コード指定も同じタイミングで流し込む。指定のある形式
  // (= 自動判別以外) だけを渡すので、既定のままなら空リストになり、復号側は
  // 従来どおり全部自動判別で動く。
  QList<FilenameEncodingRule> rules;
  for (const ResolvedArchiveFormat& r : resolvedFormats()) {
    if (!r.enabled || r.filenameEncoding.isEmpty()) continue;
    rules.append({r.patterns, r.filenameEncoding});
  }
  setFilenameEncodingRules(rules);
}

}  // namespace ArchiveFormatCatalog
}  // namespace Farman
