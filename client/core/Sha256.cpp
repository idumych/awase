/*
 * Copyright 2026, Ian Dumych, idumych@gmail.com.
 * SPDX-License-Identifier: MIT
 */


#include "Sha256.h"

#include <stdio.h>
#include <string.h>


namespace awase {


static const uint32_t kRound[64] = {
	0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1,
	0x923f82a4, 0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
	0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786,
	0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
	0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147,
	0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
	0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b,
	0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
	0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a,
	0x5b9cca4f, 0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
	0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2,
};


static inline uint32_t
Rotate(uint32_t x, int n)
{
	return (x >> n) | (x << (32 - n));
}


Sha256::Sha256()
	:
	fLength(0),
	fUsed(0)
{
	static const uint32_t kInitial[8] = {
		0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
		0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19,
	};
	memcpy(fState, kInitial, sizeof(fState));
}


void
Sha256::_Block(const uint8_t* block)
{
	uint32_t w[64];
	for (int i = 0; i < 16; i++) {
		w[i] = (uint32_t)block[i * 4] << 24 | (uint32_t)block[i * 4 + 1] << 16
			| (uint32_t)block[i * 4 + 2] << 8 | block[i * 4 + 3];
	}
	for (int i = 16; i < 64; i++) {
		uint32_t s0 = Rotate(w[i - 15], 7) ^ Rotate(w[i - 15], 18)
			^ (w[i - 15] >> 3);
		uint32_t s1 = Rotate(w[i - 2], 17) ^ Rotate(w[i - 2], 19)
			^ (w[i - 2] >> 10);
		w[i] = w[i - 16] + s0 + w[i - 7] + s1;
	}

	uint32_t a = fState[0], b = fState[1], c = fState[2], d = fState[3];
	uint32_t e = fState[4], f = fState[5], g = fState[6], h = fState[7];
	for (int i = 0; i < 64; i++) {
		uint32_t s1 = Rotate(e, 6) ^ Rotate(e, 11) ^ Rotate(e, 25);
		uint32_t choice = (e & f) ^ (~e & g);
		uint32_t t1 = h + s1 + choice + kRound[i] + w[i];
		uint32_t s0 = Rotate(a, 2) ^ Rotate(a, 13) ^ Rotate(a, 22);
		uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
		uint32_t t2 = s0 + majority;
		h = g;
		g = f;
		f = e;
		e = d + t1;
		d = c;
		c = b;
		b = a;
		a = t1 + t2;
	}
	fState[0] += a;
	fState[1] += b;
	fState[2] += c;
	fState[3] += d;
	fState[4] += e;
	fState[5] += f;
	fState[6] += g;
	fState[7] += h;
}


void
Sha256::Update(const void* data, size_t length)
{
	const uint8_t* bytes = (const uint8_t*)data;
	fLength += length;
	while (length > 0) {
		size_t take = 64 - fUsed;
		if (take > length)
			take = length;
		memcpy(fBuffer + fUsed, bytes, take);
		fUsed += take;
		bytes += take;
		length -= take;
		if (fUsed == 64) {
			_Block(fBuffer);
			fUsed = 0;
		}
	}
}


std::string
Sha256::HexDigest()
{
	uint64_t bits = fLength * 8;
	uint8_t pad = 0x80;
	Update(&pad, 1);
	uint8_t zero = 0;
	while (fUsed != 56)
		Update(&zero, 1);
	uint8_t length[8];
	for (int i = 0; i < 8; i++)
		length[i] = (uint8_t)(bits >> (56 - i * 8));
	Update(length, 8);

	char hex[65];
	for (int i = 0; i < 8; i++)
		snprintf(hex + i * 8, 9, "%08x", fState[i]);
	return std::string(hex, 64);
}


/*static*/ bool
Sha256::HashFile(const std::string& path, std::string& hex)
{
	FILE* file = fopen(path.c_str(), "rb");
	if (file == NULL)
		return false;
	Sha256 hash;
	uint8_t buffer[65536];
	size_t read;
	while ((read = fread(buffer, 1, sizeof(buffer), file)) > 0)
		hash.Update(buffer, read);
	bool ok = !ferror(file);
	fclose(file);
	if (ok)
		hex = hash.HexDigest();
	return ok;
}


}	// namespace awase
