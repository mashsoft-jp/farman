#pragma once

// 3D モデルビュアーの描画ウィジェット。
//  - Assimp 読み込み / ディフューズテクスチャ (外部・埋め込み) / UV
//  - スケルタルアニメーション (GPU スキニング, 4 ボーン加重)
//  - 複数マテリアル・メッシュ (サブメッシュ単位)
//  - グリッド + 座標軸 / ワイヤーフレーム / オービット・自動フィット
//
// 描画は「隠し QOpenGLContext + QOffscreenSurface + FBO にオフスクリーン描画し、
// 得た QImage を paintEvent で表示する」方式。ネイティブ GL 面 (QOpenGLWidget)
// を使わないため、macOS で QStackedWidget 等に埋め込んでも黒くならず正しく合成
// される (farman の動画ビュアーが QVideoWidget → QGraphicsVideoItem にした理由と
// 同じ問題への対処)。単体では farman-plugin-3d の中核として流用する。

#include <QElapsedTimer>
#include <QImage>
#include <QMatrix4x4>
#include <QOpenGLBuffer>
#include <QOpenGLFunctions_3_3_Core>
#include <QOpenGLShaderProgram>
#include <QOpenGLVertexArrayObject>
#include <QQuaternion>
#include <QSize>
#include <QString>
#include <QStringList>
#include <QVector3D>
#include <QWidget>

#include <memory>
#include <vector>

class QOpenGLContext;
class QOffscreenSurface;
class QOpenGLFramebufferObject;
class QOpenGLTexture;
class QTimer;

namespace Farman {

class ModelView : public QWidget, protected QOpenGLFunctions_3_3_Core {
  Q_OBJECT

public:
  explicit ModelView(QWidget* parent = nullptr);
  ~ModelView() override;

  bool loadModel(const QString& path, QString* error = nullptr);

  // 強制的に 1 フレーム描画して画像を返す (オフスクリーン検証 / スナップショット用)。
  QImage renderToImage();

  QString     summary() const { return m_summary; }
  QStringList infoLines() const { return m_infoLines; }  // 情報ダイアログ用

  // ── テクスチャ情報 ──
  bool        hasUV() const { return m_hasUV; }
  int         materialCount() const { return int(m_materials.size()); }
  bool        hasTexture() const;
  bool        hasEmbeddedTexture() const;
  QStringList recordedTexturePaths() const;
  QStringList resolvedTexturePaths() const;
  QStringList unresolvedTexturePaths() const;

  // ── アニメーション ──
  bool   hasAnimation() const { return m_hasAnim; }
  double animationDuration() const { return m_ticksPerSec > 0 ? m_animDurationTicks / m_ticksPerSec : 0; }

public slots:
  void setTextureEnabled(bool on);
  void setAnimationPlaying(bool on);
  void setAnimationTime(double sec);
  void setShowGrid(bool on);
  void setShowHelp(bool on);
  void setWireframe(bool on);
  void resetView();

  bool showHelp() const { return m_showHelp; }

signals:
  void textureEnabledChanged(bool on);
  void showGridChanged(bool on);
  void showHelpChanged(bool on);
  void animationPlayingChanged(bool on);
  void infoRequested();  // i キー / ツールバーの情報ボタン相当

protected:
  void paintEvent(QPaintEvent* e) override;
  void resizeEvent(QResizeEvent* e) override;
  void showEvent(QShowEvent* e) override;
  void mousePressEvent(QMouseEvent* e) override;
  void mouseMoveEvent(QMouseEvent* e) override;
  void wheelEvent(QWheelEvent* e) override;
  void keyPressEvent(QKeyEvent* e) override;

private:
  bool ensureContext();
  void ensureGLResources();
  void uploadIfNeeded();
  void buildGrid();
  void buildInfoText();
  void renderFrame();  // makeCurrent → FBO 描画 → toImage → update()
  void computeBoneMatrices(double timeTicks, bool bindPose);

  // 頂点: 位置3 + 法線3 + UV2 + boneID4 + weight4 = 16 float
  static constexpr int kFloatsPerVertex = 16;
  std::vector<float>        m_vertices;
  std::vector<unsigned int> m_indices;
  QVector3D                 m_center{0, 0, 0};
  float                     m_radius   = 1.0f;
  bool                      m_hasModel = false;
  bool                      m_uploaded = false;
  bool                      m_hasUV    = false;
  bool                      m_texEnabled = true;
  bool                      m_showGrid   = true;
  bool                      m_showHelp   = true;
  bool                      m_wireframe  = false;
  QVector3D                 m_bboxMin{0, 0, 0};
  QVector3D                 m_bboxMax{0, 0, 0};
  QString                   m_summary;

  // グリッド + 座標軸 (unlit ライン)
  std::vector<float>       m_lineVerts;
  int                      m_lineCount = 0;
  bool                     m_linesUploaded = false;

  // ── マテリアル / サブメッシュ ──
  struct Material {
    QVector3D                       baseColor{0.72f, 0.74f, 0.78f};
    bool                            hasTexture = false;
    bool                            embedded   = false;
    bool                            resolved   = false;
    QString                         recordedPath;
    QString                         resolvedPath;
    QImage                          image;
    std::unique_ptr<QOpenGLTexture> tex;
  };
  struct SubMesh {
    int indexOffset = 0;
    int indexCount  = 0;
    int material    = 0;
  };
  std::vector<Material> m_materials;
  std::vector<SubMesh>  m_submeshes;

  // ── スケルトン / アニメーション ──
  struct Node {
    QString    name;
    QMatrix4x4 bind;
    int        parent = -1;
  };
  struct Vec3Key { double t; QVector3D v; };
  struct QuatKey { double t; QQuaternion q; };
  struct Channel {
    bool                 used = false;
    std::vector<Vec3Key> pos;
    std::vector<QuatKey> rot;
    std::vector<Vec3Key> scale;
  };
  std::vector<Node>       m_nodes;
  std::vector<Channel>    m_channels;
  std::vector<QMatrix4x4> m_boneOffset;
  std::vector<int>        m_boneNode;
  QMatrix4x4              m_globalInverse;
  std::vector<QMatrix4x4> m_boneMatrices;
  double                  m_animDurationTicks = 0.0;
  double                  m_ticksPerSec       = 25.0;
  bool                    m_hasAnim   = false;
  bool                    m_playing   = true;
  double                  m_animTimeSec = 0.0;
  qint64                  m_lastMs      = 0;
  QElapsedTimer           m_clock;
  QTimer*                 m_animTimer = nullptr;

  // ── オフスクリーン GL ──
  QOpenGLContext*           m_ctx     = nullptr;
  QOffscreenSurface*        m_surface = nullptr;
  QOpenGLFramebufferObject* m_fbo     = nullptr;
  bool                      m_glResourcesReady = false;
  QImage                    m_frame;

  QOpenGLShaderProgram     m_prog;
  QOpenGLShaderProgram     m_lineProg;
  QOpenGLBuffer            m_vbo{QOpenGLBuffer::VertexBuffer};
  QOpenGLBuffer            m_ibo{QOpenGLBuffer::IndexBuffer};
  QOpenGLVertexArrayObject m_vao;
  QOpenGLBuffer            m_lineVbo{QOpenGLBuffer::VertexBuffer};
  QOpenGLVertexArrayObject m_lineVao;

  // オービットカメラ
  float     m_yaw   = 0.7f;
  float     m_pitch = 0.35f;
  float     m_dist  = 2.6f;
  QVector3D m_pan{0, 0, 0};  // 平行移動 (ワールド)
  QPoint    m_lastPos;

  // 表示補助 (右上ギズモ / 情報テキスト)
  QStringList m_infoLines;
  QString     m_filePath;
};

} // namespace Farman
