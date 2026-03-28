#include "ShellContextCompress.h"

#include "LanguageManager.h"
#include "exceptions/ImageNotSupportedException.h"
#include "exceptions/ImageTooBigException.h"
#include "models/CImage.h"
#include "services/Importer.h"
#include "utils/Utils.h"

#include <QApplication>
#include <QFileInfo>
#include <QImageReader>
#include <QMessageBox>
#include <QTranslator>

int runShellContextCompress(QApplication& app, const QStringList& filePaths)
{
    if (filePaths.isEmpty()) {
        return 1;
    }

    QTranslator translator;
    LanguageManager::loadLocale(&translator);
    app.installTranslator(&translator);

    QImageReader::setAllocationLimit(1024);

    const QString rootFolder = Importer::getRootFolder(filePaths);
    const CompressionOptions options = compressionOptionsForShellContext(rootFolder);

    QStringList errors;
    for (const QString& path : filePaths) {
        try {
            CImage image(path);
            if (!image.compress(options)) {
                QString detail = image.getFormattedStatus();
                if (detail.isEmpty()) {
                    detail = image.getAdditionalInfo();
                }
                if (detail.isEmpty()) {
                    detail = QApplication::tr("Compression failed");
                }
                errors.append(QFileInfo(path).fileName() + ": " + detail);
            }
        } catch (const ImageNotSupportedException&) {
            errors.append(QFileInfo(path).fileName() + ": " + QApplication::tr("Unsupported format"));
        } catch (const ImageTooBigException&) {
            errors.append(QFileInfo(path).fileName() + ": " + QApplication::tr("Input file is too large"));
        }
    }

    if (!errors.isEmpty()) {
        QMessageBox msg;
        msg.setIcon(QMessageBox::Warning);
        msg.setWindowTitle(QApplication::tr("Caesium Image Compressor"));
        msg.setText(QApplication::tr("Some files could not be compressed."));
        msg.setDetailedText(errors.join('\n'));
        msg.exec();
        return 1;
    }

    return 0;
}
