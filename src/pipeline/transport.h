/**
 * @file transport.h
 * @brief WiFi + UDP datagram sender for the pipeline's transport stage.
 */
#pragma once

#include <cstddef>
#include <cstdint>

namespace transport {

/// Starts WiFi (non-blocking) if include/secrets.h exists. Returns false if the
/// transport is disabled at build time.
bool begin();

/// True when WiFi is associated and has an IP.
bool ready();

/// Fire-and-forget UDP send. Never waits for the network; returns false on any error
/// (not connected, lwIP out of buffers, ...). Call only from the transport task.
bool send(const uint8_t* data, size_t len);

}  // namespace transport
