//
//  IOAPIC.swift
//  Kernel
//
//  Created by Adam Kopeć on 3/6/18.
//  Copyright © 2018 Adam Kopeć. All rights reserved.
//

import Addressing
import CustomArrays
import Loggable

final class IOAPIC: Loggable, InterruptController {
    let Name = "I/O APIC"
    fileprivate let registerSelect: UnsafeMutablePointer<UInt32>
    fileprivate let registerData: UnsafeMutablePointer<UInt32>
    
    private let ioapicTable: MADT.IOApicTable
    private let registerBase: Address
    private let overrideTable: [MADT.InterruptSourceOverrideTable]
    
    init?(ioapicTable: MADT.IOApicTable, intSourceOverrides: [MADT.InterruptSourceOverrideTable]) {
        self.ioapicTable = ioapicTable
        registerBase = Address(ioapicTable.ioApicAddress, size: 0x20)
        registerSelect = UnsafeMutablePointer<UInt32>(bitPattern: registerBase.virtual)!
        registerData = UnsafeMutablePointer<UInt32>(bitPattern: registerBase.virtual + 0x10)!
        
        overrideTable = intSourceOverrides
    }
    
    func enableIRQ(_ irq: Int) {
        var register = redirectionRegisterFor(irq: irq)
        if let entry = overrideEntryFor(irq: irq) {
            register = redirectionRegisterFor(irq: Int(entry.globalInterrupt))
        }
        
        Log("Enabling IRQ: \(irq)", level: .Verbose)
        let vector = UInt8(irq) + 0x50
        var data = IORedirectionRegister()
        data.idtVector = vector
        data.deliveryMode = .fixed
        data.destinationMode = .physical
        data.inputPinPolarity = .activeHigh
        data.triggerMode = .edge
        data.maskInterrupt = false
        data.destinationField = 0
        writeWideRegister(register, data: data)
    }
    
    func disableIRQ(_ irq: Int) {
        var register = redirectionRegisterFor(irq: irq)
        if let entry = overrideEntryFor(irq: irq) {
            register = redirectionRegisterFor(irq: Int(entry.globalInterrupt))
        }
        
        Log("Disabling IRQ: \(irq)", level: .Verbose)
        var data = readWideRegister(register)
        data.maskInterrupt = true
        writeWideRegister(register, data: data)
    }
    
    func getIRQ(from vector: Int) -> Int {
        return vector - 0x50
    }
    
    func disableAllIRQs() {
        Log("Fatal Error, \(#function) should never be called", level: .Error)
        fatalError()
    }
    
    func ackIRQ(_ irq: Int) {
        Log("Fatal Error, \(#function) should never be called", level: .Error)
        fatalError()
    }
    
    private func overrideEntryFor(irq: Int) -> MADT.InterruptSourceOverrideTable? {
        for entry in overrideTable {
            if entry.sourceIRQ == UInt8(irq) {
                return entry
            }
        }
        return nil
    }
    
    private func redirectionRegisterFor(irq: Int) -> UInt8 {
        let irqEntry = UInt8(irq)
        return UInt8(0x10 + (irqEntry * 2))
    }
}

fileprivate extension IOAPIC {
    enum IORedirectionRegisterBits: Int {
        case destinationMode = 11
        case maskInterrupt = 16
    }
    
    // Bits 10:8
    enum DeliveryMode: Int {
        case fixed = 0b000
        case lowestPriority = 0b001
        case SMI = 0b010
        case NMI = 0b100
        case INIT = 0b101
        case ExtINT = 0b111
    }
    
    // Bit 11
    enum DestinationMode: Int {
        case physical = 0
        case logical = 1
    }

    // Bit 12 (RO)
    enum DeliveryStatus: Int {
        case idle = 0
        case sendPending = 1
    }

    // Bit 13
    enum InputPinPolarity: Int {
        case activeHigh = 0
        case activeLow = 1
    }
    
    // Bit 14 (RO)
    enum RemoteIRR: Int {
        case eoiReceived = 0
        case levelInterruptAccepted = 1
    }
    
    // Bit 15
    enum TriggerMode: Int {
        case edge = 0
        case level = 1
    }
}

fileprivate typealias IORedirectionRegister = BitArray
fileprivate extension IORedirectionRegister {
    var idtVector: UInt8 {
        get {
            return UInt8(self[0...7])
        } set(newValue) {
            self[0...7] = UInt64(newValue)
        }
    }
    
    var deliveryMode: IOAPIC.DeliveryMode {
        get {
            let mode = BitArray(self[8...10]).asInt
            return IOAPIC.DeliveryMode(rawValue: mode) ?? IOAPIC.DeliveryMode.fixed
        } set(newValue) {
            //self.replaceSubrange([8...10], with: BitArray(newValue.rawValue))
        }
    }
    
    var destinationMode: IOAPIC.DestinationMode {
        get {
            let mode = self[11]
            return IOAPIC.DestinationMode(rawValue: mode) ?? IOAPIC.DestinationMode.physical
        } set(newValue) {
            self[11] = newValue.rawValue
        }
    }
    
    var deliveryStatus: IOAPIC.DeliveryStatus {
        return IOAPIC.DeliveryStatus(rawValue: self[12]) ?? IOAPIC.DeliveryStatus.idle
    }
    
    var inputPinPolarity: IOAPIC.InputPinPolarity {
        get {
            let mode = self[13]
            return IOAPIC.InputPinPolarity(rawValue: mode) ?? IOAPIC.InputPinPolarity.activeHigh
        } set(newValue) {
            self[13] = newValue.rawValue
        }
    }
    
    var remoteIRR: IOAPIC.RemoteIRR {
        let value = self[14]
        return IOAPIC.RemoteIRR(rawValue: value) ?? IOAPIC.RemoteIRR.eoiReceived
    }
    
    var triggerMode: IOAPIC.TriggerMode {
        get {
            let value = self[15]
            return IOAPIC.TriggerMode(rawValue: value) ?? IOAPIC.TriggerMode.edge
        } set(newValue) {
            self[15] = newValue.rawValue
        }
    }
    
    var maskInterrupt: Bool {
        get { return self[16] == 1 }
        set(newValue) { self[16] = newValue ? 1 : 0 }
    }
    
    var destinationField: UInt8 {
        get { return UInt8(self[56...63]) }
        set(newValue) {
            self[56...63] = UInt64(newValue)
        }
    }
}


fileprivate extension IOAPIC {
    /// Registers are 32bits wide and indexed using an 8 bit address
    /// WideRegisters are 64bits wide using 2 32bit reads at address
    /// and address+1
    func readRegister(_ register: UInt8) -> UInt32 {
        let f = UInt32(register)
        registerSelect.pointee = f
        let data = registerData.pointee
        return data
    }
    
    func writeRegister(_ register: UInt8, data: UInt32) {
        let f = UInt32(register)
        registerSelect.pointee = f
        registerData.pointee = data
    }
    
    func readWideRegister(_ register: UInt8) -> IORedirectionRegister {
        let lo = UInt64(readRegister(register))
        let hi = UInt64(readRegister(register + 1)) << 32
        return IORedirectionRegister(hi | lo)
    }
    
    func writeWideRegister(_ register: UInt8, data: IORedirectionRegister) {
        writeRegister(register + 0, data: UInt32(data[0...31]))
        writeRegister(register + 1, data: UInt32(data[32...63]))
    }
}

