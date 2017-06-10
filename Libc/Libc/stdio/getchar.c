//
//  getchar.c
//  BetaOS
//
//  Created by Adam Kopeć on 2/9/16.
//  Copyright © 2016 Adam Kopeć. All rights reserved.
//

#include <stdio.h>
#include <Modules/PS2Controller.h>

int getchar() {
    // Get in queue for key down
    // Suspend until the key presses
    // Return the key
    for (; ;) {
        int ch = pollchar();
        if (ch == LEFT_CTRL_KEY || ch == RIGHT_CTRL_KEY) {
            // Check for sys calls
            if (getchar() == 'C') {
                // Send kill to process
                return 0xFF;
            }
        }
        if (ch == '\r' || ch =='\n' || (ch >= ' ' && ch <= 0x7F) || ch=='\t' || ch == '\b') {
            return ch;
        }
    }
}