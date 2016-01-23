//
//  error.c
//  OS
//
//  Created by Adam Kopeć on 1/16/16.
//  Copyright © 2016 Adam Kopeć. All rights reserved.
//

#include <stdio.h>
#include <stdlib.h>
#include <time.h>

void error(const char* errormsg) {
    gettime();
    printf("\nError: %d:%s%d:%s%d %s %d/%d/%d\n%s\n", hour, zerom, minute, zeros, second, pmam, month, day, year, errormsg);
}
