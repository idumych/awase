/*
 * Copyright 2026, Ian Dumych, idumych@gmail.com.
 * SPDX-License-Identifier: MIT
 */
#ifndef AWASE_JSON_H
#define AWASE_JSON_H

// Just enough JSON for the coordinator's replies: parse into Json values,
// and quote strings for the few bodies the client sends.

#include <map>
#include <string>
#include <vector>


namespace awase {


class Json {
public:
	enum Type { kNull, kBool, kNumber, kString, kArray, kObject };

								Json() : fType(kNull), fNumber(0), fBool(false) {}

	static	bool				Parse(const std::string& text, Json& out);
	static	std::string			Quote(const std::string& text);

			Type				GetType() const { return fType; }
			bool				IsNull() const { return fType == kNull; }

			// Lookups return a null value when the key or index is missing or
			// the type is wrong, so callers can chain them.
			const Json&			operator[](const std::string& key) const;
			const Json&			operator[](size_t index) const;
			size_t				Size() const;

			std::string			String(const std::string& fallback = "") const;
			double				Number(double fallback = 0) const;
			bool				Bool(bool fallback = false) const;

private:
			friend class JsonParser;

			Type				fType;
			double				fNumber;
			bool				fBool;
			std::string			fString;
			std::vector<Json>	fArray;
			std::map<std::string, Json> fObject;
};


}	// namespace awase


#endif	// AWASE_JSON_H
