#include "viewer/ModelView.h"

#include <QDir>
#include <QFileInfo>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QOpenGLTexture>
#include <QTimer>
#include <QWheelEvent>
#include <QtMath>

#include <assimp/Importer.hpp>
#include <assimp/material.h>
#include <assimp/postprocess.h>
#include <assimp/scene.h>

#include <algorithm>
#include <array>
#include <limits>
#include <unordered_map>

namespace Farman {

namespace {

const char* kVertexShader = R"(#version 330 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aUV;
layout(location = 3) in vec4 aBoneIDs;
layout(location = 4) in vec4 aWeights;
const int MAX_BONES = 128;
uniform mat4 uMVP;
uniform mat3 uNormalView;
uniform mat4 uBones[MAX_BONES];
uniform bool uSkinned;
out vec3 vNormalView;
out vec2 vUV;
void main() {
  vec4 pos = vec4(aPos, 1.0);
  vec3 nrm = aNormal;
  float wsum = aWeights.x + aWeights.y + aWeights.z + aWeights.w;
  if (uSkinned && wsum > 0.0001) {
    mat4 skin = aWeights.x * uBones[int(aBoneIDs.x)] +
                aWeights.y * uBones[int(aBoneIDs.y)] +
                aWeights.z * uBones[int(aBoneIDs.z)] +
                aWeights.w * uBones[int(aBoneIDs.w)];
    pos = skin * pos;
    nrm = mat3(skin) * aNormal;
  }
  vNormalView = uNormalView * nrm;
  vUV = aUV;
  gl_Position = uMVP * pos;
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
  if (N.z < 0.0) N = -N;
  float head = max(N.z, 0.0);
  float fill = max(dot(N, normalize(vec3(0.3, 0.6, 0.5))), 0.0) * 0.35;
  float lit  = 0.28 + 0.72 * head + fill;
  vec3  base = uUseTexture ? texture(uTex, vec2(vUV.x, 1.0 - vUV.y)).rgb : uBaseColor;
  fragColor  = vec4(base * lit, 1.0);
}
)";

const char* kLineVertexShader = R"(#version 330 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aColor;
uniform mat4 uMVP;
out vec3 vColor;
void main() {
  vColor = aColor;
  gl_Position = uMVP * vec4(aPos, 1.0);
}
)";

const char* kLineFragmentShader = R"(#version 330 core
in vec3 vColor;
out vec4 fragColor;
void main() { fragColor = vec4(vColor, 1.0); }
)";

QMatrix4x4 toQM(const aiMatrix4x4& m) {
  return QMatrix4x4(m.a1, m.a2, m.a3, m.a4, m.b1, m.b2, m.b3, m.b4, m.c1, m.c2, m.c3, m.c4,
                    m.d1, m.d2, m.d3, m.d4);
}

QString resolveTexturePath(const QString& fbxPath, const QString& recorded) {
  QString rec = recorded;
  rec.replace('\\', '/');
  const QDir        dir(QFileInfo(fbxPath).absolutePath());
  const QString     base = QFileInfo(rec).fileName();
  const QStringList candidates = {rec, dir.absoluteFilePath(rec), dir.absoluteFilePath(base),
                                  dir.absoluteFilePath("textures/" + base),
                                  dir.absoluteFilePath("../textures/" + base)};
  for (const QString& c : candidates) {
    if (QFileInfo::exists(c)) return QFileInfo(c).absoluteFilePath();
  }
  return QString();
}

template <typename Key, typename Val>
Val interpKeys(const std::vector<Key>& keys, double t, Val fallback) {
  if (keys.empty()) return fallback;
  if (keys.size() == 1 || t <= keys.front().t) return keys.front().v;
  if (t >= keys.back().t) return keys.back().v;
  for (size_t i = 0; i + 1 < keys.size(); ++i) {
    if (t < keys[i + 1].t) {
      const double f = (t - keys[i].t) / (keys[i + 1].t - keys[i].t);
      return keys[i].v * (1.0f - float(f)) + keys[i + 1].v * float(f);
    }
  }
  return keys.back().v;
}

} // namespace

ModelView::ModelView(QWidget* parent) : QOpenGLWidget(parent) {
  setFocusPolicy(Qt::StrongFocus);
  m_animTimer = new QTimer(this);
  m_animTimer->setInterval(16);
  connect(m_animTimer, &QTimer::timeout, this, [this] {
    if (m_hasAnim && m_playing) update();
  });
}

ModelView::~ModelView() {
  if (m_glReady) {
    makeCurrent();
    for (auto& m : m_materials) m.tex.reset();
    m_vbo.destroy();
    m_ibo.destroy();
    m_vao.destroy();
    m_lineVbo.destroy();
    m_lineVao.destroy();
    doneCurrent();
  }
}

bool ModelView::hasTexture() const {
  for (const auto& m : m_materials)
    if (m.hasTexture) return true;
  return false;
}
bool ModelView::hasEmbeddedTexture() const {
  for (const auto& m : m_materials)
    if (m.embedded) return true;
  return false;
}
QStringList ModelView::recordedTexturePaths() const {
  QStringList out;
  for (const auto& m : m_materials)
    if (m.hasTexture && !m.embedded && !out.contains(m.recordedPath)) out << m.recordedPath;
  return out;
}
QStringList ModelView::resolvedTexturePaths() const {
  QStringList out;
  for (const auto& m : m_materials)
    if (m.resolved && !m.resolvedPath.isEmpty() && !out.contains(m.resolvedPath))
      out << m.resolvedPath;
  return out;
}
QStringList ModelView::unresolvedTexturePaths() const {
  QStringList out;
  for (const auto& m : m_materials)
    if (m.hasTexture && !m.embedded && !m.resolved && !out.contains(m.recordedPath))
      out << m.recordedPath;
  return out;
}

bool ModelView::loadModel(const QString& path, QString* error) {
  Assimp::Importer importer;
  const unsigned int flags = aiProcess_Triangulate | aiProcess_GenSmoothNormals |
                             aiProcess_JoinIdenticalVertices | aiProcess_LimitBoneWeights |
                             aiProcess_ImproveCacheLocality;
  const aiScene* scene = importer.ReadFile(path.toUtf8().constData(), flags);
  if (!scene || (scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE) || !scene->mRootNode) {
    if (error) *error = QString::fromUtf8(importer.GetErrorString());
    return false;
  }

  const bool animated = scene->mNumAnimations > 0;

  // ── ノード階層 (DFS 前順) ──
  m_nodes.clear();
  std::unordered_map<std::string, int> nodeIndex;
  {
    struct Item { const aiNode* n; int parent; };
    std::vector<Item> stack{{scene->mRootNode, -1}};
    while (!stack.empty()) {
      Item it = stack.back();
      stack.pop_back();
      const int idx = int(m_nodes.size());
      m_nodes.push_back({QString::fromUtf8(it.n->mName.C_Str()), toQM(it.n->mTransformation),
                         it.parent});
      nodeIndex[it.n->mName.C_Str()] = idx;
      for (int c = int(it.n->mNumChildren) - 1; c >= 0; --c)
        stack.push_back({it.n->mChildren[c], idx});
    }
  }
  m_globalInverse = toQM(scene->mRootNode->mTransformation).inverted();

  // ── マテリアル ──
  m_materials.clear();
  m_materials.resize(scene->mNumMaterials);
  for (unsigned int mi = 0; mi < scene->mNumMaterials; ++mi) {
    const aiMaterial* mat = scene->mMaterials[mi];
    Material&         out = m_materials[mi];
    aiColor3D         col(0, 0, 0);
    if (mat->Get(AI_MATKEY_COLOR_DIFFUSE, col) == AI_SUCCESS && (col.r + col.g + col.b) > 0.05f)
      out.baseColor = QVector3D(col.r, col.g, col.b);
    aiString tp;
    if (mat->GetTexture(aiTextureType_DIFFUSE, 0, &tp) == AI_SUCCESS && tp.length > 0) {
      out.hasTexture   = true;
      out.recordedPath = QString::fromUtf8(tp.C_Str());
      if (out.recordedPath.startsWith('*')) {  // 埋め込み
        out.embedded  = true;
        const int idx = out.recordedPath.mid(1).toInt();
        if (idx >= 0 && idx < int(scene->mNumTextures)) {
          const aiTexture* t = scene->mTextures[idx];
          if (t->mHeight == 0)
            out.image.loadFromData(reinterpret_cast<const uchar*>(t->pcData), int(t->mWidth));
          else
            out.image = QImage(reinterpret_cast<const uchar*>(t->pcData), int(t->mWidth),
                               int(t->mHeight), QImage::Format_ARGB32)
                            .rgbSwapped()
                            .copy();
          out.resolved = !out.image.isNull();
        }
      } else {  // 外部参照
        out.resolvedPath = resolveTexturePath(path, out.recordedPath);
        if (!out.resolvedPath.isEmpty() && out.image.load(out.resolvedPath)) out.resolved = true;
      }
    }
  }
  if (m_materials.empty()) m_materials.resize(1);  // マテリアル無しでも 1 つ用意

  // ── ジオメトリ + サブメッシュ + スキンウェイト ──
  std::vector<float>        verts;
  std::vector<unsigned int> indices;
  m_submeshes.clear();
  QVector3D lo(std::numeric_limits<float>::max(), std::numeric_limits<float>::max(),
              std::numeric_limits<float>::max());
  QVector3D hi(-std::numeric_limits<float>::max(), -std::numeric_limits<float>::max(),
              -std::numeric_limits<float>::max());
  unsigned int triCount = 0;
  bool         anyUV    = false;

  m_boneOffset.clear();
  m_boneNode.clear();
  std::unordered_map<std::string, int> boneId;

  for (unsigned int mi = 0; mi < scene->mNumMeshes; ++mi) {
    const aiMesh* mesh   = scene->mMeshes[mi];
    const bool    meshUV = mesh->HasTextureCoords(0);
    anyUV                = anyUV || meshUV;
    const unsigned int base = static_cast<unsigned int>(verts.size() / ModelView::kFloatsPerVertex);
    const int          subStart = int(indices.size());

    std::vector<std::array<float, 4>> vid(mesh->mNumVertices, {0, 0, 0, 0});
    std::vector<std::array<float, 4>> vwt(mesh->mNumVertices, {0, 0, 0, 0});
    std::vector<int>                  cnt(mesh->mNumVertices, 0);
    for (unsigned int b = 0; b < mesh->mNumBones; ++b) {
      const aiBone*     bone = mesh->mBones[b];
      const std::string name = bone->mName.C_Str();
      int               id;
      auto              it = boneId.find(name);
      if (it == boneId.end()) {
        id           = int(m_boneOffset.size());
        boneId[name] = id;
        m_boneOffset.push_back(toQM(bone->mOffsetMatrix));
        auto ni = nodeIndex.find(name);
        m_boneNode.push_back(ni == nodeIndex.end() ? -1 : ni->second);
      } else {
        id = it->second;
      }
      for (unsigned int w = 0; w < bone->mNumWeights; ++w) {
        const unsigned int v  = bone->mWeights[w].mVertexId;
        const float        wt = bone->mWeights[w].mWeight;
        if (cnt[v] < 4) {
          vid[v][cnt[v]] = float(id);
          vwt[v][cnt[v]] = wt;
          ++cnt[v];
        }
      }
    }

    for (unsigned int v = 0; v < mesh->mNumVertices; ++v) {
      const aiVector3D& p = mesh->mVertices[v];
      const aiVector3D  n = mesh->HasNormals() ? mesh->mNormals[v] : aiVector3D(0, 0, 1);
      const aiVector3D  t = meshUV ? mesh->mTextureCoords[0][v] : aiVector3D(0, 0, 0);
      std::array<float, 4> w = vwt[v];
      const float          s = w[0] + w[1] + w[2] + w[3];
      if (s > 0.0f)
        for (float& x : w) x /= s;
      verts.insert(verts.end(), {p.x, p.y, p.z, n.x, n.y, n.z, t.x, t.y, vid[v][0], vid[v][1],
                                 vid[v][2], vid[v][3], w[0], w[1], w[2], w[3]});
      lo = QVector3D(std::min(lo.x(), p.x), std::min(lo.y(), p.y), std::min(lo.z(), p.z));
      hi = QVector3D(std::max(hi.x(), p.x), std::max(hi.y(), p.y), std::max(hi.z(), p.z));
    }
    for (unsigned int f = 0; f < mesh->mNumFaces; ++f) {
      const aiFace& face = mesh->mFaces[f];
      if (face.mNumIndices != 3) continue;
      indices.push_back(base + face.mIndices[0]);
      indices.push_back(base + face.mIndices[1]);
      indices.push_back(base + face.mIndices[2]);
      ++triCount;
    }
    SubMesh sm;
    sm.indexOffset = subStart;
    sm.indexCount  = int(indices.size()) - subStart;
    sm.material    = std::min<int>(mesh->mMaterialIndex, int(m_materials.size()) - 1);
    if (sm.indexCount > 0) m_submeshes.push_back(sm);
  }

  if (verts.empty() || indices.empty()) {
    if (error) *error = QStringLiteral("メッシュが見つかりませんでした");
    return false;
  }

  // ── アニメーションチャンネル ──
  m_channels.assign(m_nodes.size(), Channel{});
  m_hasAnim           = false;
  m_animDurationTicks = 0.0;
  m_ticksPerSec       = 25.0;
  if (animated) {
    const aiAnimation* anim = scene->mAnimations[0];
    m_animDurationTicks     = anim->mDuration;
    m_ticksPerSec           = anim->mTicksPerSecond > 0 ? anim->mTicksPerSecond : 25.0;
    for (unsigned int c = 0; c < anim->mNumChannels; ++c) {
      const aiNodeAnim* ch = anim->mChannels[c];
      auto              it = nodeIndex.find(ch->mNodeName.C_Str());
      if (it == nodeIndex.end()) continue;
      Channel& out = m_channels[it->second];
      out.used     = true;
      for (unsigned int k = 0; k < ch->mNumPositionKeys; ++k)
        out.pos.push_back({ch->mPositionKeys[k].mTime,
                           QVector3D(ch->mPositionKeys[k].mValue.x, ch->mPositionKeys[k].mValue.y,
                                     ch->mPositionKeys[k].mValue.z)});
      for (unsigned int k = 0; k < ch->mNumRotationKeys; ++k) {
        const aiQuaternion& q = ch->mRotationKeys[k].mValue;
        out.rot.push_back({ch->mRotationKeys[k].mTime, QQuaternion(q.w, q.x, q.y, q.z)});
      }
      for (unsigned int k = 0; k < ch->mNumScalingKeys; ++k)
        out.scale.push_back({ch->mScalingKeys[k].mTime,
                             QVector3D(ch->mScalingKeys[k].mValue.x, ch->mScalingKeys[k].mValue.y,
                                       ch->mScalingKeys[k].mValue.z)});
    }
    m_hasAnim = !m_boneOffset.empty() && m_animDurationTicks > 0.0;
  }

  m_vertices = std::move(verts);
  m_indices  = std::move(indices);

  if (m_hasAnim) {
    computeBoneMatrices(0.0, /*bindPose=*/true);
    lo = QVector3D(std::numeric_limits<float>::max(), std::numeric_limits<float>::max(),
                   std::numeric_limits<float>::max());
    hi = -lo;
    const int stride = ModelView::kFloatsPerVertex;
    for (size_t i = 0; i + stride <= m_vertices.size(); i += stride) {
      const QVector3D p(m_vertices[i], m_vertices[i + 1], m_vertices[i + 2]);
      const float w0 = m_vertices[i + 12], w1 = m_vertices[i + 13], w2 = m_vertices[i + 14],
                  w3 = m_vertices[i + 15];
      QVector3D sp = p;
      if (w0 + w1 + w2 + w3 > 0.0001f) {
        const int  id0 = int(m_vertices[i + 8]), id1 = int(m_vertices[i + 9]),
                  id2 = int(m_vertices[i + 10]), id3 = int(m_vertices[i + 11]);
        QMatrix4x4 skin;
        skin.fill(0);
        auto add = [&](int id, float w) {
          if (id >= 0 && id < int(m_boneMatrices.size()))
            for (int r = 0; r < 4; ++r)
              for (int c = 0; c < 4; ++c) skin(r, c) += w * m_boneMatrices[id](r, c);
        };
        add(id0, w0);
        add(id1, w1);
        add(id2, w2);
        add(id3, w3);
        sp = skin.map(p);
      }
      lo = QVector3D(std::min(lo.x(), sp.x()), std::min(lo.y(), sp.y()), std::min(lo.z(), sp.z()));
      hi = QVector3D(std::max(hi.x(), sp.x()), std::max(hi.y(), sp.y()), std::max(hi.z(), sp.z()));
    }
  }

  m_center   = (lo + hi) * 0.5f;
  m_radius   = std::max((hi - lo).length() * 0.5f, 1e-4f);
  m_bboxMin  = lo;
  m_bboxMax  = hi;
  m_hasModel = true;
  m_hasUV    = anyUV;
  m_uploaded = false;
  m_linesUploaded = false;
  m_animTimeSec = 0.0;
  m_lastMs      = 0;
  buildGrid();

  m_summary = QStringLiteral("%1 メッシュ · %2 マテリアル · %3 頂点 · %4 三角形")
                  .arg(m_submeshes.size())
                  .arg(m_materials.size())
                  .arg(m_vertices.size() / ModelView::kFloatsPerVertex)
                  .arg(triCount);
  if (m_hasAnim)
    m_summary += QStringLiteral(" · アニメ %1s / ボーン %2")
                     .arg(animationDuration(), 0, 'f', 1)
                     .arg(m_boneOffset.size());

  if (m_hasAnim && m_playing) m_animTimer->start();
  update();
  return true;
}

void ModelView::computeBoneMatrices(double timeTicks, bool bindPose) {
  const size_t n = m_nodes.size();
  std::vector<QMatrix4x4> global(n);
  for (size_t i = 0; i < n; ++i) {
    QMatrix4x4 local = m_nodes[i].bind;
    if (!bindPose && i < m_channels.size() && m_channels[i].used) {
      const QVector3D   bindT = m_nodes[i].bind.column(3).toVector3D();
      const QVector3D   p     = interpKeys(m_channels[i].pos, timeTicks, bindT);
      const QQuaternion q =
          m_channels[i].rot.empty()
              ? QQuaternion()
              : [&] {
                  const auto& keys = m_channels[i].rot;
                  if (keys.size() == 1 || timeTicks <= keys.front().t) return keys.front().q;
                  if (timeTicks >= keys.back().t) return keys.back().q;
                  for (size_t k = 0; k + 1 < keys.size(); ++k)
                    if (timeTicks < keys[k + 1].t) {
                      const float f =
                          float((timeTicks - keys[k].t) / (keys[k + 1].t - keys[k].t));
                      return QQuaternion::slerp(keys[k].q, keys[k + 1].q, f);
                    }
                  return keys.back().q;
                }();
      const QVector3D s = interpKeys(m_channels[i].scale, timeTicks, QVector3D(1, 1, 1));
      local.setToIdentity();
      local.translate(p);
      local.rotate(q);
      local.scale(s);
    }
    global[i] = (m_nodes[i].parent < 0) ? local : global[m_nodes[i].parent] * local;
  }
  m_boneMatrices.resize(m_boneOffset.size());
  for (size_t b = 0; b < m_boneOffset.size(); ++b) {
    const int        node = m_boneNode[b];
    const QMatrix4x4 g    = (node >= 0) ? global[node] : QMatrix4x4();
    m_boneMatrices[b]     = m_globalInverse * g * m_boneOffset[b];
  }
}

void ModelView::buildGrid() {
  m_lineVerts.clear();
  const float  y    = m_bboxMin.y();                    // 床の高さ
  const float  half = std::max(m_radius * 1.2f, 1e-3f); // グリッド半径
  const int    div  = 20;
  const float  step = (half * 2.0f) / div;
  const QVector3D c(m_center.x(), y, m_center.z());
  const QVector3D gcol(0.32f, 0.34f, 0.38f);
  const QVector3D gcol2(0.42f, 0.44f, 0.48f);  // 中央線
  auto line = [&](QVector3D a, QVector3D b, QVector3D col) {
    m_lineVerts.insert(m_lineVerts.end(),
                       {a.x(), a.y(), a.z(), col.x(), col.y(), col.z(), b.x(), b.y(), b.z(),
                        col.x(), col.y(), col.z()});
  };
  for (int i = -div / 2; i <= div / 2; ++i) {
    const float o   = i * step;
    const QVector3D col = (i == 0) ? gcol2 : gcol;
    line({c.x() - half, y, c.z() + o}, {c.x() + half, y, c.z() + o}, col);  // X 方向
    line({c.x() + o, y, c.z() - half}, {c.x() + o, y, c.z() + half}, col);  // Z 方向
  }
  // 座標軸 (X=赤 / Y=緑 / Z=青)
  const float al = m_radius * 0.6f;
  line({c.x(), y, c.z()}, {c.x() + al, y, c.z()}, {0.85f, 0.30f, 0.30f});
  line({c.x(), y, c.z()}, {c.x(), y + al, c.z()}, {0.35f, 0.80f, 0.40f});
  line({c.x(), y, c.z()}, {c.x(), y, c.z() + al}, {0.35f, 0.55f, 0.95f});
  m_lineCount = int(m_lineVerts.size() / 6);
}

void ModelView::setShowGrid(bool on) {
  m_showGrid = on;
  update();
}

void ModelView::setWireframe(bool on) {
  m_wireframe = on;
  update();
}

void ModelView::resetView() {
  m_yaw   = 0.7f;
  m_pitch = 0.35f;
  m_dist  = 2.6f;
  update();
}

void ModelView::keyPressEvent(QKeyEvent* e) {
  if (e->key() == Qt::Key_R) {
    resetView();
    return;
  }
  QOpenGLWidget::keyPressEvent(e);
}

void ModelView::setTextureEnabled(bool on) {
  m_texEnabled = on;
  update();
}

void ModelView::setAnimationPlaying(bool on) {
  m_playing = on;
  if (on) {
    m_lastMs = m_clock.isValid() ? m_clock.elapsed() : 0;
    m_animTimer->start();
  } else {
    m_animTimer->stop();
  }
  update();
}

void ModelView::setAnimationTime(double sec) {
  m_playing = false;
  m_animTimer->stop();
  m_animTimeSec = sec;
  update();
}

void ModelView::initializeGL() {
  initializeOpenGLFunctions();
  m_glReady = true;
  m_clock.start();
  m_lastMs = 0;
  glEnable(GL_DEPTH_TEST);
  if (!m_prog.addShaderFromSourceCode(QOpenGLShader::Vertex, kVertexShader) ||
      !m_prog.addShaderFromSourceCode(QOpenGLShader::Fragment, kFragmentShader) ||
      !m_prog.link()) {
    qWarning("ModelView: shader error: %s", qPrintable(m_prog.log()));
  }
  m_vao.create();

  if (!m_lineProg.addShaderFromSourceCode(QOpenGLShader::Vertex, kLineVertexShader) ||
      !m_lineProg.addShaderFromSourceCode(QOpenGLShader::Fragment, kLineFragmentShader) ||
      !m_lineProg.link()) {
    qWarning("ModelView: line shader error: %s", qPrintable(m_lineProg.log()));
  }
  m_lineVao.create();
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
  const int stride = ModelView::kFloatsPerVertex * sizeof(float);
  auto attr = [&](int loc, int size, int offsetFloats) {
    glEnableVertexAttribArray(loc);
    glVertexAttribPointer(loc, size, GL_FLOAT, GL_FALSE, stride,
                          reinterpret_cast<void*>(offsetFloats * sizeof(float)));
  };
  attr(0, 3, 0);
  attr(1, 3, 3);
  attr(2, 2, 6);
  attr(3, 4, 8);
  attr(4, 4, 12);
  m_vao.release();

  for (auto& mat : m_materials) {
    if (mat.resolved && !mat.image.isNull() && !mat.tex) {
      mat.tex = std::make_unique<QOpenGLTexture>(mat.image, QOpenGLTexture::GenerateMipMaps);
      mat.tex->setMinificationFilter(QOpenGLTexture::LinearMipMapLinear);
      mat.tex->setMagnificationFilter(QOpenGLTexture::Linear);
      mat.tex->setWrapMode(QOpenGLTexture::Repeat);
    }
  }

  // グリッド + 軸のライン
  if (!m_lineVerts.empty()) {
    m_lineVao.bind();
    if (!m_lineVbo.isCreated()) m_lineVbo.create();
    m_lineVbo.bind();
    m_lineVbo.allocate(m_lineVerts.data(), int(m_lineVerts.size() * sizeof(float)));
    const int lstride = 6 * sizeof(float);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, lstride, reinterpret_cast<void*>(0));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, lstride,
                          reinterpret_cast<void*>(3 * sizeof(float)));
    m_lineVao.release();
    m_linesUploaded = true;
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

  if (m_hasAnim) {
    if (m_playing) {
      const qint64 now = m_clock.elapsed();
      m_animTimeSec += double(now - m_lastMs) / 1000.0;
      m_lastMs = now;
    }
    double timeTicks = 0.0;
    if (m_animDurationTicks > 0.0)
      timeTicks = std::fmod(m_animTimeSec * m_ticksPerSec, m_animDurationTicks);
    computeBoneMatrices(timeTicks, /*bindPose=*/false);
  } else {
    m_lastMs = m_clock.elapsed();
  }

  const float     r    = m_radius;
  const float     dist = m_dist * r;
  const QVector3D dir(std::cos(m_pitch) * std::sin(m_yaw), std::sin(m_pitch),
                      std::cos(m_pitch) * std::cos(m_yaw));
  const QVector3D eye = m_center + dir * dist;

  QMatrix4x4 view;
  view.lookAt(eye, m_center, QVector3D(0, 1, 0));
  QMatrix4x4 proj;
  const float aspect = height() > 0 ? float(width()) / float(height()) : 1.0f;
  proj.perspective(45.0f, aspect, r * 0.01f, dist + r * 10.0f);

  const QMatrix4x4 mvp     = proj * view;
  const QMatrix3x3 normal  = view.normalMatrix();
  const bool       skinned = m_hasAnim && !m_boneMatrices.empty();

  m_prog.bind();
  m_prog.setUniformValue("uMVP", mvp);
  m_prog.setUniformValue("uNormalView", normal);
  m_prog.setUniformValue("uSkinned", skinned);
  if (skinned)
    m_prog.setUniformValueArray("uBones", m_boneMatrices.data(),
                                std::min<int>(int(m_boneMatrices.size()), 128));
  m_prog.setUniformValue("uTex", 0);

  if (m_wireframe) glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
  m_vao.bind();
  for (const SubMesh& sm : m_submeshes) {
    const Material& mat    = m_materials[std::clamp(sm.material, 0, int(m_materials.size()) - 1)];
    const bool      useTex = m_texEnabled && mat.tex && !m_wireframe;
    m_prog.setUniformValue("uUseTexture", useTex);
    m_prog.setUniformValue("uBaseColor", mat.baseColor);
    if (useTex) mat.tex->bind(0);
    glDrawElements(GL_TRIANGLES, sm.indexCount, GL_UNSIGNED_INT,
                   reinterpret_cast<void*>(intptr_t(sm.indexOffset) * sizeof(unsigned int)));
    if (useTex) mat.tex->release(0);
  }
  m_vao.release();
  m_prog.release();
  if (m_wireframe) glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);

  // グリッド + 座標軸
  if (m_showGrid && m_linesUploaded && m_lineCount > 0) {
    m_lineProg.bind();
    m_lineProg.setUniformValue("uMVP", mvp);
    m_lineVao.bind();
    glDrawArrays(GL_LINES, 0, m_lineCount);
    m_lineVao.release();
    m_lineProg.release();
  }
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
