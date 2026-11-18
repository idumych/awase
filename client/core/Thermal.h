/*
 * Copyright 2026, Ian Dumych, idumych@gmail.com.
 * SPDX-License-Identifier: MIT
 */
#ifndef AWASE_THERMAL_H
#define AWASE_THERMAL_H


namespace awase {


// The CPU's temperature in °C, or -1 when the machine has no sensor Awase
// can read (many VPSs and virtual machines have none).
//   Renku OS: the acpi_thermal, pch_thermal and amd_thermal drivers'
//             devices under /dev/power; the hottest one counts.
//   Linux:    a CPU thermal zone, or the k10temp, coretemp or zenpower
//             hwmon sensor.
int		CpuTemperature();


}	// namespace awase


#endif	// AWASE_THERMAL_H
