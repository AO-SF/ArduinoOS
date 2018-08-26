#ifndef KSTR_H
#define KSTR_H

typedef char * KStr;

KStr kstrAllocStatic(char *staticBuffer);
KStr kstrallocCopy(const char *src);

void kstrFree(KStr str);

char *kstrGetC(KStr str);

#endif
