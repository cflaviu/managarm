#pragma once

namespace thor {

void unpoisonKasanShadow(void *pointer, size_t size);
void poisonKasanShadow(void *pointer, size_t size);
void cleanKasanShadow(void *pointer, size_t size);

void validateKasanClean(void *pointer, size_t size);

struct Continuation {
	void *sp;
};

void scrubStackFrom(uintptr_t top, Continuation cont);

} // namespace thor
