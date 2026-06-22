#include "MediaView.h"
#include "settings/Settings.h"
#include "utils/EnterClickFilter.h"

#include <QAudio>
#include <QAudioOutput>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFileInfo>
#include <QFont>
#include <QFontDatabase>
#include <QHash>
#include <QKeyEvent>
#include <QLabel>
#include <QLocale>
#include <QMediaMetaData>
#include <QPlainTextEdit>
#include <QMouseEvent>
#include <QSlider>
#include <QStackedWidget>
#include <QStyle>
#include <QStyleOptionSlider>
#include <QToolBar>
#include <QToolButton>
#include <QVBoxLayout>
#include <QVideoWidget>

namespace Farman {

namespace {
// ←/→ のシーク量。Shift 併用で大きく飛ぶ (SPEC: 数秒単位 / 大きく)。
constexpr qint64 kSeekStepMs      = 5000;
constexpr qint64 kSeekLargeStepMs = 30000;
constexpr int    kVolumeStep      = 5;

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
  setupUi();
}

MediaView::~MediaView() {
  // フルスクリーンのままビュアーが破棄されると QVideoWidget のトップレベル
  // ウィンドウだけが残るので、先に通常表示へ戻しておく。
  setVideoFullScreen(false);
}

void MediaView::setupUi() {
  setFocusPolicy(Qt::StrongFocus);

  m_player      = new QMediaPlayer(this);
  m_audioOutput = new QAudioOutput(this);
  m_player->setAudioOutput(m_audioOutput);

  // ── 表示ページ (動画 / 音声カード / メッセージ) ──
  m_videoWidget = new QVideoWidget(this);
  m_videoWidget->setAspectRatioMode(Qt::KeepAspectRatio);
  m_videoWidget->setFocusPolicy(Qt::StrongFocus);
  m_videoWidget->installEventFilter(this);
  m_player->setVideoOutput(m_videoWidget);

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
  m_stack->addWidget(m_videoWidget);
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
  m_toolbar->setStyleSheet(toolbarStyleSheet());

  m_playButton = new QToolButton(m_toolbar);
  m_playButton->setToolTip(tr("Play / Pause (Space)"));
  m_playButton->setFocusPolicy(Qt::StrongFocus);
  connect(m_playButton, &QToolButton::clicked,
          this,         &MediaView::togglePlayPause);
  m_toolbar->addWidget(m_playButton);

  m_stopButton = new QToolButton(m_toolbar);
  m_stopButton->setIcon(QIcon(QStringLiteral(":/icons/toolbar/stop.svg")));
  m_stopButton->setToolTip(tr("Stop"));
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
  connect(m_positionSlider, &QSlider::sliderMoved, this, [this](int value) {
    m_player->setPosition(value);
  });
  m_toolbar->addWidget(m_positionSlider);

  m_timeLabel = new QLabel(QStringLiteral("0:00 / 0:00"), m_toolbar);
  m_timeLabel->setContentsMargins(6, 0, 6, 0);
  m_toolbar->addWidget(m_timeLabel);

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

  m_rateCombo = new QComboBox(m_toolbar);
  m_rateCombo->addItem(QStringLiteral("0.5x"), 0.5);
  m_rateCombo->addItem(QStringLiteral("1.0x"), 1.0);
  m_rateCombo->addItem(QStringLiteral("1.5x"), 1.5);
  m_rateCombo->addItem(QStringLiteral("2.0x"), 2.0);
  m_rateCombo->setCurrentIndex(1);
  m_rateCombo->setToolTip(tr("Playback speed"));
  m_rateCombo->setFocusPolicy(Qt::StrongFocus);
  connect(m_rateCombo, &QComboBox::currentIndexChanged, this, [this](int) {
    m_player->setPlaybackRate(m_rateCombo->currentData().toReal());
  });
  m_toolbar->addWidget(m_rateCombo);

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
  m_volumeSlider->setToolTip(tr("Volume (Up/Down)"));
  connect(m_volumeSlider, &QSlider::valueChanged,
          this,           &MediaView::applyVolume);
  m_toolbar->addWidget(m_volumeSlider);

  m_fullScreenButton = new QToolButton(m_toolbar);
  m_fullScreenButton->setIcon(QIcon(QStringLiteral(":/icons/toolbar/fullscreen.svg")));
  m_fullScreenButton->setToolTip(tr("Full screen (F / double-click, Esc to exit)"));
  m_fullScreenButton->setFocusPolicy(Qt::StrongFocus);
  connect(m_fullScreenButton, &QToolButton::clicked, this, [this]() {
    setVideoFullScreen(true);
  });
  m_fullScreenAction = m_toolbar->addWidget(m_fullScreenButton);
  // 動画のあるファイルでのみ表示 (updateCurrentPage で切替)。
  m_fullScreenAction->setVisible(false);

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
  m_toolbar->addWidget(m_infoButton);

  auto* layout = new QVBoxLayout(this);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(0);
  layout->addWidget(m_toolbar);
  layout->addWidget(m_stack, 1);

  // Tab 順を明示 (ImageView と同じ作法。スライダは NoFocus なので含めない)。
  setTabOrder(m_playButton, m_stopButton);
  setTabOrder(m_stopButton, m_loopButton);
  setTabOrder(m_loopButton, m_rateCombo);
  setTabOrder(m_rateCombo,  m_muteButton);
  setTabOrder(m_muteButton, m_fullScreenButton);
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
  });
  connect(m_player, &QMediaPlayer::durationChanged, this, [this](qint64 dur) {
    m_positionSlider->setRange(0, int(dur));
    m_positionSlider->setEnabled(dur > 0);
    updateTimeLabel();
    // duration はメタデータより後に確定することがあるので情報も更新する。
    refreshMediaInfoDialog();
  });
  connect(m_player, &QMediaPlayer::playbackStateChanged,
          this,     &MediaView::updatePlayButton);
  connect(m_player, &QMediaPlayer::hasVideoChanged,
          this,     &MediaView::updateCurrentPage);
  connect(m_player, &QMediaPlayer::metaDataChanged,
          this,     &MediaView::updateMetadataCard);
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
}

void MediaView::openFile(const QString& filePath) {
  m_filePath     = filePath;
  m_loadNotified = false;
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
  if (watched == m_videoWidget) {
    if (event->type() == QEvent::KeyPress) {
      if (handleViewerKey(static_cast<QKeyEvent*>(event))) {
        return true;
      }
    } else if (event->type() == QEvent::MouseButtonDblClick) {
      setVideoFullScreen(!m_videoWidget->isFullScreen());
      return true;
    }
  }
  return QWidget::eventFilter(watched, event);
}

bool MediaView::handleViewerKey(QKeyEvent* event) {
  const bool shift = event->modifiers().testFlag(Qt::ShiftModifier);
  switch (event->key()) {
    case Qt::Key_Space:
      togglePlayPause();
      return true;
    case Qt::Key_Left:
      seekBy(shift ? -kSeekLargeStepMs : -kSeekStepMs);
      return true;
    case Qt::Key_Right:
      seekBy(shift ? kSeekLargeStepMs : kSeekStepMs);
      return true;
    case Qt::Key_Up:
      adjustVolume(kVolumeStep);
      return true;
    case Qt::Key_Down:
      adjustVolume(-kVolumeStep);
      return true;
    case Qt::Key_M:
      m_muteButton->toggle();
      return true;
    case Qt::Key_L:
      m_loopButton->toggle();
      return true;
    case Qt::Key_I:
      toggleMediaInfoDialog();
      return true;
    case Qt::Key_F:
      if (m_player->hasVideo()) {
        setVideoFullScreen(!m_videoWidget->isFullScreen());
        return true;
      }
      return false;
    case Qt::Key_Escape:
      // フルスクリーン解除のみここで消費。通常表示中の Esc は
      // 親 (ViewerPanel / ViewerWindow) の「閉じる」処理に流す。
      if (m_videoWidget->isFullScreen()) {
        setVideoFullScreen(false);
        return true;
      }
      return false;
    default:
      return false;
  }
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
  if (!m_videoWidget || m_videoWidget->isFullScreen() == on) return;
  m_videoWidget->setFullScreen(on);
  if (on) {
    // トップレベル化した QVideoWidget にキーが届くようフォーカスを移す。
    // キーは eventFilter で MediaView と同じ処理に流れる。
    m_videoWidget->setFocus(Qt::OtherFocusReason);
  } else {
    setFocus(Qt::OtherFocusReason);
  }
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
  m_stack->setCurrentWidget(hasVideo ? static_cast<QWidget*>(m_videoWidget)
                                     : static_cast<QWidget*>(m_audioCard));
  m_fullScreenAction->setVisible(hasVideo);
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
