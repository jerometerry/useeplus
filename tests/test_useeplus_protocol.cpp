#include <gtest/gtest.h>

extern "C" {
#include "useeplus_protocol.h"
}

TEST(ProtocolTest, ValidMjpegPayloadWithDefaultInitializer) {
    struct up_video_frm_frag_hdr payload_header = {};
    bool valid;

    valid = up_is_valid_video_frm_frag_hdr(&payload_header);

    EXPECT_TRUE(valid);
}

TEST(ProtocolTest, ValidMjpegPayloadWithInvalidCameraNumber) {
    struct up_video_frm_frag_hdr payload_header = {};
    bool valid;

    payload_header.device_number = 99;
    valid = up_is_valid_video_frm_frag_hdr(&payload_header);

    EXPECT_FALSE(valid);
}

TEST(ProtocolTest, ValidMjpegPayloadWithHasGravitySensorSet) {
    struct up_video_frm_frag_hdr payload_header = {};
    bool valid;

    up_set_has_gravity_sensor(&payload_header, true);
    valid = up_is_valid_video_frm_frag_hdr(&payload_header);

    EXPECT_FALSE(valid);
}

TEST(ProtocolTest, ValidMjpegPayloadWithButtonPressedSet) {
    struct up_video_frm_frag_hdr payload_header = {};
    bool valid;

    up_set_button_pressed(&payload_header, true);
    valid = up_is_valid_video_frm_frag_hdr(&payload_header);

    EXPECT_TRUE(valid);
}

TEST(ProtocolTest, ValidMjpegPayloadWithOtherFlagsSet) {
    struct up_video_frm_frag_hdr payload_header = {};
    bool valid;

    up_set_other_flags(&payload_header, 3);
    valid = up_is_valid_video_frm_frag_hdr(&payload_header);

    EXPECT_FALSE(valid);
}

TEST(ProtocolTest, ValidatesDeviceId) {
    EXPECT_TRUE(up_is_valid_dev_id(VIDEO_CAMERA_ID));
    EXPECT_TRUE(up_is_valid_dev_id(GRAVITY_SENSOR_ID));
    EXPECT_FALSE(up_is_valid_dev_id(0xFF));
}

TEST(ProtocolTest, ValidatesPacketDelimeter) {
    EXPECT_TRUE(up_is_valid_usb_frm_del(UP_PKT_DEL));
    EXPECT_FALSE(up_is_valid_usb_frm_del(0x0000));
}

TEST(ProtocolTest, ChecksPacketHeaderCombination) {
    EXPECT_TRUE(up_check_usb_frm_hdr(UP_PKT_DEL, VIDEO_CAMERA_ID));
    EXPECT_FALSE(up_check_usb_frm_hdr(0x0000, VIDEO_CAMERA_ID));
    EXPECT_FALSE(up_check_usb_frm_hdr(UP_PKT_DEL, 0xFF));
}

TEST(ProtocolTest, GetsPacketDelimeterAndLength) {
    struct up_usb_frm_hdr pkt = {};
    pkt.le_delimiter = le16_to_cpu(UP_PKT_DEL);
    pkt.le_length = le16_to_cpu(500);

    EXPECT_EQ(up_get_usb_frm_del(&pkt), UP_PKT_DEL);
    EXPECT_EQ(up_get_usb_frm_pl_len(&pkt), 500);
}

TEST(ProtocolTest, ValidatesFullPacketHeaderStruct) {
    struct up_usb_frm_hdr pkt = {};
    pkt.le_delimiter = le16_to_cpu(UP_PKT_DEL);
    pkt.device_id = VIDEO_CAMERA_ID;

    EXPECT_TRUE(up_is_valid_usb_frm_hdr(&pkt));

    pkt.device_id = 0xFF;
    EXPECT_FALSE(up_is_valid_usb_frm_hdr(&pkt));
}

TEST(ProtocolTest, GetsHeaderPointersFromBuffer) {
    u8 buffer[100] = {0};
    struct up_usb_frm_hdr* pkt = up_get_usb_frm_hdr(buffer, 10);
    struct up_video_frm_frag_hdr* pl = up_get_video_frm_frag_hdr(buffer, 20);

    EXPECT_EQ((u8*)pkt, buffer + 10);
    EXPECT_EQ((u8*)pl, buffer + 20);
}

TEST(ProtocolTest, GravitySensorFlags) {
    struct up_video_frm_frag_hdr pl = {};
    EXPECT_FALSE(up_has_gravity_sensor(pl.flags));

    up_set_has_gravity_sensor(&pl, true);
    EXPECT_TRUE(up_has_gravity_sensor(pl.flags));
    EXPECT_EQ(pl.flags, 0x01);

    up_set_has_gravity_sensor(&pl, false);
    EXPECT_FALSE(up_has_gravity_sensor(pl.flags));
    EXPECT_EQ(pl.flags, 0x00);
}

TEST(ProtocolTest, ButtonPressedFlags) {
    struct up_video_frm_frag_hdr pl = {};
    EXPECT_FALSE(up_is_button_pressed(pl.flags));

    up_set_button_pressed(&pl, true);
    EXPECT_TRUE(up_is_button_pressed(pl.flags));
    EXPECT_EQ(pl.flags, 0x02);

    up_set_button_pressed(&pl, false);
    EXPECT_FALSE(up_is_button_pressed(pl.flags));
    EXPECT_EQ(pl.flags, 0x00);
}

TEST(ProtocolTest, OtherFlags) {
    struct up_video_frm_frag_hdr pl = {};
    EXPECT_FALSE(up_has_other_flags(pl.flags));
    EXPECT_EQ(up_get_other_flags(pl.flags), 0);

    up_set_other_flags(&pl, 0x2A);
    EXPECT_TRUE(up_has_other_flags(pl.flags));
    EXPECT_EQ(up_get_other_flags(pl.flags), 0x2A);

    up_set_has_gravity_sensor(&pl, true);
    up_set_button_pressed(&pl, true);
    up_set_other_flags(&pl, 0x05);

    EXPECT_TRUE(up_has_gravity_sensor(pl.flags));
    EXPECT_TRUE(up_is_button_pressed(pl.flags));
    EXPECT_EQ(up_get_other_flags(pl.flags), 0x05);
}