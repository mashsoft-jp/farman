#pragma once

#include "types.h"
#include <QImage>
#include <QPixmap>
#include <QPointer>
#include <QSize>
#include <QString>
#include <QWidget>

class QComboBox;
class QLabel;
class QMovie;
class QScrollArea;
class QToolBar;
class QToolButton;

namespace Farman {

class ImageDisplay;
class EnterClickFilter;

// 画像ファイル表示用ウィジェット。
//   - 上部のツールバーで Zoom (%) / Fit / Animation をその場で上書き可能
//     (上書き内容は Settings に保存されない)
//   - 透明部分の表示方法 (単色 / チェック柄) と背景色は Settings から取得
//   - GIF / WebP (アニメあり) は QMovie で再生 (APNG は Qt PNG プラグインの
//     ビルド依存のため現状非対応)
//   - Settings::settingsChanged でローカルの上書きはリセットされる
class ImageView : public QWidget {
  Q_OBJECT

public:
  // 非同期ロード用の中間表現。アニメ画像はメインスレッドで QMovie を作る
  // 必要があるため bg では QImage を使わず「アニメかどうか」だけ確定させる。
  struct PreparedLoad {
    bool    ok          = false;
    QString filePath;
    bool    isAnimated  = false;
    QImage  image;       // 静止画のときのみ有効
  };

  explicit ImageView(QWidget* parent = nullptr);

  // 失敗時 (open エラー or 画像でない) は false。
  bool loadFile(const QString& filePath);

  // ワーカースレッドで実行可能なロード処理 (UI 非依存)。
  static PreparedLoad prepareLoad(const QString& filePath);
  // ワーカーの結果を UI に反映する。必ずメインスレッドから呼ぶこと。
  void applyPreparedLoad(const PreparedLoad& result);

  // 表示中ファイルをクリア。
  void clearContent();

  // ステータスバー表示用の要約 ("PNG · 800x600 · 24 KB" 等、アニメ時はフレーム数も)
  QString statusInfo() const;

  // 読み込み済みの画像の自然サイズ (px)。アニメ画像でも 1 フレーム目の画像
  // サイズを返す。未ロード or ロード失敗時は QSize() (= invalid)。
  // **ファイルそのもの**のサイズ (= 回転を反映しない) を返す。
  QSize naturalImageSize() const { return m_naturalImageSize; }

  // 現在の表示状態 (回転を含む) における自然サイズ。90°/270° のときは
  // naturalImageSize() の幅と高さを入れ替えたものを返す。
  // 「ウィンドウサイズを画像にあわせる」ボタンが画面上の見た目に合わせて
  // ウィンドウを調整するために使う。
  QSize displayNaturalImageSize() const;

  // 現在の **実効ズーム率** (%)。マニュアルズーム時は m_zoomPercent、
  // Fit-to-Window モード時はビューポートに収めるための自動倍率を返す。
  // 「ウィンドウサイズを画像にあわせる」処理で「画像 × 実効ズーム」 を
  // 計算するために使う。
  int effectiveZoomPercent() const;

  // 画像が描画されるエリア (= スクロールエリアのビューポート) の現在サイズ。
  // これと window 全体サイズの差が「ツールバー + ウィンドウ枠」分の chrome。
  QSize imageAreaSize() const;

  // ImageDisplay から参照される実効透明モード (ローカル上書き)。
  ImageTransparencyMode transparencyMode() const { return m_transparencyMode; }

  // 既存のツールバー (Zoom / Fit / Anim / Transparency が並んでいる行) の
  // 末尾の `addStretch()` の手前に追加 widget を挿入する。
  // ImageViewerWindow が「ウィンドウサイズを画像にあわせる」ボタンなど、
  // External モード固有の要素を同じ列に並べるために使う。
  void addToolbarWidget(QWidget* widget);

protected:
  bool eventFilter(QObject* watched, QEvent* event) override;

private slots:
  // ツールバーの "i" ボタンと `i` キーから呼ばれる。
  // 既にダイアログが開いていれば閉じる、無ければ最新のメタデータで開く
  // (= トグル動作、モードレス)。
  void toggleImageInfoDialog();
  // applyPreparedLoad から呼ばれる。ダイアログが開いている状態で別のファイルを
  // 選んだ場合に、内容を最新に差し替える。
  void refreshImageInfoDialog();
  // 内部ヘルパ: 現在の m_filePath からメタデータテキストを組み立てる。
  QString buildImageInfoText() const;
  // ツールバーの回転ボタンから呼ばれる。表示の回転を 90 度ずつ進める。
  void rotateCw90();

private:
  void setupUi();
  void syncFromSettings();
  void applyDisplayState();
  void updateZoomEnabled();
  static bool detectAnimated(const QString& filePath);

  // ツールバー
  // メインウィンドウのツールバーと同じ QToolBar を使い、ネイティブの
  // ツールバー風デザインに揃える。addToolbarWidget で末尾に widget を追加。
  QToolBar*    m_toolbar           = nullptr;
  QComboBox*   m_zoomCombo         = nullptr;
  // 「ウィンドウに合わせる」のチェック付きトグルボタン (アイコンのみ)。
  QToolButton* m_fitButton         = nullptr;
  // アニメ再生 / 停止のトグル。OFF (停止中) は ▶、ON (再生中) は ⏸ アイコンを
  // 都度切替える。GIF / WebP 等のアニメ画像でのみ有効化される。
  QToolButton* m_animButton        = nullptr;
  // 透明部分の表示モード (Checker / SolidColor) のトグル。OFF = Checker、
  // ON = SolidColor。
  QToolButton* m_transparencyButton = nullptr;
  // 画像メタデータ (フォーマット情報・QImageReader::text / 将来は Exif も) を
  // モーダルダイアログで表示するボタン。
  QToolButton* m_infoButton          = nullptr;
  // 時計回り 90° ずつ画面表示だけを回転させるボタン。ファイル切替で 0 にリセット。
  // ファイル本体は変更しない (= 保存しない)。
  QToolButton* m_rotateCwButton      = nullptr;
  // 回転角度の表示用ラベル (例: "0°" / "90°" / "180°" / "270°")。
  // 回転ボタンの右隣に置く。
  QLabel*      m_rotationLabel       = nullptr;

  // 表示部
  QScrollArea*  m_scrollArea = nullptr;
  ImageDisplay* m_display    = nullptr;

  // 既存ツールバー末尾のウィジェット (= 直前の addToolbarWidget で追加された
  // もの、初期は m_transparencyCombo)。新たに addToolbarWidget で追加する
  // ときに setTabOrder のアンカーとして使う。
  QPointer<QWidget> m_lastToolbarWidget;

  // ツールバー内ボタンの Enter→クリック化に使うフィルタ。setupUi で生成 +
  // 既存ボタンに install。後から addToolbarWidget で追加された widget にも
  // インストールするためにメンバとして保持。
  EnterClickFilter* m_clickFilter = nullptr;

  QString             m_filePath;
  QPointer<QMovie>    m_movie;        // アニメ再生用 (m_display の親で破棄)
  bool                m_fileIsAnimated = false;

  // メタデータ表示用のモードレスダイアログ (i キー / "i" ボタンでトグル)。
  // ファイル切替時に内容を更新する。
  QPointer<class QDialog> m_infoDialog;

  // 表示に使う実効設定 (ローカル上書き)。Settings 変更で再同期される。
  int                   m_zoomPercent     = 100;
  bool                  m_fitToWindow     = false;
  bool                  m_animation       = false;
  ImageTransparencyMode m_transparencyMode = ImageTransparencyMode::Checker;

  // 読み込み済みの画像の自然サイズ。loadFile / applyPreparedLoad で更新する。
  QSize                 m_naturalImageSize;
};

} // namespace Farman
