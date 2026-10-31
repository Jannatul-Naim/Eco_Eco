# 7. Test checklist

Fill in as you go. A box is ticked only when someone **saw** it on the real rover. Write the date,
your name and the actual value. Build success is not a test result. Stage numbers match
[06_bringup_guide.md](06_bringup_guide.md).

| # | Stage | Check | Pass criterion | Result / value | Date · who |
|---|---|---|---|---|---|
| 0.1 | Build | `tools/pio.sh run -e s3_main -e cam -e audio` | 3 × SUCCESS | | |
| 0.2 | Build | `tools/pio.sh test -e native` | 6/6 passed | | |
| 1.1 | S3 USB boot, motor power **disconnected** | header silkscreen matches 05_pin_maps §5.1 | every GPIO used exists with that label | | |
| 1.2 | | boot banner | chip ESP32-S3, outputs DISABLED | | |
| 1.3 | | E-stop sense not wired | boots into ESTOP (fail-safe) | | |
| 1.4 | | dashboard file system | dashboard loads, no "files not uploaded" page | | |
| 2.1 | E-stop | power-on | motor bus 0 V until ARM | V = | |
| 2.2 | | ARM | motor bus = battery V | V = | |
| 2.3 | | press E-stop | motor bus 0 V | V = | |
| 2.4 | | release E-stop | motor bus **stays** 0 V | V = | |
| 2.5 | | S3 sense | ESTOP latched on press; `reset` refused while pressed, accepted after release | | |
| 3.1 | Wi-Fi + dashboard | telemetry median | ≤ 100 ms | ms = | |
| 3.2 | | battery reading (if fitted) | within 0.1 V of multimeter after calibration | | |
| 4.1 | Sonar front-center | 100 / 200 / 500 mm | each within ± 10 mm | | |
| 4.2 | Sonar front-left | 100 / 200 / 500 mm | each within ± 10 mm | | |
| 4.3 | Sonar front-right | 100 / 200 / 500 mm | each within ± 10 mm | | |
| 4.4 | Sonar rear | 100 / 200 / 500 mm | each within ± 10 mm | | |
| 4.5 | | no target / Echo unplugged | `out_of_range` / `no_echo` / stale, never a fake value | | |
| 4.6 | Rear interlock | obstacle at 150 mm, rear not fitted, rear stale | reverse blocked in all three | | |
| 5.1 | I²C | `i2cscan` | 0x40 found | addr = | |
| 6.1 | SG90 via PCA9685, motors disconnected | end pulses found without hitting stops | pulses noted | µs = | |
| 6.2 | | soft range with camera fitted | cable never snags | ° = | |
| 7.1 | Camera | boot | camera OK, sensor PID recorded | PID = | |
| 7.2 | | stream in dashboard | live picture, fps badge | fps = | |
| 7.3 | | `/gray.jpg` | correct grey image (byte order OK) | | |
| 7.4 | | UART link | S3 shows link `ok`, `rx_bad` not climbing | | |
| 7.5 | | QR → IP shown | decoded text + IPv4 in the dashboard | | |
| 7.6 | | pan moves, scene static | **no** motion detection during or right after a pan move | | |
| 8.1 | Motors, **wheels raised** | `drive 0.2 0.2` | both sides forward | | |
| 8.2 | | `drive 0.2 -0.2` | spins right | | |
| 8.3 | | reverse without rear sonar | blocked, event logged | | |
| 8.4 | | E-stop while turning | wheels stop at once | | |
| 8.5 | | dashboard link lost while driving | stops within ~0.3 s | | |
| 8.6 | | 1 min at 0.3 | drivers and motors only warm | | |
| 9.1 | Audio (optional) | listen at 1 m | operator understands speech | | |
| 9.2 | | talk at 1 m | listener understands speech | | |
| 10.1 | Dashboard + gamepad | mapping | `standard` | | |
| 10.2 | | deadman release | stops | | |
| 10.3 | | disconnect / blur / B / AUTO | each disarms | | |
| 10.4 | | `POST /api/drive` in AUTO | 409 | | |
| 11.1 | Mission QR + ping | one echo request in the laptop's tcpdump from the rover IP | logged | | |
| 11.2 | Mission motion | 5 s track | AUTO_COMPLETE | | |
| 11.3 | Mission proximity (raised, then floor) | final gap | 100 ± 20 mm measured with a ruler | mm = | |
| 11.4 | Mission traverse (raised) | blocked → FAILED; timeout → FAILED | as designed | | |
| 12.1 | Integrated safety | E-stop during each mission | stops; release restarts nothing | | |
| 12.2 | | camera unplugged in a camera mission | SAFE_STOP | | |
| 12.3 | | sonar unplugged in a driving mission | SAFE_STOP | | |
| 13.1 | Floor run | teleop forward / stop / turns at 30 % | controlled | | |
| 13.2 | | proximity on the floor | 100 ± 20 mm | mm = | |
