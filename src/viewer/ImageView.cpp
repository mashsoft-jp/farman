#include "ImageView.h"
#include "settings/Settings.h"
#include "utils/ExifReader.h"

#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFontDatabase>
#include <QPlainTextEdit>
#include <QToolButton>
#include "utils/EnterClickFilter.h"
#include <QEvent>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QToolBar>
#include <QImage>
#include <QImageReader>
#include <QLabel>
#include <QLocale>
#include <QMovie>
#include <QPainter>
#include <QPaintEvent>
#include <QResizeEvent>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QVBoxLayout>

namespace Farman {

// 画像描画ウィジェット。背景 (チェック柄 / 単色) と画像を自前描画する。
class ImageDisplay : public QWidget {
public:
  // host: ImageView 本体への参照 (ローカルの透明モードを取得するため)
  explicit ImageDisplay(QWidget* parent, ImageView* host)
    : QWidget(parent), m_host(host) {
    setAutoFillBackground(false);
  }

  // 静止画モード: pixmap セット (movie はクリア)
  void setStaticPixmap(const QPixmap& p) {
    m_movie = nullptr;
    m_pixmap = p;
    updateGeometryAndPaint();
  }

  // アニメモード: 外部所有の QMovie を表示。frameChanged で再描画。
  void setMovie(QMovie* movie) {
    m_movie = movie;
    if (m_movie) {
      m_pixmap = m_movie->currentPixmap();
      // QMovie はサイズが分かっていないこともあるので、初期サイズは pixmap から
      QObject::connect(m_movie, &QMovie::frameChanged, this, [this](int) {
        if (m_movie) {
          m_pixmap = m_movie->currentPixmap();
        }
        update();
      });
    }
    updateGeometryAndPaint();
  }

  void clearImage() {
    m_movie = nullptr;
    m_pixmap = QPixmap();
    updateGeometryAndPaint();
  }

  void setZoomPercent(int percent) {
    m_zoomPercent = qBound(1, percent, 1000);
    m_fit = false;
    updateGeometryAndPaint();
  }
  void setFitToWindow(bool fit) {
    m_fit = fit;
    updateGeometryAndPaint();
  }

  // viewport (= QScrollArea のビューポート) を渡す。Fit 計算で使う。
  void setViewportWidget(QWidget* viewport) { m_viewport = viewport; }

  // Fit 時に算出される実効ズーム % (UI 表示用)。
  int effectiveZoomPercent() const {
    const QSize ds = displaySize();
    if (m_pixmap.isNull() || ds.isEmpty()) return m_zoomPercent;
    const double rx = static_cast<double>(ds.width())  / m_pixmap.width();
    const double ry = static_cast<double>(ds.height()) / m_pixmap.height();
    return qMax(1, static_cast<int>(qRound(qMin(rx, ry) * 100)));
  }

  // 親ウィジェットのリサイズを反映するため呼ばれる (Fit 時のスケール再計算と、
  // 非 Fit 時の背景フィル用に常に呼び出す)。
  void onViewportResized() {
    updateGeometryAndPaint();
  }

protected:
  void paintEvent(QPaintEvent* event) override {
    QPainter p(this);
    paintBackground(p, event->rect());
    if (!m_pixmap.isNull()) {
      const QSize ds = displaySize();
      const QRect target((width() - ds.width()) / 2, (height() - ds.height()) / 2,
                          ds.width(), ds.height());
      p.drawPixmap(target, m_pixmap);
    }
  }

private:
  QSize displaySize() const {
    if (m_pixmap.isNull()) return QSize();
    if (m_fit && m_viewport) {
      const QSize avail = m_viewport->size();
      return m_pixmap.size().scaled(avail, Qt::KeepAspectRatio);
    }
    return m_pixmap.size() * (m_zoomPercent / 100.0);
  }

  void updateGeometryAndPaint() {
    QSize image = displaySize();
    QSize s = image;
    // 透明部分の背景でビューポート全体を埋めるため、Fit でなくても画像が
    // ビューポートより小さい場合はウィジェットをビューポートサイズまで広げる
    if (m_viewport) {
      const QSize vp = m_viewport->size();
      s.setWidth(qMax(s.width(),  vp.width()));
      s.setHeight(qMax(s.height(), vp.height()));
    }
    if (s.isEmpty()) s = QSize(1, 1);
    resize(s);
    update();
  }

  void paintBackground(QPainter& p, const QRect& rect) {
    const Settings& s = Settings::instance();
    // モードはローカル上書きを優先 (ImageView 経由で取得)、色は Settings 由来
    const ImageTransparencyMode mode =
      m_host ? m_host->transparencyMode() : s.imageViewerTransparencyMode();
    if (mode == ImageTransparencyMode::SolidColor) {
      p.fillRect(rect, s.imageViewerSolidColor());
      return;
    }
    // チェック柄 (16px) — 2 つの色は Settings から取得
    const int cell = 16;
    const QColor c1 = s.imageViewerCheckerColor1();
    const QColor c2 = s.imageViewerCheckerColor2();
    const int x0 = (rect.left() / cell) * cell;
    const int y0 = (rect.top()  / cell) * cell;
    for (int y = y0; y <= rect.bottom(); y += cell) {
      for (int x = x0; x <= rect.right(); x += cell) {
        const bool odd = ((x / cell) + (y / cell)) & 1;
        p.fillRect(QRect(x, y, cell, cell), odd ? c2 : c1);
      }
    }
  }

  QPixmap         m_pixmap;
  QMovie*         m_movie    = nullptr;
  int             m_zoomPercent = 100;
  bool            m_fit         = false;
  QPointer<QWidget> m_viewport;
  ImageView*      m_host = nullptr;
};

// ---------- ImageView ----------

ImageView::ImageView(QWidget* parent)
  : QWidget(parent)
{
  setupUi();
  syncFromSettings();
  applyDisplayState();

  connect(&Settings::instance(), &Settings::settingsChanged, this, [this] {
    syncFromSettings();
    applyDisplayState();
    if (m_display) m_display->update();
  });
}

void ImageView::setupUi() {
  QVBoxLayout* root = new QVBoxLayout(this);
  root->setContentsMargins(0, 0, 0, 0);
  root->setSpacing(0);

  // ツールバー (メインウィンドウのツールバーと同じ QToolBar を使う)。
  // QToolButton はメイン側と同じ視覚スタイル (autoRaise / hover 等) になる。
  m_toolbar = new QToolBar(this);
  m_toolbar->setMovable(false);
  m_toolbar->setFloatable(false);
  m_toolbar->setIconSize(QSize(20, 20));
  // フォーカス枠 + checkable の押下状態 + ホバー。共通スタイル。
  m_toolbar->setStyleSheet(toolbarStyleSheet());

  m_toolbar->addWidget(new QLabel(tr("Zoom:"), m_toolbar));
  m_zoomCombo = new QComboBox(m_toolbar);
  m_zoomCombo->setEditable(true);
  for (int p : { 25, 50, 75, 100, 200 }) {
    m_zoomCombo->addItem(QString::number(p) + QLatin1Char('%'), p);
  }
  m_zoomCombo->setFocusPolicy(Qt::StrongFocus);
  m_toolbar->addWidget(m_zoomCombo);

  // 「ウィンドウに合わせる」(checkable + アイコン)。チェック ON で fitToWindow。
  m_fitButton = new QToolButton(m_toolbar);
  m_fitButton->setCheckable(true);
  m_fitButton->setIcon(QIcon(QStringLiteral(":/icons/toolbar/fit-to-window.svg")));
  m_fitButton->setToolTip(tr("Fit to Window"));
  m_fitButton->setFocusPolicy(Qt::StrongFocus);
  m_toolbar->addWidget(m_fitButton);

  // 再生 / 停止 (アニメ画像のみ)。OFF なら ▶、ON なら ⏸ アイコン。
  m_animButton = new QToolButton(m_toolbar);
  m_animButton->setCheckable(true);
  m_animButton->setIcon(QIcon(QStringLiteral(":/icons/toolbar/play.svg")));
  m_animButton->setToolTip(tr("Play / Pause animation (GIF / WebP)"));
  m_animButton->setFocusPolicy(Qt::StrongFocus);
  m_toolbar->addWidget(m_animButton);

  // 透明部分のモード (checkable トグル)。OFF = Checker、ON = SolidColor。
  m_transparencyButton = new QToolButton(m_toolbar);
  m_transparencyButton->setCheckable(true);
  m_transparencyButton->setIcon(QIcon(QStringLiteral(":/icons/toolbar/transparency.svg")));
  m_transparencyButton->setToolTip(tr(
    "Transparency background: off = checker, on = solid color"));
  m_transparencyButton->setFocusPolicy(Qt::StrongFocus);
  m_toolbar->addWidget(m_transparencyButton);

  // 画像メタデータダイアログを開くボタン (フォーマット情報・コメント
  // 等のテキストメタデータをまとめて表示する)。
  m_infoButton = new QToolButton(m_toolbar);
  m_infoButton->setText(QStringLiteral("i"));
  QFont infoFont = m_infoButton->font();
  infoFont.setItalic(true);
  infoFont.setBold(true);
  m_infoButton->setFont(infoFont);
  m_infoButton->setToolTip(tr("Show image information / metadata"));
  m_infoButton->setFocusPolicy(Qt::StrongFocus);
  connect(m_infoButton, &QToolButton::clicked,
          this,         &ImageView::toggleImageInfoDialog);
  m_toolbar->addWidget(m_infoButton);

  root->addWidget(m_toolbar);

  // 画像表示部 (スクロール可)
  m_scrollArea = new QScrollArea(this);
  m_scrollArea->setWidgetResizable(false);
  m_scrollArea->setAlignment(Qt::AlignCenter);
  m_display = new ImageDisplay(m_scrollArea, this);
  m_display->setViewportWidget(m_scrollArea->viewport());
  m_scrollArea->setWidget(m_display);
  // ビューポートのリサイズで ImageDisplay のサイズを再計算する
  m_scrollArea->viewport()->installEventFilter(this);
  root->addWidget(m_scrollArea, /*stretch*/ 1);

  setFocusProxy(m_scrollArea);

  // ── Tab 順を明示的に設定 ─────────────────────────
  // デフォルト (= ウィジェット作成順) では、Tab がツールバーのいくつかの
  // コントロールを飛ばしたり、scrollArea の子で停まったりすることがあるので、
  // setTabOrder() で明示的にチェーンを組む。
  // 順序: zoomCombo → fitCheck → animButton → transparencyCombo → scrollArea。
  // addToolbarWidget で追加された widget は m_lastToolbarWidget の後に
  // 順次差し込まれる。
  setTabOrder(m_zoomCombo,          m_fitButton);
  setTabOrder(m_fitButton,          m_animButton);
  setTabOrder(m_animButton,         m_transparencyButton);
  setTabOrder(m_transparencyButton, m_infoButton);
  setTabOrder(m_infoButton,         m_scrollArea);
  m_lastToolbarWidget = m_infoButton;

  // ツールバー内のボタンで Tab フォーカス中に Enter を押したらそのボタンを
  // クリック扱いにする (= 親ウィジェットの "Enter で戻る" 等が誤発火しない)。
  // toolbar 配下の QAbstractButton 子孫すべてに一括 install。
  // 後から addToolbarWidget で追加された widget にも install するため、
  // メンバ m_clickFilter で保持しておく。
  m_clickFilter = new EnterClickFilter(this);
  m_clickFilter->installOnButtonsIn(m_toolbar);

  // ローカル変更
  connect(m_zoomCombo, &QComboBox::currentTextChanged, this, [this](const QString& text) {
    const QString trimmed = text.trimmed();
    if (trimmed.isEmpty()) return;
    QString num = trimmed;
    if (num.endsWith(QLatin1Char('%'))) num.chop(1);
    bool ok = false;
    int v = num.toInt(&ok);
    if (!ok) return;
    m_zoomPercent = qBound(1, v, 1000);
    if (!m_fitToWindow) {
      m_display->setZoomPercent(m_zoomPercent);
    }
  });
  connect(m_fitButton, &QToolButton::toggled, this, [this](bool on) {
    if (on) {
      m_fitToWindow = true;
      updateZoomEnabled();
      m_display->setFitToWindow(true);
      // Fit 時はコンボの表示値を実効ズームに合わせる (シグナル抑止)
      QSignalBlocker b(m_zoomCombo);
      m_zoomCombo->setCurrentText(QString::number(m_display->effectiveZoomPercent()) + QLatin1Char('%'));
    } else {
      // Fit OFF: 直前の実効ズームを引き継ぐ (フィット結果のまま固定)
      const int held = m_display->effectiveZoomPercent();
      m_fitToWindow = false;
      updateZoomEnabled();
      m_display->setFitToWindow(false);
      m_zoomPercent = qBound(1, held, 1000);
      m_display->setZoomPercent(m_zoomPercent);
      QSignalBlocker b(m_zoomCombo);
      m_zoomCombo->setCurrentText(QString::number(m_zoomPercent) + QLatin1Char('%'));
    }
  });
  connect(m_animButton, &QToolButton::toggled, this, [this](bool on) {
    m_animation = on;
    // 再生中は ⏸、停止中は ▶ に差し替える。
    m_animButton->setIcon(QIcon(on
      ? QStringLiteral(":/icons/toolbar/pause.svg")
      : QStringLiteral(":/icons/toolbar/play.svg")));
    applyDisplayState();
  });
  connect(m_transparencyButton, &QToolButton::toggled, this, [this](bool on) {
    // OFF = Checker、ON = SolidColor。
    m_transparencyMode = on
      ? ImageTransparencyMode::SolidColor
      : ImageTransparencyMode::Checker;
    if (m_display) m_display->update();
  });
}

void ImageView::syncFromSettings() {
  const Settings& s = Settings::instance();
  m_zoomPercent      = s.imageViewerZoomPercent();
  m_fitToWindow      = s.imageViewerFitToWindow();
  m_animation        = s.imageViewerAnimation();
  m_transparencyMode = s.imageViewerTransparencyMode();

  {
    QSignalBlocker b(m_zoomCombo);
    m_zoomCombo->setCurrentText(QString::number(m_zoomPercent) + QLatin1Char('%'));
  }
  {
    QSignalBlocker b(m_fitButton);
    m_fitButton->setChecked(m_fitToWindow);
  }
  {
    QSignalBlocker b(m_animButton);
    m_animButton->setChecked(m_animation);
    m_animButton->setIcon(QIcon(m_animation
      ? QStringLiteral(":/icons/toolbar/pause.svg")
      : QStringLiteral(":/icons/toolbar/play.svg")));
  }
  {
    QSignalBlocker b(m_transparencyButton);
    m_transparencyButton->setChecked(
      m_transparencyMode == ImageTransparencyMode::SolidColor);
  }
  updateZoomEnabled();
}

void ImageView::updateZoomEnabled() {
  // Fit 時は手動ズーム指定を無効化 (SPEC: ON 時は設定不可)
  m_zoomCombo->setEnabled(!m_fitToWindow);
}

void ImageView::applyDisplayState() {
  if (!m_display) return;
  m_display->setFitToWindow(m_fitToWindow);
  if (!m_fitToWindow) {
    m_display->setZoomPercent(m_zoomPercent);
  }

  // アニメ再生 / 停止の切替
  if (m_movie) {
    if (m_fileIsAnimated && m_animation) {
      if (m_movie->state() != QMovie::Running) m_movie->start();
    } else {
      if (m_movie->state() == QMovie::Running) m_movie->stop();
      m_movie->jumpToFrame(0);
    }
  }
}

bool ImageView::loadFile(const QString& filePath) {
  // 同期ロード経路: prepareLoad + applyPreparedLoad を続けて呼ぶだけ。
  // 非同期化したい呼び出し元 (ViewerPanel) は両者を別スレッドに振り分ける。
  PreparedLoad p = prepareLoad(filePath);
  if (!p.ok) return false;
  applyPreparedLoad(p);
  return true;
}

ImageView::PreparedLoad ImageView::prepareLoad(const QString& filePath) {
  PreparedLoad r;
  r.filePath   = filePath;
  r.isAnimated = detectAnimated(filePath);

  if (!r.isAnimated) {
    // 静止画は QImage で読み込む (QPixmap はメインスレッド限定なので bg では避ける)。
    QImage img;
    if (!img.load(filePath)) {
      r.ok = false;
      return r;
    }
    r.image = img;
  }
  // アニメ画像の場合、QMovie 構築は applyPreparedLoad (main thread) で行う。
  r.ok = true;
  return r;
}

void ImageView::applyPreparedLoad(const PreparedLoad& r) {
  // 既存の Movie を破棄
  if (m_movie) {
    m_movie->deleteLater();
    m_movie = nullptr;
  }
  m_fileIsAnimated = r.isAnimated;
  m_filePath       = r.filePath;

  if (m_fileIsAnimated) {
    auto* movie = new QMovie(r.filePath, QByteArray(), this);
    if (!movie->isValid()) {
      movie->deleteLater();
      m_fileIsAnimated = false;
    } else {
      m_movie = movie;
      m_display->setMovie(movie);
      // 初期 1 フレーム描画
      movie->jumpToFrame(0);
      // QMovie 経由のアニメ画像でも自然サイズが取れる場合がある。
      // ダメなら frame 0 の pixmap から拾う。
      m_naturalImageSize = movie->frameRect().size();
      if (!m_naturalImageSize.isValid() || m_naturalImageSize.isEmpty()) {
        const QPixmap fp = movie->currentPixmap();
        if (!fp.isNull()) m_naturalImageSize = fp.size();
      }
    }
  }

  if (!m_fileIsAnimated) {
    // bg で QImage に読み込んだものを main で QPixmap に変換して反映。
    const QPixmap pm = QPixmap::fromImage(r.image);
    if (!pm.isNull()) {
      m_display->setStaticPixmap(pm);
      m_naturalImageSize = pm.size();
    } else {
      m_naturalImageSize = QSize();
    }
  }

  // 再生ボタンの有効化: 真にアニメで複数フレームある場合のみ。
  // - 静止画 (PNG/JPEG/BMP 等) → disable
  // - 1 フレームしかない GIF/WebP → disable (= "アニメじゃない GIF")
  // - QMovie::frameCount() が 0 (= 不明) のときは保守的に enable のまま
  //   (デコーダが事前判定できないだけで、実際は複数フレームの可能性がある)
  bool playEnabled = false;
  if (m_fileIsAnimated && m_movie) {
    const int fc = m_movie->frameCount();
    playEnabled = (fc != 1);  // 0 (不明) も再生試行を許可
  }
  if (m_animButton) {
    m_animButton->setEnabled(playEnabled);
    if (!playEnabled) {
      m_animButton->setChecked(false);
    }
  }

  // Info ダイアログが開いていれば内容を新ファイルに差し替え。
  refreshImageInfoDialog();

  applyDisplayState();
}

int ImageView::effectiveZoomPercent() const {
  // Fit-to-Window モードのときは ImageDisplay が viewport サイズから算出した
  // 自動倍率を返してくれる。マニュアルズーム時は素直に m_zoomPercent。
  return m_display ? m_display->effectiveZoomPercent() : m_zoomPercent;
}

QSize ImageView::imageAreaSize() const {
  if (m_scrollArea && m_scrollArea->viewport()) {
    return m_scrollArea->viewport()->size();
  }
  return QSize();
}

void ImageView::addToolbarWidget(QWidget* widget) {
  if (!m_toolbar || !widget) return;
  // QToolBar は addWidget で末尾追加。ImageViewerWindow が「ウィンドウ
  // サイズを画像にあわせる」ボタンを足すような用途。
  m_toolbar->addWidget(widget);

  // Tab 順: 既存ツールバー末尾 → 新 widget → scrollArea。
  // これをやらないと、デフォルトのウィジェット作成順では新 widget が
  // scrollArea の後ろに回ってしまい、Tab で巡回しづらくなる。
  if (m_lastToolbarWidget && m_scrollArea) {
    setTabOrder(m_lastToolbarWidget, widget);
    setTabOrder(widget,              m_scrollArea);
    m_lastToolbarWidget = widget;
  }

  // Enter→クリック化のフィルタを新 widget (とその子孫ボタン) に拡張する。
  if (m_clickFilter) {
    m_clickFilter->installOnButtonsIn(widget);
  }
}

void ImageView::clearContent() {
  if (m_movie) {
    m_movie->deleteLater();
    m_movie = nullptr;
  }
  m_filePath.clear();
  m_fileIsAnimated = false;
  m_naturalImageSize = QSize();
  m_display->clearImage();
}

bool ImageView::eventFilter(QObject* watched, QEvent* event) {
  if (m_scrollArea && watched == m_scrollArea->viewport()
      && event->type() == QEvent::Resize) {
    if (m_display) m_display->onViewportResized();
  }
  return QWidget::eventFilter(watched, event);
}

bool ImageView::detectAnimated(const QString& filePath) {
  QImageReader reader(filePath);
  return reader.supportsAnimation() && reader.imageCount() > 1;
}

QString ImageView::statusInfo() const {
  if (m_filePath.isEmpty()) return QString();
  QImageReader reader(m_filePath);
  const QString fmt = QString::fromLatin1(reader.format()).toUpper();
  const QSize sz   = reader.size();
  const qint64 bytes = QFileInfo(m_filePath).size();
  QString s = QStringLiteral("%1  ·  %2x%3")
                .arg(fmt.isEmpty() ? QStringLiteral("?") : fmt)
                .arg(sz.width()).arg(sz.height());
  // 色深度 (取得できるフォーマットのみ)。
  // ImageFormat は OS / プラグイン依存で取れる場合と取れない場合あり。
  const QImage::Format imgFmt = reader.imageFormat();
  if (imgFmt != QImage::Format_Invalid) {
    const int depth = QImage(1, 1, imgFmt).depth();
    if (depth > 0) s += QStringLiteral("  ·  %1 bpp").arg(depth);
  }
  if (m_fileIsAnimated && m_movie) {
    const int fc = m_movie->frameCount();
    if (fc > 0) {
      s += QStringLiteral("  ·  %1 frames").arg(fc);
    }
  }
  s += QStringLiteral("  ·  %1").arg(QLocale(QLocale::English).formattedDataSize(bytes));
  return s;
}

QString ImageView::buildImageInfoText() const {
  if (m_filePath.isEmpty()) return QString();

  QImageReader reader(m_filePath);
  const QString fmt = QString::fromLatin1(reader.format()).toUpper();
  const QSize sz    = reader.size();
  const QFileInfo fi(m_filePath);

  QString body;
  body += tr("File: %1").arg(m_filePath) + QLatin1Char('\n');
  body += tr("Format: %1").arg(fmt.isEmpty() ? QStringLiteral("(unknown)") : fmt)
        + QLatin1Char('\n');
  body += tr("Size: %1 x %2 px").arg(sz.width()).arg(sz.height())
        + QLatin1Char('\n');
  body += tr("File size: %1").arg(
            QLocale(QLocale::English).formattedDataSize(fi.size()))
        + QLatin1Char('\n');

  // 色深度
  const QImage::Format imgFmt = reader.imageFormat();
  if (imgFmt != QImage::Format_Invalid) {
    const int depth = QImage(1, 1, imgFmt).depth();
    if (depth > 0) {
      body += tr("Color depth: %1 bpp").arg(depth) + QLatin1Char('\n');
    }
  }

  // アニメ
  if (m_fileIsAnimated && m_movie) {
    const int fc = m_movie->frameCount();
    if (fc > 0) {
      body += tr("Frames: %1").arg(fc) + QLatin1Char('\n');
    }
  }

  // DPI (QImage::dotsPerMeterX/Y は実画像を読み込んで取得)。
  QImage probe = reader.read();
  if (!probe.isNull()) {
    const int dpmx = probe.dotsPerMeterX();
    const int dpmy = probe.dotsPerMeterY();
    if (dpmx > 0 && dpmy > 0) {
      const double dpix = dpmx * 0.0254;
      const double dpiy = dpmy * 0.0254;
      body += tr("Resolution: %1 x %2 DPI")
                .arg(qRound(dpix)).arg(qRound(dpiy))
            + QLatin1Char('\n');
    }
  }

  // QImageReader::text() で取れるテキストメタデータ
  // (PNG の tEXt / iTXt キー、JPEG コメント等)
  const QStringList keys = reader.textKeys();
  if (!keys.isEmpty()) {
    body += QLatin1Char('\n') + tr("--- Embedded text ---") + QLatin1Char('\n');
    for (const QString& k : keys) {
      const QString v = reader.text(k);
      if (v.isEmpty()) continue;
      body += k + QStringLiteral(": ") + v + QLatin1Char('\n');
    }
  }

  // JPEG であれば Exif (Make/Model/Date/ISO/Aperture/Focal/Flash/WhiteBalance/
  // ColorSpace/GPS など) を自前パーサで抽出して表示する。
  // 他フォーマットは Qt の対応範囲外 (PNG/WebP の Exif は将来課題)。
  if (fmt == QLatin1String("JPEG") || fmt == QLatin1String("JPG")) {
    const auto exif = ExifReader::readFromJpeg(m_filePath);
    if (!exif.isEmpty()) {
      body += QLatin1Char('\n') + tr("--- Exif ---") + QLatin1Char('\n');
      for (const auto& p : exif) {
        body += p.first + QStringLiteral(": ") + p.second + QLatin1Char('\n');
      }
    }
  }

  return body;
}

void ImageView::toggleImageInfoDialog() {
  if (m_filePath.isEmpty()) return;
  // 既に表示中なら閉じる (= トグル動作)。
  if (m_infoDialog && m_infoDialog->isVisible()) {
    m_infoDialog->close();
    return;
  }
  // 新規もしくは閉じた状態 → 開く。
  if (!m_infoDialog) {
    m_infoDialog = new QDialog(this);
    m_infoDialog->setWindowTitle(tr("Image Information"));
    m_infoDialog->setAttribute(Qt::WA_DeleteOnClose, false);
    m_infoDialog->resize(640, 480);

    auto* layout = new QVBoxLayout(m_infoDialog);
    auto* edit = new QPlainTextEdit(m_infoDialog);
    edit->setObjectName(QStringLiteral("imageInfoEdit"));
    edit->setReadOnly(true);
    edit->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    layout->addWidget(edit);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, m_infoDialog);
    connect(buttons, &QDialogButtonBox::rejected, m_infoDialog, &QDialog::close);
    layout->addWidget(buttons);
  }
  // モードレスで開く + 内容を最新化。
  if (auto* edit = m_infoDialog->findChild<QPlainTextEdit*>(
        QStringLiteral("imageInfoEdit"))) {
    edit->setPlainText(buildImageInfoText());
  }
  m_infoDialog->show();
  m_infoDialog->raise();
  m_infoDialog->activateWindow();
}

void ImageView::refreshImageInfoDialog() {
  if (!m_infoDialog || !m_infoDialog->isVisible()) return;
  if (auto* edit = m_infoDialog->findChild<QPlainTextEdit*>(
        QStringLiteral("imageInfoEdit"))) {
    edit->setPlainText(buildImageInfoText());
  }
}

} // namespace Farman
