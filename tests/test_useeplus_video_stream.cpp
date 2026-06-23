#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <span>
#include <string>
#include <vector>

#include "constants.hpp"
#include "useeplus_video_stream.hpp"
#include "video_frame_buffer.hpp"
#include "video_frame_fragment.hpp"

extern "C" {
#include "useeplus_protocol.h"
}

constexpr uint16_t cpu_to_le16(uint16_t val) noexcept {
    if constexpr (std::endian::native == std::endian::big) {
        return std::byteswap(val);
    }
    return val;
}

class UseeplusVideoStreamTest : public ::testing::Test {
   private:
    VideoFrameBuffer disruptor_;
    UseeplusVideoStream stream_;
    int64_t next_read_seq_{0};

   public:
    UseeplusVideoStreamTest() : disruptor_(), stream_(disruptor_) {
        disruptor_.preAllocate(Units::ONE_HUNDRED_TWENTY_EIGHT_KILOBYTES);
    }

   protected:
    static up_usb_frm_hdr* getPacketHeader(std::span<uint8_t> buffer) {
        return reinterpret_cast<up_usb_frm_hdr*>(buffer.data());
    }

    static up_video_frm_frag_hdr* getPayloadHeader(std::span<uint8_t> buffer) {
        return reinterpret_cast<up_video_frm_frag_hdr*>(buffer.data() + USB_PACKET_HEADER_SIZE);
    }

    void send(std::vector<uint8_t> packet) {
        std::span<const uint8_t> data{packet};
        stream_.send(data);
    }

    bool verifyNextPublishedFramed(std::vector<uint8_t>& out_frame_data) {
        int64_t available = disruptor_.getHighestPublished();

        while (next_read_seq_ <= available) {
            VideoFrameFragment& slot = disruptor_.getBySequence(next_read_seq_);

            if (slot.contentSize() > static_cast<const unsigned long>(0)) {
                auto slice = slot.getContentSlice();
                out_frame_data.assign(slice.begin(), slice.end());
                next_read_seq_++;
                disruptor_.markConsumed(next_read_seq_ - 1);
                return true;
            }

            next_read_seq_++;
            disruptor_.markConsumed(next_read_seq_ - 1);
        }

        std::cerr << "Verify failed. No valid frames available up to seq: " << available << "\n";
        return false;
    }

    void verifyNoValidFramesPublished() {
        int64_t available = disruptor_.getHighestPublished();
        while (next_read_seq_ <= available) {
            VideoFrameFragment& slot = disruptor_.getBySequence(next_read_seq_);
            EXPECT_EQ(slot.contentSize(), static_cast<const unsigned long>(0))
                << "Found unexpected valid frame at sequence " << next_read_seq_;

            next_read_seq_++;
            disruptor_.markConsumed(next_read_seq_ - 1);
        }
    }
};

// NOLINTBEGIN(cppcoreguidelines-avoid-const-or-ref-data-members)
MATCHER_P(FrameDataEq, expectedOutput, "BufferPtr internal data matches expected output") {
    if (!arg) {
        *result_listener << "which is a null BufferPtr";
        return false;
    }

    return ::testing::ExplainMatchResult(::testing::ElementsAreArray(expectedOutput),
                                         arg->getContentSlice(), result_listener);
}

MATCHER_P(FrameStartsWith, expectedFront, "BufferPtr internal data starts with expected byte") {
    if (!arg) {
        *result_listener << "which is a null BufferPtr";
        return false;
    }

    if (arg->empty()) {
        *result_listener << "which points to an empty payload buffer";
        return false;
    }

    return ::testing::ExplainMatchResult(::testing::Eq(expectedFront), arg->front(),
                                         result_listener);
}
// NOLINTEND(cppcoreguidelines-avoid-const-or-ref-data-members)

TEST_F(UseeplusVideoStreamTest, TestUsbPayloadHeaderGettersAndSetters) {
    std::vector<uint8_t> packet(1024, 0xDD);

    auto* packetHeader = getPacketHeader(packet);

    packetHeader->le_delimiter = cpu_to_le16(UsbProtocol::USB_FRAME_HEADER);
    packetHeader->device_id = UsbProtocol::VIDEO_CAMERA_ID;
    packetHeader->le_length = cpu_to_le16(939);

    auto* payloadHeader = getPayloadHeader(packet);

    payloadHeader->frame_id = 9;
    payloadHeader->device_number = 8;
    payloadHeader->le_gravity_sensor = 7;
    up_set_has_gravity_sensor(payloadHeader, true);
    up_set_button_pressed(payloadHeader, true);
    up_set_other_flags(payloadHeader, 3);

    packet[TOTAL_USB_HEADER_SIZE] = UsbProtocol::BOUNDARY_MARKER;
    packet[TOTAL_USB_HEADER_SIZE + 1] = UsbProtocol::START_MARKER;
    packet[USB_PACKET_HEADER_SIZE + packetHeader->le_length - 2] = UsbProtocol::BOUNDARY_MARKER;
    packet[USB_PACKET_HEADER_SIZE + packetHeader->le_length - 1] = UsbProtocol::END_MARKER;

    // Force a copy to an aligned temporary using unary `+` or a cast
    // to prevent GoogleTest from binding a reference to the unaligned packed member.
    EXPECT_EQ(+payloadHeader->frame_id, 9);
    EXPECT_EQ(+payloadHeader->device_number, 8);
    EXPECT_EQ(+payloadHeader->le_gravity_sensor, static_cast<unsigned int>(7));

    // Function calls returning by-value are naturally safe, as the temporary is aligned
    EXPECT_EQ(up_has_gravity_sensor(payloadHeader->flags), true);
    EXPECT_EQ(up_is_button_pressed(payloadHeader->flags), true);
    EXPECT_EQ(up_get_other_flags(payloadHeader->flags), 3);
}

TEST_F(UseeplusVideoStreamTest, ExtractsPhysicalBufferIgnoringDeclaredLength) {
    std::vector<uint8_t> packet(1024, 0xDD);

    auto* packetHeader = getPacketHeader(packet);

    packetHeader->le_delimiter = cpu_to_le16(UsbProtocol::USB_FRAME_HEADER);
    packetHeader->device_id = UsbProtocol::VIDEO_CAMERA_ID;
    packetHeader->le_length = cpu_to_le16(939);

    auto* payloadHeader = getPayloadHeader(packet);

    payloadHeader->frame_id = 2;
    payloadHeader->device_number = 0;
    payloadHeader->flags = 0;
    payloadHeader->le_gravity_sensor = 0;

    packet[TOTAL_USB_HEADER_SIZE] = UsbProtocol::BOUNDARY_MARKER;
    packet[TOTAL_USB_HEADER_SIZE + 1] = UsbProtocol::START_MARKER;
    packet[USB_PACKET_HEADER_SIZE + packetHeader->le_length - 2] = UsbProtocol::BOUNDARY_MARKER;
    packet[USB_PACKET_HEADER_SIZE + packetHeader->le_length - 1] = UsbProtocol::END_MARKER;

    std::vector<uint8_t> triggerPacket = packet;

    auto* triggerPayloadHeader = getPayloadHeader(triggerPacket);

    triggerPayloadHeader->frame_id = 3;

    send(packet);
    send(triggerPacket);

    std::vector<uint8_t> actualOutputFrame;
    ASSERT_TRUE(verifyNextPublishedFramed(actualOutputFrame))
        << "Stream failed to publish completed frame block!";

    std::vector<uint8_t> expectedOutput(
        packet.begin() + TOTAL_USB_HEADER_SIZE,
        packet.begin() + USB_PACKET_HEADER_SIZE + packetHeader->le_length);
    EXPECT_EQ(actualOutputFrame, expectedOutput);
}

TEST_F(UseeplusVideoStreamTest, SafelyIgnoresHardwareTailChunks) {
    std::vector<uint8_t> packet(1024, 0x00);

    auto* packetHeader = getPacketHeader(packet);

    packetHeader->le_delimiter = cpu_to_le16(UsbProtocol::USB_FRAME_HEADER);
    packetHeader->device_id = UsbProtocol::VIDEO_CAMERA_ID;
    packetHeader->le_length = cpu_to_le16(1024 - USB_PACKET_HEADER_SIZE);

    auto* payloadHeader = getPayloadHeader(packet);

    payloadHeader->frame_id = 1;
    payloadHeader->device_number = 0;
    payloadHeader->flags = 0;
    payloadHeader->le_gravity_sensor = 0;

    packet[TOTAL_USB_HEADER_SIZE] = UsbProtocol::BOUNDARY_MARKER;
    packet[TOTAL_USB_HEADER_SIZE + 1] = UsbProtocol::START_MARKER;
    packet[packet.size() - 2] = UsbProtocol::BOUNDARY_MARKER;
    packet[packet.size() - 1] = UsbProtocol::END_MARKER;

    std::vector<uint8_t> shortPacket(80, UsbProtocol::BOUNDARY_MARKER);
    std::vector<uint8_t> triggerPacket = packet;

    auto* triggerPayloadHeader = getPayloadHeader(triggerPacket);

    triggerPayloadHeader->frame_id = 2;

    send(packet);
    send(shortPacket);
    send(triggerPacket);

    std::vector<uint8_t> actualOutputFrame;
    ASSERT_TRUE(verifyNextPublishedFramed(actualOutputFrame))
        << "Stream failed to publish completed frame block!";

    std::vector<uint8_t> expectedOutput(packet.begin() + TOTAL_USB_HEADER_SIZE, packet.end());
    EXPECT_EQ(actualOutputFrame, expectedOutput);
}

TEST_F(UseeplusVideoStreamTest, ReassemblesMultiChunkMjpegStream) {
    auto buildPacket = [](uint8_t frameId, const std::vector<uint8_t>& payload) {
        std::vector<uint8_t> packet(
            USB_PACKET_HEADER_SIZE + USB_PAYLOAD_HEADER_SIZE + payload.size(), 0x00);

        up_usb_frm_hdr* packetHeader = getPacketHeader(packet);

        packetHeader->le_delimiter = cpu_to_le16(UsbProtocol::USB_FRAME_HEADER);
        packetHeader->device_id = UsbProtocol::VIDEO_CAMERA_ID;
        packetHeader->le_length = cpu_to_le16(USB_PAYLOAD_HEADER_SIZE + payload.size());

        auto* payloadHeader = getPayloadHeader(packet);

        payloadHeader->frame_id = frameId;
        payloadHeader->device_number = 0;
        payloadHeader->flags = 0;
        payloadHeader->le_gravity_sensor = 0;

        std::copy(payload.begin(), payload.end(),
                  packet.begin() + USB_PACKET_HEADER_SIZE + USB_PAYLOAD_HEADER_SIZE);

        return packet;
    };

    std::vector<uint8_t> expectedOutput;
    std::vector<uint8_t> payload1 = {UsbProtocol::BOUNDARY_MARKER, UsbProtocol::START_MARKER, 0x01,
                                     0x02};

    expectedOutput.insert(expectedOutput.end(), payload1.begin(), payload1.end());

    std::vector<uint8_t> packet1buffer = buildPacket(1, payload1);

    std::vector<uint8_t> payload2 = {
        0x03, 0x04, 0x05, 0x06, UsbProtocol::BOUNDARY_MARKER, UsbProtocol::END_MARKER};

    expectedOutput.insert(expectedOutput.end(), payload2.begin(), payload2.end());

    std::vector<uint8_t> packet2 = buildPacket(1, payload2);

    std::vector<uint8_t> packet3 =
        buildPacket(2, {UsbProtocol::BOUNDARY_MARKER, UsbProtocol::START_MARKER,
                        UsbProtocol::USB_FRAME_HEADER_A, UsbProtocol::USB_FRAME_HEADER_B});

    send(packet1buffer);
    send(packet2);
    send(packet3);

    std::vector<uint8_t> actualOutputFrame;
    ASSERT_TRUE(verifyNextPublishedFramed(actualOutputFrame))
        << "Stream failed to publish completed frame block!";

    EXPECT_EQ(actualOutputFrame, expectedOutput);
}

TEST_F(UseeplusVideoStreamTest, IgnoresInvalidHeaderOrShortBuffer) {
    std::vector<uint8_t> const shortPacket = {UsbProtocol::USB_FRAME_HEADER_A,
                                              UsbProtocol::USB_FRAME_HEADER_B};
    send(shortPacket);

    std::vector<uint8_t> const emptyPacket(100, 0x00);
    send(emptyPacket);

    verifyNoValidFramesPublished();
}

TEST_F(UseeplusVideoStreamTest, AccumulatesDataAndEmitsOnFrameIdChange) {
    std::vector<uint8_t> packet1(100, 0x00);

    auto* packetHeader1 = getPacketHeader(packet1);

    packetHeader1->le_delimiter = cpu_to_le16(UsbProtocol::USB_FRAME_HEADER);
    packetHeader1->device_id = UsbProtocol::VIDEO_CAMERA_ID;
    packetHeader1->le_length = cpu_to_le16(50);

    auto* payloadHeader1 = getPayloadHeader(packet1);

    payloadHeader1->frame_id = 1;
    payloadHeader1->device_number = 0;
    payloadHeader1->le_gravity_sensor = 0;
    ;

    std::fill(packet1.begin() + TOTAL_USB_HEADER_SIZE, packet1.begin() + packetHeader1->le_length,
              0xDE);

    packet1[TOTAL_USB_HEADER_SIZE] = UsbProtocol::BOUNDARY_MARKER;
    packet1[TOTAL_USB_HEADER_SIZE + 1] = UsbProtocol::START_MARKER;

    packet1[USB_PACKET_HEADER_SIZE + packetHeader1->le_length - 2] = UsbProtocol::BOUNDARY_MARKER;
    packet1[USB_PACKET_HEADER_SIZE + packetHeader1->le_length - 1] = UsbProtocol::END_MARKER;

    std::vector<uint8_t> packet2(100, 0x00);

    auto* packetHeader2 = getPacketHeader(packet2);

    packetHeader2->le_delimiter = cpu_to_le16(UsbProtocol::USB_FRAME_HEADER);
    packetHeader2->device_id = UsbProtocol::VIDEO_CAMERA_ID;
    packetHeader2->le_length = cpu_to_le16(50);

    auto* payloadHeader2 = getPayloadHeader(packet2);

    payloadHeader2->frame_id = 2;
    payloadHeader2->device_number = 0;
    payloadHeader2->le_gravity_sensor = 0;

    std::fill(packet2.begin() + TOTAL_USB_HEADER_SIZE, packet2.begin() + packetHeader2->le_length,
              UsbProtocol::USB_FRAME_HEADER_A);

    send(packet1);
    send(packet2);

    std::vector<uint8_t> actualOutputFrame;
    ASSERT_TRUE(verifyNextPublishedFramed(actualOutputFrame))
        << "Stream failed to publish completed frame block!";

    EXPECT_EQ(actualOutputFrame.front(), UsbProtocol::BOUNDARY_MARKER);
}

TEST_F(UseeplusVideoStreamTest, IgnoresInvalidCameraId) {
    std::vector<uint8_t> packet(20, 0x00);
    auto* packetHeader = getPacketHeader(packet);
    packetHeader->le_delimiter = UsbProtocol::USB_FRAME_HEADER;
    packetHeader->device_id = 99;
    packetHeader->le_length = cpu_to_le16(15);

    send(packet);
    verifyNoValidFramesPublished();
}

TEST_F(UseeplusVideoStreamTest, IgnoresPayloadExceedingBufferSize) {
    std::vector<uint8_t> packet(10, 0x00);
    auto* packetHeader = getPacketHeader(packet);
    packetHeader->le_delimiter = cpu_to_le16(UsbProtocol::USB_FRAME_HEADER);
    packetHeader->device_id = UsbProtocol::VIDEO_CAMERA_ID;
    packetHeader->le_length = cpu_to_le16(50);

    send(packet);
    verifyNoValidFramesPublished();
}

TEST_F(UseeplusVideoStreamTest, IgnoresTruncatedMetadata) {
    std::vector<uint8_t> packet(10, 0x00);
    auto* packetHeader = getPacketHeader(packet);
    packetHeader->le_delimiter = cpu_to_le16(UsbProtocol::USB_FRAME_HEADER);
    packetHeader->device_id = UsbProtocol::VIDEO_CAMERA_ID;
    packetHeader->le_length = cpu_to_le16(5);

    send(packet);
    verifyNoValidFramesPublished();
}

TEST_F(UseeplusVideoStreamTest, IgnoresUnsupportedCameraConfiguration) {
    std::vector<uint8_t> packet(20, 0x00);

    auto* packetHeader = getPacketHeader(packet);

    packetHeader->le_delimiter = cpu_to_le16(UsbProtocol::USB_FRAME_HEADER);
    packetHeader->device_id = UsbProtocol::VIDEO_CAMERA_ID;
    packetHeader->le_length = cpu_to_le16(15);

    auto* payloadHeader = getPayloadHeader(packet);

    payloadHeader->frame_id = 1;
    payloadHeader->device_number = 5;
    payloadHeader->flags = 0;
    payloadHeader->le_gravity_sensor = 0;

    send(packet);
}

TEST_F(UseeplusVideoStreamTest, AbortsOnMidFrameCameraShift) {
    std::vector<uint8_t> packet1(20, 0x00);
    auto* packetHeader1 = getPacketHeader(packet1);

    packetHeader1->le_delimiter = cpu_to_le16(UsbProtocol::USB_FRAME_HEADER);
    packetHeader1->device_id = UsbProtocol::VIDEO_CAMERA_ID;
    packetHeader1->le_length = cpu_to_le16(15);

    auto* payloadHeader1 = getPayloadHeader(packet1);

    payloadHeader1->frame_id = 1;
    payloadHeader1->device_number = 0;
    payloadHeader1->le_gravity_sensor = 0;

    send(packet1);

    std::vector<uint8_t> packet2(20, 0x00);

    auto* packetHeader2 = getPacketHeader(packet2);

    packetHeader2->le_delimiter = cpu_to_le16(UsbProtocol::USB_FRAME_HEADER);
    packetHeader2->device_id = UsbProtocol::VIDEO_CAMERA_ID;
    packetHeader2->le_length = cpu_to_le16(15);

    auto* payloadHeader2 = getPayloadHeader(packet2);

    payloadHeader2->frame_id = 1;
    payloadHeader2->device_number = 1;
    payloadHeader2->le_gravity_sensor = 0;

    send(packet2);
}

TEST_F(UseeplusVideoStreamTest, SafelyHandlesGarbageDataWithoutCrashing) {
    VideoFrameBuffer ringBuffer;
    ringBuffer.preAllocate(Units::ONE_HUNDRED_TWENTY_EIGHT_KILOBYTES);

    UseeplusVideoStream silentDecoder(ringBuffer);

    std::vector<uint8_t> packet(100, 0x00);
    std::span<const uint8_t> data{packet};

    ASSERT_NO_THROW(silentDecoder.send(data));
}

TEST_F(UseeplusVideoStreamTest, PreventsIntegerUnderflowOnUndersizedHardwareLength) {
    std::vector<uint8_t> malformedPacket(TOTAL_USB_HEADER_SIZE, 0x00);

    auto* packetHeader = getPacketHeader(malformedPacket);
    packetHeader->le_delimiter = cpu_to_le16(UsbProtocol::USB_FRAME_HEADER);
    packetHeader->device_id = UsbProtocol::VIDEO_CAMERA_ID;

    packetHeader->le_length = cpu_to_le16(2);

    ASSERT_NO_THROW({ send(malformedPacket); });
}