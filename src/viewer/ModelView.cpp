#include "viewer/ModelView.h"

#include <QMouseEvent>
#include <QWheelEvent>
#include <QtMath>

#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>
#include <assimp/scene.h>

#include <algorithm>
#include <limits>

namespace Farman {

namespace {

const char* kVertexShader = R"(#version 330 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
uniform mat4 uMVP;
uniform mat3 uNormalView;   // ビュー空間の法線行列
out vec3 vNormalView;
void main() {
  vNormalView = uNormalView * aNormal;
  gl_Position = uMVP * vec4(aPos, 1.0);
}
)";

const char* kFragmentShader = R"(#version 330 core
in vec3 vNormalView;
out vec4 fragColor;
void main() {
  vec3 N = normalize(vNormalView);
  if (N.z < 0.0) N = -N;                 // 両面シェーディング
  float head = max(N.z, 0.0);            // カメラ方向のヘッドライト
  float fill = max(dot(N, normalize(vec3(0.3, 0.6, 0.5))), 0.0) * 0.35;
  float lit  = 0.25 + 0.75 * head + fill;
  vec3  base = vec3(0.72, 0.74, 0.78);
  fragColor  = vec4(base * lit, 1.0);
}
)";

} // namespace

ModelView::ModelView(QWidget* parent) : QOpenGLWidget(parent) {
  setFocusPolicy(Qt::StrongFocus);
}

ModelView::~ModelView() {
  // GL リソースはコンテキスト内で解放する
  if (m_glReady) {
    makeCurrent();
    m_vbo.destroy();
    m_ibo.destroy();
    m_vao.destroy();
    doneCurrent();
  }
}

bool ModelView::loadModel(const QString& path, QString* error) {
  Assimp::Importer importer;
  const unsigned int flags = aiProcess_Triangulate | aiProcess_GenSmoothNormals |
                             aiProcess_JoinIdenticalVertices |
                             aiProcess_PreTransformVertices |
                             aiProcess_ImproveCacheLocality;
  const aiScene* scene = importer.ReadFile(path.toUtf8().constData(), flags);
  if (!scene || (scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE) || !scene->mRootNode) {
    if (error) *error = QString::fromUtf8(importer.GetErrorString());
    return false;
  }

  std::vector<float>        verts;
  std::vector<unsigned int> indices;
  QVector3D lo(std::numeric_limits<float>::max(), std::numeric_limits<float>::max(),
              std::numeric_limits<float>::max());
  QVector3D hi(-std::numeric_limits<float>::max(), -std::numeric_limits<float>::max(),
              -std::numeric_limits<float>::max());
  unsigned int triCount = 0;

  for (unsigned int m = 0; m < scene->mNumMeshes; ++m) {
    const aiMesh* mesh = scene->mMeshes[m];
    const unsigned int base = static_cast<unsigned int>(verts.size() / 6);
    for (unsigned int v = 0; v < mesh->mNumVertices; ++v) {
      const aiVector3D& p = mesh->mVertices[v];
      const aiVector3D  n = mesh->HasNormals() ? mesh->mNormals[v] : aiVector3D(0, 0, 1);
      verts.insert(verts.end(), {p.x, p.y, p.z, n.x, n.y, n.z});
      lo = QVector3D(std::min(lo.x(), p.x), std::min(lo.y(), p.y), std::min(lo.z(), p.z));
      hi = QVector3D(std::max(hi.x(), p.x), std::max(hi.y(), p.y), std::max(hi.z(), p.z));
    }
    for (unsigned int f = 0; f < mesh->mNumFaces; ++f) {
      const aiFace& face = mesh->mFaces[f];
      if (face.mNumIndices != 3) continue;  // Triangulate 済みだが念のため
      indices.push_back(base + face.mIndices[0]);
      indices.push_back(base + face.mIndices[1]);
      indices.push_back(base + face.mIndices[2]);
      ++triCount;
    }
  }

  if (verts.empty() || indices.empty()) {
    if (error) *error = QStringLiteral("メッシュが見つかりませんでした");
    return false;
  }

  m_vertices = std::move(verts);
  m_indices  = std::move(indices);
  m_center   = (lo + hi) * 0.5f;
  m_radius   = std::max((hi - lo).length() * 0.5f, 1e-4f);
  m_hasModel = true;
  m_uploaded = false;
  m_summary  = QStringLiteral("%1 メッシュ · %2 頂点 · %3 三角形")
                   .arg(scene->mNumMeshes)
                   .arg(m_vertices.size() / 6)
                   .arg(triCount);
  update();
  return true;
}

void ModelView::initializeGL() {
  initializeOpenGLFunctions();
  m_glReady = true;
  glEnable(GL_DEPTH_TEST);

  if (!m_prog.addShaderFromSourceCode(QOpenGLShader::Vertex, kVertexShader) ||
      !m_prog.addShaderFromSourceCode(QOpenGLShader::Fragment, kFragmentShader) ||
      !m_prog.link()) {
    qWarning("ModelView: shader error: %s", qPrintable(m_prog.log()));
  }
  m_vao.create();
}

void ModelView::uploadIfNeeded() {
  if (m_uploaded || m_vertices.empty()) return;
  m_vao.bind();
  if (!m_vbo.isCreated()) m_vbo.create();
  m_vbo.bind();
  m_vbo.allocate(m_vertices.data(), int(m_vertices.size() * sizeof(float)));
  if (!m_ibo.isCreated()) m_ibo.create();
  m_ibo.bind();
  m_ibo.allocate(m_indices.data(), int(m_indices.size() * sizeof(unsigned int)));
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float),
                        reinterpret_cast<void*>(0));
  glEnableVertexAttribArray(1);
  glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float),
                        reinterpret_cast<void*>(3 * sizeof(float)));
  m_vao.release();
  m_uploaded = true;
}

void ModelView::resizeGL(int, int) {
  // ビューポートは paintGL で devicePixelRatio を考慮して設定する
}

void ModelView::paintGL() {
  const qreal dpr = devicePixelRatioF();
  glViewport(0, 0, GLint(width() * dpr), GLint(height() * dpr));
  glClearColor(0.12f, 0.13f, 0.15f, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

  if (!m_hasModel) return;
  uploadIfNeeded();

  const float r    = m_radius;
  const float dist = m_dist * r;
  const QVector3D dir(std::cos(m_pitch) * std::sin(m_yaw), std::sin(m_pitch),
                      std::cos(m_pitch) * std::cos(m_yaw));
  const QVector3D eye = m_center + dir * dist;

  QMatrix4x4 view;
  view.lookAt(eye, m_center, QVector3D(0, 1, 0));
  QMatrix4x4 proj;
  const float aspect = height() > 0 ? float(width()) / float(height()) : 1.0f;
  proj.perspective(45.0f, aspect, r * 0.01f, dist + r * 10.0f);

  const QMatrix4x4 mvp    = proj * view;   // model = identity
  const QMatrix3x3 normal = view.normalMatrix();

  m_prog.bind();
  m_prog.setUniformValue("uMVP", mvp);
  m_prog.setUniformValue("uNormalView", normal);
  m_vao.bind();
  glDrawElements(GL_TRIANGLES, GLsizei(m_indices.size()), GL_UNSIGNED_INT, nullptr);
  m_vao.release();
  m_prog.release();
}

void ModelView::mousePressEvent(QMouseEvent* e) { m_lastPos = e->pos(); }

void ModelView::mouseMoveEvent(QMouseEvent* e) {
  if (!(e->buttons() & Qt::LeftButton)) return;
  const QPoint d = e->pos() - m_lastPos;
  m_lastPos      = e->pos();
  m_yaw -= d.x() * 0.01f;
  m_pitch += d.y() * 0.01f;
  m_pitch = std::clamp(m_pitch, -1.53f, 1.53f);
  update();
}

void ModelView::wheelEvent(QWheelEvent* e) {
  const float f = e->angleDelta().y() > 0 ? 0.9f : 1.1f;
  m_dist        = std::clamp(m_dist * f, 0.2f, 40.0f);
  update();
}

} // namespace Farman
