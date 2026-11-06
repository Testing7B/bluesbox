/* OpenPocket X — minimal string header (implementation: felucca-bsp libc.c, linked through openpocket.c) */
#pragma once
void *memset(void *d, int c, unsigned n);
void *memcpy(void *d, const void *s, unsigned n);
int memcmp(const void *a, const void *b, unsigned n);   /* felucca-bsp libc.c */
