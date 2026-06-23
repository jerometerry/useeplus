#include <gtest/gtest.h>
#include <vector>
#include <cstdint>
#include <cstring>

#include "useeplus_protocol.h"

struct DecoderEvent {
    enum Type { START, FRAGMENT, COMPLETE, INCOMPLETE } type;
    uint8_t frame_id;
    size_t payload_size;
};

class CameraHardwareEmulator {
public:
    std::vector<uint8_t> stream;
    size_t page_offset = 0;

    void pad_to_next_4k_page() {
        if (page_offset == 0) return;

        size_t padding_size = 4096 - page_offset;
        for (size_t i = 0; i < padding_size; ++i) {
            // Inject a malicious Ghost Header in the middle of the padding gap
            if (i == 40 && padding_size >= 80) {
                stream.push_back(0xAA);
                stream.push_back(0xBB);
                stream.push_back(VIDEO_CAMERA_ID);
                stream.push_back(0xFF); // Garbage length
                stream.push_back(0xFF);
                i += 4;
            } else {
                stream.push_back(0x00);
            }
        }
        page_offset = 0;
    }

    void emit_packet(uint8_t dev_id, uint8_t frame_id, uint8_t flags, const std::vector<uint8_t>& payload) {
        uint16_t packet_size = 5 + 7 + payload.size();

        // If it crosses the 4KB hardware boundary, force the padding
        if (page_offset + packet_size > 4096) {
            pad_to_next_4k_page();
        }

        // Write USB Header (5 Bytes)
        stream.push_back(0xAA);
        stream.push_back(0xBB);
        stream.push_back(dev_id);
        uint16_t payload_len = 7 + payload.size();
        stream.push_back(payload_len & 0xFF);
        stream.push_back((payload_len >> 8) & 0xFF);

        // Write Payload Header (7 Bytes)
        stream.push_back(frame_id);
        stream.push_back(0x00); // device_number
        stream.push_back(flags);
        stream.push_back(0x00); // gravity sensor matrix
        stream.push_back(0x00);
        stream.push_back(0x00);
        stream.push_back(0x00);

        stream.insert(stream.end(), payload.begin(), payload.end());
        page_offset += packet_size;
    }

    void emit_video_frame(uint8_t frame_id, const std::vector<uint8_t>& jpeg_data, bool inject_gravity = false) {
        size_t offset = 0;
        int packet_counter = 0;

        while (offset < jpeg_data.size()) {
            size_t chunk_size = std::min<size_t>(932, jpeg_data.size() - offset);
            std::vector<uint8_t> chunk(jpeg_data.begin() + offset, jpeg_data.begin() + offset + chunk_size);

            emit_packet(VIDEO_CAMERA_ID, frame_id, 0x00, chunk);
            offset += chunk_size;
            packet_counter++;

            if (inject_gravity && (packet_counter % 3 == 0)) {
                std::vector<uint8_t> gs_data(420, 0x99);
                emit_packet(GRAVITY_SENSOR_ID, frame_id, 0x01, gs_data);
            }
        }
    }

    std::vector<uint8_t> finish() {
        pad_to_next_4k_page();
        return stream;
    }
};

class UseeplusDecoderTest : public ::testing::Test {
protected:
    std::vector<DecoderEvent> events;
    up_decoder decoder{};
    CameraHardwareEmulator camera;

    void SetUp() override {
        memset(&decoder, 0, sizeof(decoder));
        decoder.context = this;

        decoder.cb.on_video_frame_start = [](void* ctx, u8 fid, u8 dnum) {
            static_cast<UseeplusDecoderTest*>(ctx)->events.push_back({DecoderEvent::START, fid, 0});
        };
        decoder.cb.on_video_frame_fragment = [](void* ctx, u8* data, size_t len) {
            static_cast<UseeplusDecoderTest*>(ctx)->events.push_back({DecoderEvent::FRAGMENT, 0, len});
        };
        decoder.cb.on_video_frame_complete = [](void* ctx) {
            static_cast<UseeplusDecoderTest*>(ctx)->events.push_back({DecoderEvent::COMPLETE, 0, 0});
        };
        decoder.cb.on_video_frame_incomplete = [](void* ctx) {
            static_cast<UseeplusDecoderTest*>(ctx)->events.push_back({DecoderEvent::INCOMPLETE, 0, 0});
        };
    }

    std::vector<uint8_t> generate_mock_jpeg(size_t size_kb) {
        std::vector<uint8_t> jpeg(size_kb * 1024, 0x42);

        jpeg[0] = JPEG_DEL;
        jpeg[1] = JPEG_SOI;

        jpeg[jpeg.size() - 2] = JPEG_DEL;
        jpeg[jpeg.size() - 1] = JPEG_EOI;
        return jpeg;
    }
};

TEST_F(UseeplusDecoderTest, DecodesClean10KbVideoFrame) {
    std::vector<uint8_t> image = generate_mock_jpeg(10);
    camera.emit_video_frame(1, image);
    std::vector<uint8_t> stream = camera.finish();

    EXPECT_EQ(stream.size(), 4096 * 3);

    size_t consumed = up_decode_bulk(&decoder, stream.data(), stream.size());

    EXPECT_GE(consumed, stream.size() - 12);

    ASSERT_GT(events.size(), 10);
    EXPECT_EQ(events.front().type, DecoderEvent::START);
    EXPECT_EQ(events.back().type, DecoderEvent::COMPLETE);
}

TEST_F(UseeplusDecoderTest, NavigatesMultiplexedGravitySensors) {
    std::vector<uint8_t> image = generate_mock_jpeg(20);

    camera.emit_video_frame(2, image, true);
    std::vector<uint8_t> stream = camera.finish();

    size_t consumed = up_decode_bulk(&decoder, stream.data(), stream.size());

    EXPECT_GE(consumed, stream.size() - 12);

    EXPECT_EQ(events.front().type, DecoderEvent::START);
    EXPECT_EQ(events.back().type, DecoderEvent::COMPLETE);

    for (const auto& ev : events) {
        if (ev.type == DecoderEvent::FRAGMENT) {
            EXPECT_NE(ev.payload_size, 420);
        }
    }
}

TEST_F(UseeplusDecoderTest, HandlesDroppedUsbBulkTransfers) {
    std::vector<uint8_t> image1 = generate_mock_jpeg(10);
    std::vector<uint8_t> image2 = generate_mock_jpeg(10);

    camera.emit_video_frame(10, image1);

    camera.pad_to_next_4k_page();

    camera.emit_video_frame(11, image2);
    std::vector<uint8_t> stream = camera.finish();

    stream.erase(stream.begin() + 4096, stream.begin() + 12288);

    size_t consumed = up_decode_bulk(&decoder, stream.data(), stream.size());

    EXPECT_GE(consumed, stream.size() - 12);

    bool found_incomplete = false;
    for (const auto& ev : events) {
        if (ev.type == DecoderEvent::INCOMPLETE) {
            found_incomplete = true;
        }
    }

    EXPECT_TRUE(found_incomplete);

    EXPECT_EQ(events.back().type, DecoderEvent::COMPLETE);
}

TEST_F(UseeplusDecoderTest, RecoversFromCorruptedPacketLength) {
    std::vector<uint8_t> image = generate_mock_jpeg(10);
    camera.emit_video_frame(5, image);
    std::vector<uint8_t> stream = camera.finish();

    // Maliciously corrupt the length field of the very first packet.
    // The length field sits at offset 3 and 4 (after AA BB and the Dev ID).
    // We set it to 65535 (0xFFFF).
    stream[3] = 0xFF;
    stream[4] = 0xFF;

    size_t consumed = up_decode_bulk(&decoder, stream.data(), stream.size());

    // The parser should reject the massive length, fall into seek mode,
    // find the SECOND packet of the frame, and continue parsing safely.
    EXPECT_GE(consumed, stream.size() - 12);

    // Because we destroyed the first packet (which contained the SOI marker),
    // the parser should refuse to build Frame 5 and drop the remaining fragments.
    // It should NOT crash, and it should NOT fire a COMPLETE event.
    bool found_complete = false;
    for (const auto& ev : events) {
        if (ev.type == DecoderEvent::COMPLETE) found_complete = true;
    }
    EXPECT_FALSE(found_complete);
}

TEST_F(UseeplusDecoderTest, HuntsForSignatureWhenStreamIsUnaligned) {
    std::vector<uint8_t> image = generate_mock_jpeg(10);
    camera.emit_video_frame(6, image);
    std::vector<uint8_t> stream = camera.finish();

    // Prepend 3 bytes of pure garbage to the front of the stream.
    // This perfectly misaligns the 0xBBAA signature.
    stream.insert(stream.begin(), {0xDE, 0xAD, 0xBE});

    size_t consumed = up_decode_bulk(&decoder, stream.data(), stream.size());

    // The parser must consume the 3 garbage bytes, find the signature at offset 3,
    // and successfully parse the entire rest of the stream.
    EXPECT_GE(consumed, stream.size() - 12);

    // Because the stream was just shifted (not destroyed), the frame should complete!
    EXPECT_EQ(events.front().type, DecoderEvent::START);
    EXPECT_EQ(events.back().type, DecoderEvent::COMPLETE);
}

TEST_F(UseeplusDecoderTest, ReturnsZeroWhenStarvedOfHeaderData) {
    std::vector<uint8_t> image = generate_mock_jpeg(10);
    camera.emit_video_frame(7, image);
    std::vector<uint8_t> stream = camera.finish();

    // Feed the decoder ONLY the first 4 bytes of the stream.
    // A full USB + Payload header requires 12 bytes.
    size_t consumed = up_decode_bulk(&decoder, stream.data(), 4);

    // It must refuse to consume any bytes.
    EXPECT_EQ(consumed, 0);

    // It must not fire any callbacks.
    EXPECT_EQ(events.size(), 0);
}

