#include "GlbDBImporter.hpp"

#include "Foundation/Log.h"

#include <QCryptographicHash>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QString>
#include <QtEndian>

#include <vtkCell.h>
#include <vtkCompositeDataIterator.h>
#include <vtkGLTFReader.h>
#include <vtkMemoryResourceStream.h>
#include <vtkMultiBlockDataSet.h>
#include <vtkNew.h>
#include <vtkPointData.h>
#include <vtkPolyData.h>

#include <algorithm>
#include <cfloat>
#include <cstring>
#include <limits>
#include <memory>

namespace {

constexpr quint32 kGlbMagic = 0x46546C67;
constexpr quint32 kGlbJsonChunk = 0x4E4F534A;
constexpr auto kIridescenceExtension = "KHR_materials_iridescence";

quint32 readUint32(const QByteArray& bytes, qsizetype offset) {
    quint32 value = 0;
    std::memcpy(&value, bytes.constData() + offset, sizeof(value));
    return qFromLittleEndian(value);
}

void appendUint32(QByteArray& bytes, quint32 value) {
    const quint32 littleEndian = qToLittleEndian(value);
    bytes.append(
        reinterpret_cast<const char*>(&littleEndian), sizeof(littleEndian));
}

bool containsString(const QJsonArray& values, const QString& target) {
    return std::any_of(
        values.begin(), values.end(), [&target](const QJsonValue& value) {
            return value.isString() && value.toString() == target;
        });
}

QJsonValue removeExtension(const QJsonValue& value, const QString& name) {
    if (value.isArray()) {
        QJsonArray result;
        for (const auto& entry : value.toArray()) {
            if (entry.isString() && entry.toString() == name) continue;
            result.append(removeExtension(entry, name));
        }
        return result;
    }
    if (value.isObject()) {
        QJsonObject result;
        const auto source = value.toObject();
        for (auto it = source.begin(); it != source.end(); ++it) {
            if (it.key() == name) continue;
            result.insert(it.key(), removeExtension(it.value(), name));
        }
        return result;
    }
    return value;
}

bool inspectAndPrepareBytes(
    const QByteArray& source,
    QByteArray& readerBytes,
    GlbAssetMetadata& metadata,
    QString& error) {
    if (source.size() < 20 || readUint32(source, 0) != kGlbMagic) {
        error = QStringLiteral("Invalid GLB header");
        return false;
    }
    if (readUint32(source, 4) != 2) {
        error = QStringLiteral("Only GLB version 2 is supported");
        return false;
    }
    const quint32 declaredLength = readUint32(source, 8);
    if (declaredLength < 20 ||
        declaredLength > static_cast<quint32>(source.size())) {
        error = QStringLiteral("Invalid GLB byte length");
        return false;
    }

    readerBytes.clear();
    appendUint32(readerBytes, kGlbMagic);
    appendUint32(readerBytes, 2);
    appendUint32(readerBytes, 0);

    bool rebuilt = false;
    bool foundJson = false;
    qsizetype offset = 12;
    while (offset + 8 <= declaredLength) {
        const quint32 chunkLength = readUint32(source, offset);
        const quint32 chunkType = readUint32(source, offset + 4);
        const qsizetype dataOffset = offset + 8;
        const qsizetype chunkEnd = dataOffset + chunkLength;
        if (chunkEnd > declaredLength) {
            error = QStringLiteral("Invalid GLB chunk length");
            return false;
        }

        QByteArray chunk = source.mid(dataOffset, chunkLength);
        if (chunkType == kGlbJsonChunk) {
            foundJson = true;
            QJsonParseError parseError;
            const auto document = QJsonDocument::fromJson(chunk, &parseError);
            if (parseError.error != QJsonParseError::NoError ||
                !document.isObject()) {
                error = QStringLiteral("Invalid GLB JSON: %1")
                            .arg(parseError.errorString());
                return false;
            }

            const auto root = document.object();
            const auto asset = root.value(QStringLiteral("asset")).toObject();
            metadata.gltfVersion =
                asset.value(QStringLiteral("version")).toString().toStdString();
            metadata.generator =
                asset.value(QStringLiteral("generator")).toString().toStdString();
            metadata.nodeCount = static_cast<std::size_t>(
                root.value(QStringLiteral("nodes")).toArray().size());
            const auto meshes = root.value(QStringLiteral("meshes")).toArray();
            metadata.meshCount = static_cast<std::size_t>(meshes.size());
            for (const auto& mesh : meshes) {
                metadata.primitiveCount += static_cast<std::size_t>(
                    mesh.toObject().value(QStringLiteral("primitives")).toArray().size());
            }
            metadata.materialCount = static_cast<std::size_t>(
                root.value(QStringLiteral("materials")).toArray().size());
            metadata.textureCount = static_cast<std::size_t>(
                root.value(QStringLiteral("textures")).toArray().size());
            metadata.animationCount = static_cast<std::size_t>(
                root.value(QStringLiteral("animations")).toArray().size());

            const QString extension = QString::fromLatin1(kIridescenceExtension);
            if (containsString(
                    root.value(QStringLiteral("extensionsRequired")).toArray(),
                    extension)) {
                chunk = QJsonDocument(
                    removeExtension(root, extension).toObject())
                            .toJson(QJsonDocument::Compact);
                while (chunk.size() % 4 != 0) chunk.append(' ');
                rebuilt = true;
            }
        }

        appendUint32(readerBytes, static_cast<quint32>(chunk.size()));
        appendUint32(readerBytes, chunkType);
        readerBytes.append(chunk);
        offset = chunkEnd;
    }

    if (!foundJson) {
        error = QStringLiteral("GLB has no JSON chunk");
        return false;
    }
    if (!rebuilt) {
        readerBytes = source.left(declaredLength);
    } else {
        const quint32 size = qToLittleEndian(
            static_cast<quint32>(readerBytes.size()));
        std::memcpy(readerBytes.data() + 8, &size, sizeof(size));
    }
    return true;
}

Vector3 toProjectPoint(const double point[3], float scale) {
    constexpr float kMetersToMillimeters = 1000.0f;
    const float unit = scale * kMetersToMillimeters;
    return Vector3(
        static_cast<float>(point[0]) * unit,
        -static_cast<float>(point[2]) * unit,
        static_cast<float>(point[1]) * unit);
}

} // namespace

GlbImportResult GlbDBImporter::importFile(
    const std::string& filePath,
    float scale) const {
    GlbImportResult result;
    QFile file(QString::fromStdString(filePath));
    if (!file.open(QIODevice::ReadOnly)) {
        result.errorMessage = "Unable to read GLB file";
        return result;
    }
    const QByteArray sourceBytes = file.readAll();
    if (sourceBytes.isEmpty()) {
        result.errorMessage = "GLB file is empty";
        return result;
    }

    GlbAssetMetadata metadata;
    metadata.byteSize = static_cast<std::size_t>(sourceBytes.size());
    metadata.contentHash = QCryptographicHash::hash(
        sourceBytes, QCryptographicHash::Sha256).toHex().toStdString();

    QByteArray readerBytes;
    QString inspectError;
    if (!inspectAndPrepareBytes(
            sourceBytes, readerBytes, metadata, inspectError)) {
        result.errorMessage = inspectError.toStdString();
        return result;
    }

    vtkNew<vtkMemoryResourceStream> stream;
    stream->SetBuffer(
        readerBytes.constData(), static_cast<std::size_t>(readerBytes.size()), false);
    vtkNew<vtkGLTFReader> reader;
    reader->SetStream(stream);
    reader->Update();

    auto* blocks = reader->GetOutput();
    if (!blocks) {
        result.errorMessage = "VTK failed to decode GLB printable geometry";
        return result;
    }

    Vector3 minimum(FLT_MAX, FLT_MAX, FLT_MAX);
    Vector3 maximum(-FLT_MAX, -FLT_MAX, -FLT_MAX);
    std::size_t sourceVertexCount = 0;
    vtkCompositeDataIterator* iterator = blocks->NewIterator();
    for (iterator->InitTraversal(); !iterator->IsDoneWithTraversal();
         iterator->GoToNextItem()) {
        auto* polyData = vtkPolyData::SafeDownCast(
            iterator->GetCurrentDataObject());
        if (!polyData || !polyData->GetPoints()) continue;
        sourceVertexCount += static_cast<std::size_t>(
            polyData->GetNumberOfPoints());
        for (vtkIdType cellId = 0;
             cellId < polyData->GetNumberOfCells(); ++cellId) {
            vtkCell* cell = polyData->GetCell(cellId);
            if (!cell || cell->GetNumberOfPoints() != 3) continue;
            double p0[3];
            double p1[3];
            double p2[3];
            polyData->GetPoint(cell->GetPointId(0), p0);
            polyData->GetPoint(cell->GetPointId(1), p1);
            polyData->GetPoint(cell->GetPointId(2), p2);
            const Vector3 a = toProjectPoint(p0, scale);
            const Vector3 b = toProjectPoint(p1, scale);
            const Vector3 c = toProjectPoint(p2, scale);
            result.printableTriangles.emplace_back(a, b, c);
            for (const Vector3& point : {a, b, c}) {
                minimum.x = std::min(minimum.x, point.x);
                minimum.y = std::min(minimum.y, point.y);
                minimum.z = std::min(minimum.z, point.z);
                maximum.x = std::max(maximum.x, point.x);
                maximum.y = std::max(maximum.y, point.y);
                maximum.z = std::max(maximum.z, point.z);
            }
        }
    }
    iterator->Delete();

    if (result.printableTriangles.empty()) {
        result.errorMessage = "GLB contains no printable triangle geometry";
        return result;
    }

    const Vector3 center = (minimum + maximum) * 0.5f;
    for (auto& triangle : result.printableTriangles) {
        const Color color = triangle.color;
        triangle = GeomTriangle(
            triangle.vertex1 - center,
            triangle.vertex2 - center,
            triangle.vertex3 - center);
        triangle.color = color;
    }
    result.boundingBoxMin = minimum - center;
    result.boundingBoxMax = maximum - center;
    result.vertexCount = sourceVertexCount;
    result.faceCount = result.printableTriangles.size();

    auto bytes = std::make_shared<GlbAssetData::Bytes>(
        reinterpret_cast<const std::uint8_t*>(sourceBytes.constData()),
        reinterpret_cast<const std::uint8_t*>(sourceBytes.constData()) +
            sourceBytes.size());
    result.asset = std::make_shared<const GlbAssetData>(
        std::move(bytes), std::move(metadata));
    result.success = true;
    LOG_INFO(
        "GlbDBImporter: imported '{}' bytes={} vertices={} faces={}",
        filePath, sourceBytes.size(), result.vertexCount, result.faceCount);
    return result;
}
