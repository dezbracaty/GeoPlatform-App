#pragma once

#include <vtkMapper.h>
#include <vtkAssembly.h>
#include <vtkSmartPointer.h>

#include <cstdint>
#include <vector>

class vtkShaderProgram;
class vtkOpenGLRenderWindow;
class vtkActor;
class vtkRenderer;

// Instanced billboard renderer for the toolpath preview, ported from OrcaSlicer / libvgcode.
//
// Each extrusion segment is drawn as an 8-vertex "diamond" template (24 indices) instanced once per
// visible segment. The vertex shader computes each vertex's world position from per-segment data
// fetched out of texture buffers (endpoints, height/width/angle, colour), producing a flat
// width x height cross-section that billboards to face the camera — the look real slicers give.
//
// Updates are cheap by design:
//   - colour mode change  -> refill the colour buffer (O(vertices))
//   - layer / visibility  -> refill the visible-segment index buffer (O(visible)); geometry untouched.
class ToolpathBillboardMapper : public vtkMapper {
public:
    static ToolpathBillboardMapper* New();
    vtkTypeMacro(ToolpathBillboardMapper, vtkMapper);

    // Static geometry. positions: 4 floats per path vertex (x,y,z,pad). hwa: 4 floats per path vertex
    // (height, width, angle, bias). Each visible index names a pair [id, id+1].
    void setGeometry(std::vector<float> positions, std::vector<float> heightWidthAngle);
    // Per-path-vertex packed colour (1 float per vertex; RGB packed as (r<<16)|(g<<8)|b as a float).
    void setColors(std::vector<float> packedColors);
    // Visible segments in normal layer order and reversed layer order. Both lists preserve the
    // original order inside a layer so changing camera direction cannot change same-layer joins.
    void setVisibleSegments(std::vector<std::uint32_t> segmentStartIds,
                            std::vector<std::uint32_t> reverseLayerSegmentStartIds,
                            std::vector<std::uint32_t> finishSegmentStartIds);

    // Screen-space annotations reuse the geometry/color buffers, after opaque scene rendering.
    void renderFinishOverlay(vtkRenderer* renderer, vtkActor* actor);

    void Render(vtkRenderer* renderer, vtkActor* actor) override;
    double* GetBounds() override;
    void ReleaseGraphicsResources(vtkWindow* window) override;

protected:
    ToolpathBillboardMapper();
    ~ToolpathBillboardMapper() override;

private:
    bool ensureGL(vtkOpenGLRenderWindow* renderWindow);
    void syncBuffers();
    void renderSegments(vtkRenderer* renderer, vtkActor* actor, bool annotation);

    // CPU-side data + dirty flags (written from the render thread's dispatched commands).
    std::vector<float> m_positions;             // 4 per vertex
    std::vector<float> m_heightWidthAngle;      // 4 per vertex
    std::vector<float> m_colors;                // 1 per vertex
    // Forward/reverse body indices, then finish indices, share one texture buffer.
    std::vector<std::uint32_t> m_segmentStart;
    bool m_geometryDirty = false;
    bool m_colorsDirty = false;
    bool m_segmentsDirty = false;
    std::size_t m_vertexCount = 0;
    std::size_t m_visibleCount = 0;
    std::size_t m_finishCount = 0;
    double m_bounds6[6] = {0, 0, 0, 0, 0, 0};

    // GL resources (created lazily inside the render, released on ReleaseGraphicsResources). Raw GL
    // ids: the shader is the only thing driven through VTK (vtkShaderProgram + the window shader
    // cache); everything else (VAO, template VBO, texture-buffer objects) is plain GL to keep the
    // integer attribute / instancing / TBO paths unambiguous.
    bool m_glReady = false;
    vtkShaderProgram* m_program = nullptr;  // owned by the render window's shader cache
    unsigned int m_vaoId = 0;
    unsigned int m_templateVboId = 0;
    unsigned int m_templateEboId = 0;
    unsigned int m_positionVbo = 0, m_positionTex = 0;
    unsigned int m_hwaVbo = 0, m_hwaTex = 0;
    unsigned int m_colorVbo = 0, m_colorTex = 0;
    unsigned int m_segmentVbo = 0, m_segmentTex = 0;

    ToolpathBillboardMapper(const ToolpathBillboardMapper&) = delete;
    void operator=(const ToolpathBillboardMapper&) = delete;
};

// One prop owns both passes, so visibility, transforms and disposal cannot diverge.
class ToolpathPreviewAssembly : public vtkAssembly {
public:
    static ToolpathPreviewAssembly* New();
    vtkTypeMacro(ToolpathPreviewAssembly, vtkAssembly);
    int RenderOverlay(vtkViewport* viewport) override;
};
