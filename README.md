# Automotive ECU Fault Monitoring & Recovery Framework

A QNX-based multi-ECU supervision framework for detecting ECU failures, prioritizing recovery, and connecting real-time fault handling to a physical 4WD rover.

Built for the **QNX India Hackathon** using **QNX 8.0, Raspberry Pi 4, QNX Momentics, ESP32, UART, and a physical rover prototype**.

---

## Overview

Modern vehicles depend on multiple Electronic Control Units (ECUs) operating continuously and reliably.

This project simulates three automotive ECU services:

- **ECU1 – Engine**
- **ECU2 – Braking**
- **ECU3 – Steering**

A central **QNX Supervisor** monitors these ECU processes through heartbeat messages using QNX IPC.

If an ECU stops responding, the Supervisor:

1. Detects the missing heartbeat
2. Identifies the failed ECU
3. Calculates its dynamic recovery priority
4. Selects the ECU for recovery
5. Restarts the failed ECU process
6. Verifies recovery through the ECU's next heartbeat

The QNX system is also connected to an **ESP32-controlled 4WD rover** through UART, while a separate **dashboard** provides system-level visualization.

---

## System Architecture

```text
                    ┌─────────────────────────┐
                    │     Raspberry Pi 4      │
                    │        QNX 8.0          │
                    │                         │
                    │     QNX SUPERVISOR      │
                    │                         │
                    │  Heartbeat Monitoring   │
                    │  Fault Detection       │
                    │  Dynamic Priority      │
                    │  Recovery Scheduling   │
                    │  Recovery Verification │
                    └────────────┬────────────┘
                                 │
                            QNX IPC Pulses
                                 │
                 ┌───────────────┼───────────────┐
                 │               │               │
              ┌──────┐        ┌──────┐        ┌──────┐
              │ ECU1 │        │ ECU2 │        │ ECU3 │
              │Engine│        │ Brake│        │ Steer│
              └──────┘        └──────┘        └──────┘
                                 │
                            UART /dev/ser1
                                 │
                                 ▼
                         ┌──────────────┐
                         │    ESP32     │
                         │ Rover Control│
                         └──────┬───────┘
                                │
                    ┌───────────┼───────────┐
                    ▼           ▼           ▼
                  L298N       SG90       HC-SR04
                  Motors     Steering    Ultrasonic
                    │
                    ▼
                4WD Rover


                         ┌──────────────┐
                         │  Dashboard   │
                         │              │
                         │ ECU Health   │
                         │ Fault Status │
                         │ Heartbeats   │
                         │ Recovery     │
                         │ System State │
                         └──────────────┘
```

---

## Key Features

### QNX Multi-Process Architecture

- Independent ECU processes
- Central QNX Supervisor
- QNX IPC pulse-based communication
- Process management using QNX APIs
- Priority-based recovery scheduling

### Heartbeat Monitoring

Each ECU sends a heartbeat approximately every second.

```text
ECU → MsgSendPulse() → Supervisor
```

The Supervisor receives the heartbeat using:

```c
MsgReceivePulse()
```

The ECU ID is transferred through:

```c
pulse.value.sival_int
```

---

## ECU Health States

```text
ECU_NOT_SEEN
      │
      ▼
ECU_HEALTHY
      │
      │ Heartbeat timeout
      ▼
ECU_FAIL
```

| State | Meaning |
|---|---|
| `ECU_NOT_SEEN` | No heartbeat received yet |
| `ECU_HEALTHY` | ECU is responding normally |
| `ECU_FAIL` | Heartbeat timeout detected |

The current heartbeat timeout is **3 seconds**.

---

## Dynamic Priority

Recovery priority is calculated using vehicle-context variables rather than a single fixed recovery order.

```text
Engine   = 30 + engine_load / 2
Braking  = 50 + braking_demand / 2
Steering = 40 + steering_demand / 2
```

Current values in the implementation:

```text
engine_load      = 30
braking_demand   = 80
steering_demand  = 20
```

Resulting priorities:

```text
Engine   → 45
Braking  → 90
Steering → 50
```

The Supervisor selects the highest-priority failed ECU that is not already under recovery.

---

## Fault Detection & Recovery

```text
ECU Running
     │
     ▼
Heartbeat Monitoring
     │
     ▼
Heartbeat Lost
     │
     ▼
3 Second Timeout
     │
     ▼
ECU Marked FAILED
     │
     ▼
Dynamic Priority Calculation
     │
     ▼
ECU Selected
     │
     ▼
Recovery Initiated
     │
     ▼
Process Restart
     │
     ▼
Heartbeat Received
     │
     ▼
Recovery Verified
```

The current implementation uses:

```c
kill(pid, SIGTERM);
```

to terminate the failed ECU and:

```c
spawnv(P_NOWAIT, ...);
```

to restart it.

---

## Recovery Timing

The Supervisor records timestamps for:

- Fault Detection
- Recovery Initiation
- Process Restart
- Recovery Verification

The total recovery interval is measured as:

```text
Fault Detection → Recovery Verification
```

This allows the system to produce measurable recovery-performance data for evaluation.

---

# Physical Rover Integration

The Raspberry Pi running QNX communicates with an ESP32 through UART.

### UART

```text
Raspberry Pi GPIO14 TX ─────→ ESP32 GPIO3 RX

Raspberry Pi GPIO15 RX ←───── ESP32 GPIO1 TX

Raspberry Pi GND ──────────── ESP32 GND
```

QNX serial device:

```text
/dev/ser1
```

UART configuration:

```text
115200 baud
8 data bits
No parity
1 stop bit
```

---

## Rover Hardware

- Raspberry Pi 4
- QNX 8.0
- ESP32
- L298N motor driver
- 4 DC motors
- 4WD rover chassis
- SG90 steering servo
- HC-SR04 ultrasonic sensor

### Ultrasonic Interface

```text
TRIG → ESP32 GPIO5
ECHO → ESP32 GPIO18
```

The HC-SR04 is used for autonomous obstacle detection.

The rover behavior is:

```text
Forward
   │
   ▼
Obstacle Detected
   │
   ▼
Automatic Stop
   │
   ▼
Obstacle Clears
   │
   ▼
Automatic Forward Resume
```

---

# Rover Commands

The ESP32 rover controller supports:

| Command | Action |
|---|---|
| `F` | Forward |
| `B` | Backward |
| `L` | Left |
| `R` | Right |
| `S` | Stop |
| `BRAKE_TEST` | Sudden/emergency brake |
| `ENGINE_FAIL` | Engine-fault degraded-speed operation |

---

# Dashboard

The project includes a **separate monitoring dashboard**.

The dashboard provides a visual representation of the system, including:

- ECU health
- ECU status
- Heartbeat activity
- Fault events
- Recovery activity
- System state
- Rover-related status

The dashboard is the **visualization layer**.

The QNX Supervisor remains responsible for the core ECU monitoring and recovery logic.

```text
QNX Supervisor
      │
      ├──── ECU Health
      ├──── Heartbeats
      ├──── Fault Events
      ├──── Recovery Events
      │
      ▼
   Dashboard
```

---

# Repository Structure

```text
QNX-ECU-Fault-Monitoring/
│
├── README.md
│
├── qnx/
│   ├── supervisor/
│   │   └── supervisor.c
│   │
│   ├── ecu1/
│   │   └── ecu1.c
│   │
│   ├── ecu2/
│   │   └── ecu2.c
│   │
│   └── ecu3/
│       └── ecu3.c
│
├── esp32/
│   └── rover_controller/
│       └── rover_controller.ino
│
├── dashboard/
│   └── ...
│
├── hardware/
│   ├── wiring/
│   └── pinout/
│
├── docs/
│   ├── architecture/
│   ├── screenshots/
│   └── test-results/
│
└── LICENSE
```

---

# Setup

## 1. QNX Environment

Install:

- QNX Software Center
- QNX SDP 8.0
- QNX Momentics IDE
- Raspberry Pi 4 QNX image

Create the following projects in Momentics:

```text
Supervisor
ECU1
ECU2
ECU3
```

Build the projects for the Raspberry Pi 4 QNX target.

---

## 2. Deploy ECU Binaries

Copy the ECU binaries to:

```text
/tmp/ecu1
/tmp/ecu2
/tmp/ecu3
```

Verify:

```sh
ls /tmp/ecu*
```

Expected:

```text
/tmp/ecu1
/tmp/ecu2
/tmp/ecu3
```

---

## 3. Run the System

Start the Supervisor.

The Supervisor creates the named IPC endpoint:

```text
supervisor
```

It then launches:

```text
ECU1
ECU2
ECU3
```

The console should show their PIDs and heartbeat activity.

Example:

```text
ecu1 pid=...
ecu2 pid=...
ecu3 pid=...

heartbeat received from ECU 1
heartbeat received from ECU 2
heartbeat received from ECU 3
```

---

# Useful QNX Commands

### Check serial devices

```sh
ls /dev/ser*
```

### Check serial driver

```sh
pidin ar | grep devc-ser
```

### Check UART configuration

```sh
stty < /dev/ser1
```

### Check QNX target information

```sh
pidin info
```

### Check GPIO alternate functions

```sh
gpio-bcm2711 funcs 14,15
```

---

# UART Rover Testing

Send commands from the QNX target:

### Forward

```sh
echo "F" > /dev/ser1
```

### Stop

```sh
echo "S" > /dev/ser1
```

### Brake Test

```sh
echo "BRAKE_TEST" > /dev/ser1
```

Communication path:

```text
QNX Raspberry Pi
       │
       ▼
 /dev/ser1 UART
       │
       ▼
     ESP32
       │
       ▼
     L298N
       │
       ▼
     Rover
```

---

# Fault Injection Test

To demonstrate fault handling:

1. Start the Supervisor.
2. Allow all three ECUs to send heartbeats.
3. Verify healthy status.
4. Intentionally stop one ECU process.
5. Wait for the heartbeat timeout.
6. Observe the ECU failure message.
7. Observe dynamic priority selection.
8. Observe the recovery sequence.
9. Observe the restarted ECU's heartbeat.
10. Confirm recovery verification.

Expected flow:

```text
Healthy ECU
     ↓
Fault Injected
     ↓
Heartbeat Lost
     ↓
Timeout Detected
     ↓
ECU Failed
     ↓
Priority Evaluation
     ↓
Recovery
     ↓
Heartbeat Restored
     ↓
Recovery Verified
```

---

# Performance Evaluation

QNX Momentics / System Profiler can be used to evaluate:

- CPU utilization
- Thread activity
- Scheduling behavior
- IPC activity
- Timing
- Fault response

The final repository should contain actual measured test results rather than estimated values.

Recommended evidence:

```text
docs/
└── test-results/
    ├── cpu-usage.png
    ├── thread-timing.png
    ├── fault-response.png
    └── recovery-results.csv
```

---

# Technologies

| Category | Technology |
|---|---|
| RTOS | QNX 8.0 |
| Target | Raspberry Pi 4 |
| IDE | QNX Momentics |
| Language | C |
| IPC | QNX Pulses |
| Communication | UART |
| Physical Controller | ESP32 |
| Motor Driver | L298N |
| Steering | SG90 |
| Sensor | HC-SR04 |
| Visualization | Project Dashboard |

---

# Project Workflow

```text
             ┌───────────────┐
             │ ECU1 / ECU2   │
             │ / ECU3         │
             └───────┬───────┘
                     │
                QNX IPC
                     │
                     ▼
             ┌───────────────┐
             │ QNX SUPERVISOR│
             └───────┬───────┘
                     │
       ┌─────────────┼─────────────┐
       ▼             ▼             ▼
    Monitor       Detect        Prioritize
       │             │             │
       └─────────────┼─────────────┘
                     ▼
                 Recovery
                     │
                     ▼
               Verification
                     │
                     ▼
                    UART
                     │
                     ▼
                  ESP32
                     │
          ┌──────────┴──────────┐
          ▼                     ▼
       Rover                 Dashboard
```

---

# Project Highlights

- Multi-process QNX architecture
- QNX IPC-based heartbeat monitoring
- ECU health-state management
- 3-second heartbeat timeout detection
- Context-aware dynamic recovery priority
- Automated ECU process recovery
- Recovery verification
- Recovery timing measurement
- Raspberry Pi + QNX integration
- QNX-to-ESP32 UART communication
- Physical 4WD rover prototype
- Ultrasonic obstacle handling
- Real-time dashboard visualization

---

# Project Objective

> **Build a reliable QNX-based supervisory framework that detects ECU failures, prioritizes recovery, verifies system restoration, and demonstrates the response through a connected physical rover and monitoring dashboard.**
