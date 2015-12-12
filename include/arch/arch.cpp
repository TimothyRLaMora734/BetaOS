//
//  arch.cpp
//  OS
//
//  Created by Adam Kopeć on 12/10/15.
//  Copyright © 2015 Adam Kopeć. All rights reserved.
//

#include "arch.hpp"

void cpuid() {
    
}

void reboot() {
    unsigned char good = 0x02;
    while ((good & 0x02) != 0)
        good = inb(0x64);
    outb(0x64, 0xFE);
}

void shutdown() {
    
}