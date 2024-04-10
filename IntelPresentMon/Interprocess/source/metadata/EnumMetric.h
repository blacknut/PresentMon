#pragma once
#include "../../../PresentMonAPI2/PresentMonAPI.h"

// enum annotation (enum_name_fragment, key_name_fragment, name, short_name, description)
#define ENUM_KEY_LIST_METRIC(X_) \
		X_(METRIC, APPLICATION, "Application", "", "Name of the executable of the process being targeted") \
		X_(METRIC, SWAP_CHAIN_ADDRESS, "Swap Chain Address", "", "Address of the swap chain used to present, useful as a unique identifier") \
\
		X_(METRIC, GPU_VENDOR, "GPU Vendor", "", "Vendor name of the GPU") \
		X_(METRIC, GPU_NAME, "GPU Name", "", "Device name of the GPU") \
		X_(METRIC, CPU_VENDOR, "CPU Vendor", "", "Vendor name of the CPU") \
		X_(METRIC, CPU_NAME, "CPU Name", "", "Device name of the CPU") \
\
		X_(METRIC, TIME, "Time", "", "Time elapsed since the start of ETW event tracing") \
		X_(METRIC, CPU_FRAME_QPC, "CPU Frame QPC", "", "The QueryPerformanceCounter timestamp when the CPU started working on the frame") \
		X_(METRIC, FRAME_TIME, "Frame Time", "", "The total amount of time in between frames on the CPU") \
		X_(METRIC, CPU_BUSY, "CPU Busy", "", "How long the CPU was generating the frame in milliseconds") \
		X_(METRIC, CPU_WAIT, "CPU Wait", "", "How long the CPU spent waiting before it could start generating the frame in milliseconds") \
\
		X_(METRIC, DISPLAYED_FPS, "Displayed FPS", "", "Rate of frame change measurable at display") \
		X_(METRIC, PRESENTED_FPS, "Presented FPS", "", "Rate of application calls to a Present() function") \
\
		X_(METRIC, GPU_TIME, "GPU Time", "", "Total amount of time between when GPU started frame and when it finished in milliseconds. The GPU may not have been fully busy during this time") \
		X_(METRIC, GPU_BUSY, "GPU Busy", "", "How long the GPU spent working on this frame") \
		X_(METRIC, GPU_WAIT, "GPU Wait", "", "How long the GPU spent waiting while working on this frame") \
\
		X_(METRIC, DROPPED_FRAMES, "Dropped Frames", "", "Indicates if the frame was not displayed") \
\
		X_(METRIC, DISPLAYED_TIME, "Displayed Time", "", "How long this frame was displayed on screen") \
\
		X_(METRIC, SYNC_INTERVAL, "Sync Interval", "", "The application's requested interval between presents measured in vertical sync/vblank events") \
		X_(METRIC, PRESENT_FLAGS, "Present Flags", "", "Flags used to configure the present operation") \
		X_(METRIC, PRESENT_MODE, "Present Mode", "", "Method used to present the frame") \
		X_(METRIC, PRESENT_RUNTIME, "Present Runtime", "", "The graphics runtime used for the present operation (DXGI, D3D9, etc.)") \
		X_(METRIC, ALLOWS_TEARING, "Allows Tearing", "", "Indicates if the frame allows tearing") \
\
		X_(METRIC, GPU_LATENCY, "GPU Latency", "", "How long it took until GPU work for this frame started") \
		X_(METRIC, DISPLAY_LATENCY, "Display Latency", "", "Time between frame submission and scan out to display") \
		X_(METRIC, CLICK_TO_PHOTON_LATENCY, "Click To Photon Latency", "", "Time between input and display") \
		\
		X_(METRIC, GPU_SUSTAINED_POWER_LIMIT, "GPU Sustained Power Limit", "", "Sustained power limit of the GPU") \
		X_(METRIC, GPU_POWER, "GPU Power", "", "Power consumed by the graphics adapter") \
		X_(METRIC, GPU_VOLTAGE, "GPU Voltage", "", "Voltage consumed by the graphics adapter") \
		X_(METRIC, GPU_FREQUENCY, "GPU Frequency", "", "Clock speed of the GPU cores") \
		X_(METRIC, GPU_TEMPERATURE, "GPU Temperature", "", "Temperature of the GPU") \
		X_(METRIC, GPU_FAN_SPEED, "GPU Fan Speed", "", "Rate at which a GPU cooler fan is rotating") \
		X_(METRIC, GPU_UTILIZATION, "GPU Utilization", "", "Amount of GPU processing capacity being used") \
		X_(METRIC, GPU_RENDER_COMPUTE_UTILIZATION, "3D/Compute Utilization", "", "Amount of 3D/Compute processing capacity being used") \
		X_(METRIC, GPU_MEDIA_UTILIZATION, "Media Utilization", "", "Amount of media processing capacity being used") \
\
		X_(METRIC, GPU_POWER_LIMITED, "GPU Power Limited", "", "GPU frequency is being limited because GPU is exceeding maximum power limits") \
		X_(METRIC, GPU_TEMPERATURE_LIMITED, "GPU Temperature Limited", "", "GPU frequency is being limited because GPU is exceeding maximum temperature limits") \
		X_(METRIC, GPU_CURRENT_LIMITED, "GPU Current Limited", "", "GPU frequency is being limited because GPU is exceeding maximum current limits") \
		X_(METRIC, GPU_VOLTAGE_LIMITED, "GPU Voltage Limited", "", "GPU frequency is being limited because GPU is exceeding maximum voltage limits") \
		X_(METRIC, GPU_UTILIZATION_LIMITED, "GPU Utilization Limited", "", "GPU frequency is being limited due to low GPU utilization") \
\
		X_(METRIC, GPU_MEM_POWER, "GPU Memory Power", "", "Power consumed by the GPU memory") \
		X_(METRIC, GPU_MEM_VOLTAGE, "GPU Memory Voltage", "", "Voltage consumed by the GPU memory") \
		X_(METRIC, GPU_MEM_FREQUENCY, "GPU Memory Frequency", "", "Clock speed of the GPU memory") \
		X_(METRIC, GPU_MEM_EFFECTIVE_FREQUENCY, "GPU Memory Effective Frequency", "", "Effective data transfer rate GPU memory can sustain") \
		X_(METRIC, GPU_MEM_TEMPERATURE, "GPU Memory Temperature", "", "Temperature of the GPU memory") \
\
		X_(METRIC, GPU_MEM_SIZE, "GPU Memory Size", "", "Size of the GPU memory") \
		X_(METRIC, GPU_MEM_USED, "GPU Memory Size Used", "", "Amount of used GPU memory") \
		X_(METRIC, GPU_MEM_UTILIZATION, "GPU Memory Utilization", "", "Percent of GPU memory used") \
		X_(METRIC, GPU_MEM_MAX_BANDWIDTH, "GPU Memory Max Bandwidth", "", "Maximum total GPU memory bandwidth") \
		X_(METRIC, GPU_MEM_WRITE_BANDWIDTH, "GPU Memory Write Bandwidth", "", "Maximum GPU memory bandwidth for writing") \
		X_(METRIC, GPU_MEM_READ_BANDWIDTH, "GPU Memory Read Bandwidth", "", "Maximum GPU memory bandwidth for reading") \
\
		X_(METRIC, GPU_MEM_POWER_LIMITED, "GPU Memory Power Limited", "", "Memory frequency is being limited because the memory modules are exceeding the maximum power limits") \
		X_(METRIC, GPU_MEM_TEMPERATURE_LIMITED, "GPU Memory Temperature Limited", "", "Memory frequency is being limited because the memory modules are exceeding the maximum temperature limits") \
		X_(METRIC, GPU_MEM_CURRENT_LIMITED, "GPU Memory Current Limited", "", "Memory frequency is being limited because the memory modules are exceeding the maximum current limits") \
		X_(METRIC, GPU_MEM_VOLTAGE_LIMITED, "GPU Memory Voltage Limited", "", "Memory frequency is being limited because the memory modules are exceeding the maximum voltage limits") \
		X_(METRIC, GPU_MEM_UTILIZATION_LIMITED, "GPU Memory Utilization Limited", "", "Memory frequency is being limited due to low memory traffic") \
		\
		X_(METRIC, CPU_UTILIZATION, "CPU Utilization", "", "Amount of CPU processing capacity being used") \
		X_(METRIC, CPU_POWER_LIMIT, "CPU Power Limit", "", "Power limit of the CPU") \
		X_(METRIC, CPU_POWER, "CPU Power", "", "Power consumed by the CPU") \
		X_(METRIC, CPU_TEMPERATURE, "CPU Temperature", "", "Temperature of the CPU") \
		X_(METRIC, CPU_FREQUENCY, "CPU Frequency", "", "Clock speed of the CPU") \
		X_(METRIC, CPU_CORE_UTILITY, "CPU Core Utility", "", "Amount of CPU processing utility being used per core")