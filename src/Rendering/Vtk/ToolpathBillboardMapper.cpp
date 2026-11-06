#include "ToolpathBillboardMapper.hpp"

#include "Foundation/Log.h"

#include "vtk_glad.h"

#include <vtkObjectFactory.h>
#include <vtkActor.h>
#include <vtkAssemblyPath.h>
#include <vtkAssemblyNode.h>
#include <vtkOpenGLCamera.h>
#include <vtkOpenGLRenderWindow.h>
#include <vtkOpenGLShaderCache.h>
#include <vtkMatrix4x4.h>
#include <vtkMatrix3x3.h>
#include <vtkNew.h>
#include <vtkRenderer.h>
#include <vtkShaderProgram.h>
#include <vtkViewport.h>

#include <array>
#include <iterator>
#include <limits>

namespace {

// The Orca/libvgcode template expands these 8 logical vertices into a 24-entry draw-arrays stream.
// Keep the 8 unique vertices indexed here so the post-transform cache runs the expensive vertex
// shader once per logical vertex instead of once per triangle corner.
constexpr std::array<std::uint8_t, 8> kTemplateVertexIds = {
    0, 1, 2, 3, 4, 5, 6, 7
};
constexpr std::array<std::uint8_t, 24> kTemplateTriangleIndices = {
    0, 1, 2, // front spike
    0, 2, 3, // front spike
    0, 3, 4, // right/bottom body
    0, 4, 5, // right/bottom body
    0, 5, 6, // left/top body
    0, 6, 1, // left/top body
    5, 4, 7, // back spike
    5, 7, 6, // back spike
};

// Ported from libvgcode Segments_Vertex_Shader (desktop GL 1.50).
const char* kVertexShader = R"GLSL(#version 150
#define POINTY_CAPS
#define FIX_TWISTING
const vec3  light_top_dir = vec3(-0.4574957, 0.4574957, 0.7624929);
const float light_top_diffuse = 0.6 * 0.8;
const float light_top_specular = 0.6 * 0.125;
const float light_top_shininess = 20.0;
const vec3  light_front_dir = vec3(0.6985074, 0.1397015, 0.6985074);
const float light_front_diffuse = 0.6 * 0.2;
const float ambient = 0.2;
const float emission = 0.15;
const vec3 UP = vec3(0, 0, 1);
uniform mat4 view_matrix;
uniform mat4 projection_matrix;
uniform vec3 camera_position;
uniform samplerBuffer position_tex;
uniform samplerBuffer height_width_angle_tex;
uniform samplerBuffer color_tex;
uniform usamplerBuffer segment_index_tex;
uniform int segment_index_offset;
uniform bool annotation_pass;
uniform vec2 viewport_size;
in int vertex_id;
out vec3 color;
noperspective out float stroke_distance;
vec3 decode_color(float c_in) {
  int c = int(round(c_in));
  int r = (c >> 16) & 0xFF;
  int g = (c >> 8) & 0xFF;
  int b = (c >> 0) & 0xFF;
  float f = 1.0 / 255.0;
  return f * vec3(r, g, b);
}
float lighting(vec3 eye_position, vec3 eye_normal) {
  float top_diffuse = light_top_diffuse * max(dot(eye_normal, light_top_dir), 0.0);
  float front_diffuse = light_front_diffuse * max(dot(eye_normal, light_front_dir), 0.0);
  float top_specular = light_top_specular * pow(max(dot(-normalize(eye_position), reflect(-light_top_dir, eye_normal)), 0.0), light_top_shininess);
  return ambient + top_diffuse + front_diffuse + top_specular + emission;
}
void main() {
  int id_a = int(texelFetch(segment_index_tex, segment_index_offset + gl_InstanceID).r);
  int id_b = id_a + 1;
  vec3 pos_a = texelFetch(position_tex, id_a).xyz;
  vec3 pos_b = texelFetch(position_tex, id_b).xyz;
  stroke_distance = 0.0;
  if (annotation_pass) {
    // Stored bead centers are lowered by half their display height. Restore the actual
    // nozzle coordinates for the annotation; do not move the data or the pick geometry.
    pos_a.z += 0.5 * texelFetch(height_width_angle_tex, id_a).x;
    pos_b.z += 0.5 * texelFetch(height_width_angle_tex, id_b).x;
    vec4 a = projection_matrix * view_matrix * vec4(pos_a, 1.0);
    vec4 b = projection_matrix * view_matrix * vec4(pos_b, 1.0);
    float da = a.z + a.w, db = b.z + b.w;
    if (da < 0.0 && db < 0.0) {
      gl_Position = vec4(2.0, 2.0, 2.0, 1.0);
      color = vec3(0.0);
      return;
    }
    // Clip to the near plane before perspective division.
    if (da < 0.0) a = mix(a, b, da / (da - db));
    if (db < 0.0) b = mix(b, a, db / (db - max(da, 0.0)));
    vec2 pa = a.xy / a.w, pb = b.xy / b.w;
    vec2 delta = (pb - pa) * 0.5 * viewport_size;
    float pixels = length(delta);
    vec2 normal = pixels > 0.001 ? vec2(-delta.y, delta.x) / pixels : vec2(0.0, 1.0);
    bool end = vertex_id == 2 || vertex_id == 3;
    float side = vertex_id == 0 || vertex_id == 3 ? -1.0 : 1.0;
    vec4 clip = end ? b : a;
    // 2 physical framebuffer pixels, independent of projection and model scale.
    clip.xy += normal * side * (2.0 / viewport_size) * clip.w;
    gl_Position = clip;
    stroke_distance = end ? pixels : 0.0;
    color = decode_color(texelFetch(color_tex, id_a).r);
    return;
  }
  vec3 line = pos_b - pos_a;
  float line_len = length(line);
  vec3 line_dir = (line_len < 1e-4) ? vec3(1.0, 0.0, 0.0) : line / line_len;
  vec3 line_right_dir;
  if (abs(dot(line_dir, UP)) > 0.9)
    line_right_dir = normalize(cross(vec3(1, 0, 0), line_dir));
  else
    line_right_dir = normalize(cross(line_dir, UP));
  vec3 line_up_dir = normalize(cross(line_right_dir, line_dir));
  const vec2 signs_array[16] = vec2[](
    vec2(1.0, 0.0), vec2(0.0, 1.0), vec2(0.0, 0.0), vec2(0.0, -1.0),
    vec2(0.0, -1.0), vec2(1.0, 0.0), vec2(0.0, 1.0), vec2(0.0, 0.0),
    vec2(0.0, 1.0), vec2(-1.0, 0.0), vec2(0.0, 0.0), vec2(1.0, 0.0),
    vec2(1.0, 0.0), vec2(0.0, 1.0), vec2(-1.0, 0.0), vec2(0.0, 0.0));
  int id = vertex_id < 4 ? id_a : id_b;
  vec3 endpoint_pos = vertex_id < 4 ? pos_a : pos_b;
  vec4 hwa = texelFetch(height_width_angle_tex, id);
  vec3 height_width_angle = hwa.xyz;
  float bias = hwa.w;
  int closer_id = (dot(camera_position - pos_a, camera_position - pos_a) < dot(camera_position - pos_b, camera_position - pos_b)) ? id_a : id_b;
  vec3 closer_pos = (closer_id == id_a) ? pos_a : pos_b;
  vec3 camera_view_dir = normalize(closer_pos - camera_position);
  vec3 closer_hwa = texelFetch(height_width_angle_tex, closer_id).xyz;
  vec3 diagonal_dir_border = normalize(closer_hwa.x * line_up_dir + closer_hwa.y * line_right_dir);
  bool is_vertical_view = abs(dot(camera_view_dir, line_up_dir)) / abs(dot(diagonal_dir_border, line_up_dir)) >
    abs(dot(camera_view_dir, line_right_dir)) / abs(dot(diagonal_dir_border, line_right_dir));
  vec2 signs = signs_array[vertex_id + 8 * int(is_vertical_view)];
  float view_right_sign = sign(dot(-camera_view_dir, line_right_dir));
  float view_top_sign = sign(dot(-camera_view_dir, line_up_dir));
  float half_height = 0.5 * height_width_angle.x;
  float half_width = 0.5 * height_width_angle.y;
  vec3 horizontal_dir = half_width * line_right_dir;
  vec3 vertical_dir = half_height * line_up_dir;
  vec3 pos = endpoint_pos + (signs.x * view_right_sign) * horizontal_dir + (signs.y * view_top_sign) * vertical_dir;
  if (vertex_id == 2 || vertex_id == 7) {
    float line_dir_sign = (vertex_id == 2) ? -1.0 : 1.0;
    if (height_width_angle.z == 0.0) {
      pos += line_dir_sign * line_dir * half_width;
    } else {
      pos += line_dir_sign * line_dir * half_width * sin(abs(height_width_angle.z) * 0.5);
      pos += sign(height_width_angle.z) * horizontal_dir * cos(abs(height_width_angle.z) * 0.5);
    }
  }
  vec3 eye_position = (view_matrix * vec4(pos, 1.0)).xyz;
  eye_position.z += bias;
  vec3 eye_normal = (view_matrix * vec4(normalize(pos - endpoint_pos), 0.0)).xyz;
  vec3 color_base = decode_color(texelFetch(color_tex, id).r);
  color = color_base * lighting(eye_position, eye_normal);
  gl_Position = projection_matrix * vec4(eye_position, 1.0);
}
)GLSL";

const char* kFragmentShader = R"GLSL(#version 150
in vec3 color;
noperspective in float stroke_distance;
uniform bool annotation_pass;
out vec4 fragment_color;
void main() {
  // Very short curve subdivisions remain visible; long strokes expose travel as dashes.
  if (annotation_pass && mod(stroke_distance, 10.0) > 6.0) discard;
  fragment_color = vec4(color, 1.0);
}
)GLSL";

// Create/refresh a texture-buffer object: (re)upload `bytes` into `vbo`, and bind it to `tex` as a
// buffer texture with the given internal format.
void uploadTextureBuffer(unsigned int vbo, unsigned int tex, GLenum internalFormat,
                         const void* data, std::size_t bytes) {
    glBindBuffer(GL_TEXTURE_BUFFER, vbo);
    glBufferData(GL_TEXTURE_BUFFER, static_cast<GLsizeiptr>(bytes), data, GL_DYNAMIC_DRAW);
    glBindTexture(GL_TEXTURE_BUFFER, tex);
    glTexBuffer(GL_TEXTURE_BUFFER, internalFormat, vbo);
    glBindBuffer(GL_TEXTURE_BUFFER, 0);
    glBindTexture(GL_TEXTURE_BUFFER, 0);
}

} // namespace

vtkStandardNewMacro(ToolpathBillboardMapper);
vtkStandardNewMacro(ToolpathPreviewAssembly);

int ToolpathPreviewAssembly::RenderOverlay(vtkViewport* viewport) {
    auto* renderer = vtkRenderer::SafeDownCast(viewport);
    if (!GetVisibility() || !renderer) return 0;
    // vtkAssembly traverses opaque/translucent geometry, but does not forward RenderOverlay.
    // Use its existing paths so annotations receive exactly the same accumulated transforms.
    int rendered = 0;
    InitPathTraversal();
    while (auto* path = GetNextPath()) {
        auto* actor = vtkActor::SafeDownCast(path->GetLastNode()->GetViewProp());
        auto* mapper = actor ? ToolpathBillboardMapper::SafeDownCast(actor->GetMapper()) : nullptr;
        if (!mapper || !actor->GetVisibility()) continue;
        actor->PokeMatrix(path->GetLastNode()->GetMatrix());
        mapper->renderFinishOverlay(renderer, actor);
        actor->PokeMatrix(nullptr);
        rendered = 1;
    }
    return rendered;
}

ToolpathBillboardMapper::ToolpathBillboardMapper() {
    for (int i = 0; i < 6; ++i) {
        m_bounds6[i] = 0.0;
    }
}

ToolpathBillboardMapper::~ToolpathBillboardMapper() = default;

void ToolpathBillboardMapper::setGeometry(std::vector<float> positions, std::vector<float> heightWidthAngle) {
    m_positions = std::move(positions);
    m_heightWidthAngle = std::move(heightWidthAngle);
    m_vertexCount = m_positions.size() / 4;
    m_geometryDirty = true;

    // Bounds from the path vertices (xyzw stride 4).
    double lo = std::numeric_limits<double>::max();
    double hi = std::numeric_limits<double>::lowest();
    m_bounds6[0] = m_bounds6[2] = m_bounds6[4] = lo;
    m_bounds6[1] = m_bounds6[3] = m_bounds6[5] = hi;
    for (std::size_t v = 0; v < m_vertexCount; ++v) {
        const float x = m_positions[v * 4 + 0];
        const float y = m_positions[v * 4 + 1];
        const float z = m_positions[v * 4 + 2];
        m_bounds6[0] = std::min(m_bounds6[0], static_cast<double>(x));
        m_bounds6[1] = std::max(m_bounds6[1], static_cast<double>(x));
        m_bounds6[2] = std::min(m_bounds6[2], static_cast<double>(y));
        m_bounds6[3] = std::max(m_bounds6[3], static_cast<double>(y));
        m_bounds6[4] = std::min(m_bounds6[4], static_cast<double>(z));
        m_bounds6[5] = std::max(m_bounds6[5], static_cast<double>(z));
    }
    if (m_vertexCount == 0) {
        for (int i = 0; i < 6; ++i) {
            m_bounds6[i] = 0.0;
        }
    }
    Modified();
}

void ToolpathBillboardMapper::setColors(std::vector<float> packedColors) {
    m_colors = std::move(packedColors);
    m_colorsDirty = true;
    Modified();
}

void ToolpathBillboardMapper::setVisibleSegments(
    std::vector<std::uint32_t> segmentStartIds,
    std::vector<std::uint32_t> reverseLayerSegmentStartIds,
    std::vector<std::uint32_t> finishSegmentStartIds) {
    m_visibleCount = segmentStartIds.size();
    if (reverseLayerSegmentStartIds.size() != m_visibleCount) {
        LOG_WARN("ToolpathBillboardMapper: reverse layer order size mismatch forward={} reverse={}",
                 m_visibleCount, reverseLayerSegmentStartIds.size());
        reverseLayerSegmentStartIds = segmentStartIds;
    }
    m_segmentStart = std::move(segmentStartIds);
    m_segmentStart.insert(
        m_segmentStart.end(),
        std::make_move_iterator(reverseLayerSegmentStartIds.begin()),
        std::make_move_iterator(reverseLayerSegmentStartIds.end()));
    m_finishCount = finishSegmentStartIds.size();
    m_segmentStart.insert(m_segmentStart.end(), finishSegmentStartIds.begin(), finishSegmentStartIds.end());
    m_segmentsDirty = true;
    Modified();
}

double* ToolpathBillboardMapper::GetBounds() {
    return m_bounds6;
}

bool ToolpathBillboardMapper::ensureGL(vtkOpenGLRenderWindow* renderWindow) {
    if (m_glReady) {
        return m_program != nullptr;
    }
    m_glReady = true;

    m_program = renderWindow->GetShaderCache()->ReadyShaderProgram(kVertexShader, kFragmentShader, nullptr);
    if (!m_program || !m_program->GetCompiled()) {
        LOG_ERROR("ToolpathBillboardMapper: failed to compile segment shader");
        m_program = nullptr;
        return false;
    }

    glGenVertexArrays(1, &m_vaoId);
    glBindVertexArray(m_vaoId);
    glGenBuffers(1, &m_templateVboId);
    glBindBuffer(GL_ARRAY_BUFFER, m_templateVboId);
    glBufferData(GL_ARRAY_BUFFER, kTemplateVertexIds.size() * sizeof(std::uint8_t),
                 kTemplateVertexIds.data(), GL_STATIC_DRAW);
    const int loc = m_program->GetHandle() != 0
        ? glGetAttribLocation(m_program->GetHandle(), "vertex_id")
        : -1;
    if (loc >= 0) {
        glEnableVertexAttribArray(static_cast<GLuint>(loc));
        glVertexAttribIPointer(static_cast<GLuint>(loc), 1, GL_UNSIGNED_BYTE, 0, nullptr);
    } else {
        LOG_ERROR("ToolpathBillboardMapper: vertex_id attribute not found in shader");
    }
    glGenBuffers(1, &m_templateEboId);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_templateEboId);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                 kTemplateTriangleIndices.size() * sizeof(std::uint8_t),
                 kTemplateTriangleIndices.data(), GL_STATIC_DRAW);
    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    glGenBuffers(1, &m_positionVbo);
    glGenTextures(1, &m_positionTex);
    glGenBuffers(1, &m_hwaVbo);
    glGenTextures(1, &m_hwaTex);
    glGenBuffers(1, &m_colorVbo);
    glGenTextures(1, &m_colorTex);
    glGenBuffers(1, &m_segmentVbo);
    glGenTextures(1, &m_segmentTex);
    return true;
}

void ToolpathBillboardMapper::syncBuffers() {
    if (m_geometryDirty) {
        uploadTextureBuffer(m_positionVbo, m_positionTex, GL_RGBA32F,
                            m_positions.data(), m_positions.size() * sizeof(float));
        uploadTextureBuffer(m_hwaVbo, m_hwaTex, GL_RGBA32F,
                            m_heightWidthAngle.data(), m_heightWidthAngle.size() * sizeof(float));
        m_geometryDirty = false;
    }
    if (m_colorsDirty) {
        uploadTextureBuffer(m_colorVbo, m_colorTex, GL_R32F,
                            m_colors.data(), m_colors.size() * sizeof(float));
        m_colorsDirty = false;
    }
    if (m_segmentsDirty) {
        uploadTextureBuffer(m_segmentVbo, m_segmentTex, GL_R32UI,
                            m_segmentStart.data(), m_segmentStart.size() * sizeof(std::uint32_t));
        m_segmentsDirty = false;
    }
}

void ToolpathBillboardMapper::Render(vtkRenderer* renderer, vtkActor* actor) {
    renderSegments(renderer, actor, false);
}

void ToolpathBillboardMapper::renderFinishOverlay(vtkRenderer* renderer, vtkActor* actor) {
    renderSegments(renderer, actor, true);
}

void ToolpathBillboardMapper::renderSegments(vtkRenderer* renderer, vtkActor* actor, bool annotation) {
    if ((annotation ? m_finishCount : m_visibleCount) == 0 || m_vertexCount == 0 || m_colors.empty()) {
        return;
    }
    auto* renderWindow = vtkOpenGLRenderWindow::SafeDownCast(renderer->GetRenderWindow());
    auto* camera = vtkOpenGLCamera::SafeDownCast(renderer->GetActiveCamera());
    if (!renderWindow || !camera) {
        return;
    }
    if (!ensureGL(renderWindow)) {
        return;
    }
    syncBuffers();

    // Save the GL state we touch so VTK's subsequent rendering is undisturbed.
    GLint prevProgram = 0, prevVao = 0, prevActiveTex = 0;
    glGetIntegerv(GL_CURRENT_PROGRAM, &prevProgram);
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &prevVao);
    glGetIntegerv(GL_ACTIVE_TEXTURE, &prevActiveTex);
    const GLboolean depthTest = glIsEnabled(GL_DEPTH_TEST);
    const GLboolean cullFace = glIsEnabled(GL_CULL_FACE);
    GLboolean depthWrite = GL_TRUE;
    glGetBooleanv(GL_DEPTH_WRITEMASK, &depthWrite);
    if (annotation) {
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_CULL_FACE);
        glDepthMask(GL_FALSE);
    }

    renderWindow->GetShaderCache()->ReadyShaderProgram(m_program);

    vtkMatrix4x4* wcvc = nullptr;  // world -> view
    vtkMatrix3x3* normMat = nullptr;
    vtkMatrix4x4* vcdc = nullptr;  // view -> clip
    vtkMatrix4x4* wcdc = nullptr;  // world -> clip
    camera->GetKeyMatrices(renderer, wcvc, normMat, vcdc, wcdc);
    vtkNew<vtkMatrix4x4> modelView;
    // GetKeyMatrices already transposes the camera matrices for OpenGL.
    // Upload (view * model)^T, as vtkOpenGLPolyDataMapper does.
    vtkMatrix4x4::Transpose(actor->GetMatrix(), modelView);
    vtkMatrix4x4::Multiply4x4(modelView, wcvc, modelView);
    m_program->SetUniformMatrix("view_matrix", modelView);
    m_program->SetUniformMatrix("projection_matrix", vcdc);
    m_program->SetUniformi("annotation_pass", annotation ? 1 : 0);
    GLint viewport[4];
    glGetIntegerv(GL_VIEWPORT, viewport);
    const float viewportSize[2] = {float(std::max(1, viewport[2])), float(std::max(1, viewport[3]))};
    m_program->SetUniform2f("viewport_size", viewportSize);
    const double* camPos = camera->GetPosition();
    vtkNew<vtkMatrix4x4> inverseModel;
    vtkMatrix4x4::Invert(actor->GetMatrix(), inverseModel);
    const double worldCamera[4] = {camPos[0], camPos[1], camPos[2], 1.0};
    double localCamera[4] = {};
    inverseModel->MultiplyPoint(worldCamera, localCamera);
    const float camPosF[3] = {
        static_cast<float>(localCamera[0]),
        static_cast<float>(localCamera[1]),
        static_cast<float>(localCamera[2])
    };
    m_program->SetUniform3f("camera_position", camPosF);
    const double* worldDirection = camera->GetDirectionOfProjection();
    const double worldDirection4[4] = {
        worldDirection[0], worldDirection[1], worldDirection[2], 0.0
    };
    double localDirection4[4] = {};
    inverseModel->MultiplyPoint(worldDirection4, localDirection4);
    const bool drawHighLayersFirst = localDirection4[2] < -0.05;
    m_program->SetUniformi(
        "segment_index_offset",
        annotation ? static_cast<int>(2 * m_visibleCount) :
        drawHighLayersFirst ? static_cast<int>(m_visibleCount) : 0);

    // Bind the four texture buffers to units 0..3 and point the samplers at them.
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_BUFFER, m_positionTex);
    m_program->SetUniformi("position_tex", 0);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_BUFFER, m_hwaTex);
    m_program->SetUniformi("height_width_angle_tex", 1);
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_BUFFER, m_colorTex);
    m_program->SetUniformi("color_tex", 2);
    glActiveTexture(GL_TEXTURE3);
    glBindTexture(GL_TEXTURE_BUFFER, m_segmentTex);
    m_program->SetUniformi("segment_index_tex", 3);

    glBindVertexArray(m_vaoId);
    glDrawElementsInstanced(
        GL_TRIANGLES,
        annotation ? 6 : static_cast<GLsizei>(kTemplateTriangleIndices.size()),
        GL_UNSIGNED_BYTE,
        nullptr,
        static_cast<GLsizei>(annotation ? m_finishCount : m_visibleCount));
    glBindVertexArray(static_cast<GLuint>(prevVao));

    if (annotation) {
        if (depthTest) glEnable(GL_DEPTH_TEST);
        if (cullFace) glEnable(GL_CULL_FACE);
        glDepthMask(depthWrite);
    }
    // Unbind our textures and restore state.
    glActiveTexture(GL_TEXTURE3);
    glBindTexture(GL_TEXTURE_BUFFER, 0);
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_BUFFER, 0);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_BUFFER, 0);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_BUFFER, 0);
    glActiveTexture(static_cast<GLenum>(prevActiveTex));
    // Keep VTK's shader-cache bookkeeping consistent with the raw OpenGL
    // program restoration below. Otherwise the cache may believe our program
    // is still bound and skip a required bind for the next prop.
    renderWindow->GetShaderCache()->ReleaseCurrentShader();
    glUseProgram(static_cast<GLuint>(prevProgram));
}

void ToolpathBillboardMapper::ReleaseGraphicsResources(vtkWindow* window) {
    if (m_vaoId) { glDeleteVertexArrays(1, &m_vaoId); m_vaoId = 0; }
    const auto delBuf = [](unsigned int& b) { if (b) { glDeleteBuffers(1, &b); b = 0; } };
    const auto delTex = [](unsigned int& t) { if (t) { glDeleteTextures(1, &t); t = 0; } };
    delBuf(m_templateVboId);
    delBuf(m_templateEboId);
    delBuf(m_positionVbo); delTex(m_positionTex);
    delBuf(m_hwaVbo); delTex(m_hwaTex);
    delBuf(m_colorVbo); delTex(m_colorTex);
    delBuf(m_segmentVbo); delTex(m_segmentTex);
    m_program = nullptr;
    m_glReady = false;
    m_geometryDirty = m_colorsDirty = m_segmentsDirty = true;
    Superclass::ReleaseGraphicsResources(window);
}
