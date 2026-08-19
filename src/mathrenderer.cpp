#include "mathrenderer.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJSValue>
#include <QPainter>
#include <QRegularExpression>
#include <QSettings>
#include <QStandardPaths>
#include <QSvgRenderer>

#include <cmath>
#include <limits>

namespace {

constexpr int defaultImageCacheBudget = 48 * 1024 * 1024;

QString digestKey(const QString &raw) {
    return QString::fromLatin1(
        QCryptographicHash::hash(raw.toUtf8(), QCryptographicHash::Sha256).toHex());
}

qreal parseWithRegex(const QString &svg, const QRegularExpression &re, qreal fallback = 0.0) {
    const QRegularExpressionMatch match = re.match(svg);
    if (!match.hasMatch())
        return fallback;
    bool ok = false;
    const qreal value = match.captured(1).toDouble(&ok);
    return ok ? value : fallback;
}

qreal parseWidthEx(const QString &svg) {
    static const QRegularExpression widthRe(QStringLiteral("width=\\\"([0-9.+-]+)ex\\\""));
    return parseWithRegex(svg, widthRe);
}

qreal parseHeightEx(const QString &svg) {
    static const QRegularExpression heightRe(QStringLiteral("height=\\\"([0-9.+-]+)ex\\\""));
    return parseWithRegex(svg, heightRe);
}

qreal parseVerticalAlignEx(const QString &svg) {
    static const QRegularExpression re(QStringLiteral("vertical-align:\\s*([0-9.+-]+)ex"));
    return parseWithRegex(svg, re);
}

} // namespace

MathRenderer::MathRenderer()
    : m_imageCache(defaultImageCacheBudget) {
    QSettings settings;
    m_saveFormulaAsSvg = settings.value(QStringLiteral("math/SaveFormulaAsSvg"), true).toBool();
}

void MathRenderer::setDocumentDirectory(const QString &directoryPath) {
    m_documentDirectory = directoryPath;
}

bool MathRenderer::ensureInitialized() {
    if (m_initialized)
        return m_engineReady;

    m_initialized = true;

    auto evalResource = [this](const QString &path) -> bool {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            m_lastError = QStringLiteral("Failed to open %1").arg(path);
            return false;
        }
        const QString script = QString::fromUtf8(file.readAll());
        QJSValue result = m_engine.evaluate(script, path);
        if (result.isError()) {
            m_lastError = QStringLiteral("%1:%2: %3")
                              .arg(path)
                              .arg(result.property(QStringLiteral("lineNumber")).toInt())
                              .arg(result.toString());
            return false;
        }
        return true;
    };

    const QString shim = QStringLiteral(
        "if (typeof globalThis === 'undefined') globalThis = this;"
        "if (typeof window === 'undefined') window = this;"
        "if (typeof self === 'undefined') self = this;"
        "if (typeof navigator === 'undefined') navigator = {userAgent:'QJSEngine'};");
    {
        QJSValue shimResult = m_engine.evaluate(shim, QStringLiteral("<shim>"));
        if (shimResult.isError()) {
            m_lastError = shimResult.toString();
            m_engineReady = false;
            return false;
        }
    }

    if (!evalResource(QStringLiteral(":/mathjax/adapter.js"))
            || !evalResource(QStringLiteral(":/mathjax/tex-svg-full.js"))) {
        m_engineReady = false;
        return false;
    }

    m_engineReady = true;
    return true;
}

QString MathRenderer::svgCacheKey(const QString &tex, bool display) const {
    return digestKey(tex + QLatin1Char('\x1f') + (display ? QStringLiteral("1") : QStringLiteral("0")));
}

QString MathRenderer::imageCacheKey(const QString &tex, bool display, qreal pointSize,
                                    qreal devicePixelRatio, const QColor &foreground) const {
    return digestKey(QStringLiteral("%1|%2|%3|%4|%5")
                         .arg(tex)
                         .arg(display ? 1 : 0)
                         .arg(pointSize, 0, 'f', 3)
                         .arg(devicePixelRatio, 0, 'f', 3)
                         .arg(foreground.rgba(), 0, 16));
}

QString MathRenderer::renderSvg(const QString &tex, bool display) {
    m_lastError.clear();

    const QString key = svgCacheKey(tex, display);
    const auto cachedIt = m_svgCache.constFind(key);
    if (cachedIt != m_svgCache.cend())
        return *cachedIt;

    if (!ensureInitialized())
        return {};

    QJSValue converter = m_engine.globalObject().property(QStringLiteral("texToSvg"));
    if (!converter.isCallable()) {
        m_lastError = QStringLiteral("texToSvg() is not available");
        return {};
    }

    QJSValue result = converter.call({QJSValue(tex), QJSValue(display)});
    if (result.isError()) {
        m_lastError = result.toString();
        return {};
    }

    const QString error = result.property(QStringLiteral("error")).toString();
    if (!error.isEmpty()) {
        m_lastError = error;
        return {};
    }

    const QString svg = result.property(QStringLiteral("svg")).toString();
    if (svg.isEmpty()) {
        m_lastError = QStringLiteral("MathJax returned empty SVG output");
        return {};
    }

    m_svgCache.insert(key, svg);
    writeSvgCache(key, svg);
    return svg;
}

QString MathRenderer::applySvgSizingAndColor(const QString &svg, qreal pointSize,
                                             const QColor &foreground,
                                             qreal *baselineOffsetPx) const {
    QString rewritten = svg;

    const qreal pxPerEm = pointSize * (96.0 / 72.0);
    const qreal pxPerEx = pxPerEm * 0.5;

    const qreal widthEx = parseWidthEx(rewritten);
    const qreal heightEx = parseHeightEx(rewritten);
    if (widthEx > 0.0 && heightEx > 0.0) {
        static const QRegularExpression widthExRe(QStringLiteral("width=\\\"[0-9.+-]+ex\\\""));
        static const QRegularExpression heightExRe(QStringLiteral("height=\\\"[0-9.+-]+ex\\\""));
        const QString widthAttr = QStringLiteral("width=\"%1px\"").arg(widthEx * pxPerEx, 0, 'f', 3);
        const QString heightAttr = QStringLiteral("height=\"%1px\"").arg(heightEx * pxPerEx, 0, 'f', 3);
        rewritten.replace(widthExRe, widthAttr);
        rewritten.replace(heightExRe, heightAttr);
    }

    rewritten.replace(QStringLiteral("currentColor"), foreground.name(QColor::HexRgb));

    if (baselineOffsetPx) {
        const qreal verticalAlignEx = parseVerticalAlignEx(rewritten);
        *baselineOffsetPx = -verticalAlignEx * pxPerEx;
    }

    return rewritten;
}

void MathRenderer::writeSvgCache(const QString &svgKey, const QString &svg) {
    if (!m_saveFormulaAsSvg)
        return;

    QString cacheDirPath;
    if (!m_documentDirectory.isEmpty())
        cacheDirPath = QDir(m_documentDirectory).filePath(QStringLiteral(".svg-cache"));
    else
        cacheDirPath = QDir(QStandardPaths::writableLocation(QStandardPaths::CacheLocation))
                           .filePath(QStringLiteral("svg-cache"));

    QDir dir;
    if (!dir.mkpath(cacheDirPath))
        return;

    const QString path = QDir(cacheDirPath).filePath(svgKey + QStringLiteral(".svg"));
    if (QFileInfo::exists(path))
        return;

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
        return;
    file.write(svg.toUtf8());
}

QImage MathRenderer::renderImage(const QString &tex, bool display, qreal pointSize,
                                 qreal devicePixelRatio, const QColor &foreground) {
    m_lastError.clear();

    const QString key = imageCacheKey(tex, display, pointSize, devicePixelRatio, foreground);
    if (QImage *cached = m_imageCache.object(key)) {
        m_lastBaselineOffset = m_imageBaselineCache.value(key, 0.0);
        return *cached;
    }

    QString svg = renderSvg(tex, display);
    if (svg.isEmpty())
        return {};

    qreal baselineOffsetPx = 0.0;
    svg = applySvgSizingAndColor(svg, pointSize, foreground, &baselineOffsetPx);

    QSvgRenderer renderer(svg.toUtf8());
    if (!renderer.isValid()) {
        m_lastError = QStringLiteral("Failed to parse generated SVG");
        return {};
    }

    QSize svgSize = renderer.defaultSize();
    if (svgSize.width() <= 0 || svgSize.height() <= 0) {
        const QRectF viewBox = renderer.viewBoxF();
        svgSize = viewBox.size().toSize();
    }
    if (svgSize.width() <= 0 || svgSize.height() <= 0)
        svgSize = QSize(1, 1);

    const qreal dpr = std::max<qreal>(1.0, devicePixelRatio);
    const QSize deviceSize(std::max(1, int(std::ceil(svgSize.width() * dpr))),
                           std::max(1, int(std::ceil(svgSize.height() * dpr))));

    QImage image(deviceSize, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    image.setDevicePixelRatio(dpr);

    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::TextAntialiasing, true);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    renderer.render(&painter, QRectF(QPointF(0, 0), QSizeF(svgSize)));

    m_lastBaselineOffset = baselineOffsetPx;
    m_imageBaselineCache.insert(key, baselineOffsetPx);

    auto *stored = new QImage(image);
    const int cost = static_cast<int>(std::min<qint64>(stored->sizeInBytes(), std::numeric_limits<int>::max()));
    m_imageCache.insert(key, stored, cost);
    return image;
}
