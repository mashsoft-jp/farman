#pragma once

#include <QMediaPlayer>
#include <QWidget>

class QAction;
class QAudioOutput;
class QComboBox;
class QDialog;
class QMediaMetaData;
class QLabel;
class QSlider;
class QStackedWidget;
class QToolBar;
class QToolButton;
class QVideoWidget;

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

signals:
  // openFile ごとに最初のロード結末を 1 回だけ通知する。
  // Loaded/Buffered → true、InvalidMedia / エラー → false。
  void loadFinished(bool ok);

protected:
  void keyPressEvent(QKeyEvent* event) override;
  // フルスクリーン中の QVideoWidget はトップレベル化されて MediaView の
  // keyPressEvent が届かないので、eventFilter 経由で同じキー処理に流す。
  bool eventFilter(QObject* watched, QEvent* event) override;

private:
  void setupUi();
  bool handleViewerKey(QKeyEvent* event);
  void togglePlayPause();
  void seekBy(qint64 deltaMs);
  void adjustVolume(int delta);
  void setVideoFullScreen(bool on);
  void applyVolume();
  void notifyLoadResult(bool ok);
  void showMessage(const QString& text);
  void updatePlayButton();
  void updateMuteButton();
  void updateTimeLabel();
  void updateMetadataCard();
  void updateCurrentPage();

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

  QStackedWidget* m_stack        = nullptr;
  QVideoWidget*   m_videoWidget  = nullptr;
  QWidget*        m_audioCard    = nullptr;
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
  QToolButton* m_fullScreenButton = nullptr;
  QToolButton* m_infoButton        = nullptr;
  QDialog*     m_infoDialog         = nullptr;  // モードレス。WA_DeleteOnClose=false
  // QToolBar::addWidget が返す QAction。ツールバー内ウィジェットの表示 /
  // 非表示はウィジェット直接ではなくこの action で切り替える必要がある。
  QAction*     m_fullScreenAction = nullptr;

  // ツールバー内ボタンの Enter→クリック化 (ImageView と同じ作法)。
  EnterClickFilter* m_clickFilter = nullptr;
};

} // namespace Farman
