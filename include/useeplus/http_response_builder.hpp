#pragma once
#include <string_view>

struct VideoFrameFragment;

/**
 * @brief Utility for formatting raw JPEG buffers into HTTP chunked responses.
 */
namespace HttpResponseBuilder {
/**
 * @brief Prepends HTTP multipart boundaries directly into the Buffer's reserved padding.
 * @details By walking a pointer backward into the pre-allocated 128-byte prefix padding
 * of the Buffer, this function constructs the entire HTTP chunk header (Boundary, Content-Type,
 * and Content-Length) perfectly flush against the raw image payload.
 * * This achieves zero-copy network broadcasting, allowing the OS to transmit the header
 * and image data in a single continuous system call.
 *
 * @param frame The populated Buffer containing the JPEG image.
 * @return A single contiguous string_view encompassing both the newly written HTTP headers
 * and the image payload.
 */
std::string_view build(VideoFrameFragment& frame);
}  // namespace HttpResponseBuilder
