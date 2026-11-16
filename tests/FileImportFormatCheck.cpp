#include <ImportFileFormat.hpp>
#include <QCoreApplication>
#include <iostream>

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    struct Case { const char* path; ImportFileFormat expected; };
    const Case cases[] = {
        {"model.stl", ImportFileFormat::Stl},
        {"model.STL", ImportFileFormat::Stl},
        {"model.StL", ImportFileFormat::Stl},
        {"model.gcode", ImportFileFormat::GCode},
        {"model.GCODE", ImportFileFormat::GCode},
        {"model.GcOdE", ImportFileFormat::GCode},
        {"models.obj/part.stl", ImportFileFormat::Stl},
        {"job.3mf.gcode", ImportFileFormat::GCode},
        {"part.obj", ImportFileFormat::Unsupported},
        {"part.OBJ", ImportFileFormat::Unsupported},
        {"project.3mf", ImportFileFormat::ThreeMf},
        {"part.glb", ImportFileFormat::Unsupported},
        {"part.gltf", ImportFileFormat::Unsupported},
        {"part.ply", ImportFileFormat::Unsupported},
        {"part.3ds", ImportFileFormat::Unsupported},
        {"part.dae", ImportFileFormat::Unsupported},
        {"part.fbx", ImportFileFormat::Unsupported},
        {"part.amf", ImportFileFormat::Unsupported},
        {"part.off", ImportFileFormat::Unsupported},
        {"part.step", ImportFileFormat::Unsupported},
        {"part.stp", ImportFileFormat::Unsupported},
        {"job.gco", ImportFileFormat::Unsupported},
        {"job.GCO", ImportFileFormat::Unsupported},
        {"job.g", ImportFileFormat::Unsupported},
        {"job.gcode.3mf", ImportFileFormat::ThreeMf},
        {"part.stl.zip", ImportFileFormat::Unsupported},
        {"notes.txt", ImportFileFormat::Unsupported},
        {"", ImportFileFormat::Unsupported},
        {"stl", ImportFileFormat::Unsupported},
        {"gcode", ImportFileFormat::Unsupported},
        {"folder.with.stl/model", ImportFileFormat::Unsupported},
        {"part.stl ", ImportFileFormat::Unsupported},
    };
    int checked = 0;
    for (const auto& item : cases) {
        if (classifyImportFile(QString::fromUtf8(item.path)) != item.expected) {
            std::cerr << "Wrong import format for: " << item.path << '\n';
            return 1;
        }
        ++checked;
    }
    std::cout << "PASS " << checked << " import format cases\n";
    return 0;
}
