#pragma once

// 3D モデルビュアーの描画ウィジェット (PoC)。
// Assimp で読み込んだメッシュを QOpenGLWidget + OpenGL 3.3 Core で表示する。
//  - 複数マテリアル / メッシュ (サブメッシュごとにマテリアル・テクスチャ)
//  - ディフューズテクスチャ (外部参照 / 埋め込み) と UV
//  - スケルタルアニメーション (ボーンスキニング。GPU で 4 ボーン加重変形)
//  - オービットカメラ / 自動フィット / テクスチャ ON/OFF
// 静的モデルもスキニング用の頂点フォーマットで統一して扱う (weight=0 → 素通し)。
// 将来的にはこのクラスを外部ビュアープラグイン (IViewerPlugin) の中核に流用する。

#include <QElapsedTimer>
#include <QImage>
#include <QMatrix4x4>
#include <QOpenGLBuffer>
#include <QOpenGLFunctions_3_3_Core>
#include <QOpenGLShaderProgram>
#include <QOpenGLVertexArrayObject>
#include <QOpenGLWidget>
#include <QPoint>
#include <QQuaternion>
#include <QString>
#include <QStringList>
#include <QVector3D>

#include <memory>
#include <vector>

class QOpenGLTexture;
class QTimer;

namespace Farman {

class ModelView : public QOpenGLWidget, protected QOpenGLFunctions_3_3_Core {
  Q_OBJECT

public:
  explicit ModelView(QWidget* parent = nullptr);
  ~ModelView() override;

  bool loadModel(const QString& path, QString* error = nullptr);

  QString summary() const { return m_summary; }

  // ── テクスチャ情報 (複数マテリアル対応) ──
  bool        hasUV() const { return m_hasUV; }
  int         materialCount() const { return int(m_materials.size()); }
  bool        hasTexture() const;             // いずれかのマテリアルがテクスチャ参照を持つ
  bool        hasEmbeddedTexture() const;     // 埋め込みが 1 つでもあるか
  QStringList recordedTexturePaths() const;   // FBX 記録値 (外部参照ぶん)
  QStringList resolvedTexturePaths() const;   // 実ファイル (解決できたぶん)
  QStringList unresolvedTexturePaths() const; // 見つからなかった記録値

  // ── アニメーション ──
  bool   hasAnimation() const { return m_hasAnim; }
  double animationDuration() const { return m_ticksPerSec > 0 ? m_animDurationTicks / m_ticksPerSec : 0; }

public slots:
  void setTextureEnabled(bool on);
  void setAnimationPlaying(bool on);
  void setAnimationTime(double sec);
  void setShowGrid(bool on);
  void setWireframe(bool on);
  void resetView();

protected:
  void initializeGL() override;
  void resizeGL(int w, int h) override;
  void paintGL() override;
  void mousePressEvent(QMouseEvent* e) override;
  void mouseMoveEvent(QMouseEvent* e) override;
  void wheelEvent(QWheelEvent* e) override;
  void keyPressEvent(QKeyEvent* e) override;

private:
  void uploadIfNeeded();
  void buildGrid();
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
  bool                      m_wireframe  = false;
  QVector3D                 m_bboxMin{0, 0, 0};
  QVector3D                 m_bboxMax{0, 0, 0};
  QString                   m_summary;

  // グリッド + 座標軸 (unlit ライン)
  std::vector<float>       m_lineVerts;  // pos3 + color3
  int                      m_lineCount = 0;
  bool                     m_linesUploaded = false;
  QOpenGLShaderProgram     m_lineProg;
  QOpenGLBuffer            m_lineVbo{QOpenGLBuffer::VertexBuffer};
  QOpenGLVertexArrayObject m_lineVao;

  // ── マテリアル / サブメッシュ ──
  struct Material {
    QVector3D                       baseColor{0.72f, 0.74f, 0.78f};
    bool                            hasTexture = false;
    bool                            embedded   = false;
    bool                            resolved   = false;
    QString                         recordedPath;
    QString                         resolvedPath;
    QImage                          image;  // GL アップロード待ち
    std::unique_ptr<QOpenGLTexture> tex;
  };
  struct SubMesh {
    int indexOffset = 0;  // m_indices 内のオフセット (要素数)
    int indexCount  = 0;
    int material    = 0;  // m_materials の index
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

  // GL リソース
  QOpenGLShaderProgram     m_prog;
  QOpenGLBuffer            m_vbo{QOpenGLBuffer::VertexBuffer};
  QOpenGLBuffer            m_ibo{QOpenGLBuffer::IndexBuffer};
  QOpenGLVertexArrayObject m_vao;
  bool                     m_glReady = false;

  // オービットカメラ
  float  m_yaw   = 0.7f;
  float  m_pitch = 0.35f;
  float  m_dist  = 2.6f;
  QPoint m_lastPos;
};

} // namespace Farman
