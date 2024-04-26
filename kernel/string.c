#include "types.h"

void *
memset(void *addr, int c, uint size)
{
  if(!addr)
    return 0;

  char *caddr = (char *)addr;
  int i;
  for(i = 0; i < size; i++)
    *(caddr + i) = c;

  return addr;
}

void*
memmove(void *dst, const void *src, uint n)
{
  const char *s;
  char *d;

  if(n == 0)
    return dst;
  
  s = src;
  d = dst;
  if(s < d && s + n > d){
    s += n;
    d += n;
    while(n-- > 0)
      *--d = *--s;
  } else
    while(n-- > 0)
      *d++ = *s++;

  return dst;
}

int strncmp(const char *p, const char *q, uint n)
{
  while(n > 0 && *p && *p == *q)
    n--, p++, q++;
  if(n == 0)
    return 0;
  return (uchar)*p - (uchar)*q;
}

char*
strncpy(char *s, const char *t, int n)
{
  char *os;

  os = s;
  while(n-- > 0 && (*s++ = *t++) != 0)
    ;
  while(n-- > 0)
    *s++ = 0;
  return os;
}
/*
* 在字符串中查找指定字符的第一个出现位置
*/
char*
strchr(const char *s, char c)
{
  for(; *s; s++)
    if(*s == c)
      return (char*)s;
  return 0;
}

int
strlen(const char *s)
{
  int n;

  for(n = 0; s[n]; n++)
    ;
  return n;
}

/*
 * 此函数实现了将宽字符源字符串按照单字节字符编码
 （如ASCII）转换为长度不超过len的目标字符串，
  并在目标字符串未填满时用空字符补足剩余空间。
*/
void snstr(char *dst, uint16 const *src, int len) {
  while (len -- && *src) {
    *dst++ = (uchar)(*src & 0xff);
    src ++;
  }
  while(len-- > 0)
    *dst++ = 0;
}