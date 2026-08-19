#pragma once

#include <QCache>
#include <QColor>
#include <QHash>
#include <QImage>
#include <QJSEngine>
#include <QString>

class MathRenderer {
public:
    MathRenderer();

    QString renderSvg(const QString &tex, bool display);
    QImage renderImage(const QString &tex, bool display, qreal pointSize,
                       qreal devicePixelRatio, const QColor &foreground);

    QString lastError() const { return m_lastError; }
    qreal lastBaselineOffset() const { return m_lastBaselineOffset; }
    void setDocumentDirectory(const QString &directoryPath);

private:
    bool ensureInitialized();
    QString svgCacheKey(const QString &tex, bool display) const;
    QString imageCacheKey(const QString &tex, bool display, qreal pointSize,
                         qreal devicePixelRatio, const QColor &foreground) const;
    QString applySvgSizingAndColor(const QString &svg, qreal pointSize, const QColor &foreground,
                                   qreal *baselineOffsetPx) const;
    void writeSvgCache(const QString &svgKey, const QString &svg);

    QJSEngine m_engine;
    bool m_initialized = false;
    bool m_engineReady = false;
    QString m_lastError;
    qreal m_lastBaselineOffset = 0.0;

    QHash<QString, QString> m_svgCache;
    QHash<QString, qreal> m_imageBaselineCache;
    QCache<QString, QImage> m_imageCache;

    bool m_saveFormulaAsSvg = true;
    QString m_documentDirectory;
};
