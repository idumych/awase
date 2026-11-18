/*
 * Copyright 2026, Ian Dumych, idumych@gmail.com.
 * SPDX-License-Identifier: MIT
 */


#include "Json.h"

#include <stdio.h>
#include <stdlib.h>


namespace awase {


static const Json kNullJson;


class JsonParser {
public:
	JsonParser(const std::string& text)
		:
		fText(text),
		fPos(0),
		fDepth(0)
	{
	}

	bool ParseDocument(Json& out)
	{
		if (!_Value(out))
			return false;
		_Space();
		return fPos == fText.size();
	}

private:
	void _Space()
	{
		while (fPos < fText.size() && (fText[fPos] == ' ' || fText[fPos] == '\t'
				|| fText[fPos] == '\n' || fText[fPos] == '\r'))
			fPos++;
	}

	bool _Literal(const char* word)
	{
		size_t length = 0;
		while (word[length] != '\0')
			length++;
		if (fText.compare(fPos, length, word) != 0)
			return false;
		fPos += length;
		return true;
	}

	bool _Value(Json& out)
	{
		if (++fDepth > 64)
			return false;
		_Space();
		if (fPos >= fText.size())
			return false;
		bool ok;
		char c = fText[fPos];
		if (c == '{')
			ok = _Object(out);
		else if (c == '[')
			ok = _Array(out);
		else if (c == '"') {
			out.fType = Json::kString;
			ok = _String(out.fString);
		} else if (_Literal("true")) {
			out.fType = Json::kBool;
			out.fBool = true;
			ok = true;
		} else if (_Literal("false")) {
			out.fType = Json::kBool;
			out.fBool = false;
			ok = true;
		} else if (_Literal("null")) {
			out.fType = Json::kNull;
			ok = true;
		} else
			ok = _Number(out);
		fDepth--;
		return ok;
	}

	bool _Number(Json& out)
	{
		const char* start = fText.c_str() + fPos;
		char* end;
		double value = strtod(start, &end);
		if (end == start)
			return false;
		fPos += end - start;
		out.fType = Json::kNumber;
		out.fNumber = value;
		return true;
	}

	static void _Utf8(std::string& out, unsigned code)
	{
		if (code < 0x80)
			out += (char)code;
		else if (code < 0x800) {
			out += (char)(0xc0 | (code >> 6));
			out += (char)(0x80 | (code & 0x3f));
		} else if (code < 0x10000) {
			out += (char)(0xe0 | (code >> 12));
			out += (char)(0x80 | ((code >> 6) & 0x3f));
			out += (char)(0x80 | (code & 0x3f));
		} else {
			out += (char)(0xf0 | (code >> 18));
			out += (char)(0x80 | ((code >> 12) & 0x3f));
			out += (char)(0x80 | ((code >> 6) & 0x3f));
			out += (char)(0x80 | (code & 0x3f));
		}
	}

	bool _Hex4(unsigned& code)
	{
		if (fPos + 4 > fText.size())
			return false;
		code = 0;
		for (int i = 0; i < 4; i++) {
			char c = fText[fPos++];
			code <<= 4;
			if (c >= '0' && c <= '9')
				code |= c - '0';
			else if (c >= 'a' && c <= 'f')
				code |= c - 'a' + 10;
			else if (c >= 'A' && c <= 'F')
				code |= c - 'A' + 10;
			else
				return false;
		}
		return true;
	}

	bool _String(std::string& out)
	{
		fPos++;	// "
		out.clear();
		while (fPos < fText.size()) {
			char c = fText[fPos++];
			if (c == '"')
				return true;
			if (c != '\\') {
				out += c;
				continue;
			}
			if (fPos >= fText.size())
				return false;
			char e = fText[fPos++];
			switch (e) {
				case '"': out += '"'; break;
				case '\\': out += '\\'; break;
				case '/': out += '/'; break;
				case 'b': out += '\b'; break;
				case 'f': out += '\f'; break;
				case 'n': out += '\n'; break;
				case 'r': out += '\r'; break;
				case 't': out += '\t'; break;
				case 'u':
				{
					unsigned code;
					if (!_Hex4(code))
						return false;
					if (code >= 0xd800 && code < 0xdc00
						&& fText.compare(fPos, 2, "\\u") == 0) {
						fPos += 2;
						unsigned low;
						if (!_Hex4(low))
							return false;
						code = 0x10000 + ((code - 0xd800) << 10) + (low - 0xdc00);
					}
					_Utf8(out, code);
					break;
				}
				default:
					return false;
			}
		}
		return false;
	}

	bool _Array(Json& out)
	{
		fPos++;	// [
		out.fType = Json::kArray;
		_Space();
		if (fPos < fText.size() && fText[fPos] == ']') {
			fPos++;
			return true;
		}
		while (true) {
			Json item;
			if (!_Value(item))
				return false;
			out.fArray.push_back(item);
			_Space();
			if (fPos >= fText.size())
				return false;
			if (fText[fPos] == ',') {
				fPos++;
				continue;
			}
			if (fText[fPos] == ']') {
				fPos++;
				return true;
			}
			return false;
		}
	}

	bool _Object(Json& out)
	{
		fPos++;	// {
		out.fType = Json::kObject;
		_Space();
		if (fPos < fText.size() && fText[fPos] == '}') {
			fPos++;
			return true;
		}
		while (true) {
			_Space();
			if (fPos >= fText.size() || fText[fPos] != '"')
				return false;
			std::string key;
			if (!_String(key))
				return false;
			_Space();
			if (fPos >= fText.size() || fText[fPos] != ':')
				return false;
			fPos++;
			Json value;
			if (!_Value(value))
				return false;
			out.fObject[key] = value;
			_Space();
			if (fPos >= fText.size())
				return false;
			if (fText[fPos] == ',') {
				fPos++;
				continue;
			}
			if (fText[fPos] == '}') {
				fPos++;
				return true;
			}
			return false;
		}
	}

	const std::string&	fText;
	size_t				fPos;
	int					fDepth;
};


/*static*/ bool
Json::Parse(const std::string& text, Json& out)
{
	out = Json();
	JsonParser parser(text);
	return parser.ParseDocument(out);
}


/*static*/ std::string
Json::Quote(const std::string& text)
{
	std::string out = "\"";
	for (size_t i = 0; i < text.size(); i++) {
		unsigned char c = text[i];
		switch (c) {
			case '"': out += "\\\""; break;
			case '\\': out += "\\\\"; break;
			case '\n': out += "\\n"; break;
			case '\r': out += "\\r"; break;
			case '\t': out += "\\t"; break;
			default:
				if (c < 0x20) {
					char buffer[8];
					snprintf(buffer, sizeof(buffer), "\\u%04x", c);
					out += buffer;
				} else
					out += (char)c;
		}
	}
	return out + "\"";
}


const Json&
Json::operator[](const std::string& key) const
{
	if (fType != kObject)
		return kNullJson;
	std::map<std::string, Json>::const_iterator found = fObject.find(key);
	return found != fObject.end() ? found->second : kNullJson;
}


const Json&
Json::operator[](size_t index) const
{
	if (fType != kArray || index >= fArray.size())
		return kNullJson;
	return fArray[index];
}


size_t
Json::Size() const
{
	return fType == kArray ? fArray.size()
		: fType == kObject ? fObject.size() : 0;
}


std::string
Json::String(const std::string& fallback) const
{
	return fType == kString ? fString : fallback;
}


double
Json::Number(double fallback) const
{
	return fType == kNumber ? fNumber : fallback;
}


bool
Json::Bool(bool fallback) const
{
	return fType == kBool ? fBool : fallback;
}


}	// namespace awase
