#pragma once

#include <stdint.h>
#include <stddef.h>

/**
 * Diagnostic Trouble Code (DTC) Information Entry
 */
struct DtcInfo {
    const char* code;        // e.g. "P0171"
    const char* title;       // e.g. "System Too Lean (Bank 1)"
    const char* category;    // e.g. "Fuel & Air Metering"
    const char* meaning;     // What the ECU is detecting
    const char* inspection;  // What physical items to check
    const char* repair;      // Most common repair / fix
};

class DtcDatabase {
public:
    /**
     * Finds DTC information by 5-character string (e.g. "P0300", "P0171").
     * If code is not in the explicit dictionary, dynamically generates an
     * SAE J2012 standard diagnostic classification.
     */
    static DtcInfo lookup(const char* code);
};
