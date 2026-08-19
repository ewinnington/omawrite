#include "mathscanner.h"

#include <QChar>
#include <QRegularExpression>

#include <algorithm>

namespace {

struct CandidateSpan {
    MathSpan span;
    int priority = 0;
};

bool overlaps(const MathSpan &a, const MathSpan &b) {
    return a.start < b.end && b.start < a.end;
}

bool isEscaped(const QString &text, int index) {
    int backslashes = 0;
    for (int i = index - 1; i >= 0 && text.at(i) == QLatin1Char('\\'); --i)
        ++backslashes;
    return (backslashes % 2) == 1;
}

bool isFollowedByDigit(const QString &text, int index) {
    const int next = index + 1;
    return next < text.size() && text.at(next).isDigit();
}

void maskRange(QString &masked, int start, int end) {
    for (int i = start; i < end && i < masked.size(); ++i) {
        if (masked.at(i) != QLatin1Char('\n'))
            masked[i] = QLatin1Char(' ');
    }
}

bool parseFenceLine(const QString &line, QChar &marker, int &count) {
    int i = 0;
    int spaces = 0;
    while (i < line.size() && line.at(i) == QLatin1Char(' ') && spaces < 3) {
        ++i;
        ++spaces;
    }
    if (i >= line.size())
        return false;

    const QChar c = line.at(i);
    if (c != QLatin1Char('`') && c != QLatin1Char('~'))
        return false;

    int j = i;
    while (j < line.size() && line.at(j) == c)
        ++j;
    if (j - i < 3)
        return false;

    marker = c;
    count = j - i;
    return true;
}

bool isFenceCloseLine(const QString &line, QChar marker, int minCount) {
    int i = 0;
    int spaces = 0;
    while (i < line.size() && line.at(i) == QLatin1Char(' ') && spaces < 3) {
        ++i;
        ++spaces;
    }
    int count = 0;
    while (i < line.size() && line.at(i) == marker) {
        ++count;
        ++i;
    }
    if (count < minCount)
        return false;

    while (i < line.size()) {
        if (!line.at(i).isSpace())
            return false;
        ++i;
    }
    return true;
}

void collectDoubleDollar(const QString &text, QVector<CandidateSpan> &candidates) {
    int i = 0;
    while (i + 1 < text.size()) {
        if (text.at(i) == QLatin1Char('$') && text.at(i + 1) == QLatin1Char('$')
                && !isEscaped(text, i)) {
            int j = i + 2;
            while (j + 1 < text.size()) {
                if (text.at(j) == QLatin1Char('$') && text.at(j + 1) == QLatin1Char('$')
                        && !isEscaped(text, j)) {
                    candidates.append({MathSpan{i, j + 2, true}, 0});
                    i = j + 2;
                    break;
                }
                ++j;
            }
            if (j + 1 >= text.size())
                ++i;
            continue;
        }
        ++i;
    }
}

void collectBracketDisplay(const QString &text, QVector<CandidateSpan> &candidates) {
    int i = 0;
    while (i + 1 < text.size()) {
        if (text.at(i) == QLatin1Char('\\') && text.at(i + 1) == QLatin1Char('[')
                && !isEscaped(text, i)) {
            const int end = text.indexOf(QStringLiteral("\\]"), i + 2);
            if (end >= 0 && !isEscaped(text, end)) {
                candidates.append({MathSpan{i, end + 2, true}, 0});
                i = end + 2;
                continue;
            }
        }
        ++i;
    }
}

void collectParenInline(const QString &text, QVector<CandidateSpan> &candidates) {
    int i = 0;
    while (i + 1 < text.size()) {
        if (text.at(i) == QLatin1Char('\\') && text.at(i + 1) == QLatin1Char('(')
                && !isEscaped(text, i)) {
            const int end = text.indexOf(QStringLiteral("\\)"), i + 2);
            if (end >= 0 && !isEscaped(text, end)) {
                candidates.append({MathSpan{i, end + 2, false}, 1});
                i = end + 2;
                continue;
            }
        }
        ++i;
    }
}

void collectSingleDollar(const QString &text, QVector<CandidateSpan> &candidates) {
    for (int i = 0; i < text.size(); ++i) {
        if (text.at(i) != QLatin1Char('$') || isEscaped(text, i))
            continue;
        if ((i + 1 < text.size() && text.at(i + 1) == QLatin1Char('$'))
                || (i - 1 >= 0 && text.at(i - 1) == QLatin1Char('$')))
            continue;
        // Pandoc-style: opening $ cannot be followed by space, and does not
        // need whitespace before it, so `$n$,` and `($x$)` still parse.
        if (i + 1 >= text.size() || text.at(i + 1).isSpace())
            continue;

        const int lineEnd = text.indexOf(QLatin1Char('\n'), i + 1);
        const int limit = lineEnd < 0 ? text.size() : lineEnd;
        int j = i + 1;
        while (j < limit) {
            if (text.at(j) == QLatin1Char('$') && !isEscaped(text, j)) {
                if ((j + 1 < text.size() && text.at(j + 1) == QLatin1Char('$'))
                        || (j - 1 >= 0 && text.at(j - 1) == QLatin1Char('$'))) {
                    ++j;
                    continue;
                }

                const QChar beforeClose = text.at(j - 1);
                if (beforeClose.isSpace()) {
                    ++j;
                    continue;
                }

                // A closer followed by a digit is currency (`$5 and $6`), not math.
                if (isFollowedByDigit(text, j)) {
                    ++j;
                    continue;
                }

                candidates.append({MathSpan{i, j + 1, false}, 1});
                i = j;
                break;
            }
            ++j;
        }
    }
}

} // namespace

QString maskCode(const QString &text) {
    QString masked = text;

    bool inFence = false;
    QChar fenceMarker;
    int fenceMinLength = 0;

    int lineStart = 0;
    while (lineStart <= text.size()) {
        int lineEnd = text.indexOf(QLatin1Char('\n'), lineStart);
        if (lineEnd < 0)
            lineEnd = text.size();

        const QString line = text.mid(lineStart, lineEnd - lineStart);

        if (inFence) {
            maskRange(masked, lineStart, lineEnd);
            if (isFenceCloseLine(line, fenceMarker, fenceMinLength)) {
                inFence = false;
                fenceMarker = QChar();
                fenceMinLength = 0;
            }
        } else {
            QChar marker;
            int markerLength = 0;
            if (parseFenceLine(line, marker, markerLength)) {
                inFence = true;
                fenceMarker = marker;
                fenceMinLength = markerLength;
                maskRange(masked, lineStart, lineEnd);
            } else {
                int i = 0;
                while (i < line.size()) {
                    if (line.at(i) != QLatin1Char('`')) {
                        ++i;
                        continue;
                    }
                    int openEnd = i;
                    while (openEnd < line.size() && line.at(openEnd) == QLatin1Char('`'))
                        ++openEnd;
                    const int tickCount = openEnd - i;

                    int close = openEnd;
                    while (close < line.size()) {
                        if (line.at(close) != QLatin1Char('`')) {
                            ++close;
                            continue;
                        }
                        int closeEnd = close;
                        while (closeEnd < line.size() && line.at(closeEnd) == QLatin1Char('`'))
                            ++closeEnd;
                        if (closeEnd - close == tickCount) {
                            maskRange(masked, lineStart + i, lineStart + closeEnd);
                            i = closeEnd;
                            break;
                        }
                        close = closeEnd;
                    }

                    if (close >= line.size())
                        break;
                }
            }
        }

        if (lineEnd >= text.size())
            break;
        lineStart = lineEnd + 1;
    }

    return masked;
}

QVector<MathSpan> scanMath(const QString &text) {
    const QString masked = maskCode(text);

    QVector<CandidateSpan> candidates;
    collectDoubleDollar(masked, candidates);
    collectBracketDisplay(masked, candidates);
    collectParenInline(masked, candidates);
    collectSingleDollar(masked, candidates);

    std::sort(candidates.begin(), candidates.end(), [](const CandidateSpan &a, const CandidateSpan &b) {
        if (a.span.start != b.span.start)
            return a.span.start < b.span.start;
        if (a.priority != b.priority)
            return a.priority < b.priority;
        return a.span.end < b.span.end;
    });

    QVector<MathSpan> accepted;
    for (const CandidateSpan &candidate : candidates) {
        bool blocked = false;
        for (const MathSpan &existing : accepted) {
            if (!overlaps(existing, candidate.span))
                continue;
            blocked = true;
            break;
        }
        if (!blocked)
            accepted.append(candidate.span);
    }

    std::sort(accepted.begin(), accepted.end(), [](const MathSpan &a, const MathSpan &b) {
        if (a.start != b.start)
            return a.start < b.start;
        return a.end < b.end;
    });

    return accepted;
}

QString mathContent(const QString &delimited) {
    if (delimited.startsWith(QLatin1String("$$")) && delimited.endsWith(QLatin1String("$$"))
            && delimited.size() >= 4)
        return delimited.mid(2, delimited.size() - 4);
    if (delimited.startsWith(QLatin1String("\\[")) && delimited.endsWith(QLatin1String("\\]"))
            && delimited.size() >= 4)
        return delimited.mid(2, delimited.size() - 4);
    if (delimited.startsWith(QLatin1String("\\(")) && delimited.endsWith(QLatin1String("\\)"))
            && delimited.size() >= 4)
        return delimited.mid(2, delimited.size() - 4);
    if (delimited.startsWith(QLatin1Char('$')) && delimited.endsWith(QLatin1Char('$'))
            && delimited.size() >= 2)
        return delimited.mid(1, delimited.size() - 2);
    return delimited;
}

