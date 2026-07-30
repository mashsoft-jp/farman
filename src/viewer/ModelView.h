#pragma once

// 3D モデルビュアーの描画ウィジェット (PoC)。
// Assimp で読み込んだメッシュを QOpenGLWidget + OpenGL 3.3 Core で表示する。
// ディフューズテクスチャ (外部参照 / 埋め込み) と UV に対応し、テクスチャ表示は
// ON/OFF できる。オービットカメラ (ドラッグで回転 / ホイールでズーム) と自動フィット付き。
// 将来的にはこのクラスを外部ビュアープラグイン (IViewerPlugin) の中核に流用する。

#include <QImage>
#include <QMatrix4x4>
#include <QOpenGLBuffer>
#include <QOpenGLFunctions_3_3_Core>
#include <QOpenGLShaderProgram>
#include <QOpenGLVertexArrayObject>
#include <QOpenGLWidget>
#include <QPoint>
#include <QString>
#include <QVector3D>

#include <memory>
#include <vector>

class QOpenGLTexture;

namespace Farman {

class ModelView : public QOpenGLWidget, protected QOpenGLFunctions_3_3_Core {
  Q_OBJECT

public:
  explicit ModelView(QWidget* parent = nullptr);
  ~ModelView() override;

  // Assimp でモデルを読み込む。失敗時 false（error に理由）。
  bool loadModel(const QString& path, QString* error = nullptr);

  QString summary() const { return m_summary; }

  // ── テクスチャ情報 ──
  bool    hasTexture() const { return m_hasTexture; }        // マテリアルが参照を持つ
  bool    hasUV() const { return m_hasUV; }                  // メッシュに UV がある
  bool    textureEmbedded() const { return m_texEmbedded; }  // FBX 埋め込みか
  bool    textureResolved() const { return m_texResolved; }  // 実体を解決して読めたか
  QString textureRecordedPath() const { return m_texRecordedPath; }  // FBX 記録値
  QString textureResolvedPath() const { return m_texResolvedPath; }  // 実ファイルパス

public slots:
  void setTextureEnabled(bool on);

protected:
  void initializeGL() override;
  void resizeGL(int w, int h) override;
  void paintGL() override;
  void mousePressEvent(QMouseEvent* e) override;
  void mouseMoveEvent(QMouseEvent* e) override;
  void wheelEvent(QWheelEvent* e) override;

private:
  void uploadIfNeeded();

  // CPU 側メッシュ（インターリーブ: 位置3 + 法線3 + UV2）
  std::vector<float>        m_vertices;
  std::vector<unsigned int> m_indices;
  QVector3D                 m_center{0, 0, 0};
  float                     m_radius   = 1.0f;
  bool                      m_hasModel = false;
  bool                      m_uploaded = false;
  QString                   m_summary;

  // テクスチャ / マテリアル
  bool                            m_hasTexture = false;
  bool                            m_hasUV      = false;
  bool                            m_texEmbedded = false;
  bool                            m_texResolved = false;
  bool                            m_texEnabled  = true;
  QString                         m_texRecordedPath;
  QString                         m_texResolvedPath;
  QImage                          m_texImage;
  std::unique_ptr<QOpenGLTexture> m_tex;
  QVector3D                       m_baseColor{0.72f, 0.74f, 0.78f};

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
