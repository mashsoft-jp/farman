#pragma once

// 3D モデルビュアーの描画ウィジェット (PoC)。
// Assimp で読み込んだメッシュを QOpenGLWidget + OpenGL 3.3 Core で表示する。
// オービットカメラ (ドラッグで回転 / ホイールでズーム) と自動フィット付き。
// 将来的にはこのクラスを外部ビュアープラグイン (IViewerPlugin) の中核に流用する。

#include <QMatrix4x4>
#include <QOpenGLBuffer>
#include <QOpenGLFunctions_3_3_Core>
#include <QOpenGLShaderProgram>
#include <QOpenGLVertexArrayObject>
#include <QOpenGLWidget>
#include <QPoint>
#include <QString>
#include <QVector3D>

#include <vector>

namespace Farman {

class ModelView : public QOpenGLWidget, protected QOpenGLFunctions_3_3_Core {
  Q_OBJECT

public:
  explicit ModelView(QWidget* parent = nullptr);
  ~ModelView() override;

  // Assimp でモデルを読み込む。失敗時 false（error に理由）。
  // GL リソースへのアップロードは次の paintGL で遅延実行する。
  bool loadModel(const QString& path, QString* error = nullptr);

  // 読み込んだモデルの概要（ステータス表示用）。
  QString summary() const { return m_summary; }

protected:
  void initializeGL() override;
  void resizeGL(int w, int h) override;
  void paintGL() override;
  void mousePressEvent(QMouseEvent* e) override;
  void mouseMoveEvent(QMouseEvent* e) override;
  void wheelEvent(QWheelEvent* e) override;

private:
  void uploadIfNeeded();

  // CPU 側メッシュ（インターリーブ: 位置3 + 法線3）
  std::vector<float>        m_vertices;
  std::vector<unsigned int> m_indices;
  QVector3D                 m_center{0, 0, 0};
  float                     m_radius   = 1.0f;
  bool                      m_hasModel = false;
  bool                      m_uploaded = false;
  QString                   m_summary;

  // GL リソース
  QOpenGLShaderProgram     m_prog;
  QOpenGLBuffer            m_vbo{QOpenGLBuffer::VertexBuffer};
  QOpenGLBuffer            m_ibo{QOpenGLBuffer::IndexBuffer};
  QOpenGLVertexArrayObject m_vao;
  bool                     m_glReady = false;

  // オービットカメラ
  float  m_yaw   = 0.7f;   // rad
  float  m_pitch = 0.35f;  // rad
  float  m_dist  = 2.6f;   // 半径倍率
  QPoint m_lastPos;
};

} // namespace Farman
