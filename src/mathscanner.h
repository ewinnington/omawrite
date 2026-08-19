#pragma once

#include <QString>
#include <QVector>

struct MathSpan {
    int start = 0;
    int end = 0;
    bool display = false;
};

QString maskCode(const QString &text);
QVector<MathSpan> scanMath(const QString &text);

