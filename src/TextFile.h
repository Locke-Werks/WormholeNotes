#pragma once

#include <QString>

enum class Encoding { Utf8, Utf8Bom, Utf16LE, Utf16BE, Ansi };
enum class LineEnding { CRLF, LF, CR };

// A text file as Notepad sees it: the text with line breaks normalized to \n,
// plus enough about the original bytes to write it back the same way.
struct TextFile
{
    QString text;
    Encoding encoding = Encoding::Utf8;
    LineEnding lineEnding = LineEnding::CRLF;

    static bool read(const QString &path, TextFile &out, QString *error);
    bool write(const QString &path, QString *error) const;

    // False when saving in this encoding would lose characters. Only the ANSI
    // code page can; the others encode all of Unicode.
    static bool canEncode(const QString &text, Encoding encoding);
};

QString encodingName(Encoding encoding);
QString lineEndingName(LineEnding lineEnding);
