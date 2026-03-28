#include "Utils.h"
#include "widgets/QCaesiumMessageBox.h"

#include <QDesktopServices>
#include <QDir>
#include <QImageReader>
#include <QJsonObject>
#include <QMessageBox>
#include <QProcess>
#include <QSettings>
#include <cmath>

QString toHumanSize(double size)
{
    bool isNegative = size < 0;
    if (isNegative) {
        size = abs(size);
    }
    if (size == 0) {
        return "0 bytes";
    }

    QStringList unit;
    unit << "bytes"
         << "KB"
         << "MB"
         << "GB"
         << "TB";
    double order = floor(log2(size) / 10);

    if (order > 4) {
        qWarning() << "Woah, that's huge!";
        order = 4;
    }

    return QString::number(size / (pow(1024, order)) * (isNegative ? -1 : 1), 'f', 2) + ' ' + unit[static_cast<int>(order)];
}

std::tuple<unsigned int, unsigned int> cResize(const QImageReader* reader, const CompressionOptions& compressionOptions)
{
    int fitTo = compressionOptions.fitTo;
    int width = compressionOptions.width;
    int height = compressionOptions.height;
    int size = compressionOptions.size;
    bool doNotEnlarge = compressionOptions.doNotEnlarge;
    bool keepMetadata = compressionOptions.keepMetadata;
    bool rotatedByMetadata = false;

    QSize originalSize = reader->size();
    if (keepMetadata && (fitTo == ResizeMode::FIXED_WIDTH || fitTo == ResizeMode::FIXED_HEIGHT)) {
        originalSize = getSizeWithMetadata(reader);
        rotatedByMetadata = isRotatedByMetadata(reader);
    }

    int originalWidth = originalSize.width();
    int originalHeight = originalSize.height();

    if (fitTo == ResizeMode::DIMENSIONS) {
        int outputWidth = width;
        int outputHeight = height;
        if (doNotEnlarge && (outputWidth >= originalWidth || outputHeight >= originalHeight)) {
            return { originalWidth, originalHeight };
        }
        return { outputWidth, outputHeight };
    } else if (fitTo == ResizeMode::PERCENTAGE) {
        int outputWidthPerc = width;
        int outputHeightPerc = height;

        if (doNotEnlarge && (outputWidthPerc >= 100 || outputHeightPerc >= 100)) {
            return { originalWidth, originalHeight };
        }

        int outputWidth = static_cast<int>(round(static_cast<double>(originalWidth) * static_cast<double>(outputWidthPerc) / 100));
        int outputHeight = static_cast<int>(round(static_cast<double>(originalHeight) * static_cast<double>(outputHeightPerc) / 100));
        return { outputWidth, outputHeight };
    } else if (fitTo == ResizeMode::FIXED_WIDTH) {
        if (doNotEnlarge && keepMetadata && width > originalWidth) {
            return { 0, 0 };
        } else if (doNotEnlarge && !keepMetadata && width > originalWidth) {
            return { originalWidth, originalHeight };
        }

        if (rotatedByMetadata && keepMetadata) {
            return { 0, width };
        }
        return { width, 0 };
    } else if (fitTo == ResizeMode::FIXED_HEIGHT) {
        if (doNotEnlarge && keepMetadata && height > originalHeight) {
            return { 0, 0 };
        } else if (doNotEnlarge && !keepMetadata && height > originalHeight) {
            return { originalWidth, originalHeight };
        }

        if (rotatedByMetadata && keepMetadata) {
            return { height, 0 };
        }
        return { 0, height };
    } else if (fitTo == ResizeMode::LONG_EDGE || fitTo == ResizeMode::SHORT_EDGE) {
        // TODO Refactor this section
        if ((fitTo == ResizeMode::LONG_EDGE && originalWidth >= originalHeight) || (fitTo == ResizeMode::SHORT_EDGE && originalWidth <= originalHeight)) {
            if (doNotEnlarge && originalWidth <= size) {
                return { originalWidth, originalHeight };
            }
            return { size, 0 };
        } else if ((fitTo == ResizeMode::LONG_EDGE && originalHeight >= originalWidth) || (fitTo == ResizeMode::SHORT_EDGE && originalHeight <= originalWidth)) {
            if (doNotEnlarge && originalHeight <= size) {
                return { originalWidth, originalHeight };
            }
            return { 0, size };
        }
    }

    return { originalWidth, originalHeight };
}

void showFileInNativeFileManager(const QString& filePath, const QString& fallbackDirectory)
{
    if (!QFileInfo::exists(filePath)) {
        QCaesiumMessageBox msgBox;
        msgBox.setText(QIODevice::tr("File not found"));
        msgBox.addButton(QIODevice::tr("Ok"), QMessageBox::AcceptRole);
        msgBox.exec();
        return;
    }

#if defined(Q_OS_WIN)
    QStringList param = QStringList { "/select", ",", filePath };
    if (QProcess::startDetached("explorer", param)) {
        return;
    }
#elif defined(Q_OS_MAC)
    if (QProcess::startDetached("open", QStringList() << filePath << "-R"))
        return;
#endif
    QDesktopServices::openUrl(QUrl::fromLocalFile(fallbackDirectory));
}

void showDirectoryInNativeFileManager(const QString& dirPath)
{
    QDesktopServices::openUrl(QUrl::fromLocalFile(dirPath));
}

QJsonObject getSystemData()
{
    QSettings settings;
    QJsonObject systemData {
        { "uuid", settings.value("uuid").toString() },
        { "appVersion", QCoreApplication::applicationVersion() },
        { "kernelType", QSysInfo::kernelType() },
        { "kernelVersion", QSysInfo::kernelVersion() },
        { "productType", QSysInfo::productType() },
        { "productVersion", QSysInfo::productVersion() },
        { "cpuArchitecture", QSysInfo::currentCpuArchitecture() },
    };
    return systemData;
}

QJsonObject getCompressionOptionsAsJSON()
{
    QSettings settings;
    settings.beginGroup("compression_options");
    QMap<QString, QVariant> settingsMap;
    QStringList settingsKeys = settings.allKeys();
    QStringListIterator it(settingsKeys);
    while (it.hasNext()) {
        QString currentKey = it.next();
        settingsMap.insert(currentKey, settings.value(currentKey));
    }
    settings.endGroup();
    return QJsonObject::fromVariantMap(settingsMap);
}

CompressionOptions compressionOptionsForShellContext(const QString& rootFolder)
{
    QSettings settings;

    const int fitTo = settings.value("compression_options/resize/fit_to", 0).toInt();
    const bool resizeEnabled = fitTo != static_cast<int>(ResizeMode::NO_RESIZE);

    FileDatesOutputOption datesMap = {
        settings.value("compression_options/output/keep_creation_date", false).toBool(),
        settings.value("compression_options/output/keep_last_modified_date", false).toBool(),
        settings.value("compression_options/output/keep_last_access_date", false).toBool(),
    };

    const int jpegQuality = settings.value("compression_options/compression/jpeg_quality", 80).toInt();
    const int pngQuality = settings.value("compression_options/compression/png_quality", 80).toInt();
    const int webpQuality = settings.value("compression_options/compression/webp_quality", 60).toInt();
    const int tiffDeflateSlider = settings.value("compression_options/compression/tiff_deflate_level", 2).toInt();

    CompressionOptions compressionOptions = {
        settings.value("compression_options/output/output_folder", "").toString(),
        rootFolder,
        QStringLiteral("_comp"),
        settings.value("compression_options/output/format", 0).toInt(),
        settings.value("compression_options/compression/lossless", false).toBool(),
        settings.value("compression_options/compression/keep_metadata", true).toBool(),
        false,
        resizeEnabled,
        fitTo,
        settings.value("compression_options/resize/width", 1000).toInt(),
        settings.value("compression_options/resize/height", 1000).toInt(),
        settings.value("compression_options/resize/size", 1000).toInt(),
        settings.value("compression_options/resize/do_not_enlarge", false).toBool(),
        true,
        settings.value("compression_options/output/skip_if_bigger", true).toBool(),
        false,
        0,
        qBound(1, jpegQuality, 100),
        settings.value("compression_options/compression/jpeg_chroma_subsampling", 0).toInt(),
        settings.value("compression_options/compression/jpeg_progressive", true).toBool(),
        qBound(0, pngQuality, 100),
        qBound(1, settings.value("compression_options/compression/png_optimization_level", 3).toInt(), 6),
        qBound(1, webpQuality, 100),
        settings.value("compression_options/compression/tiff_method", 1).toInt(),
        qBound(1, tiffDeflateSlider * 3, 9),
        settings.value("compression_options/output/keep_dates", Qt::Unchecked).value<Qt::CheckState>() != Qt::Unchecked,
        datesMap,
        static_cast<CompressionMode>(settings.value("compression_options/compression/mode", 0).toInt()),
        MaxOutputSize {
            static_cast<MaxOutputSizeUnit>(settings.value("compression_options/compression/max_output_size_unit", 0).toInt()),
            static_cast<size_t>(settings.value("compression_options/compression/max_output_size", 500).toInt()),
        },
    };

    return compressionOptions;
}

QString getCompressionOptionsHash()
{
    QSettings settings;
    QString implodedSettings;
    settings.beginGroup("compression_options");
    QStringList settingsKeys = settings.allKeys();
    QStringListIterator it(settingsKeys);
    while (it.hasNext()) {
        QString currentKey = it.next();
        implodedSettings.append(currentKey + ":" + settings.value(currentKey).toString() + "||");
    }
    settings.endGroup();
    return hashString(implodedSettings, QCryptographicHash::Sha256);
}

QString hashString(const QString& data, QCryptographicHash::Algorithm algorithm)
{
    QCryptographicHash hash = QCryptographicHash(algorithm);
    hash.addData(data.toUtf8());
    return QString::fromUtf8(hash.result().toHex());
}

QStringList getOutputSupportedFormats()
{
    return {
        QIODevice::tr("Same as input"),
        QIODevice::tr("JPG"),
        QIODevice::tr("PNG"),
        QIODevice::tr("WebP"),
        QIODevice::tr("TIFF"),
    };
}

bool isRotatedByMetadata(const QImageReader* reader)
{
    QFlags<QImageIOHandler::Transformation> transformation = reader->transformation();
    return (transformation == QImageIOHandler::TransformationRotate90
        || transformation == QImageIOHandler::TransformationMirrorAndRotate90
        || transformation == QImageIOHandler::TransformationFlipAndRotate90
        || transformation == QImageIOHandler::TransformationRotate270);
}

QSize getSizeWithMetadata(const QImageReader* reader)
{
    QSize imageSize = reader->size();
    QSize actualSize(imageSize.width(), imageSize.height());
    // We need to check if the image is rotated by metadata and adjust the values accordingly
    if (isRotatedByMetadata(reader)) {
        actualSize.setWidth(imageSize.height());
        actualSize.setHeight(imageSize.width());
    }

    return actualSize;
}

QMap<int, QString> getChromaSubsamplingOptions()
{
    return {
        { JPEGChromaSubsampling::CHROMA_AUTO, QIODevice::tr("Auto") },
        { JPEGChromaSubsampling::CHROMA_444, QString("4:4:4") },
        { JPEGChromaSubsampling::CHROMA_422, QString("4:2:2") },
        { JPEGChromaSubsampling::CHROMA_420, QString("4:2:0") },
        { JPEGChromaSubsampling::CHROMA_411, QString("4:1:1") },
    };
}