#pragma once

#include "viewer/ViewerShortcutMap.h"
#include <QMediaPlayer>
#include <QSize>
#include <QVariantMap>
#include <QWidget>

class QAction;
class QAudioOutput;
class QComboBox;
class QDialog;
class QGraphicsScene;
class QGraphicsVideoItem;
class QGraphicsView;
class QMediaMetaData;
class QLabel;
class QSlider;
class QStackedWidget;
class QTimer;
class QToolBar;
class QToolButton;
class QVBoxLayout;

namespace Farman {

class EnterClickFilter;

// 動画 / 音声プレイヤー widget (media_viewer プラグインの中身)。
// QMediaPlayer + プラットフォームバックエンドでデコードし、動画は
// QVideoWidget、音声のみのファイルはメタデータカード (カバーアート +
// タイトル / アーティスト) を表示する。
// レイアウトは画像ビュアー (ImageView) と同じ「上部 QToolBar + 表示部」
// 構成。再生コントロールはすべてツールバーに並べる。
//
// キー操作:
//   Space        再生 / 一時停止
//   ←/→         5 秒シーク (Shift 併用で 30 秒)
//   ↑/↓         音量 ±5
//   M            ミュート切替
//   L            ループ再生切替
//   I            メディア情報 / メタデータの表示切替
//   F / ダブルクリック  フルスクリーン切替 (動画のみ。Esc で解除)
class MediaView : public QWidget {
  Q_OBJECT

public:
  explicit MediaView(QWidget* parent = nullptr);
  ~MediaView() override;

  // setSource してそのまま再生を開始する。QMediaPlayer::setSource は
  // メタデータのみ先読みし、本体ストリームは再生開始時にロードされる。
  void openFile(const QString& filePath);
  void clearContent();

  // ステータスバー用の 1 行要約 (フォーマット / コーデック / 解像度 / 長さ)。
  // メタデータが揃う前は得られた分だけを返す。
  QString statusInfo() const;

  // ── 外部ウィンドウ (MediaViewerWindow) 連携用 ──
  // ツールバー末尾に widget を追加し、その QAction を返す (表示制御に使う)。
  // ImageView::addToolbarWidget と同じ用途 (「ウィンドウサイズを動画にあわせる」)。
  QAction* addToolbarWidget(QWidget* widget);
  // 動画の自然サイズ (px)。音声 / 未確定時は QSize() (= invalid)。
  QSize naturalVideoSize() const;
  // 動画が描画される領域 (= スクロールエリアのビューポート) の現在サイズ。
  QSize videoAreaSize() const;
  // 「ウィンドウを動画に合わせる」で使う基準倍率 (%)。fit 中は 100 (実寸)、
  // 手動ズーム時は設定倍率。
  int windowFitZoomPercent() const;
  // フルスクリーンを解除する (生存中に呼ぶこと。MediaViewerWindow::closeEvent 用)。
  void exitFullscreen();
  // Fit 中に動画が viewport に収まる実効倍率 (%)。解像度未確定時は m_zoomPercent。
  int effectiveVideoZoomPercent() const;

signals:
  // openFile ごとに最初のロード結末を 1 回だけ通知する。
  // Loaded/Buffered → true、InvalidMedia / エラー → false。
  void loadFinished(bool ok);
  // ステータスバー用の要約が更新されたとき (メタデータ / 長さの確定時)。
  void statusInfoChanged(const QString& info);

protected:
  void keyPressEvent(QKeyEvent* event) override;
  // フルスクリーン中の QVideoWidget はトップレベル化されて MediaView の
  // keyPressEvent が届かないので、eventFilter 経由で同じキー処理に流す。
  bool eventFilter(QObject* watched, QEvent* event) override;

private:
  void setupUi();
  bool handleViewerKey(QKeyEvent* event);
  // 本体 (キーバインド設定) から push されるショートカット割り当てを保持する
  // (ローカル保持・ストレージは読まない)。QMetaObject::invokeMethod で本体から
  // 一様に呼べるよう Q_INVOKABLE。
  Q_INVOKABLE void applyShortcutBindings(const QVariantMap& bindings);
  ViewerShortcutMap m_shortcuts;
  void togglePlayPause();
  void seekBy(qint64 deltaMs);
  void adjustVolume(int delta);
  void setVideoFullScreen(bool on);
  // 動画がフルスクリーン中か (同じシーンに載せたフルスクリーン用ビューが
  // 生きているか)。
  bool isVideoFullScreen() const { return m_fsView != nullptr; }
  // 指定ビューを、動画アイテムがちょうど収まる倍率にフィットさせる
  // (KeepAspectRatio でレターボックス)。ネイティブサイズ未確定時は何もしない。
  void fitVideoView(QGraphicsView* view);
  // フルスクリーン中の下部オーバーレイ操作パネルの表示 / 配置。
  void showFsControls();                  // 表示して自動非表示タイマーを再始動
  void layoutFsControlBar();              // フルスクリーンビュー下部へ配置
  void applyVolume();
  void notifyLoadResult(bool ok);
  void showMessage(const QString& text);
  void updatePlayButton();
  void updateMuteButton();
  void updateTimeLabel();
  void updateMetadataCard();
  void updateCurrentPage();

  // 動画表示の拡大縮小 (ImageView と同じ作法。設定には保存しない)。
  // Fit ON 時はスクロールエリアにフィット、OFF 時は m_zoomPercent で固定。
  void applyVideoZoom();
  // ズーム UI の有効/無効を更新 (Fit 中はコンボを無効化)。
  void updateZoomEnabled();
  // 動画の自然サイズ (px)。コンテナ→トラック→QVideoWidget::sizeHint の順で取得。
  // 音声 / 未確定時は QSize() (= invalid)。
  QSize videoResolution() const;
  // ネイティブ解像度が確定したら一度だけキャッシュし、手動ズーム中なら再適用する。
  // フレーム描画後でないと sizeHint が無効なため、再生中に解決する。
  void updateNativeVideoSize();
  // ズームコンボの表示値を更新する (Fit 中は実効倍率、手動時は m_zoomPercent)。
  void updateZoomComboText();

  // メディア情報 / メタデータダイアログ (ImageView の情報表示と同じ作法)。
  void toggleMediaInfoDialog();
  void refreshMediaInfoDialog();
  QString buildMediaInfoText() const;
  // 1 つの QMediaMetaData が持つ全キーを body へ追記する (取得できた分だけ)。
  // 既知キーは日本語ラベル、未知キーは Qt のキー名で表示する。
  void appendMetaData(QString& body, const QMediaMetaData& md,
                      const QString& indent) const;

  QString formatTime(qint64 ms) const;

  QString m_filePath;
  bool    m_loadNotified = false;

  QMediaPlayer* m_player      = nullptr;
  QAudioOutput* m_audioOutput = nullptr;

  QStackedWidget* m_stack           = nullptr;
  // 動画は QGraphicsScene 上の QGraphicsVideoItem として描画する。QVideoWidget
  // と違いネイティブ動画サーフェス (NSView) を持たないため、フルスクリーンは
  // 「同じシーンを映す 2 つ目のビュー」を出すだけで済み、再ペアレントや出力
  // 切替 (macOS でサーフェスが壊れる) が一切不要になる。
  QGraphicsScene*     m_scene     = nullptr;
  QGraphicsVideoItem* m_videoItem = nullptr;
  QGraphicsView*      m_videoView = nullptr;  // 埋め込み表示 (m_stack 内)
  QGraphicsView*      m_fsView    = nullptr;  // フルスクリーン用。非 null = FS 中
  // 本体レイアウト (フルスクリーン時にツールバーを退避 / 復帰させるため保持)。
  QVBoxLayout*        m_mainLayout   = nullptr;
  // フルスクリーン中だけ生成する下部オーバーレイ操作パネル。既存ツールバーを
  // ここへ移設し、マウス移動で表示・一定時間後に自動非表示する。
  QWidget*            m_fsControlBar = nullptr;
  QTimer*             m_fsControlHideTimer = nullptr;
  QWidget*        m_audioCard       = nullptr;
  QLabel*         m_coverLabel   = nullptr;
  QLabel*         m_titleLabel   = nullptr;
  QLabel*         m_detailLabel  = nullptr;
  QLabel*         m_messageLabel = nullptr;

  // ツールバー (ImageView と同じ QToolBar + toolbarStyleSheet() 構成)
  QToolBar*    m_toolbar          = nullptr;
  QToolButton* m_playButton       = nullptr;
  QToolButton* m_stopButton       = nullptr;
  QSlider*     m_positionSlider   = nullptr;
  QLabel*      m_timeLabel        = nullptr;
  QToolButton* m_loopButton       = nullptr;
  QComboBox*   m_rateCombo        = nullptr;
  QToolButton* m_muteButton       = nullptr;
  QSlider*     m_volumeSlider     = nullptr;
  QComboBox*   m_zoomCombo        = nullptr;
  QToolButton* m_fitButton        = nullptr;
  QToolButton* m_fullScreenButton = nullptr;
  QToolButton* m_infoButton        = nullptr;
  QDialog*     m_infoDialog         = nullptr;  // モードレス。WA_DeleteOnClose=false
  // 情報 (i) ボタンの QAction。addToolbarWidget が i の直前に挿入するのに使う。
  QAction*     m_infoAction        = nullptr;
  // QToolBar::addWidget が返す QAction。ツールバー内ウィジェットの表示 /
  // 非表示はウィジェット直接ではなくこの action で切り替える必要がある。
  // ズーム関連 / フルスクリーンは動画のみ表示 (音声では非表示)。
  QAction*     m_zoomLabelAction  = nullptr;
  QAction*     m_zoomComboAction  = nullptr;
  QAction*     m_fitAction        = nullptr;
  QAction*     m_fullScreenAction = nullptr;

  // 動画ズームの実効状態 (ローカル上書き。Settings には保存しない)。
  int  m_zoomPercent = 100;
  bool m_fitToWindow = true;
  // 確定したネイティブ動画解像度のキャッシュ (100% の基準)。未確定なら空。
  QSize m_nativeVideoSize;

  // ツールバー内ボタンの Enter→クリック化 (ImageView と同じ作法)。
  EnterClickFilter* m_clickFilter = nullptr;
};

} // namespace Farman
