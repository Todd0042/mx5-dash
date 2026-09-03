#include "DtcDatabase.h"
#include <string.h>
#include <stdio.h>

static const DtcInfo DTC_TABLE[] = {
    // Fuel & Air Metering
    {"P0171", "System Too Lean (Bank 1)", "Fuel & Air Metering",
     "ECU detects excess oxygen in exhaust; positive fuel trim is maxed out (+20%+).",
     "1. Inspect intake bellows/accordion boot for tears/splits.\n2. Check PCV hose and valve for cracks.\n3. Spray carb cleaner around intake manifold to find vacuum leaks.\n4. Inspect MAF sensor wire for dirt/oil contamination.\n5. Test fuel rail pressure under load.",
     "Clean MAF with CRC MAF Cleaner, replace cracked intake hose, PCV hose, or faulty Bank 1 O2 sensor."},

    {"P0172", "System Too Rich (Bank 1)", "Fuel & Air Metering",
     "ECU detects excessive unburned fuel in exhaust; negative fuel trim is maxed out (-20%-).",
     "1. Check engine oil for strong gasoline smell (high-pressure pump seal leak).\n2. Inspect air filter for heavy blockage.\n3. Inspect spark plugs for fuel fouling (stuck-open injector).\n4. Check EVAP purge solenoid (stuck open pulling raw fuel vapor).",
     "Replace leaking direct injector, replace stuck EVAP purge valve, or change oil & HPFP seal."},

    {"P0101", "MAF Sensor Circuit Range/Perf", "Fuel & Air Metering",
     "Mass Air Flow signal is inconsistent with engine RPM, throttle angle, and calculated load.",
     "1. Inspect MAF sensor electrical connector and wiring pins.\n2. Check intake ducting between MAF and throttle body for air leaks.\n3. Check air filter box seating.",
     "Clean MAF sensing element using dedicated MAF spray; replace MAF sensor if signal drifts."},

    {"P0102", "MAF Sensor Circuit Low Input", "Fuel & Air Metering",
     "MAF sensor signal voltage is below normal minimum threshold (< 0.2V).",
     "1. Check 12V power supply and sensor ground at MAF plug.\n2. Check signal wire continuity to PCM.\n3. Inspect sensor connector for corrosion or backed-out pins.",
     "Repair broken wiring harness or replace failed MAF sensor."},

    {"P0106", "MAP Sensor Circuit Range/Perf", "Fuel & Air Metering",
     "Manifold Absolute Pressure sensor reading does not match barometric reference at key-on/idle.",
     "1. Inspect MAP sensor port for carbon/oil buildup.\n2. Check intake manifold vacuum at idle (18-21 in-Hg normal).\n3. Check MAP connector wiring.",
     "Clean MAP sensor vacuum port or replace faulty MAP sensor."},

    {"P0113", "Intake Air Temp High Input", "Fuel & Air Metering",
     "IAT sensor circuit indicates open circuit or short to voltage (indicates <-40°F reading).",
     "1. Inspect IAT connector (integrated into MAF assembly on Skyactiv).\n2. Check wiring harness resistance to PCM.\n3. Test IAT thermistor resistance with multimeter.",
     "Clean/reseat MAF/IAT connector or replace MAF/IAT assembly."},

    {"P0128", "Coolant Temp Below Thermostat Reg", "Cooling & Thermal",
     "Engine coolant temperature takes too long to reach closed-loop operating temp (180°F / 82°C).",
     "1. Feel upper radiator hose as car warms up (should stay cool until thermostat opens).\n2. Check coolant level in reservoir.\n3. Read live ECT sensor temperature stream.",
     "Replace stuck-open thermostat assembly and fresh coolant bleed."},

    // Direct Injection & Fuel Pressure
    {"P0087", "Fuel Rail/System Pressure Too Low", "High-Pressure Direct Injection",
     "Direct injection high-pressure fuel rail pressure is significantly below commanded target.",
     "1. Monitor live Fuel Rail Pressure (should be 4.0-20.0 MPa / 580-2900 PSI under throttle).\n2. Check low-pressure in-tank fuel pump pressure (50-60 PSI).\n3. Inspect camshaft HPFP follower lobe for mechanical wear.",
     "Replace high-pressure fuel pump (HPFP), camshaft follower, or low-pressure in-tank pump."},

    {"P0088", "Fuel Rail/System Pressure Too High", "High-Pressure Direct Injection",
     "Direct injection rail pressure exceeds upper safety limit.",
     "1. Check HPFP pressure regulator control valve harness.\n2. Test rail pressure sensor output voltage.\n3. Inspect for sticking spill valve in HPFP.",
     "Replace high-pressure fuel rail pressure regulator or fuel rail pressure sensor."},

    {"P0191", "Fuel Rail Pressure Sensor Range/Perf", "High-Pressure Direct Injection",
     "FRP sensor output is erratic, noisy, or unresponsive during engine acceleration.",
     "1. Check FRP sensor 5V reference, signal, and ground wires.\n2. Tap sensor gently while monitoring live voltage trace on scan tool.",
     "Replace fuel rail pressure sensor on the DI fuel rail."},

    // VVT & Camshaft Timing
    {"P0011", "Intake VVT Timing Over-Advanced (Bank 1)", "Valvetrain & VVT",
     "Intake camshaft phase angle is advanced beyond commanded target angle.",
     "1. Check engine oil level and condition (Skyactiv VVT requires clean 0W-20 oil pressure).\n2. Remove and test Intake Oil Control Valve (OCV) solenoid for varnish/sludge.\n3. Check OCV solenoid resistance (6.9-7.9 ohms).",
     "Change engine oil & OEM filter, clean/replace intake VVT Oil Control Valve solenoid."},

    {"P0012", "Intake VVT Timing Over-Retarded (Bank 1)", "Valvetrain & VVT",
     "Intake camshaft phase angle fails to advance when commanded by PCM.",
     "1. Check for low oil pressure or sludged oil passages.\n2. Inspect VVT solenoid spool valve for sticking.\n3. Check intake camshaft sprocket phaser actuator.",
     "Clean/replace VVT solenoid; replace VVT camshaft phaser actuator if mechanically locked."},

    {"P0014", "Exhaust VVT Timing Over-Advanced", "Valvetrain & VVT",
     "Exhaust camshaft phase angle is advanced beyond target.",
     "1. Check exhaust VVT oil control valve solenoid.\n2. Verify oil viscosity (0W-20 full synthetic).",
     "Clean/replace exhaust VVT solenoid and change engine oil."},

    {"P0016", "Crank-Cam Correlation (Bank 1 Sensor A)", "Timing & Synchronization",
     "PCM detects mechanical timing misalignment between crankshaft and intake camshaft.",
     "1. Inspect timing chain tensioner and guide wear.\n2. Check for stretched timing chain.\n3. Inspect CKP and CMP reluctor wheels for physical damage.",
     "Inspect/replace timing chain assembly, tensioner, and inspect camshaft sprockets."},

    // Ignition & Cylinder Misfires
    {"P0300", "Random/Multiple Cylinder Misfire", "Ignition & Combustion",
     "PCM detected misfires occurring across multiple cylinders during the drive cycle.",
     "1. Check Mode $06 cylinder misfire counters to identify which cylinders are firing.\n2. Inspect spark plugs for wear or carbon buildup (replace every 75k miles).\n3. Check fuel pressure and inspect for vacuum leaks affecting all cylinders.",
     "Replace spark plugs (Laser Iridium), replace failing ignition coil, or clean dirty intake valves."},

    {"P0301", "Cylinder 1 Misfire Detected", "Ignition & Combustion",
     "PCM detected rotational deceleration on Cylinder 1 power stroke.",
     "1. Swap Ignition Coil #1 with Coil #2 (see if misfire follows coil to Cyl 2).\n2. Inspect Spark Plug #1 condition and gap (0.040-0.044 in).\n3. Perform compression test on Cyl 1.",
     "Replace Cylinder 1 ignition coil pack or spark plug; replace DI injector if fuel-related."},

    {"P0302", "Cylinder 2 Misfire Detected", "Ignition & Combustion",
     "PCM detected rotational deceleration on Cylinder 2 power stroke.",
     "1. Swap Ignition Coil #2 with Coil #1 (see if misfire moves).\n2. Inspect Spark Plug #2 for carbon fouling or wet fuel.\n3. Check wiring harness to Coil #2.",
     "Replace Cylinder 2 ignition coil pack or spark plug."},

    {"P0303", "Cylinder 3 Misfire Detected", "Ignition & Combustion",
     "PCM detected rotational deceleration on Cylinder 3 power stroke.",
     "1. Swap Ignition Coil #3 with Coil #1.\n2. Inspect Spark Plug #3.\n3. Check DI fuel injector #3 pulse.",
     "Replace Cylinder 3 ignition coil pack or spark plug."},

    {"P0304", "Cylinder 4 Misfire Detected", "Ignition & Combustion",
     "PCM detected rotational deceleration on Cylinder 4 power stroke.",
     "1. Swap Ignition Coil #4 with Coil #1.\n2. Inspect Spark Plug #4.\n3. Check Cylinder 4 compression.",
     "Replace Cylinder 4 ignition coil pack or spark plug."},

    {"P0325", "Knock Sensor 1 Circuit Malfunction", "Ignition & Knock Control",
     "PCM detected abnormal electrical resistance or open circuit on the engine knock sensor.",
     "1. Inspect knock sensor harness under intake manifold for rodent chewing/chafing.\n2. Measure knock sensor resistance.\n3. Check knock sensor mounting bolt torque (critical: ~15-18 lb-ft).",
     "Repair damaged wiring harness or replace knock sensor."},

    {"P0335", "Crankshaft Position Sensor A Circuit", "Engine Synchronization",
     "PCM lost crankshaft position trigger signal during engine cranking or running.",
     "1. Check CKP sensor connector near crankshaft pulley/harmonic balancer.\n2. Check tone ring on crank pulley for missing/bent teeth.\n3. Check sensor air gap.",
     "Replace crankshaft position sensor (CKP) or repair connector pigtail."},

    // Catalytic Converter & Oxygen Sensors
    {"P0420", "Catalytic Converter System Efficiency Below Threshold", "Exhaust & Emissions",
     "Downstream O2 sensor switching mirrors upstream sensor, indicating depleted catalytic oxygen storage.",
     "1. Inspect exhaust manifold for cracks or gasket leaks before/after catalytic converter.\n2. Verify upstream O2 sensor is switching properly.\n3. Check for engine misfires or oil burning poisoning the catalyst.",
     "Fix exhaust leaks first; if catalytic substrate is depleted/melted, replace exhaust manifold/catalytic assembly."},

    {"P0421", "Warm-Up Catalytic Converter Efficiency (Bank 1)", "Exhaust & Emissions",
     "Integrated exhaust manifold warm-up catalytic converter oxygen storage efficiency is below threshold.",
     "1. Check for exhaust header leaks at cylinder head flange.\n2. Inspect downstream heated oxygen sensor (HO2S 1/2).\n3. Check Mode $06 TID $21 catalytic monitor test values.",
     "Repair exhaust manifold leak or replace Bank 1 manifold catalytic converter."},

    {"P013A", "O2 Sensor Slow Response - Rich to Lean (B1 S2)", "Oxygen Sensor",
     "Downstream post-cat O2 sensor response time is sluggish transitioning from rich to lean.",
     "1. Check downstream O2 sensor wiring for oil/water contamination.\n2. Inspect sensor heater circuit resistance (approx 5-15 ohms).\n3. Check for exhaust pinhole leaks near sensor bung.",
     "Replace downstream Heated Oxygen Sensor (Sensor 2) on midpipe."},

    {"P2096", "Post-Catalyst Fuel Trim System Too Lean (Bank 1)", "Fuel Trim Diagnostics",
     "Downstream O2 sensor indicates lean bias despite upstream fuel trim adjustments.",
     "1. Inspect exhaust flange connections between header and midpipe for small air leaks.\n2. Check downstream O2 sensor bung weld.\n3. Check for dirty upstream A/F wideband sensor.",
     "Replace exhaust donut gasket/flange seals, or replace downstream O2 sensor."},

    // EVAP Emissions System
    {"P0442", "EVAP System Small Leak Detected", "EVAP Emissions",
     "PCM detected slight vacuum decay in fuel tank EVAP system during engine-off natural vacuum (EONV) test.",
     "1. Inspect gas cap rubber seal for cracks, dirt, or improper tightening.\n2. Perform smoke test on EVAP test port under hood.\n3. Check EVAP charcoal canister and vent valve under rear subframe.",
     "Clean gas cap filler neck / replace OEM fuel cap, or replace cracked EVAP vapor hose."},

    {"P0455", "EVAP System Large Leak Detected", "EVAP Emissions",
     "PCM detected massive vacuum leak or inability to pull vacuum in fuel tank system.",
     "1. Check if gas cap was left loose or missing.\n2. Check EVAP canister purge solenoid under hood (disconnect and verify it does not flow air when unpowered).\n3. Inspect fuel tank rollover/vapor lines.",
     "Tighten/replace gas cap, replace stuck-open EVAP purge solenoid, or reconnect detached vapor hose."},

    // Throttle Body & Electronic Throttle
    {"P0506", "Idle Air Control RPM Lower Than Expected", "Throttle & Idle Control",
     "Engine idle speed is consistently 100+ RPM below commanded target idle speed.",
     "1. Inspect electronic throttle body bore and throttle plate for black carbon ring.\n2. Check for heavy PCV oil buildup on throttle plate.\n3. Check battery voltage at idle.",
     "Clean throttle body plate and bore with throttle cleaner and lint-free rag; perform electronic throttle idle relearn."},

    {"P0507", "Idle Air Control RPM Higher Than Expected", "Throttle & Idle Control",
     "Engine idle speed is 200+ RPM above target idle speed.",
     "1. Check for unmetered vacuum leaks (intake manifold gasket, brake booster hose, PCV hose).\n2. Verify throttle plate is fully closing to physical stop.",
     "Fix intake vacuum leak or clean throttle body and perform idle relearn."},

    {"P061B", "Internal Control Module Torque Calculation Perf", "Electronic Throttle / ECU",
     "PCM torque calculation does not match actual calculated engine torque vs throttle position.",
     "1. Check for aftermarket intake pipe causing incorrect MAF airflow velocity table.\n2. Inspect accelerator pedal position (APP) sensor connectors.\n3. Check battery ground integrity.",
     "Clean MAF sensor, inspect intake piping for unmetered air leaks, or update PCM calibration."},

    {"P2101", "Throttle Actuator Control Motor Range/Perf", "Electronic Throttle",
     "Electronic throttle plate actuator motor failed to reach commanded angle within time limit.",
     "1. Remove intake tube and check throttle plate for physical binding or foreign debris.\n2. Check throttle motor wiring harness pins for fretting corrosion.",
     "Clean throttle body or replace electronic throttle body assembly."},

    // Transmission & Drivetrain
    {"P0700", "Transmission Control System Malfunction", "Automatic Transmission (6AT)",
     "TCM has requested Check Engine Light illumination; specific transmission code is stored in TCM.",
     "1. Scan TCM module specifically for sub-codes (P0711, P0730, P0741).\n2. Check automatic transmission fluid (ATF) level and color (FZ fluid).",
     "Read TCM module codes and address specific solenoid, fluid, or clutch fault."},

    {"P0711", "Transmission Fluid Temp Sensor Range/Perf", "Automatic Transmission (6AT)",
     "TFT sensor reading is erratic or contradicts engine coolant warm-up curve.",
     "1. Check TFT live temperature data stream on scan tool.\n2. Inspect transmission external wiring harness connector.",
     "Replace internal transmission fluid temperature sensor harness or TCM assembly."},

    {"P0741", "TCC Solenoid Circuit Performance / Stuck Off", "Automatic Transmission (6AT)",
     "Torque converter clutch failed to lock up; excessive slip RPM detected in gears 4-6.",
     "1. Check transmission fluid level and quality.\n2. Monitor live TCC Slip RPM (should drop to 0-20 RPM when locked).\n3. Check TCC lockup solenoid resistance.",
     "Flush ATF with genuine Mazda FZ fluid, replace TCC lockup solenoid valve, or replace torque converter."},

    // Chassis, ABS & Wheel Speed
    {"C0031", "Left Front Wheel Speed Sensor Malfunction", "ABS & Stability Control",
     "ABS/DSC module lost AC/Digital signal from Front-Left wheel speed sensor.",
     "1. Inspect FL wheel speed sensor wiring near strut tower and steering knuckle for stretching/rubbing.\n2. Inspect magnetic tone ring integrated into front wheel bearing for metallic debris or cracking.\n3. Measure sensor resistance.",
     "Replace Front-Left ABS wheel speed sensor or replace front wheel hub/bearing assembly."},

    {"C0034", "Right Front Wheel Speed Sensor Malfunction", "ABS & Stability Control",
     "ABS/DSC module lost signal from Front-Right wheel speed sensor.",
     "1. Inspect FR sensor wiring harness at steering knuckle.\n2. Check FR wheel bearing tone ring.",
     "Replace Front-Right ABS wheel speed sensor or hub assembly."},

    {"C0037", "Left Rear Wheel Speed Sensor Malfunction", "ABS & Stability Control",
     "ABS/DSC module lost signal from Rear-Left wheel speed sensor.",
     "1. Inspect RL sensor wiring near rear control arm and axle hub.\n2. Check rear wheel bearing magnetic encoder ring.",
     "Replace Rear-Left ABS sensor or rear wheel hub."},

    {"C003A", "Right Rear Wheel Speed Sensor Malfunction", "ABS & Stability Control",
     "ABS/DSC module lost signal from Rear-Right wheel speed sensor.",
     "1. Inspect RR sensor wiring harness at rear suspension knuckle.\n2. Check RR bearing tone ring.",
     "Replace Rear-Right ABS sensor or rear wheel hub."},

    // CAN Bus & Module Communication
    {"U0100", "Lost Communication with PCM (Engine)", "CAN Bus & Network",
     "Instrument Cluster, ABS, or BCM lost high-speed CAN message frame from Engine Control Module.",
     "1. Check battery 12V voltage (low voltage during cranking is #1 cause of false U0100).\n2. Inspect PCM main ground terminals on engine block/chassis.\n3. Check HS-CAN bus termination resistance (should be 60 ohms between CAN-H and CAN-L with battery disconnected).",
     "Recharge/replace weak 12V battery, clean chassis ground lugs, or repair CAN wiring harness."},

    {"U0121", "Lost Communication with ABS/DSC Module", "CAN Bus & Network",
     "PCM or BCM lost high-speed CAN communication with Anti-Lock Brake / Stability Control module.",
     "1. Check ABS module main power fuses in engine bay fuse box.\n2. Inspect ABS module connector for moisture or loose locking tab.\n3. Check battery health.",
     "Replace blown ABS fuse, clean ABS connector pins, or repair CAN-bus wiring."},

    {"U0140", "Lost Communication with BCM (Body Controller)", "CAN Bus & Network",
     "Modules lost medium-speed/high-speed CAN communication with Body Control Module.",
     "1. Check BCM fuses under driver dash / kick panel.\n2. Inspect BCM wiring harness connectors for loose seating.\n3. Test 12V battery condition.",
     "Reseat BCM connectors or replace blown BCM power fuse."}
};

static const size_t DTC_COUNT = sizeof(DTC_TABLE) / sizeof(DTC_TABLE[0]);

DtcInfo DtcDatabase::lookup(const char* code) {
    if (!code || strlen(code) < 4) {
        return {code ? code : "---", "Unknown Diagnostic Code", "General Diagnostic",
                "No specific definition available for this code.",
                "1. Connect OBD-II scan tool and inspect live sensor readings.\n2. Check for related system DTCs.",
                "Follow standard diagnostic trouble tree for this system."};
    }

    // Exact dictionary match
    for (size_t i = 0; i < DTC_COUNT; i++) {
        if (strcasecmp(code, DTC_TABLE[i].code) == 0) {
            return DTC_TABLE[i];
        }
    }

    // Dynamic standard fallback classification based on SAE J2012 prefix
    char pfx[3] = {code[0], code[1], '\0'};
    if (strncasecmp(pfx, "P0", 2) == 0) {
        char sub = code[2];
        if (sub == '1' || sub == '2') {
            return {code, "Fuel and Air Metering Fault", "Fuel & Air System",
                    "ECU detected abnormal fuel trim, airflow, or fuel pressure reading.",
                    "1. Check for intake vacuum leaks.\n2. Inspect MAF/MAP sensors.\n3. Test fuel pressure.",
                    "Clean/replace airflow sensors or fix intake vacuum leaks."};
        } else if (sub == '3') {
            return {code, "Ignition System or Engine Misfire", "Ignition System",
                    "ECU detected cylinder combustion instability or ignition circuit fault.",
                    "1. Inspect spark plugs and ignition coils.\n2. Check cylinder compression and fuel injector pulse.",
                    "Replace faulty spark plug or ignition coil pack."};
        } else if (sub == '4') {
            return {code, "Auxiliary Emissions Controls Fault", "Emissions System",
                    "ECU detected catalytic converter or EVAP vapor recovery fault.",
                    "1. Inspect gas cap and EVAP purge valve.\n2. Check exhaust for pre-cat exhaust leaks.",
                    "Replace gas cap, EVAP purge solenoid, or exhaust gaskets."};
        } else if (sub == '5') {
            return {code, "Vehicle Speed & Idle Control System", "Speed & Idle Control",
                    "ECU detected idle speed deviation or electronic throttle motor issue.",
                    "1. Inspect throttle body for carbon buildup.\n2. Check accelerator pedal position sensor.",
                    "Clean throttle body and perform electronic throttle idle relearn."};
        } else if (sub == '7' || sub == '8') {
            return {code, "Transmission Control System Fault", "Transmission System",
                    "TCM detected internal transmission solenoid, speed sensor, or clutch slip fault.",
                    "1. Check automatic transmission fluid (ATF) level and color.\n2. Check transmission wiring harness.",
                    "Service transmission fluid or replace shift/lockup solenoid."};
        }
    } else if (strncasecmp(pfx, "C0", 2) == 0 || strncasecmp(pfx, "C1", 2) == 0) {
        return {code, "Chassis / ABS & Stability Control Fault", "Chassis & ABS",
                "ABS/DSC module detected wheel speed, steering angle, or brake hydraulic sensor fault.",
                "1. Inspect wheel speed sensor harnesses at all 4 corners.\n2. Check brake fluid level.",
                "Replace faulty wheel speed sensor or repair damaged wheel harness."};
    } else if (strncasecmp(pfx, "U0", 2) == 0 || strncasecmp(pfx, "U1", 2) == 0) {
        return {code, "CAN Bus Network Communication Fault", "CAN Network",
                "One or more control modules lost communication messages over the CAN bus network.",
                "1. Test 12V battery health and charge state.\n2. Inspect chassis ground lugs.\n3. Check module fuses.",
                "Recharge/replace weak 12V battery or clean ground connections."};
    } else if (strncasecmp(pfx, "B0", 2) == 0 || strncasecmp(pfx, "B1", 2) == 0) {
        return {code, "Body Control System Fault", "Body & Accessories",
                "Body Control Module (BCM) detected lighting, keyless start, or power distribution fault.",
                "1. Check BCM fuses under driver dash.\n2. Test key fob battery and 12V main battery.",
                "Replace blown fuse or service BCM circuit."};
    }

    return {code, "Manufacturer Specific Diagnostic Code", "Vehicle Control System",
            "ECU recorded a diagnostic trouble code for this powertrain or body subsystem.",
            "1. Check live OBD sensor data stream.\n2. Inspect related wiring connectors and fuses.",
            "Follow diagnostic procedure for the flagged sensor/actuator circuit."};
}
