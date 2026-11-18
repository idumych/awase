/*
 * Copyright 2026, Ian Dumych, idumych@gmail.com.
 * SPDX-License-Identifier: MIT
 */
#ifndef AWASE_SHA256_H
#define AWASE_SHA256_H

// SHA-256 (FIPS 180-4). The coordinator wants each package's hash before
// its bytes, so the client hashes the file first.

#include <stddef.h>
#include <stdint.h>

#include <string>


namespace awase {


class Sha256 {
public:
								Sha256();

			void				Update(const void* data, size_t length);
			std::string			HexDigest();	// finishes the hash

	static	bool				HashFile(const std::string& path,
									std::string& hex);

private:
			void				_Block(const uint8_t* block);

			uint32_t			fState[8];
			uint64_t			fLength;
			uint8_t				fBuffer[64];
			size_t				fUsed;
};


}	// namespace awase


#endif	// AWASE_SHA256_H
