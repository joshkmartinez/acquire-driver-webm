#include "device/props/storage.h"
#include "device/kit/storage.h"
#include "platform.h"
#include "logger.h"

#include <string.h>
#include <stdlib.h>
#include <vector>

#include <vpx/vpx_image.h>
#include <vpx/vpx_encoder.h>
#include <vpx/vp8cx.h>
#include <vp9/common/vp9_common.h>
#include <mkvmuxer/mkvwriter.h>

#define LOG(...) aq_logger(0, __FILE__, __LINE__, __FUNCTION__, __VA_ARGS__)
#define LOGE(...) aq_logger(1, __FILE__, __LINE__, __FUNCTION__, __VA_ARGS__)
#define CHECK(e)                                                               \
    do {                                                                       \
        if (!(e)) {                                                            \
            LOGE("Expression evaluated as false:\n\t%s", #e);                  \
            goto Error;                                                        \
        }                                                                      \
    } while (0)
#define TODO                                                                   \
    do {                                                                       \
        LOGE("TODO: Unimplemented");                                           \
        goto Error;                                                            \
    } while (0)

#define containerof(ptr, T, V) ((T*)(((char*)(ptr)) - offsetof(T, V)))

struct WebmStorage : public Storage
{
    struct StorageProperties properties;
    vpx_codec_ctx_t codec = { 0 }; // The VPX codec context, start w/ zerod out
                                   // memory to know when its uninitialized
    vpx_codec_enc_cfg_t cfg;       // The VPX codec configuration
    mkvmuxer::MkvWriter mkvWriter;
    mkvmuxer::Segment segment;
    uint64_t track; // Track Number

    WebmStorage();
};

static enum DeviceState
webm_set(struct Storage* self_, const struct StorageProperties* properties)
{
    struct WebmStorage* self = (struct WebmStorage*)self_;

    // TODO: What's the purpose of this check?
    CHECK(storage_properties_copy(&(self->properties), properties));
    {
        self->cfg.g_timebase.num = 1;
        self->cfg.g_timebase.den =
          30; // 30 fps // TODO: Check, just assuming 30fps? Or some other configuration prop?
        vpx_codec_enc_config_default(vpx_codec_vp9_cx(), &(self->cfg), 0);
    }

    return DeviceState_Armed;
Error:
    return DeviceState_AwaitingConfiguration;
}

static void
webm_get(const struct Storage* self_, struct StorageProperties* settings)
{
    struct WebmStorage* self = (struct WebmStorage*)self_;
    *settings = self->properties;
}

static void
webm_get_meta(const struct Storage* self_, struct StoragePropertyMetadata* meta)
{
    LOG("herfe");
    // LOG(meta);
    CHECK(meta);
    *meta = (struct StoragePropertyMetadata){ 0 };
Error:
    return;
}

static enum DeviceState
webm_start(struct Storage* self_)
{
    LOG("Webm: Starting");
    struct WebmStorage* self = (struct WebmStorage*)self_;

    const uint64_t track = self->segment.AddVideoTrack(
      self->cfg.g_w, self->cfg.g_h, 0); // Track starting at index 0
    if (!track) {
        LOGE("Failed to add video track");
        goto Error;
    }
    {
        self->track = track;

        self->mkvWriter.Open(self->properties.filename.str);

        mkvmuxer::SegmentInfo* info = self->segment.GetSegmentInfo();
        info->set_writing_app("AcquireWebmWriter");
        info->set_timecode_scale(
          1e9 * (static_cast<double>(self->cfg.g_timebase.num) /
                 self->cfg.g_timebase.den));

        if (!self->segment.Init(&self->mkvWriter)) {
            LOGE("Failed to initialize muxer segment");
            goto Error;
        }
    }

    return DeviceState_Running;

Error:
    return DeviceState_AwaitingConfiguration;
}

vpx_image_t*
convert_to_vpx_image_u8(const struct VideoFrame* frame)
{
    const uint32_t width = frame->shape.dims.width;
    const uint32_t height = frame->shape.dims.height;

    vpx_image_t* img =
      vpx_img_alloc(nullptr, VPX_IMG_FMT_I420, width, height, 1);
    if (!img) {
        LOGE("Failed to allocate vpx image");
        goto Error;
    }
    CHECK(img->bit_depth == 8);

    for (int y = 0; y < height; ++y) {
        memcpy(img->planes[VPX_PLANE_Y] + y * img->stride[VPX_PLANE_Y],
               frame->data + y * frame->shape.strides.height,
               width);
    }

    for (int y = 0; y < height / 2; ++y) {
        memset(
          img->planes[VPX_PLANE_U] + y * img->stride[VPX_PLANE_U], 128, width);
        memset(
          img->planes[VPX_PLANE_V] + y * img->stride[VPX_PLANE_V], 128, width);
    }

    return img;
Error:
    throw(std::runtime_error("Failed to convert to vpx image u8"));
}

static void
memset16(void* ptr, uint16_t val, size_t n)
{
    uint16_t* p = (uint16_t*)ptr;
    for (size_t i = 0; i < n; ++i) {
        p[i] = val;
    }
}

// TODO: FIX, known issue
// Not producing correct images
// Y & U & V channels are funny
vpx_image_t*
convert_to_vpx_image_u16(const struct VideoFrame* frame)
{
    const uint32_t width = frame->shape.dims.width;
    const uint32_t height = frame->shape.dims.height;

    vpx_image_t* img =
      vpx_img_alloc(nullptr, VPX_IMG_FMT_I42016, width, height, 1);
    if (!img) {
        LOGE("Failed to allocate vpx image");
        goto Error;
    }
    CHECK(img->bit_depth == 16);

    for (int y = 0; y < height; ++y) {
        memcpy(img->planes[VPX_PLANE_Y] + y * img->stride[VPX_PLANE_Y],
               frame->data + y * frame->shape.strides.height * 2,
               width * 2);
    }

    for (int y = 0; y < height / 2; ++y) {
        memset16(img->planes[VPX_PLANE_U] + y * img->stride[VPX_PLANE_U],
                 1 << 15, // Midpoint of 16 bits
                 width);
        memset16(img->planes[VPX_PLANE_V] + y * img->stride[VPX_PLANE_V],
                 1 << 15, // Midpoint of 16 bits
                 width);
    }

    return img;

Error:
    throw(std::runtime_error("Failed to convert to vpx image u16"));
}

// Given a VideoFrame, convert its images to a vector of vpx_image_t's which
// will be passed to the encoder.
std::vector<vpx_image_t*>
convert_to_vpx_images(struct Storage* self_, const struct VideoFrame* frames)
{
    struct WebmStorage* self = (struct WebmStorage*)self_;

    std::vector<vpx_image_t*> images;

    // Lambda to advance to the next frame
    const auto next = [](VideoFrame* cur) -> VideoFrame* {
        return (VideoFrame*)(((uint8_t*)cur) + cur->bytes_of_frame);
    };

    VideoFrame *beg, *end, *cur;

    beg = (VideoFrame*)frames;
    end = (VideoFrame*)(((uint8_t*)frames) +
                        frames->bytes_of_frame); // TODO: Check, is this right?

    for (cur = beg; cur < end; cur = next(cur)) {
        LOG("Stream %d counting frame w id %d", 0, cur->frame_id);

        CHECK(cur->shape.dims.width == self->cfg.g_w);
        CHECK(cur->shape.dims.height == self->cfg.g_h);

        vpx_image_t* img;

        switch (cur->shape.type) {
            case SampleType_u8: {
                img = convert_to_vpx_image_u8(cur);
                break;
            }
            case SampleType_u10:
            case SampleType_u12:
            case SampleType_u14:
            case SampleType_u16: {
                img = convert_to_vpx_image_u16(cur); // TODO: Known issue, see function signature
                break;
            }
            default:
                LOGE("Unsupported sample type");
                goto Error;
        }

        images.push_back(img);
    }

    return images;
Error:
    throw(std::runtime_error("Failed to convert to vpx images"));
}

static int
encode_frame(vpx_codec_ctx_t* codec,
             vpx_image_t* img,
             int frame_index,
             int flags,
             mkvmuxer::IMkvWriter* writer,
             mkvmuxer::Segment& segment,
             const uint64_t& track) noexcept
{
    LOG("Encoding frame %d", frame_index);
    int got_pkts = 0;
    vpx_codec_iter_t iter = NULL;
    const vpx_codec_cx_pkt_t* pkt = NULL;

    const vpx_codec_err_t res =
      vpx_codec_encode(codec, img, frame_index, 1, flags, VPX_DL_REALTIME);
    if (res != VPX_CODEC_OK) {
        LOGE("Error during encoding: ", vpx_codec_error(codec));
        const char* detail = vpx_codec_error_detail(codec);
        if (detail) {
            LOGE("vpx_codec_error_detail: ", detail);
        }
        goto Error;
    }

    while ((pkt = vpx_codec_get_cx_data(codec, &iter)) != NULL) {
        got_pkts = 1;

        if (pkt->kind == VPX_CODEC_CX_FRAME_PKT) {
            // std::cout << "Frame index: " << frame_index << " - Frame size: "
            // << pkt->data.frame.sz << std::endl;
            const int keyframe =
              (pkt->data.frame.flags & VPX_FRAME_IS_KEY) != 0;
            // std::cout << "Frame PTS: " << pkt->data.frame.pts << std::endl;
            int addFrame =
              segment.AddFrame(static_cast<const uint8_t*>(pkt->data.frame.buf),
                               pkt->data.frame.sz,
                               track,
                               pkt->data.frame.pts * 1e9 / 30,
                               keyframe);
            // std::cout << "Add frame result: " << addFrame << std::endl;
            if (!addFrame) {
                LOGE("Failed to add frame to webm");
                goto Error;
            }
        }
    }

    return got_pkts;

Error:
    throw(std::runtime_error("Failed to encode frame"));
}

static enum DeviceState
webm_stop(struct Storage* self_) noexcept
{
    try {
        LOG("Webm: Stopping");
        struct WebmStorage* self = (struct WebmStorage*)self_;

        if (self->state != DeviceState_Running) {
            LOG("Webm: Not running, nothing to stop");
            return DeviceState_Armed;
        }

        if (self->codec.name !=
            NULL) { // If the codec is uninitialized, no need to flush frames
            while (encode_frame(&(self->codec),
                                nullptr,
                                -1,
                                0,
                                &(self->mkvWriter),
                                self->segment,
                                self->track)) {
                // Flushes any remaining frames
            }
        }


        if (!self->segment.Finalize()) {
            LOGE("Error finalizing segment.");
            goto Error;
        }

        self->mkvWriter.Close();
        LOG("Webm: written");
        vpx_codec_destroy(&(self->codec));

        LOG("Webm: Stopped");

        return DeviceState_Armed;
    } catch (const std::exception& exc) {
        LOGE("Exception: %s\n", exc.what());
    } catch (...) {
        LOGE("Exception: (unknown)");
    }

Error:
    return DeviceState_Running;
}

static enum DeviceState
webm_append(struct Storage* self_,
            const struct VideoFrame* frames,
            size_t* nbytes)
{
    LOG("Webm: Appending VideoFrame, frameid: %d", frames->frame_id);
    struct WebmStorage* self = (struct WebmStorage*)self_;

    if (!frames || frames->bytes_of_frame == 0 || *nbytes == 0) {
        LOGE("Invalid frame data.");
        goto Error;
    }

    {
        const uint32_t width = frames->shape.dims.width;
        const uint32_t height = frames->shape.dims.height;

        // Fetch the video track and set its properties
        mkvmuxer::VideoTrack* video = static_cast<mkvmuxer::VideoTrack*>(
          self->segment.GetTrackByNumber(self->track));
        if (!video) {
            LOGE("Could not get video track.");
            goto Error;
        }
        {
            video->set_default_duration(
              uint64_t(1e9 * (static_cast<double>(self->cfg.g_timebase.num) /
                              self->cfg.g_timebase.den)));

            video->set_codec_id("V_VP9");
            video->set_display_width(width);
            video->set_display_height(height);
            video->set_pixel_width(width);
            video->set_pixel_height(height);

            std::vector<vpx_image_t*> converted_frames =
              convert_to_vpx_images(self_, frames);

            for (vpx_image_t* img : converted_frames) {
                encode_frame(&(self->codec),
                             img,
                             frames->frame_id,
                             0,
                             &(self->mkvWriter),
                             self->segment,
                             self->track);

                vpx_img_free(img);
            }

            *nbytes = frames->bytes_of_frame;
        }
    }
    return DeviceState_Running;

Error:
    *nbytes = 0;
    return webm_stop(self_);
}

// TODO: (nclack) these don't have to be extern C
static void
webm_destroy(struct Storage* self_)
{
    struct WebmStorage* self = (struct WebmStorage*)self_;
    webm_stop(self_);
    storage_properties_destroy(&self->properties);
    free(self);
}

static void
webm_reserve_image_shape(struct Storage* self_, const struct ImageShape* shape)
{
    struct WebmStorage* self = (struct WebmStorage*)self_;
    self->cfg.g_w = shape->dims.width;
    self->cfg.g_h = shape->dims.height;

    // Initialize the VPX codec
    if (vpx_codec_enc_init(
          &(self->codec), vpx_codec_vp9_cx(), &(self->cfg), 0) !=
        VPX_CODEC_OK) {
        LOGE("Failed to initialize encoder: %s",
             vpx_codec_error(&(self->codec)));
        return; // TODO: Change to goto Error?
    }
}

WebmStorage::WebmStorage()
  : Storage{ .state = DeviceState_AwaitingConfiguration,
             .set = webm_set,
             .get = webm_get,
             .get_meta = webm_get_meta,
             .start = webm_start,
             .append = webm_append,
             .stop = webm_stop,
             .destroy = webm_destroy,
             .reserve_image_shape = webm_reserve_image_shape }
  , cfg{
      .g_timebase = { .num = 1, .den = 30 },
  }
{
    const struct PixelScale pixel_scale_um = { 1, 1 };

    CHECK(storage_properties_init(
      &properties,
      0,
      "out.webm", // TODO: this should be self -> properties.filename.str??
      sizeof("out.webm"),
      0,
      0,
      pixel_scale_um));

    return;
Error:
    throw(std::runtime_error("Failed to initialize WebmStorage"));
}

// Only make and close have to be extern, since called in webm.driver.c
extern "C" struct Storage*
make_webm_storage()
{
    try {
        return new WebmStorage;
    } catch (const std::exception& exc) {
        LOGE("Exception: %s\n", exc.what());
    } catch (...) {
        LOGE("Exception: (unknown)");
    }
    return 0;
}

extern "C" enum DeviceStatusCode
close_webm_storage(struct Storage* self_)
{
    struct WebmStorage* self = (struct WebmStorage*)self_;

    webm_stop(self_);

    delete self;

    return Device_Ok;
Error:
    return Device_Err;
}
