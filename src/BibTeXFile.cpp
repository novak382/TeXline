/*
	This is part of TeXworks, an environment for working with TeX documents
	Copyright (C) 2017-2024  Jonathan Kew, Stefan Löffler, Charlie Sharpsteen

	This program is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; either version 2 of the License, or
	(at your option) any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with this program.  If not, see <https://www.gnu.org/licenses/>.

	For links to further information, or to contact the authors,
	see <https://tug.org/texworks/>.
*/
#include "BibTeXFile.h"

#include <QFile>
#include <QTextCodec>
#include <cctype>

BibTeXFile::Entry::Type BibTeXFile::Entry::type() const
{
	if (_type.toLower() == QString::fromLatin1("comment"))
		return COMMENT;
	if (_type.toLower() == QString::fromLatin1("preamble"))
		return PREAMBLE;
	if (_type.toLower() == QString::fromLatin1("string"))
		return STRING;
	return NORMAL;
}

QString BibTeXFile::Entry::howPublished() const
{
	if (_cache.valid) {
		return _cache.howPublished;
	}
	if (hasField(QString::fromLatin1("howpublished"))) {
		return value(QString::fromLatin1("howpublished"));
	}
	if (hasField(QStringLiteral("journal"))) {
		return value(QString::fromLatin1("journal"));
	}
	return value(QString::fromLatin1("booktitle"));
}

QString BibTeXFile::Entry::value(const QString & key) const
{
	QString retVal;
	for (QMap<QString, QString>::const_iterator it = _fields.constBegin(); it != _fields.constEnd(); ++it) {
		if (QString::compare(key, it.key(), Qt::CaseInsensitive) == 0) {
			retVal = it.value();
			break;
		}
	}
	// strip surrounding {} (if any)
	if (retVal.startsWith(QLatin1Char('{')) && retVal.endsWith(QLatin1Char('}')))
		retVal = retVal.mid(1, retVal.length() - 2);
	// or surrounding "" (if any)
	else if (retVal.startsWith(QLatin1Char('"')) && retVal.endsWith(QLatin1Char('"')))
		retVal = retVal.mid(1, retVal.length() - 2);
	return retVal;
}

bool BibTeXFile::Entry::hasField(const QString & key) const
{
	for (QMap<QString, QString>::const_iterator it = _fields.constBegin(); it != _fields.constEnd(); ++it) {
		if (QString::compare(key, it.key(), Qt::CaseInsensitive) == 0)
			return true;
	}
	return false;
}

void BibTeXFile::Entry::updateCache()
{
	// invalidate cache so that all subsequent method calls actually search the
	// fields
	_cache.valid = false;
	_cache.author = author();
	_cache.howPublished = howPublished();
	_cache.title = title();
	_cache.year = year();
	_cache.valid = true;
}

bool BibTeXFile::load(const QString & filename)
{
	QFile file(filename);
	_entries.clear();
	if (!file.open(QFile::ReadOnly | QFile::Text))
		return false;
	return loadData(file.readAll());
}

bool BibTeXFile::loadData(const QByteArray & content)
{
	_entries.clear();
	QTextCodec * codec = QTextCodec::codecForName("utf-8");
	size_type curPos = 0;

	// FIXME: Encoding detection
	if (!codec)
		return false;

	do {
		Entry e(this);
		curPos = readEntry(e, content, curPos, codec);
		if (curPos > 0) {
			e.updateCache();
			_entries.append(e);
		}
		// DEBUG
	} while(curPos > 0);

	return true;
}

template <class S, class C> BibTeXFile::size_type findBlock(const S & content, const BibTeXFile::size_type from, const C & startDelim, const C & endDelim, const C & escapeChar)
{
	using size_type = BibTeXFile::size_type;
	// Blocks are enclosed in {}.
	if (content[from] != startDelim)
		return -1;

	size_type open = 1;
	size_type i{from + 1};
	bool escaped = false;
	for (i = from + 1; i < content.size() && open > 0; ++i) {
		if (escaped) escaped = false;
		else if (content[i] == escapeChar) escaped = true;
		// put endDelim check before startDelim check to ensure proper handling
		// of startDelim == endDelim (e.g., '"')
		else if (content[i] == endDelim) --open;
		else if (content[i] == startDelim) ++open;
	}
	if (open == 0) return i - 1;
	return -1;
}

inline BibTeXFile::size_type findBlock(const QByteArray & content, const BibTeXFile::size_type from, char startDelim = '{', char endDelim = '}', char escapeChar = 0)
{
	return findBlock<QByteArray, char>(content, from, startDelim, endDelim, escapeChar);
}

inline BibTeXFile::size_type findBlock(const QString & content, const BibTeXFile::size_type from, const QChar & startDelim = QChar::fromLatin1('{'), const QChar & endDelim = QChar::fromLatin1('}'), const QChar & escapeChar = QChar())
{
	return findBlock<QString, QChar>(content, from, startDelim, endDelim, escapeChar);
}

namespace {
using BibPosition = BibTeXFile::size_type;
struct BibRecord {BibPosition start{-1},typeFirst{0},typeLast{0},open{0},end{-1};};
BibPosition recordEnd(const QByteArray &content,BibPosition from) {
  if(content[from]=='{')return findBlock(content,from);
  // Parentheses in quoted/braced field values do not close the outer record.
  int braces=0;bool quoted=false;
  for(BibPosition at=from+1;at<content.size();++at){const char ch=content[at];
    if(ch=='\\'&&at+1<content.size()){++at;continue;}
    if(ch=='{')++braces;else if(ch=='}'){if(--braces<0)return -1;}
    else if(!braces&&ch=='"')quoted=!quoted;
    else if(!braces&&!quoted&&ch==')')return at;
  }
  return -1;
}
BibRecord nextRecord(const QByteArray &content,BibPosition from) {
  while(from<content.size()){
    const auto first=content.indexOf('@',from);if(first<0)return {};
    BibPosition at=first+1;while(at<content.size()&&std::isspace(static_cast<unsigned char>(content[at])))++at;
    const auto typeFirst=at;
    // BibTeX identifiers may contain digits and punctuation (custom dataset
    // types commonly do). Whitespace and these grammar delimiters end them.
    static const QByteArray delimiters("\"#%'(),={}");
    if(at<content.size()&&std::isdigit(static_cast<unsigned char>(content[at]))){from=first+1;continue;}
    while(at<content.size()&&!std::isspace(static_cast<unsigned char>(content[at]))&&!delimiters.contains(content[at]))++at;const auto last=at;
    while(at<content.size()&&std::isspace(static_cast<unsigned char>(content[at])))++at;
    if(last==typeFirst||at>=content.size()||(content[at]!='{'&&content[at]!='(')){from=first+1;continue;}
    const auto end=recordEnd(content,at);if(end<0)return {};
    return {first,typeFirst,last,at,end};
  }
  return {};
}
}

QString BibTeXFile::entrySource(const QString &content,const QString &key) {
  const auto bytes=content.toUtf8(),expected=key.toUtf8();BibPosition at=0;
  while(at<bytes.size()){
    const auto record=nextRecord(bytes,at);if(record.start<0)return {};
    const auto type=bytes.mid(record.typeFirst,record.typeLast-record.typeFirst).toLower();
    const auto comma=bytes.indexOf(',',record.open+1);
    if(type!="comment"&&type!="preamble"&&type!="string"&&comma>=0&&comma<record.end&&bytes.mid(record.open+1,comma-record.open-1).trimmed()==expected)
      return QString::fromUtf8(bytes.mid(record.start,record.end-record.start+1)).left(12000);
    at=record.end+1;
  }
  return {};
}

// static
BibTeXFile::size_type BibTeXFile::readEntry(Entry & e, const QByteArray & content, const size_type startPos, const QTextCodec * codec)
{
	const auto record=nextRecord(content,startPos);
	if(record.start<0)return -1;
	e._type=codec->toUnicode(content.mid(record.typeFirst,record.typeLast-record.typeFirst));
	const size_type start=record.open+1,end=record.end;
	QByteArray block = content.mid(start, end - start);

	switch (e.type()) {
	case Entry::COMMENT:
		e._key = codec->toUnicode(block);
		break;
	case Entry::PREAMBLE:
		e._key = codec->toUnicode(block);
		break;
	case Entry::STRING:
		// FIXME
		parseFields(e, codec->toUnicode(block));
		break;
	case Entry::NORMAL:
		parseEntry(e, codec->toUnicode(block));
		break;
	}

	return end + 1;
}

//static
void BibTeXFile::parseEntry(Entry & e, const QString & block)
{
	size_type pos = block.indexOf(QChar::fromLatin1(','));
	e._key = block.mid(0, pos).trimmed();
	if (pos == -1) return;

	parseFields(e, block, pos + 1);
}

void BibTeXFile::parseFields(BibTeXFile::Entry & e, const QString & block, const size_type startPos)
{
	QChar startDelim, endDelim;
	size_type pos{startPos};

	do {
		size_type start{pos};
		pos = block.indexOf(QChar::fromLatin1('='), start);
		if (pos < 0) break;
		const QString key = block.mid(start, pos - start).trimmed();
		QString val;

		start = -1;

		// Skip initial whitespace
		size_type i;
		for (i = pos + 1; i < block.size() && start < 0; ++i) {
			if (!block[i].isSpace()) start = i;
		}

		for (i = start; i < block.size(); ++i) {
			if (block[i] == QChar::fromLatin1(',')) {
				// Skip ',' and break out of the loop
				++i;
				break;
			}
			if (block[i] == QChar::fromLatin1('{')) {
				startDelim = QChar::fromLatin1('{');
				endDelim = QChar::fromLatin1('}');
			}
			else if (block[i] == QChar::fromLatin1('"')) {
				startDelim = QChar::fromLatin1('"');
				endDelim = QChar::fromLatin1('"');
			}
			else {
				val += block[i];
				continue;
			}

			size_type end = findBlock(block, i, startDelim, endDelim);
			if (end < 0) {
				val += block.mid(i);
				i = block.size();
			}
			else {
				val += block.mid(i, end - i + 1);
				i = end;
			}
		}
		e._fields[key] = val.trimmed();
		pos = i;
	} while (pos >= 0 && pos + 1 < block.size());
}

unsigned int BibTeXFile::numEntries() const
{
	// Only count "normal" entries
	unsigned int retVal = 0;
	for (int i = 0; i < _entries.size(); ++i) {
		if (_entries[i].type() == Entry::NORMAL) ++retVal;
	}
	return retVal;
}

QMap<QString, QString> BibTeXFile::strings() const
{
	QMap<QString, QString> rv;

	for (const Entry & e : _entries) {
		if (e.type() == Entry::STRING) {
#if QT_VERSION < QT_VERSION_CHECK(5, 15, 0)
			rv.unite(e._fields);
#else
			rv.insert(e._fields);
#endif
		}
	}
	return rv;
}

const BibTeXFile::Entry & BibTeXFile::entry(const unsigned int idx) const
{
	unsigned int j = 0;
	for (int i = 0; i < _entries.size(); ++i) {
		if (_entries[i].type() != Entry::NORMAL) continue;
		if (j == idx) return _entries[i];
		++j;
	}
	// We should never get here
	static BibTeXFile::Entry e(nullptr);
	return e;
}
