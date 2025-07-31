# ESP32-CAM GenICam Compatibility Project Plan

This document outlines the implementation plan for making an ESP32-CAM GenICam-compatible camera for internal testing purposes. The camera should be recognized and usable by existing software based on go-aravis, which expects a GigE Vision (GVCP/GVSP) and GenICam-compliant device.

---

## Phase 0: Preparation

### Checklist:
- [x] Ensure ESP32-CAM board is functional and programmable
  - [x] Connect board via USB
  - [x] Flash a basic example using ESP-IDF (`hello_world`)
- [x] Install and configure PlatformIO and VSCode integration
  - [x] Install PlatformIO Core and VSCode extension
  - [x] Install VSCode extensions (PlatformIO IDE, C/C++)
- [x] Confirm Wi-Fi or USB connectivity from ESP32 to Ubuntu PC
  - [x] Connect ESP32 to local Wi-Fi
  - [x] Ping ESP32 from Ubuntu PC
- [x] Install Aravis and go-aravis-based tools for testing
  - [x] Install `libaravis-0.8-dev` and `arv-tool` on Ubuntu
  - [x] Build and run sample go-aravis project
- [x] Prepare sample GenICam XML files and study Aravis fake camera implementation
  - [x] Clone Aravis repo and inspect `fake-camera.c`
  - [x] Save minimal XML examples for reuse

---

## Phase 1: Basic GVCP Discovery and Control

### Goals:
Implement GVCP over UDP port 3956 and respond to discovery and register read/write commands.

### Checklist:
- [x] Open UDP socket on port 3956 (Wi-Fi interface)
  - [x] Bind to INADDR_ANY:3956
  - [x] Create listener task in ESP-IDF
- [x] Parse and respond to GVCP discovery packets
  - [x] Detect `GVCP_DISCOVERY_CMD` message
  - [x] Send correct `DISCOVERY_ACK` with device MAC, IP, version
- [x] Implement static bootstrap registers:
  - [x] Register: Manufacturer Name
  - [x] Register: Model Name
  - [x] Register: Device Version
  - [x] Register: Serial Number
  - [x] Register: User-defined Name
  - [x] Register: MAC Address
- [x] Implement read/write for bootstrap memory range
  - [x] Return correct byte data for `READMEM_CMD`
  - [x] Update values on `WRITEMEM_CMD`
- [x] Implement register to point to GenICam XML memory location (e.g., `Local:0x10000`)
- [x] Test with `arv-tool` to confirm the device is discoverable and readable
  - [x] Run `arv-tool-0.8 --debug=all` and verify response

---

## Phase 2: GenICam XML Support

### Goals:
Provide a GenICam-compliant XML file describing basic device features.

### Checklist:
- [x] Write minimal GenICam XML with:
  - [x] Node: DeviceInformation (VendorName, ModelName, SerialNumber)
  - [x] Node: ImageFormatControl (Width, Height)
  - [x] Node: PixelFormat (Mono8)
  - [x] Node: AcquisitionControl (AcquisitionStart, AcquisitionStop)
  - [x] Node: Stream parameters (GevSCPSPacketSize, GevSCPHostPort)
- [x] Store XML in flash or embedded array
  - [x] Place XML as binary blob at known memory address (e.g., 0x10000)
- [x] Serve XML via memory-mapped GVCP reads
  - [x] Handle multi-packet reads from 0x10000
- [x] Test XML retrieval via Aravis
  - [x] Confirm successful XML download with Aravis log
- [ ] Validate XML correctness using GenICam tools (optional)

---

## Phase 3: Frame Capture with ESP32 Camera

### Goals:
Capture grayscale image frames using `esp_camera` module.

### Checklist:
- [x] Initialize ESP32-CAM with `esp_camera` driver
  - [x] Configure pins and settings for OV2640 (pins configured and used)
- [x] Test capture with grayscale-friendly format (YUV, RGB565) (real capture implemented)
- [x] Convert YUV to Mono8 (use Y channel) (conversion implemented)
  - [x] Write converter from YUV422 to Mono8 (converter implemented)
- [x] Store captured frame in memory buffer (stores real captured frames)
- [x] Implement dummy frame generation fallback
  - [x] Generate checkerboard or gradient pattern
- [x] Log frame size, resolution, and buffer timing

---

## Phase 3a: Real ESP32-CAM Hardware Integration

### Goals:
Integrate actual ESP32-CAM hardware for real frame capture instead of dummy patterns.

### Checklist:
- [x] Add esp_camera component dependency to CMakeLists.txt
- [x] Include esp_camera.h in camera_handler.c
- [x] Initialize real ESP32-CAM with esp_camera_init() using pin config from platformio.ini
- [x] Replace dummy frame generation with actual esp_camera_fb_get()
- [x] Implement proper frame buffer management with esp_camera_fb_return()
- [x] Test real camera capture and verify frame data
- [x] Handle camera initialization errors and fallback modes
- [x] Add camera configuration options (resolution, quality, etc.)

---

## Phase 3b: Image Format Processing

### Goals:
Implement format conversion from ESP32-CAM native formats to GenICam Mono8.

### Checklist:
- [x] Detect incoming frame format from ESP32-CAM (JPEG, YUV422, RGB565)
- [x] Implement JPEG to Mono8 conversion if camera outputs JPEG
- [x] Implement YUV422 to Mono8 conversion (extract Y channel)
- [x] Implement RGB565 to Mono8 conversion
- [x] Add format detection and automatic conversion routing
- [x] Optimize conversion performance for real-time streaming

---

## Phase 4: GVSP Streaming Protocol

### Goals:
Implement GVSP to transmit captured frames as UDP packet streams.

### Checklist:
- [ ] Open UDP socket for streaming (e.g., port 50010)
- [ ] Structure and send GVSP Leader packet
  - [ ] Include block ID, image size, pixel format code
- [ ] Split frame into data packets (~1400 bytes payload)
  - [ ] Packet IDs increment sequentially
- [ ] Send Trailer packet at end of frame
- [ ] Implement frame counter (BlockID)
- [ ] Handle AcquisitionStart / Stop commands via GVCP
- [ ] Stream frames at fixed interval or based on camera rate
- [ ] Add configurable delay between packets (if needed)
- [ ] Cleanly stop stream on disconnect or AcquisitionStop

---

## Phase 4a: GVSP Integration with Real Camera

### Goals:
Connect real ESP32-CAM capture to GVSP streaming pipeline.

### Checklist:
- [ ] Connect camera capture to GVSP streaming pipeline
- [ ] Trigger real frame capture on AcquisitionStart
- [ ] Stream real camera frames (not dummy patterns)
- [ ] Handle frame timing and buffer management
- [ ] Add frame sequence numbering from real captures

---

## Phase 5: Integration Testing with Aravis and go-aravis

### Goals:
Validate full GenICam camera pipeline with existing software stack.

### Checklist:
- [ ] Confirm discovery with `arv-tool-0.8` on Ubuntu
- [ ] Confirm XML is downloaded and parsed
- [ ] Confirm AcquisitionStart triggers stream
- [ ] Confirm image frames are received and assembled
- [ ] Display frames using `arv-viewer`
- [ ] Debug dropped or malformed packets using Wireshark
- [ ] Optimize packet size, delay, and resolution
- [ ] Verify with internal go-aravis application

---

## Phase 6: Refinement and Extensions (Optional)

### Ideas:
- [ ] Implement resend support for GVSP packets
- [ ] Add features: ExposureTime, Gain, TriggerMode
- [ ] Compress GenICam XML (Zip format)
- [ ] Create Web UI or Serial interface for configuration
- [ ] Support Bayer or RGB pixel formats
- [ ] Integrate watchdog and heartbeat timer

---

## Phase 7: Documentation and Delivery

### Checklist:
- [x] Create build and flash instructions (`pio run`, `pio run --target upload`)
- [x] Include GenICam XML file in repo
- [x] Document register map and memory layout
- [ ] Write usage instructions for Aravis CLI and viewer
- [ ] Provide prebuilt binary or image for quick flashing
- [ ] Annotate Wireshark capture of protocol for reference

---

## Notes
- [ ] Start with hard-coded/default values, then allow dynamic configuration
- [ ] Use Aravis debug mode (`ARV_DEBUG=all`) to troubleshoot
- [ ] Separate tasks for GVCP, camera, and GVSP components
- [ ] Keep image size and frame rate minimal at first

---

**End of Plan**

