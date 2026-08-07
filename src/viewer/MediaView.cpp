#include "MediaView.h"
#include "settings/Settings.h"
#include "keybinding/ViewerKeyBindingManager.h"
#include "utils/EnterClickFilter.h"

#include <QAction>
#include <QAudio>
#include <QAudioOutput>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFileInfo>
#include <QFont>
#include <QFontDatabase>
#include <QFile>
#include <QGraphicsScene>
#include <QGraphicsVideoItem>
#include <QGraphicsView>
#include <QHBoxLayout>
#include <QHash>
#include <QPainter>
#include <QSvgRenderer>
#include <QTimer>
#include <QKeyEvent>
#include <QLabel>
#include <QLocale>
#include <QMediaMetaData>
#include <QSize>
#include <QStringList>
#include <QPlainTextEdit>
#include <QMouseEvent>
#include <QFrame>
#include <QPalette>
#include <QSlider>
#include <QStackedWidget>
#include <QStyle>
#include <QStyleOptionSlider>
#include <QToolBar>
#include <QToolButton>
#include <QVBoxLayout>

namespace Farman {

namespace {
// ←/→ のシーク量。Shift 併用で大きく飛ぶ (SPEC: 数秒単位 / 大きく)。
constexpr qint64 kSeekStepMs      = 5000;
constexpr qint64 kSeekLargeStepMs = 30000;
constexpr int    kVolumeStep      = 5;

// ツールバー用 SVG アイコン (固定 #6a6a6a) を白系に差し替えて描画する。
// フルスクリーンの暗い半透明パネル上ではグレーが沈んで見えないため。
QIcon whiteToolbarIcon(const QString& resPath) {
  QFile f(resPath);
  if (!f.open(QIODevice::ReadOnly)) return QIcon(resPath);
  QByteArray svg = f.readAll();
  svg.replace("#6a6a6a", "#e8e8e8");
  QSvgRenderer renderer(svg);
  QPixmap pm(48, 48);  // 2x で描いて Retina でも滲まないように
  pm.fill(Qt::transparent);
  QPainter p(&pm);
  renderer.render(&p);
  p.end();
  pm.setDevicePixelRatio(2.0);
  return QIcon(pm);
}

// 溝の任意の位置をクリックしたら、その位置へ即移動するスライダー。
// 既定の QSlider はクリックでページ単位送りになるだけで、シーク/音量バーでは
// 「クリックした位置で即変わってほしい」という挙動にならない。マウス押下時に
// クリック座標から値を求めて setValue + sliderMoved を発火し、既存の接続
// (sliderMoved→シーク / valueChanged→音量) でそのまま反映させる。
// その後 base 実装を呼ぶことで、つまみがクリック位置に来た状態からドラッグも
// 継続できる。新しい signal/slot は追加しないので Q_OBJECT は不要。
class ClickSeekSlider : public QSlider {
public:
  using QSlider::QSlider;

protected:
  void mousePressEvent(QMouseEvent* ev) override {
    if (ev->button() == Qt::LeftButton) {
      QStyleOptionSlider opt;
      initStyleOption(&opt);
      const QRect groove =
        style()->subControlRect(QStyle::CC_Slider, &opt, QStyle::SC_SliderGroove, this);
      const QRect handle =
        style()->subControlRect(QStyle::CC_Slider, &opt, QStyle::SC_SliderHandle, this);
      // つまみそのものを掴んだときは通常のドラッグに任せる。
      if (!handle.contains(ev->position().toPoint())) {
        const int span = (orientation() == Qt::Horizontal)
                           ? groove.width()  - handle.width()
                           : groove.height() - handle.height();
        const int pos  = (orientation() == Qt::Horizontal)
                           ? ev->position().toPoint().x() - groove.x() - handle.width()  / 2
                           : ev->position().toPoint().y() - groove.y() - handle.height() / 2;
        const int value = QStyle::sliderValueFromPosition(
          minimum(), maximum(), pos, span, opt.upsideDown);
        setValue(value);
        emit sliderMoved(value);
      }
    }
    QSlider::mousePressEvent(ev);
  }
};
} // namespace

MediaView::MediaView(QWidget* parent)
  : QWidget(parent)
{
  // 表示の既定 (fit-to-window / 既定拡大率) を設定から取得する。ImageView と同様、
  // ツールバー操作によるローカル上書きは Settings には保存しない。
  const Settings& s = Settings::instance();
  m_fitToWindow = s.mediaViewerFitToWindow();
  m_zoomPercent = s.mediaViewerZoomPercent();
  setupUi();
}

MediaView::~MediaView() {
  if (m_player) {
    m_player->stop();
    m_player->setVideoOutput(static_cast<QObject*>(nullptr));
  }
  // フルスクリーン用ビューはトップレベル (MediaView の子ではない) なので明示的に
  // 破棄する。QGraphicsView は通常のウィジェットで、動画はシーン側が描くため
  // ネイティブサーフェスの後始末は不要 (QVideoWidget のような createWinId 再帰の
  // 心配がない)。シーン / アイテムは MediaView の子として通常どおり破棄される。
  if (m_fsView) {
    delete m_fsView;
    m_fsView = nullptr;
  }
}

void MediaView::setupUi() {
  setFocusPolicy(Qt::StrongFocus);

  m_player      = new QMediaPlayer(this);
  m_audioOutput = new QAudioOutput(this);
  m_player->setAudioOutput(m_audioOutput);

  // ── 表示ページ (動画 / 音声カード / メッセージ) ──
  // 動画は QGraphicsScene 上の QGraphicsVideoItem として描画する。QVideoWidget
  // (ネイティブ NSView) と違い、フルスクリーンは同じシーンを映す 2 つ目のビュー
  // を出すだけで済む。出力先 (m_videoItem) は一度設定したら切り替えない。
  m_scene     = new QGraphicsScene(this);
  m_videoItem = new QGraphicsVideoItem;
  m_videoItem->setAspectRatioMode(Qt::KeepAspectRatio);
  m_scene->addItem(m_videoItem);
  m_player->setVideoOutput(m_videoItem);
  // ネイティブ解像度が判明したらアイテムサイズ / シーン矩形を確定し、
  // ズーム / フィットを適用し直す。
  connect(m_videoItem, &QGraphicsVideoItem::nativeSizeChanged,
          this, [this](const QSizeF&) { updateNativeVideoSize(); });

  m_videoView = new QGraphicsView(m_scene, this);
  m_videoView->setFrameShape(QFrame::NoFrame);
  m_videoView->setBackgroundBrush(Qt::black);
  m_videoView->setAlignment(Qt::AlignCenter);
  m_videoView->setDragMode(QGraphicsView::NoDrag);
  m_videoView->setFocusPolicy(Qt::StrongFocus);
  m_videoView->installEventFilter(this);            // キー / ダブルクリック
  m_videoView->viewport()->installEventFilter(this);  // リサイズで再フィット

  m_audioCard = new QWidget(this);
  {
    auto* cardLayout = new QVBoxLayout(m_audioCard);
    cardLayout->addStretch();

    m_coverLabel = new QLabel(m_audioCard);
    m_coverLabel->setAlignment(Qt::AlignCenter);
    cardLayout->addWidget(m_coverLabel);

    m_titleLabel = new QLabel(m_audioCard);
    m_titleLabel->setAlignment(Qt::AlignCenter);
    m_titleLabel->setWordWrap(true);
    QFont titleFont = m_titleLabel->font();
    titleFont.setPointSizeF(titleFont.pointSizeF() * 1.4);
    titleFont.setBold(true);
    m_titleLabel->setFont(titleFont);
    cardLayout->addWidget(m_titleLabel);

    m_detailLabel = new QLabel(m_audioCard);
    m_detailLabel->setAlignment(Qt::AlignCenter);
    m_detailLabel->setWordWrap(true);
    // テーマ (Light/Dark) 追随の抑制色。palette() 関数はテーマ切替時に
    // 自動で再解決される。
    m_detailLabel->setStyleSheet(QStringLiteral("color: palette(mid);"));
    cardLayout->addWidget(m_detailLabel);

    cardLayout->addStretch();
  }

  m_messageLabel = new QLabel(this);
  m_messageLabel->setAlignment(Qt::AlignCenter);
  m_messageLabel->setWordWrap(true);

  m_stack = new QStackedWidget(this);
  m_stack->addWidget(m_videoView);
  m_stack->addWidget(m_audioCard);
  m_stack->addWidget(m_messageLabel);
  m_stack->setCurrentWidget(m_audioCard);

  // ── ツールバー ──
  // 画像ビュアー (ImageView) と同じ構成: 上部に QToolBar を置き、再生
  // コントロールをすべてそこに並べる。視覚スタイルも共通の
  // toolbarStyleSheet() で揃える。
  m_toolbar = new QToolBar(this);
  m_toolbar->setMovable(false);
  m_toolbar->setFloatable(false);
  m_toolbar->setIconSize(QSize(20, 20));
  applyToolbarStyle(m_toolbar);

  m_playButton = new QToolButton(m_toolbar);
  m_playButton->setToolTip(tr("Play / Pause (Space)"));
  m_playButton->setFocusPolicy(Qt::StrongFocus);
  connect(m_playButton, &QToolButton::clicked,
          this,         &MediaView::togglePlayPause);
  m_toolbar->addWidget(m_playButton);

  m_stopButton = new QToolButton(m_toolbar);
  m_stopButton->setIcon(QIcon(QStringLiteral(":/icons/toolbar/stop.svg")));
  m_stopButton->setToolTip(tr("Stop (S)"));
  m_stopButton->setFocusPolicy(Qt::StrongFocus);
  connect(m_stopButton, &QToolButton::clicked,
          m_player,     &QMediaPlayer::stop);
  m_toolbar->addWidget(m_stopButton);

  // 矢印キーをビュアー全体のシーク / 音量操作に使うので、スライダ自体には
  // キーボードフォーカスを与えない (マウス操作専用)。
  // Expanding ポリシーでツールバーの余白を全部使う。
  // 溝のクリックでその位置へ即シークできるよう ClickSeekSlider を使う。
  m_positionSlider = new ClickSeekSlider(Qt::Horizontal, m_toolbar);
  m_positionSlider->setFocusPolicy(Qt::NoFocus);
  m_positionSlider->setEnabled(false);
  m_positionSlider->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
  m_positionSlider->setToolTip(tr("Seek (Left / Right, Shift for larger steps)"));
  connect(m_positionSlider, &QSlider::sliderMoved, this, [this](int value) {
    m_player->setPosition(value);
  });
  m_toolbar->addWidget(m_positionSlider);

  m_timeLabel = new QLabel(QStringLiteral("0:00 / 0:00"), m_toolbar);
  m_timeLabel->setContentsMargins(6, 0, 6, 0);
  m_timeLabel->setToolTip(tr("Elapsed / total time"));
  m_toolbar->addWidget(m_timeLabel);

  // 再生速度ドロップダウンはシークバー (＋時間表示) のすぐ隣に置く。
  m_rateCombo = new QComboBox(m_toolbar);
  m_rateCombo->addItem(QStringLiteral("0.5x"), 0.5);
  m_rateCombo->addItem(QStringLiteral("1.0x"), 1.0);
  m_rateCombo->addItem(QStringLiteral("1.5x"), 1.5);
  m_rateCombo->addItem(QStringLiteral("2.0x"), 2.0);
  m_rateCombo->setCurrentIndex(1);
  m_rateCombo->setToolTip(tr("Playback speed ([ slower / ] faster)"));
  m_rateCombo->setFocusPolicy(Qt::StrongFocus);
  connect(m_rateCombo, &QComboBox::currentIndexChanged, this, [this](int) {
    m_player->setPlaybackRate(m_rateCombo->currentData().toReal());
  });
  m_toolbar->addWidget(m_rateCombo);

  m_loopButton = new QToolButton(m_toolbar);
  m_loopButton->setCheckable(true);
  m_loopButton->setIcon(QIcon(QStringLiteral(":/icons/toolbar/loop.svg")));
  m_loopButton->setToolTip(tr("Loop playback (L)"));
  m_loopButton->setFocusPolicy(Qt::StrongFocus);
  connect(m_loopButton, &QToolButton::toggled, this, [this](bool checked) {
    m_player->setLoops(checked ? QMediaPlayer::Infinite : QMediaPlayer::Once);
  });
  m_loopButton->setChecked(Settings::instance().mediaViewerLoop());
  m_toolbar->addWidget(m_loopButton);

  m_muteButton = new QToolButton(m_toolbar);
  m_muteButton->setCheckable(true);
  m_muteButton->setToolTip(tr("Mute (M)"));
  m_muteButton->setFocusPolicy(Qt::StrongFocus);
  connect(m_muteButton, &QToolButton::toggled, this, [this](bool checked) {
    m_audioOutput->setMuted(checked);
    updateMuteButton();
  });
  m_toolbar->addWidget(m_muteButton);

  // 音量バーもクリック位置へ即移動できるよう ClickSeekSlider を使う。
  m_volumeSlider = new ClickSeekSlider(Qt::Horizontal, m_toolbar);
  m_volumeSlider->setFocusPolicy(Qt::NoFocus);
  m_volumeSlider->setRange(0, 100);
  m_volumeSlider->setValue(Settings::instance().mediaViewerVolume());
  m_volumeSlider->setFixedWidth(100);
  m_volumeSlider->setToolTip(tr("Volume (Up / Down)"));
  connect(m_volumeSlider, &QSlider::valueChanged,
          this,           &MediaView::applyVolume);
  m_toolbar->addWidget(m_volumeSlider);

  // ── 動画ズーム (ImageView と同じ Zoom% コンボ + Fit トグル) ──
  // 動画のみ表示し、音声では updateCurrentPage で非表示にする。
  auto* zoomLabel = new QLabel(tr("Zoom:"), m_toolbar);
  zoomLabel->setContentsMargins(6, 0, 2, 0);
  m_zoomLabelAction = m_toolbar->addWidget(zoomLabel);

  m_zoomCombo = new QComboBox(m_toolbar);
  m_zoomCombo->setEditable(true);
  for (int p : {25, 50, 75, 100, 150, 200, 400}) {
    m_zoomCombo->addItem(QString::number(p) + QLatin1Char('%'), p);
  }
  m_zoomCombo->setCurrentText(QString::number(m_zoomPercent) + QLatin1Char('%'));
  m_zoomCombo->setToolTip(tr("Zoom level (- / +)"));
  m_zoomCombo->setFocusPolicy(Qt::StrongFocus);
  m_zoomComboAction = m_toolbar->addWidget(m_zoomCombo);
  connect(m_zoomCombo, &QComboBox::currentTextChanged, this, [this](const QString& text) {
    QString digits = text;
    digits.remove(QLatin1Char('%'));
    bool okNum = false;
    const int p = digits.trimmed().toInt(&okNum);
    if (!okNum || p <= 0) return;
    m_zoomPercent = qBound(10, p, 800);
    m_fitToWindow = false;
    if (m_fitButton) {
      QSignalBlocker b(m_fitButton);
      m_fitButton->setChecked(false);
    }
    updateZoomEnabled();
    applyVideoZoom();
  });

  m_fitButton = new QToolButton(m_toolbar);
  m_fitButton->setCheckable(true);
  m_fitButton->setIcon(QIcon(QStringLiteral(":/icons/toolbar/fit-to-window.svg")));
  m_fitButton->setToolTip(tr("Fit to window"));
  m_fitButton->setFocusPolicy(Qt::StrongFocus);
  m_fitButton->setChecked(m_fitToWindow);
  m_fitAction = m_toolbar->addWidget(m_fitButton);
  connect(m_fitButton, &QToolButton::toggled, this, [this](bool checked) {
    m_fitToWindow = checked;
    updateZoomEnabled();
    applyVideoZoom();
    // Fit 中はその時点の実効倍率を、手動復帰時は m_zoomPercent をコンボへ。
    updateZoomComboText();
  });

  m_fullScreenButton = new QToolButton(m_toolbar);
  m_fullScreenButton->setIcon(QIcon(QStringLiteral(":/icons/toolbar/fullscreen.svg")));
  m_fullScreenButton->setToolTip(tr("Full screen (F / double-click, Esc to exit)"));
  m_fullScreenButton->setFocusPolicy(Qt::StrongFocus);
  connect(m_fullScreenButton, &QToolButton::clicked, this, [this]() {
    setVideoFullScreen(true);
  });
  m_fullScreenAction = m_toolbar->addWidget(m_fullScreenButton);
  // 動画のあるファイルでのみ表示 (updateCurrentPage で切替)。ズーム関連も同様。
  m_fullScreenAction->setVisible(false);
  m_zoomLabelAction->setVisible(false);
  m_zoomComboAction->setVisible(false);
  m_fitAction->setVisible(false);

  // 情報 (メタデータ) ボタン。ImageView と同じく斜体太字の "i"。
  m_infoButton = new QToolButton(m_toolbar);
  m_infoButton->setText(QStringLiteral("i"));
  QFont infoFont = m_infoButton->font();
  infoFont.setItalic(true);
  infoFont.setBold(true);
  m_infoButton->setFont(infoFont);
  m_infoButton->setToolTip(tr("Show media information / metadata (I)"));
  m_infoButton->setFocusPolicy(Qt::StrongFocus);
  connect(m_infoButton, &QToolButton::clicked,
          this,         &MediaView::toggleMediaInfoDialog);
  m_infoAction = m_toolbar->addWidget(m_infoButton);

  m_mainLayout = new QVBoxLayout(this);
  m_mainLayout->setContentsMargins(0, 0, 0, 0);
  m_mainLayout->setSpacing(0);
  m_mainLayout->addWidget(m_toolbar);
  m_mainLayout->addWidget(m_stack, 1);

  // Tab 順を明示 (ImageView と同じ作法。スライダは NoFocus なので含めない)。
  setTabOrder(m_playButton, m_stopButton);
  setTabOrder(m_stopButton, m_rateCombo);
  setTabOrder(m_rateCombo,  m_loopButton);
  setTabOrder(m_loopButton, m_muteButton);
  setTabOrder(m_muteButton, m_zoomCombo);
  setTabOrder(m_zoomCombo,  m_fitButton);
  setTabOrder(m_fitButton,  m_fullScreenButton);
  setTabOrder(m_fullScreenButton, m_infoButton);

  // ツールバー内ボタンで Tab フォーカス中に Enter を押したらクリック扱いに
  // する (親の「Enter で戻る」が誤発火しないように)。ImageView と同じ。
  m_clickFilter = new EnterClickFilter(this);
  m_clickFilter->installOnButtonsIn(m_toolbar);

  // ── プレイヤーシグナル ──
  connect(m_player, &QMediaPlayer::positionChanged, this, [this](qint64 pos) {
    if (!m_positionSlider->isSliderDown()) {
      m_positionSlider->setValue(int(pos));
    }
    updateTimeLabel();
    // 再生が進めばフレームが描画され sizeHint が確定する。確定し次第キャッシュ。
    updateNativeVideoSize();
  });
  connect(m_player, &QMediaPlayer::durationChanged, this, [this](qint64 dur) {
    m_positionSlider->setRange(0, int(dur));
    m_positionSlider->setEnabled(dur > 0);
    updateTimeLabel();
    // duration はメタデータより後に確定することがあるので情報も更新する。
    refreshMediaInfoDialog();
    emit statusInfoChanged(statusInfo());
  });
  connect(m_player, &QMediaPlayer::playbackStateChanged,
          this,     &MediaView::updatePlayButton);
  connect(m_player, &QMediaPlayer::hasVideoChanged,
          this,     &MediaView::updateCurrentPage);
  connect(m_player, &QMediaPlayer::metaDataChanged, this, [this]() {
    updateMetadataCard();
    emit statusInfoChanged(statusInfo());
    updateNativeVideoSize();  // 解像度が確定したら手動ズームを実ピクセル基準で再適用
  });
  // コーデック / 解像度はトラックが解析されて初めて分かるので、トラック確定でも
  // ステータスバー要約を更新する (metaDataChanged より後に来ることがある)。
  connect(m_player, &QMediaPlayer::tracksChanged, this, [this]() {
    emit statusInfoChanged(statusInfo());
    updateNativeVideoSize();
  });
  connect(m_player, &QMediaPlayer::mediaStatusChanged,
          this,     [this](QMediaPlayer::MediaStatus status) {
    switch (status) {
      case QMediaPlayer::LoadedMedia:
      case QMediaPlayer::BufferedMedia:
        notifyLoadResult(true);
        break;
      case QMediaPlayer::InvalidMedia:
        showMessage(tr("Cannot play this file.\n"
                       "The format or codec is not supported on this platform."));
        notifyLoadResult(false);
        break;
      default:
        break;
    }
  });
  connect(m_player, &QMediaPlayer::errorOccurred, this,
          [this](QMediaPlayer::Error, const QString& errorString) {
    showMessage(tr("Failed to load media:\n%1").arg(errorString));
    notifyLoadResult(false);
  });

  applyVolume();
  updatePlayButton();
  updateMuteButton();
  updateZoomEnabled();  // Fit 既定なら手動ズームコンボを無効化
}

void MediaView::openFile(const QString& filePath) {
  m_filePath     = filePath;
  m_loadNotified = false;
  m_nativeVideoSize = QSize();  // 別ファイルでは解像度を取り直す
  updateMetadataCard();
  m_player->setSource(QUrl::fromLocalFile(filePath));
  // ビュアーとして明示的に開かれた場面なので、既定では再生を開始する
  // (QuickLook 等のプレビュー系と同じ挙動)。自動再生は設定で無効化できる。
  if (Settings::instance().mediaViewerAutoplay()) {
    m_player->play();
  }
}

void MediaView::clearContent() {
  setVideoFullScreen(false);
  m_player->stop();
  m_player->setSource(QUrl());
  m_filePath.clear();
}

void MediaView::keyPressEvent(QKeyEvent* event) {
  if (handleViewerKey(event)) {
    return;
  }
  QWidget::keyPressEvent(event);
}

bool MediaView::eventFilter(QObject* watched, QEvent* event) {
  // 埋め込み / フルスクリーン、どちらのビューでも同じキー / ダブルクリック処理。
  if (watched == m_videoView || watched == m_fsView) {
    if (event->type() == QEvent::KeyPress) {
      if (handleViewerKey(static_cast<QKeyEvent*>(event))) {
        return true;
      }
    } else if (event->type() == QEvent::MouseButtonDblClick) {
      setVideoFullScreen(!isVideoFullScreen());
      return true;
    } else if (event->type() == QEvent::Close && watched == m_fsView) {
      // フルスクリーン窓を WM 経由で閉じられたら状態を解除する。
      setVideoFullScreen(false);
      return true;
    } else if (event->type() == QEvent::Resize && watched == m_fsView) {
      // フルスクリーンビューのリサイズ: 再フィット + 操作パネルを下部へ再配置。
      fitVideoView(m_fsView);
      layoutFsControlBar();
    }
  } else if (m_videoView && watched == m_videoView->viewport()
             && event->type() == QEvent::Resize) {
    // 埋め込みビューのリサイズ: Fit 中は再フィットし、コンボ表示も追従させる。
    if (m_fitToWindow) {
      fitVideoView(m_videoView);
      updateZoomComboText();
    }
  } else if (m_fsView
             && (watched == m_fsView->viewport() || watched == m_fsControlBar)
             && event->type() == QEvent::MouseMove) {
    // マウスが動いたら操作パネルを出す (一定時間で自動的に消える)。
    showFsControls();
  }
  return QWidget::eventFilter(watched, event);
}

bool MediaView::handleViewerKey(QKeyEvent* event) {
  const bool shift = event->modifiers().testFlag(Qt::ShiftModifier);

  // ズームは他ビュアーと同じく - / + で。プリセット段階を上下する
  // (動画のみ。音声には拡大縮小が無いので何もしない)。
  auto stepZoom = [this](bool zoomIn) -> bool {
    if (!m_zoomCombo || !m_player->hasVideo()) {
      return false;
    }
    static const int presets[] = {25, 50, 75, 100, 150, 200, 400};
    const int count = int(sizeof(presets) / sizeof(presets[0]));
    int np = m_zoomPercent;
    if (zoomIn) {
      for (int i = 0; i < count; ++i) {
        if (presets[i] > m_zoomPercent) { np = presets[i]; break; }
      }
    } else {
      for (int i = count - 1; i >= 0; --i) {
        if (presets[i] < m_zoomPercent) { np = presets[i]; break; }
      }
    }
    m_zoomCombo->setCurrentText(QString::number(np) + QLatin1Char('%'));
    return true;
  };

  // フルスクリーン解除の Esc は固定。通常表示中の Esc は親 (ViewerPanel /
  // ViewerWindow) の「閉じる」処理に流すので、ここでは消費しない。
  if (event->key() == Qt::Key_Escape) {
    if (isVideoFullScreen()) {
      setVideoFullScreen(false);
      return true;
    }
    return false;
  }

  // 以降は設定で再割り当て可能なキー。押されたキーに対応するコマンドを引く。
  auto& vkb = ViewerKeyBindingManager::instance();
  QString cmd = vkb.commandForKey(QStringLiteral("media"),
                                  ViewerKeyBindingManager::sequenceForEvent(event));
  // Shift 付きのシークは「大きく移動」。Shift を外して再照合し、seek のときだけ採用。
  bool largeSeek = false;
  if (cmd.isEmpty() && shift) {
    Qt::KeyboardModifiers m = event->modifiers() & ~Qt::ShiftModifier & ~Qt::KeypadModifier;
    const QString baseCmd = vkb.commandForKey(
      QStringLiteral("media"),
      QKeySequence(QKeyCombination(m, static_cast<Qt::Key>(event->key()))));
    if (baseCmd == QLatin1String("viewer.media.seek_back")
        || baseCmd == QLatin1String("viewer.media.seek_forward")) {
      cmd = baseCmd;
      largeSeek = true;
    }
  }
  if (cmd.isEmpty()) {
    return false;
  }

  if (cmd == QLatin1String("viewer.media.play_pause")) {
    togglePlayPause();
  } else if (cmd == QLatin1String("viewer.media.stop")) {
    if (m_stopButton) m_stopButton->click();
  } else if (cmd == QLatin1String("viewer.media.seek_back")) {
    seekBy(largeSeek ? -kSeekLargeStepMs : -kSeekStepMs);
  } else if (cmd == QLatin1String("viewer.media.seek_forward")) {
    seekBy(largeSeek ? kSeekLargeStepMs : kSeekStepMs);
  } else if (cmd == QLatin1String("viewer.media.volume_up")) {
    adjustVolume(kVolumeStep);
  } else if (cmd == QLatin1String("viewer.media.volume_down")) {
    adjustVolume(-kVolumeStep);
  } else if (cmd == QLatin1String("viewer.media.rate_slower")) {
    if (m_rateCombo && m_rateCombo->currentIndex() > 0) {
      m_rateCombo->setCurrentIndex(m_rateCombo->currentIndex() - 1);
    }
  } else if (cmd == QLatin1String("viewer.media.rate_faster")) {
    if (m_rateCombo && m_rateCombo->currentIndex() < m_rateCombo->count() - 1) {
      m_rateCombo->setCurrentIndex(m_rateCombo->currentIndex() + 1);
    }
  } else if (cmd == QLatin1String("viewer.media.zoom_in")) {
    return stepZoom(true);
  } else if (cmd == QLatin1String("viewer.media.zoom_out")) {
    return stepZoom(false);
  } else if (cmd == QLatin1String("viewer.media.toggle_mute")) {
    if (m_muteButton) m_muteButton->toggle();
  } else if (cmd == QLatin1String("viewer.media.toggle_loop")) {
    if (m_loopButton) m_loopButton->toggle();
  } else if (cmd == QLatin1String("viewer.media.info")) {
    toggleMediaInfoDialog();
  } else if (cmd == QLatin1String("viewer.media.toggle_fullscreen")) {
    if (m_player->hasVideo()) {
      setVideoFullScreen(!isVideoFullScreen());
    } else {
      return false;
    }
  } else {
    return false;
  }
  return true;
}

void MediaView::togglePlayPause() {
  if (m_player->playbackState() == QMediaPlayer::PlayingState) {
    m_player->pause();
  } else {
    m_player->play();
  }
}

void MediaView::seekBy(qint64 deltaMs) {
  if (!m_player->isSeekable()) return;
  const qint64 target = qBound<qint64>(0, m_player->position() + deltaMs,
                                       m_player->duration());
  m_player->setPosition(target);
}

void MediaView::adjustVolume(int delta) {
  m_volumeSlider->setValue(
    qBound(0, m_volumeSlider->value() + delta, 100));
}

void MediaView::setVideoFullScreen(bool on) {
  if (!m_videoView || isVideoFullScreen() == on) return;

  // 動画は QGraphicsScene 上のアイテムなので、フルスクリーンは「同じシーンを
  // 映す 2 つ目のビュー」をトップレベルで全画面表示するだけでよい。出力先
  // (m_videoItem) は一切変えず、ウィジェットの再ペアレントも起きないため、
  // macOS でも映像が固まったり真っ黒になったりしない。
  if (on) {
    m_fsView = new QGraphicsView(m_scene, nullptr);
    m_fsView->setWindowTitle(tr("Full Screen"));
    m_fsView->setFrameShape(QFrame::NoFrame);
    m_fsView->setBackgroundBrush(Qt::black);
    m_fsView->setAlignment(Qt::AlignCenter);
    m_fsView->setDragMode(QGraphicsView::NoDrag);
    m_fsView->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_fsView->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_fsView->setFocusPolicy(Qt::StrongFocus);
    m_fsView->installEventFilter(this);
    m_fsView->viewport()->installEventFilter(this);  // リサイズで再フィット
    // マウス移動でオーバーレイ操作パネルを出すため、常時ムーブイベントを拾う。
    m_fsView->setMouseTracking(true);
    m_fsView->viewport()->setMouseTracking(true);

    // ── 下部オーバーレイ操作パネル (フルスクリーン専用 UI) ──
    // 通常表示のツールバーは流用せず、必要な操作だけ (再生/一時停止・停止・
    // シークバー・時間・リピート・音量) を暗い半透明の板に並べる。状態は
    // 既存コントロール / プレイヤーと双方向に同期し、パネル破棄と同時に
    // 接続も切れる (context = 各ウィジェット)。
    m_fsControlBar = new QWidget(m_fsView);
    m_fsControlBar->setObjectName(QStringLiteral("fsControlBar"));
    m_fsControlBar->setStyleSheet(QStringLiteral(
      "#fsControlBar { background-color: rgba(14, 14, 14, 200);"
      "                border-radius: 10px; }"
      "QToolButton { border: none; padding: 5px; border-radius: 5px; }"
      "QToolButton:hover { background-color: rgba(255, 255, 255, 40); }"
      "QToolButton:checked { background-color: rgba(255, 255, 255, 70); }"
      "QLabel { color: #e8e8e8; }"));
    {
      auto* barLay = new QHBoxLayout(m_fsControlBar);
      barLay->setContentsMargins(14, 8, 14, 8);
      barLay->setSpacing(8);

      const QSize iconSz(22, 22);
      auto makeButton = [&](const QString& icon, const QString& tip) {
        auto* b = new QToolButton(m_fsControlBar);
        b->setIcon(whiteToolbarIcon(icon));
        b->setIconSize(iconSz);
        b->setToolTip(tip);
        b->setFocusPolicy(Qt::NoFocus);  // キーは FS ビュー側で処理する
        return b;
      };

      // 再生 / 一時停止 (状態でアイコンが切り替わる)
      auto* fsPlay = makeButton(
        m_player->playbackState() == QMediaPlayer::PlayingState
          ? QStringLiteral(":/icons/toolbar/pause.svg")
          : QStringLiteral(":/icons/toolbar/play.svg"),
        tr("Play / Pause (Space)"));
      connect(fsPlay, &QToolButton::clicked, this, &MediaView::togglePlayPause);
      connect(m_player, &QMediaPlayer::playbackStateChanged, fsPlay,
              [this, fsPlay](QMediaPlayer::PlaybackState st) {
        fsPlay->setIcon(whiteToolbarIcon(st == QMediaPlayer::PlayingState
          ? QStringLiteral(":/icons/toolbar/pause.svg")
          : QStringLiteral(":/icons/toolbar/play.svg")));
      });
      barLay->addWidget(fsPlay);

      // 停止
      auto* fsStop = makeButton(QStringLiteral(":/icons/toolbar/stop.svg"),
                                tr("Stop (S)"));
      connect(fsStop, &QToolButton::clicked, m_player, &QMediaPlayer::stop);
      barLay->addWidget(fsStop);

      // シークバー (クリック位置へ即シーク)
      auto* fsSeek = new ClickSeekSlider(Qt::Horizontal, m_fsControlBar);
      fsSeek->setFocusPolicy(Qt::NoFocus);
      fsSeek->setToolTip(tr("Seek (Left / Right, Shift for larger steps)"));
      fsSeek->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
      fsSeek->setRange(0, int(m_player->duration()));
      fsSeek->setValue(int(m_player->position()));
      fsSeek->setEnabled(m_player->duration() > 0);
      connect(fsSeek, &QSlider::sliderMoved, this, [this](int value) {
        m_player->setPosition(value);
      });
      connect(m_player, &QMediaPlayer::positionChanged, fsSeek,
              [fsSeek](qint64 pos) {
        if (!fsSeek->isSliderDown()) fsSeek->setValue(int(pos));
      });
      connect(m_player, &QMediaPlayer::durationChanged, fsSeek,
              [fsSeek](qint64 dur) {
        fsSeek->setRange(0, int(dur));
        fsSeek->setEnabled(dur > 0);
      });
      barLay->addWidget(fsSeek, 1);

      // 時間表示
      auto* fsTime = new QLabel(m_fsControlBar);
      fsTime->setToolTip(tr("Elapsed / total time"));
      fsTime->setText(QStringLiteral("%1 / %2")
        .arg(formatTime(m_player->position()), formatTime(m_player->duration())));
      connect(m_player, &QMediaPlayer::positionChanged, fsTime,
              [this, fsTime](qint64 pos) {
        fsTime->setText(QStringLiteral("%1 / %2")
          .arg(formatTime(pos), formatTime(m_player->duration())));
      });
      barLay->addWidget(fsTime);

      // リピート (通常表示のループボタンと双方向同期)
      auto* fsLoop = makeButton(QStringLiteral(":/icons/toolbar/loop.svg"),
                                tr("Loop playback (L)"));
      fsLoop->setCheckable(true);
      fsLoop->setChecked(m_loopButton->isChecked());
      connect(fsLoop, &QToolButton::toggled,
              m_loopButton, &QToolButton::setChecked);
      connect(m_loopButton, &QToolButton::toggled,
              fsLoop, &QToolButton::setChecked);
      barLay->addWidget(fsLoop);

      // ミュート (通常表示のミュートボタンと双方向同期)
      auto* fsMute = makeButton(
        m_muteButton->isChecked()
          ? QStringLiteral(":/icons/toolbar/volume-muted.svg")
          : QStringLiteral(":/icons/toolbar/volume.svg"),
        tr("Mute (M)"));
      fsMute->setCheckable(true);
      fsMute->setChecked(m_muteButton->isChecked());
      connect(fsMute, &QToolButton::toggled,
              m_muteButton, &QToolButton::setChecked);
      connect(m_muteButton, &QToolButton::toggled, fsMute,
              [fsMute](bool checked) {
        fsMute->setChecked(checked);
        fsMute->setIcon(whiteToolbarIcon(checked
          ? QStringLiteral(":/icons/toolbar/volume-muted.svg")
          : QStringLiteral(":/icons/toolbar/volume.svg")));
      });
      barLay->addWidget(fsMute);

      // 音量 (通常表示の音量スライダと双方向同期。同値 setValue は無限ループ
      // にならない)
      auto* fsVolume = new QSlider(Qt::Horizontal, m_fsControlBar);
      fsVolume->setFocusPolicy(Qt::NoFocus);
      fsVolume->setToolTip(tr("Volume (Up / Down)"));
      fsVolume->setRange(0, 100);
      fsVolume->setFixedWidth(96);
      fsVolume->setValue(m_volumeSlider->value());
      connect(fsVolume, &QSlider::valueChanged,
              m_volumeSlider, &QSlider::setValue);
      connect(m_volumeSlider, &QSlider::valueChanged,
              fsVolume, &QSlider::setValue);
      barLay->addWidget(fsVolume);
    }
    m_fsControlBar->installEventFilter(this);  // ホバー中は消さない
    m_fsControlBar->hide();

    if (!m_fsControlHideTimer) {
      m_fsControlHideTimer = new QTimer(this);
      m_fsControlHideTimer->setSingleShot(true);
      connect(m_fsControlHideTimer, &QTimer::timeout, this, [this]() {
        // パネル上にカーソルがある間は消さない。
        if (m_fsControlBar && !m_fsControlBar->underMouse()) {
          m_fsControlBar->hide();
        }
      });
    }

    m_fsView->showFullScreen();
    fitVideoView(m_fsView);
    m_fsView->setFocus(Qt::OtherFocusReason);
    showFsControls();  // 開始直後は一度出しておく (数秒で自動的に消える)
  } else {
    if (m_fsControlHideTimer) m_fsControlHideTimer->stop();
    m_fsControlBar = nullptr;  // m_fsView の子なので下の破棄で消える
    QGraphicsView* v = m_fsView;
    m_fsView = nullptr;
    if (v) {
      v->removeEventFilter(this);
      v->deleteLater();
    }
    setFocus(Qt::OtherFocusReason);
  }
}

void MediaView::showFsControls() {
  if (!m_fsControlBar) return;
  layoutFsControlBar();
  m_fsControlBar->show();
  m_fsControlBar->raise();
  if (m_fsControlHideTimer) m_fsControlHideTimer->start(2500);
}

void MediaView::layoutFsControlBar() {
  if (!m_fsView || !m_fsControlBar) return;
  // 画面下部に少し浮かせた角丸の板として配置する (左右・下に余白)。
  const int margin = 24;
  const int h = m_fsControlBar->sizeHint().height();
  const int w = m_fsView->width() - margin * 2;
  m_fsControlBar->setGeometry(margin, m_fsView->height() - h - margin, w, h);
}

void MediaView::applyVolume() {
  // スライダの直線値を聴感に合う対数スケールへ変換してから渡す
  // (Qt Multimedia ドキュメント推奨の変換)。
  const qreal linear = QAudio::convertVolume(
    m_volumeSlider->value() / qreal(100.0),
    QAudio::LogarithmicVolumeScale,
    QAudio::LinearVolumeScale);
  m_audioOutput->setVolume(float(linear));
}

void MediaView::notifyLoadResult(bool ok) {
  if (m_loadNotified) return;
  m_loadNotified = true;
  emit loadFinished(ok);
}

void MediaView::showMessage(const QString& text) {
  m_messageLabel->setText(text);
  m_stack->setCurrentWidget(m_messageLabel);
}

void MediaView::updatePlayButton() {
  // 再生中は ⏸、停止中は ▶ に差し替える (ImageView の anim ボタンと同じ)。
  const bool playing =
    m_player->playbackState() == QMediaPlayer::PlayingState;
  m_playButton->setIcon(QIcon(playing
    ? QStringLiteral(":/icons/toolbar/pause.svg")
    : QStringLiteral(":/icons/toolbar/play.svg")));
}

void MediaView::updateMuteButton() {
  m_muteButton->setIcon(QIcon(m_audioOutput->isMuted()
    ? QStringLiteral(":/icons/toolbar/volume-muted.svg")
    : QStringLiteral(":/icons/toolbar/volume.svg")));
}

void MediaView::updateTimeLabel() {
  m_timeLabel->setText(QStringLiteral("%1 / %2")
                         .arg(formatTime(m_player->position()),
                              formatTime(m_player->duration())));
}

void MediaView::updateMetadataCard() {
  if (!m_player) return;
  const QMediaMetaData metaData = m_player->metaData();

  QString title = metaData.stringValue(QMediaMetaData::Title);
  if (title.isEmpty()) {
    title = QFileInfo(m_filePath).fileName();
  }
  m_titleLabel->setText(title);

  QString artist = metaData.stringValue(QMediaMetaData::ContributingArtist);
  if (artist.isEmpty()) {
    artist = metaData.stringValue(QMediaMetaData::AlbumArtist);
  }
  const QString album = metaData.stringValue(QMediaMetaData::AlbumTitle);
  QStringList detail;
  if (!artist.isEmpty()) detail << artist;
  if (!album.isEmpty())  detail << album;
  m_detailLabel->setText(detail.join(QStringLiteral(" — ")));

  // 埋め込みカバーアートがあれば表示、無ければ音符グリフ。
  QImage cover = metaData.value(QMediaMetaData::CoverArtImage).value<QImage>();
  if (cover.isNull()) {
    cover = metaData.value(QMediaMetaData::ThumbnailImage).value<QImage>();
  }
  if (!cover.isNull()) {
    m_coverLabel->setPixmap(QPixmap::fromImage(cover).scaled(
      200, 200, Qt::KeepAspectRatio, Qt::SmoothTransformation));
  } else {
    QFont noteFont = font();
    noteFont.setPointSize(64);
    m_coverLabel->setFont(noteFont);
    m_coverLabel->setText(QStringLiteral("♪"));
  }

  // 情報ダイアログが開いていればメタデータ更新に追従させる。
  refreshMediaInfoDialog();
}

void MediaView::toggleMediaInfoDialog() {
  if (m_filePath.isEmpty()) return;
  // 表示中ならトグルで閉じる。
  if (m_infoDialog && m_infoDialog->isVisible()) {
    m_infoDialog->close();
    return;
  }
  if (!m_infoDialog) {
    m_infoDialog = new QDialog(this);
    m_infoDialog->setWindowTitle(tr("Media Information"));
    m_infoDialog->setAttribute(Qt::WA_DeleteOnClose, false);
    m_infoDialog->resize(560, 420);

    auto* layout = new QVBoxLayout(m_infoDialog);
    auto* edit = new QPlainTextEdit(m_infoDialog);
    edit->setObjectName(QStringLiteral("mediaInfoEdit"));
    edit->setReadOnly(true);
    edit->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    layout->addWidget(edit);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, m_infoDialog);
    connect(buttons, &QDialogButtonBox::rejected, m_infoDialog, &QDialog::close);
    layout->addWidget(buttons);
  }
  if (auto* edit = m_infoDialog->findChild<QPlainTextEdit*>(
        QStringLiteral("mediaInfoEdit"))) {
    edit->setPlainText(buildMediaInfoText());
  }
  m_infoDialog->show();
  m_infoDialog->raise();
  m_infoDialog->activateWindow();
}

void MediaView::refreshMediaInfoDialog() {
  if (!m_infoDialog || !m_infoDialog->isVisible()) return;
  if (auto* edit = m_infoDialog->findChild<QPlainTextEdit*>(
        QStringLiteral("mediaInfoEdit"))) {
    edit->setPlainText(buildMediaInfoText());
  }
}

void MediaView::appendMetaData(QString& body, const QMediaMetaData& md,
                               const QString& indent) const {
  // 既知キーは日本語ラベル、未知キーは Qt のキー名で表示する。
  const QHash<QMediaMetaData::Key, QString> labels = {
    {QMediaMetaData::Title,              tr("Title")},
    {QMediaMetaData::ContributingArtist, tr("Artist")},
    {QMediaMetaData::AlbumArtist,        tr("Album artist")},
    {QMediaMetaData::LeadPerformer,      tr("Lead performer")},
    {QMediaMetaData::AlbumTitle,         tr("Album")},
    {QMediaMetaData::TrackNumber,        tr("Track number")},
    {QMediaMetaData::Genre,              tr("Genre")},
    {QMediaMetaData::Date,               tr("Date")},
    {QMediaMetaData::Composer,           tr("Composer")},
    {QMediaMetaData::Comment,            tr("Comment")},
    {QMediaMetaData::Description,         tr("Description")},
    {QMediaMetaData::Copyright,          tr("Copyright")},
    {QMediaMetaData::Author,             tr("Author")},
    {QMediaMetaData::Publisher,          tr("Publisher")},
    {QMediaMetaData::Language,           tr("Language")},
    {QMediaMetaData::Url,                tr("URL")},
    {QMediaMetaData::Orientation,        tr("Orientation")},
    {QMediaMetaData::MediaType,          tr("Media type")},
    {QMediaMetaData::FileFormat,         tr("Format")},
    {QMediaMetaData::VideoCodec,         tr("Video codec")},
    {QMediaMetaData::AudioCodec,         tr("Audio codec")},
    {QMediaMetaData::Resolution,         tr("Resolution")},
    {QMediaMetaData::VideoFrameRate,     tr("Frame rate")},
    {QMediaMetaData::VideoBitRate,       tr("Video bit rate")},
    {QMediaMetaData::AudioBitRate,       tr("Audio bit rate")},
  };

  const QList<QMediaMetaData::Key> keys = md.keys();
  for (QMediaMetaData::Key key : keys) {
    // Duration は上部でプレイヤー値を表示するので重複を避ける。
    if (key == QMediaMetaData::Duration) continue;

    const QString label =
      labels.value(key, QMediaMetaData::metaDataKeyToString(key));

    QString value;
    switch (key) {
      case QMediaMetaData::CoverArtImage:
      case QMediaMetaData::ThumbnailImage:
        // バイナリ画像はテキストに出さず存在のみ示す。
        value = tr("(embedded)");
        break;
      case QMediaMetaData::Resolution: {
        const QSize sz = md.value(key).toSize();
        if (sz.isValid() && sz.width() > 0 && sz.height() > 0) {
          value = tr("%1 x %2 px").arg(sz.width()).arg(sz.height());
        }
        break;
      }
      case QMediaMetaData::VideoFrameRate: {
        const double fr = md.value(key).toDouble();
        if (fr > 0.0) value = tr("%1 fps").arg(QString::number(fr, 'g', 4));
        break;
      }
      case QMediaMetaData::VideoBitRate:
      case QMediaMetaData::AudioBitRate: {
        const int br = md.value(key).toInt();
        if (br > 0) value = tr("%1 kbps").arg(br / 1000);
        break;
      }
      default:
        value = md.stringValue(key);
        break;
    }

    if (!value.trimmed().isEmpty()) {
      body += indent + label + QStringLiteral(": ") + value + QLatin1Char('\n');
    }
  }
}

QString MediaView::buildMediaInfoText() const {
  if (m_filePath.isEmpty()) return QString();
  const QFileInfo fi(m_filePath);

  QString body;
  body += tr("File: %1").arg(m_filePath) + QLatin1Char('\n');
  body += tr("File size") + QStringLiteral(": ")
        + QLocale(QLocale::English).formattedDataSize(fi.size())
        + QLatin1Char('\n');
  body += tr("Type") + QStringLiteral(": ")
        + (m_player->hasVideo() ? tr("Video") : tr("Audio"))
        + QLatin1Char('\n');
  if (m_player->duration() > 0) {
    body += tr("Duration") + QStringLiteral(": ")
          + formatTime(m_player->duration()) + QLatin1Char('\n');
  }
  body += tr("Seekable") + QStringLiteral(": ")
        + (m_player->isSeekable() ? tr("Yes") : tr("No"))
        + QLatin1Char('\n');

  // コンテナ / ストリーム全体のメタデータ (取得できた分だけ総当たりで表示)。
  QString general;
  appendMetaData(general, m_player->metaData(), QString());
  if (!general.isEmpty()) {
    body += QLatin1Char('\n') + tr("--- Metadata ---") + QLatin1Char('\n')
          + general;
  }

  // トラック単位のメタデータ (多言語音声・字幕など)。
  auto dumpTracks = [this, &body](const QString& heading,
                                  const QList<QMediaMetaData>& tracks) {
    for (int i = 0; i < tracks.size(); ++i) {
      QString t;
      appendMetaData(t, tracks.at(i), QStringLiteral("  "));
      if (t.isEmpty()) continue;
      body += QLatin1Char('\n') + heading.arg(i + 1) + QLatin1Char('\n') + t;
    }
  };
  dumpTracks(tr("--- Audio track %1 ---"),    m_player->audioTracks());
  dumpTracks(tr("--- Video track %1 ---"),    m_player->videoTracks());
  dumpTracks(tr("--- Subtitle track %1 ---"), m_player->subtitleTracks());

  return body;
}

void MediaView::updateCurrentPage() {
  // エラーメッセージ表示中はそのまま維持する。
  if (m_stack->currentWidget() == m_messageLabel) return;
  const bool hasVideo = m_player->hasVideo();
  m_stack->setCurrentWidget(hasVideo ? static_cast<QWidget*>(m_videoView)
                                     : static_cast<QWidget*>(m_audioCard));
  // ズーム / フルスクリーンは動画のみ (音声では非表示)。
  m_fullScreenAction->setVisible(hasVideo);
  m_zoomLabelAction->setVisible(hasVideo);
  m_zoomComboAction->setVisible(hasVideo);
  m_fitAction->setVisible(hasVideo);
  updateZoomEnabled();
  applyVideoZoom();
  updateZoomComboText();
}

void MediaView::fitVideoView(QGraphicsView* view) {
  if (!view || !m_videoItem) return;
  if (m_videoItem->boundingRect().isEmpty()) return;  // ネイティブサイズ未確定
  view->resetTransform();
  view->fitInView(m_videoItem, Qt::KeepAspectRatio);
}

void MediaView::applyVideoZoom() {
  if (!m_videoView) return;
  if (m_fitToWindow) {
    // 埋め込みビューを viewport にフィット (KeepAspectRatio でレターボックス)。
    m_videoView->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_videoView->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    fitVideoView(m_videoView);
    return;
  }
  // 手動ズーム: 100% = 動画の自然サイズ (実ピクセル等倍。ImageView と同義)。
  // アイテムは実解像度サイズなので、ビューの変換を ×倍率にすれば実ピクセル基準
  // になる。はみ出す分はスクロールバーを出す。
  m_videoView->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
  m_videoView->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
  const double z = m_zoomPercent / 100.0;
  m_videoView->resetTransform();
  m_videoView->scale(z, z);
}

void MediaView::updateNativeVideoSize() {
  if (!m_videoItem) return;
  const QSizeF ns = m_videoItem->nativeSize();
  if (ns.isEmpty()) return;
  m_nativeVideoSize = ns.toSize();
  // アイテムを実解像度サイズにし、シーン矩形も合わせる。以後ズーム/フィットは
  // このアイテム矩形を基準に計算される。
  m_videoItem->setSize(ns);
  if (m_scene) m_scene->setSceneRect(m_videoItem->boundingRect());
  applyVideoZoom();
  if (m_fsView) fitVideoView(m_fsView);
  updateZoomComboText();
}

int MediaView::effectiveVideoZoomPercent() const {
  if (m_nativeVideoSize.isEmpty() || !m_videoView) return m_zoomPercent;
  const QSize vp = m_videoView->viewport()->size();
  if (vp.isEmpty()) return m_zoomPercent;
  // KeepAspectRatio で viewport に収めたときの実倍率。
  const double rx = double(vp.width())  / m_nativeVideoSize.width();
  const double ry = double(vp.height()) / m_nativeVideoSize.height();
  return qMax(1, int(qRound(qMin(rx, ry) * 100)));
}

void MediaView::updateZoomComboText() {
  if (!m_zoomCombo) return;
  const int pct = m_fitToWindow ? effectiveVideoZoomPercent() : m_zoomPercent;
  QSignalBlocker b(m_zoomCombo);
  m_zoomCombo->setCurrentText(QString::number(pct) + QLatin1Char('%'));
}

QAction* MediaView::addToolbarWidget(QWidget* widget) {
  if (!m_toolbar || !widget) return nullptr;
  // 情報 (i) ボタンの直前に挿入する。フルスクリーン / ズームなどの表示系
  // コントロールと同じグループ (i の左) に並べるため。
  QAction* action = m_infoAction ? m_toolbar->insertWidget(m_infoAction, widget)
                                  : m_toolbar->addWidget(widget);
  // Tab 順: フルスクリーン → 新 widget → 情報。
  setTabOrder(m_fullScreenButton, widget);
  setTabOrder(widget, m_infoButton);
  if (m_clickFilter) m_clickFilter->installOnButtonsIn(widget);
  return action;
}

QSize MediaView::naturalVideoSize() const {
  return videoResolution();
}

QSize MediaView::videoAreaSize() const {
  return m_videoView ? m_videoView->viewport()->size() : QSize();
}

void MediaView::updateZoomEnabled() {
  // Fit 中は手動ズーム指定を無効化 (ImageView と同じ作法)。
  if (m_zoomCombo) m_zoomCombo->setEnabled(!m_fitToWindow);
}

QSize MediaView::videoResolution() const {
  if (!m_player) return QSize();
  QSize res = m_player->metaData().value(QMediaMetaData::Resolution).toSize();
  if (!res.isValid() || res.isEmpty()) {
    for (const QMediaMetaData& t : m_player->videoTracks()) {
      const QSize r = t.value(QMediaMetaData::Resolution).toSize();
      if (r.isValid() && !r.isEmpty()) { res = r; break; }
    }
  }
  // メタデータに解像度が無いバックエンド (macOS/AVFoundation 等) でも、実際に
  // 描画された動画の自然サイズ (QGraphicsVideoItem::nativeSize) からネイティブ
  // 解像度を取得できる。これにより 100% = 実ピクセル等倍 を ImageView と揃える。
  if ((!res.isValid() || res.isEmpty()) && m_videoItem) {
    const QSize ns = m_videoItem->nativeSize().toSize();
    if (ns.isValid() && !ns.isEmpty()) res = ns;
  }
  return res;
}

void MediaView::exitFullscreen() {
  // 表示を閉じる (closeEvent / clearContent) 際にフルスクリーンを解除する。
  setVideoFullScreen(false);
}

int MediaView::windowFitZoomPercent() const {
  // 「ウィンドウを動画に合わせる」で使う基準倍率。fit-to-window 中は実寸
  // (100%)、手動ズーム時は設定倍率を使う。effectiveVideoZoomPercent は常に
  // フィット比を返すのでここでは使わない。
  return m_fitToWindow ? 100 : m_zoomPercent;
}

QString MediaView::statusInfo() const {
  if (!m_player) return QString();
  const QMediaMetaData md = m_player->metaData();
  QStringList parts;

  // フォーマット: メタデータが取れなければ拡張子から補う (バックエンドが
  // FileFormat を埋めないことがあるため)。
  QString fmt = md.stringValue(QMediaMetaData::FileFormat);
  if (fmt.isEmpty() && !m_filePath.isEmpty()) {
    fmt = QFileInfo(m_filePath).suffix().toUpper();
  }

  // コーデック / 解像度はコンテナ全体の metaData() には入らないことが多く
  // (特に macOS/AVFoundation)、トラック単位のメタデータに入っている。情報
  // ダイアログと同じく、コンテナ→トラックの順で最初に見つかった値を使う。
  auto fromTracks = [](const QList<QMediaMetaData>& tracks,
                       QMediaMetaData::Key key) -> QString {
    for (const QMediaMetaData& t : tracks) {
      const QString v = t.stringValue(key);
      if (!v.isEmpty()) return v;
    }
    return QString();
  };

  QString vcodec = md.stringValue(QMediaMetaData::VideoCodec);
  if (vcodec.isEmpty()) {
    vcodec = fromTracks(m_player->videoTracks(), QMediaMetaData::VideoCodec);
  }
  QString acodec = md.stringValue(QMediaMetaData::AudioCodec);
  if (acodec.isEmpty()) {
    acodec = fromTracks(m_player->audioTracks(), QMediaMetaData::AudioCodec);
  }

  // コーデック (映像 / 音声) は「フォーマット (vcodec, acodec)」とまとめる。
  QStringList codecs;
  if (!vcodec.isEmpty()) codecs << vcodec;
  if (!acodec.isEmpty()) codecs << acodec;

  if (!fmt.isEmpty()) {
    QString formatPart = fmt;
    if (!codecs.isEmpty()) {
      formatPart += QStringLiteral(" (%1)").arg(codecs.join(QStringLiteral(", ")));
    }
    parts << formatPart;
  } else if (!codecs.isEmpty()) {
    parts << codecs.join(QStringLiteral(", "));
  }

  QSize res = md.value(QMediaMetaData::Resolution).toSize();
  if (!res.isValid() || res.isEmpty()) {
    for (const QMediaMetaData& t : m_player->videoTracks()) {
      const QSize r = t.value(QMediaMetaData::Resolution).toSize();
      if (r.isValid() && !r.isEmpty()) { res = r; break; }
    }
  }
  if (res.isValid() && !res.isEmpty()) {
    parts << QStringLiteral("%1x%2").arg(res.width()).arg(res.height());
  }
  if (m_player->duration() > 0) {
    parts << formatTime(m_player->duration());
  }
  return parts.join(QStringLiteral("   "));
}

QString MediaView::formatTime(qint64 ms) const {
  const qint64 totalSecs = qMax<qint64>(0, ms) / 1000;
  const qint64 hours     = totalSecs / 3600;
  const qint64 minutes   = (totalSecs % 3600) / 60;
  const qint64 seconds   = totalSecs % 60;
  if (hours > 0 || m_player->duration() >= 3600000) {
    return QStringLiteral("%1:%2:%3")
      .arg(hours)
      .arg(minutes, 2, 10, QLatin1Char('0'))
      .arg(seconds, 2, 10, QLatin1Char('0'));
  }
  return QStringLiteral("%1:%2")
    .arg(minutes)
    .arg(seconds, 2, 10, QLatin1Char('0'));
}

} // namespace Farman
