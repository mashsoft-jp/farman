#include "viewer/ModelView.h"

#include <QDir>
#include <QFileInfo>
#include <QMouseEvent>
#include <QOpenGLTexture>
#include <QWheelEvent>
#include <QtMath>

#include <assimp/Importer.hpp>
#include <assimp/material.h>
#include <assimp/postprocess.h>
#include <assimp/scene.h>

#include <algorithm>
#include <limits>

namespace Farman {

namespace {

const char* kVertexShader = R"(#version 330 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aUV;
uniform mat4 uMVP;
uniform mat3 uNormalView;
out vec3 vNormalView;
out vec2 vUV;
void main() {
  vNormalView = uNormalView * aNormal;
  vUV = aUV;
  gl_Position = uMVP * vec4(aPos, 1.0);
}
)";

const char* kFragmentShader = R"(#version 330 core
in vec3 vNormalView;
in vec2 vUV;
uniform sampler2D uTex;
uniform bool uUseTexture;
uniform vec3 uBaseColor;
out vec4 fragColor;
void main() {
  vec3 N = normalize(vNormalView);
  if (N.z < 0.0) N = -N;                 // 両面シェーディング
  float head = max(N.z, 0.0);            // カメラ方向のヘッドライト
  float fill = max(dot(N, normalize(vec3(0.3, 0.6, 0.5))), 0.0) * 0.35;
  float lit  = 0.28 + 0.72 * head + fill;
  vec3  base = uUseTexture ? texture(uTex, vec2(vUV.x, 1.0 - vUV.y)).rgb : uBaseColor;
  fragColor  = vec4(base * lit, 1.0);
}
)";

// FBX に記録された（多くは作者環境の）テクスチャパスから、実在するファイルを探す。
// 記録値そのまま → FBX 基準の相対 → ファイル名だけを FBX 隣 / textures/ から、の順。
QString resolveTexturePath(const QString& fbxPath, const QString& recorded) {
  QString rec = recorded;
  rec.replace('\\', '/');
  const QDir     dir(QFileInfo(fbxPath).absolutePath());
  const QString  base = QFileInfo(rec).fileName();
  const QStringList candidates = {
      rec,                                        // 絶対パスそのまま
      dir.absoluteFilePath(rec),                  // FBX 基準の相対
      dir.absoluteFilePath(base),                 // FBX と同じフォルダ
      dir.absoluteFilePath("textures/" + base),   // ./textures/
      dir.absoluteFilePath("../textures/" + base) // ../textures/
  };
  for (const QString& c : candidates) {
    if (QFileInfo::exists(c)) return QFileInfo(c).absoluteFilePath();
  }
  return QString();
}

} // namespace

ModelView::ModelView(QWidget* parent) : QOpenGLWidget(parent) {
  setFocusPolicy(Qt::StrongFocus);
}

ModelView::~ModelView() {
  if (m_glReady) {
    makeCurrent();
    m_tex.reset();
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
  bool         anyUV    = false;

  for (unsigned int m = 0; m < scene->mNumMeshes; ++m) {
    const aiMesh* mesh    = scene->mMeshes[m];
    const bool    meshUV  = mesh->HasTextureCoords(0);
    anyUV                 = anyUV || meshUV;
    const unsigned int bs = static_cast<unsigned int>(verts.size() / 8);
    for (unsigned int v = 0; v < mesh->mNumVertices; ++v) {
      const aiVector3D& p = mesh->mVertices[v];
      const aiVector3D  n = mesh->HasNormals() ? mesh->mNormals[v] : aiVector3D(0, 0, 1);
      const aiVector3D  t = meshUV ? mesh->mTextureCoords[0][v] : aiVector3D(0, 0, 0);
      verts.insert(verts.end(), {p.x, p.y, p.z, n.x, n.y, n.z, t.x, t.y});
      lo = QVector3D(std::min(lo.x(), p.x), std::min(lo.y(), p.y), std::min(lo.z(), p.z));
      hi = QVector3D(std::max(hi.x(), p.x), std::max(hi.y(), p.y), std::max(hi.z(), p.z));
    }
    for (unsigned int f = 0; f < mesh->mNumFaces; ++f) {
      const aiFace& face = mesh->mFaces[f];
      if (face.mNumIndices != 3) continue;
      indices.push_back(bs + face.mIndices[0]);
      indices.push_back(bs + face.mIndices[1]);
      indices.push_back(bs + face.mIndices[2]);
      ++triCount;
    }
  }

  if (verts.empty() || indices.empty()) {
    if (error) *error = QStringLiteral("メッシュが見つかりませんでした");
    return false;
  }

  // ── マテリアル / テクスチャ ──
  m_hasTexture = m_texEmbedded = m_texResolved = false;
  m_texRecordedPath.clear();
  m_texResolvedPath.clear();
  m_texImage = QImage();
  m_baseColor = QVector3D(0.72f, 0.74f, 0.78f);
  if (scene->mNumMaterials > 0) {
    const aiMaterial* mat = scene->mMaterials[scene->mMeshes[0]->mMaterialIndex];
    aiColor3D col(0, 0, 0);
    if (mat->Get(AI_MATKEY_COLOR_DIFFUSE, col) == AI_SUCCESS &&
        (col.r + col.g + col.b) > 0.05f) {
      m_baseColor = QVector3D(col.r, col.g, col.b);
    }
    aiString tp;
    if (mat->GetTexture(aiTextureType_DIFFUSE, 0, &tp) == AI_SUCCESS && tp.length > 0) {
      m_hasTexture      = true;
      m_texRecordedPath = QString::fromUtf8(tp.C_Str());
      if (m_texRecordedPath.startsWith('*')) {  // 埋め込み
        m_texEmbedded = true;
        const int idx = m_texRecordedPath.mid(1).toInt();
        if (idx >= 0 && idx < int(scene->mNumTextures)) {
          const aiTexture* t = scene->mTextures[idx];
          if (t->mHeight == 0) {  // png/jpg 等の圧縮データ
            m_texImage.loadFromData(reinterpret_cast<const uchar*>(t->pcData), int(t->mWidth));
          } else {  // 生 BGRA
            m_texImage = QImage(reinterpret_cast<const uchar*>(t->pcData), int(t->mWidth),
                                int(t->mHeight), QImage::Format_ARGB32)
                             .rgbSwapped()
                             .copy();
          }
          m_texResolved = !m_texImage.isNull();
        }
      } else {  // 外部参照
        m_texResolvedPath = resolveTexturePath(path, m_texRecordedPath);
        if (!m_texResolvedPath.isEmpty() && m_texImage.load(m_texResolvedPath)) {
          m_texResolved = true;
        }
      }
    }
  }

  m_vertices = std::move(verts);
  m_indices  = std::move(indices);
  m_center   = (lo + hi) * 0.5f;
  m_radius   = std::max((hi - lo).length() * 0.5f, 1e-4f);
  m_hasModel = true;
  m_hasUV    = anyUV;
  m_uploaded = false;
  m_tex.reset();
  m_summary = QStringLiteral("%1 メッシュ · %2 頂点 · %3 三角形")
                  .arg(scene->mNumMeshes)
                  .arg(m_vertices.size() / 8)
                  .arg(triCount);
  update();
  return true;
}

void ModelView::setTextureEnabled(bool on) {
  m_texEnabled = on;
  update();
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
  const int stride = 8 * sizeof(float);
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(0));
  glEnableVertexAttribArray(1);
  glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(3 * sizeof(float)));
  glEnableVertexAttribArray(2);
  glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(6 * sizeof(float)));
  m_vao.release();

  if (m_texResolved && !m_texImage.isNull() && !m_tex) {
    m_tex = std::make_unique<QOpenGLTexture>(m_texImage, QOpenGLTexture::GenerateMipMaps);
    m_tex->setMinificationFilter(QOpenGLTexture::LinearMipMapLinear);
    m_tex->setMagnificationFilter(QOpenGLTexture::Linear);
    m_tex->setWrapMode(QOpenGLTexture::Repeat);
  }
  m_uploaded = true;
}

void ModelView::resizeGL(int, int) {}

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

  const QMatrix4x4 mvp    = proj * view;
  const QMatrix3x3 normal = view.normalMatrix();
  const bool       useTex = m_texEnabled && m_tex;

  m_prog.bind();
  m_prog.setUniformValue("uMVP", mvp);
  m_prog.setUniformValue("uNormalView", normal);
  m_prog.setUniformValue("uUseTexture", useTex);
  m_prog.setUniformValue("uBaseColor", m_baseColor);
  if (useTex) {
    m_tex->bind(0);
    m_prog.setUniformValue("uTex", 0);
  }
  m_vao.bind();
  glDrawElements(GL_TRIANGLES, GLsizei(m_indices.size()), GL_UNSIGNED_INT, nullptr);
  m_vao.release();
  if (useTex) m_tex->release(0);
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
