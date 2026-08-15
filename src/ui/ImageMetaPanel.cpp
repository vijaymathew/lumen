#include "ui/ImageMetaPanel.h"

#include "core/RawLoader.h"

#include <QFileInfo>
#include <QGridLayout>
#include <QImageReader>
#include <QLabel>
#include <QLocale>
#include <QVBoxLayout>

namespace {
constexpr int kPanelWidth = 260;
}

ImageMetaPanel::ImageMetaPanel(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("imageMetaPanel"));
    setAttribute(Qt::WA_StyledBackground, true);
    setFixedWidth(kPanelWidth);

    auto *title = new QLabel(QStringLiteral("Info"), this);
    title->setObjectName(QStringLiteral("toolTitle"));

    m_placeholder = new QLabel(QStringLiteral("No image selected"), this);
    m_placeholder->setObjectName(QStringLiteral("rowName"));
    m_placeholder->setWordWrap(true);

    m_grid = new QGridLayout;
    m_grid->setContentsMargins(0, 0, 0, 0);
    m_grid->setHorizontalSpacing(10);
    m_grid->setVerticalSpacing(6);
    m_grid->setColumnStretch(1, 1);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(14, 14, 14, 14);
    layout->setSpacing(10);
    layout->addWidget(title);
    layout->addWidget(m_placeholder);
    layout->addLayout(m_grid);
    layout->addStretch(1);

    setStyleSheet(QStringLiteral(R"(
        #imageMetaPanel { background: #1c1c1f; border-left: 1px solid #38383d; }
        #toolTitle { color: #e8e8ea; font-size: 13px; }
        #rowName { color: #b4b4b8; font-size: 12px; }
        #infoKey { color: #8a8a90; font-size: 11px; }
        #infoValue { color: #e2e2e5; font-size: 12px; }
    )"));
}

void ImageMetaPanel::clearRows()
{
    for (QWidget *w : m_cells)
        w->deleteLater();
    m_cells.clear();
}

void ImageMetaPanel::setRows(const QVector<Row> &rows)
{
    clearRows();
    m_placeholder->setVisible(rows.isEmpty());

    for (int i = 0; i < rows.size(); ++i) {
        auto *key = new QLabel(rows[i].label, this);
        key->setObjectName(QStringLiteral("infoKey"));
        key->setAlignment(Qt::AlignTop | Qt::AlignLeft);

        auto *val = new QLabel(rows[i].value, this);
        val->setObjectName(QStringLiteral("infoValue"));
        val->setWordWrap(true);
        val->setTextInteractionFlags(Qt::TextSelectableByMouse);
        val->setAlignment(Qt::AlignTop | Qt::AlignLeft);

        m_grid->addWidget(key, i, 0);
        m_grid->addWidget(val, i, 1);
        m_cells.push_back(key);
        m_cells.push_back(val);
    }
}

void ImageMetaPanel::setSelection(const QStringList &paths)
{
    if (paths.isEmpty()) {
        setRows({});
        return;
    }
    if (paths.size() > 1) {
        setRows({{QStringLiteral("Selected"), QStringLiteral("%1 images").arg(paths.size())}});
        return;
    }

    const QString &path = paths.first();
    const QFileInfo info(path);
    QVector<Row> rows;
    const auto add = [&rows](const QString &label, const QString &value) {
        if (!value.isEmpty())
            rows.push_back({label, value});
    };

    add(QStringLiteral("File"), info.fileName());
    add(QStringLiteral("Size"), QLocale().formattedDataSize(info.size()));
    add(QStringLiteral("Modified"),
        info.lastModified().toString(QStringLiteral("yyyy-MM-dd HH:mm")));

    if (raw::isRawPath(path)) {
        raw::LensMetadata m;
        if (raw::readMetadata(path, &m)) {
            QString camera = m.cameraMaker;
            if (!m.cameraModel.isEmpty()) {
                if (camera.isEmpty() || m.cameraModel.startsWith(camera, Qt::CaseInsensitive))
                    camera = m.cameraModel;
                else
                    camera = camera + QLatin1Char(' ') + m.cameraModel;
            }
            add(QStringLiteral("Camera"), camera.trimmed());
            add(QStringLiteral("Lens"), m.lensModel);
            if (m.focalLength > 0.0f)
                add(QStringLiteral("Focal length"),
                    QStringLiteral("%1 mm").arg(m.focalLength, 0, 'f', 0));
            if (m.aperture > 0.0f)
                add(QStringLiteral("Aperture"), QStringLiteral("f/%1").arg(m.aperture, 0, 'f', 1));
            if (m.shutter > 0.0f) {
                const QString shutter = m.shutter < 1.0f
                    ? QStringLiteral("1/%1 s").arg(qRound(1.0f / m.shutter))
                    : QStringLiteral("%1 s").arg(m.shutter, 0, 'f', 1);
                add(QStringLiteral("Shutter"), shutter);
            }
            if (m.iso > 0.0f)
                add(QStringLiteral("ISO"), QString::number(qRound(m.iso)));
            if (m.captureTime.isValid())
                add(QStringLiteral("Captured"),
                    m.captureTime.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")));
            add(QStringLiteral("Color"), m.monochrome ? QStringLiteral("Monochrome")
                                                       : QStringLiteral("Color"));
        }
    } else {
        QImageReader reader(path);
        const QSize size = reader.size();
        if (size.isValid())
            add(QStringLiteral("Dimensions"),
                QStringLiteral("%1 × %2").arg(size.width()).arg(size.height()));
    }

    setRows(rows);
}
