//
//  HDA.swift
//  Kernel
//
//  Created by Adam Kopeć on 12/8/17.
//  Copyright © 2017-2018 Adam Kopeć. All rights reserved.
//

import Addressing
import Loggable

final class HDA: PCIModule {
    var Name: String = "HDA"
    var description: String {
        return "Intel High Definition Audio Device"
    }
    static var CompatibleDevices: [PCIDevice] = [(0x8086, 0x2668), (0x8086, 0x27D8), (0x1002, 0x4383), (0x15AD, 0x1977)]
    static var CompatibleClasses: [PCIClass]? = nil
    
    init?(pci: PCI) {
        Log("Found Intel HDA", level: .Verbose)
    }
}
