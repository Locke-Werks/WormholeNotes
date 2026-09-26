#include "TextFile.h"

#include <QFile>
#include <QSaveFile>
#include <QStringDecoder>
#include <QStringEncoder>

namespace {

const QByteArray kBomUtf8("\xEF\xBB\xBF", 3);
const QByteArray kBomUtf16LE("\xFF\xFE", 2);
const QByteArray kBomUtf16BE("\xFE\xFF", 2);

LineEnding detectLineEnding(const QString &text)
{
    qsizetype crlf = 0, lf = 0, cr = 0;
    for (qsizetype i = 0; i < text.size(); ++i) {
        const QChar ch = text.at(i);
        if (ch == u'\r') {
            if (i + 1 < text.size() && text.at(i + 1) == u'\n') {
                ++crlf;
                ++i;
            } else {
                ++cr;
            }
        } else if (ch == u'\n') {
            ++lf;
        }
    }
    // A file with no line breaks at all gets the Windows default, like Notepad.
    if (lf > crlf && lf >= cr)
        return LineEnding::LF;
    if (cr > crlf && cr > lf)
        return LineEnding::CR;
    return LineEnding::CRLF;
}

} // namespace

bool TextFile::read(const QString &path, TextFile &out, QString *error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error)
            *error = file.errorString();
        return false;
    }
    const QByteArray bytes = file.readAll();

    QString text;
    Encoding encoding;
    if (bytes.startsWith(kBomUtf8)) {
        encoding = Encoding::Utf8Bom;
        text = QString::fromUtf8(bytes.mid(kBomUtf8.size()));
    } else if (bytes.startsWith(kBomUtf16LE)) {
        encoding = Encoding::Utf16LE;
        QStringDecoder decoder(QStringConverter::Utf16LE);
        text = decoder(bytes.mid(kBomUtf16LE.size()));
    } else if (bytes.startsWith(kBomUtf16BE)) {
        encoding = Encoding::Utf16BE;
        QStringDecoder decoder(QStringConverter::Utf16BE);
        text = decoder(bytes.mid(kBomUtf16BE.size()));
    } else {
        // No BOM: valid UTF-8 is taken as UTF-8, anything else falls back to the
        // ANSI code page, which is what Notepad would have written it in.
        QStringDecoder utf8(QStringConverter::Utf8, QStringConverter::Flag::Stateless);
        text = utf8(bytes);
        if (utf8.hasError()) {
            QStringDecoder ansi(QStringConverter::System);
            text = ansi(bytes);
            encoding = Encoding::Ansi;
        } else {
            encoding = Encoding::Utf8;
        }
    }

    out.lineEnding = detectLineEnding(text);
    text.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    text.replace(u'\r', u'\n');
    out.text = text;
    out.encoding = encoding;
    return true;
}

bool TextFile::write(const QString &path, QString *error) const
{
    QString body = text;
    switch (lineEnding) {
    case LineEnding::CRLF:
        body.replace(u'\n', QStringLiteral("\r\n"));
        break;
    case LineEnding::CR:
        body.replace(u'\n', u'\r');
        break;
    case LineEnding::LF:
        break;
    }

    QByteArray bytes;
    switch (encoding) {
    case Encoding::Utf8:
        bytes = body.toUtf8();
        break;
    case Encoding::Utf8Bom:
        bytes = kBomUtf8 + body.toUtf8();
        break;
    case Encoding::Utf16LE: {
        QStringEncoder encoder(QStringConverter::Utf16LE);
        bytes = kBomUtf16LE + QByteArray(encoder(body));
        break;
    }
    case Encoding::Utf16BE: {
        QStringEncoder encoder(QStringConverter::Utf16BE);
        bytes = kBomUtf16BE + QByteArray(encoder(body));
        break;
    }
    case Encoding::Ansi: {
        QStringEncoder encoder(QStringConverter::System);
        bytes = encoder(body);
        break;
    }
    }

    // QSaveFile writes beside the target and renames over it, so a failed save
    // never leaves a half-written file where the good one was.
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) {
        if (error)
            *error = file.errorString();
        return false;
    }
    return true;
}

QString encodingName(Encoding encoding)
{
    switch (encoding) {
    case Encoding::Utf8: return QStringLiteral("UTF-8");
    case Encoding::Utf8Bom: return QStringLiteral("UTF-8 with BOM");
    case Encoding::Utf16LE: return QStringLiteral("UTF-16 LE");
    case Encoding::Utf16BE: return QStringLiteral("UTF-16 BE");
    case Encoding::Ansi: return QStringLiteral("ANSI");
    }
    return {};
}

QString lineEndingName(LineEnding lineEnding)
{
    switch (lineEnding) {
    case LineEnding::CRLF: return QStringLiteral("Windows (CRLF)");
    case LineEnding::LF: return QStringLiteral("Unix (LF)");
    case LineEnding::CR: return QStringLiteral("Macintosh (CR)");
    }
    return {};
}
