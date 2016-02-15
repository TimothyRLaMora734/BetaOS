//
//  stdio.h
//  OS
//
//  Created by Adam Kopeć on 12/8/15.
//  Copyright © 2015 Adam Kopeć. All rights reserved.
//

#ifndef _STDIO_H
#define _STDIO_H 1

#include <sys/cdefs.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

int printf(const char*, ...);
int putchar(int);
int puts(const char*);
int getchar(void);
char* gets(char*);

#ifdef __cplusplus
}
#endif

#endif /* stdio_h */
