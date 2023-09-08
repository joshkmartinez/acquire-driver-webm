#include "acquire.h"
#include "device/hal/device.manager.h"
#include "platform.h"
#include "logger.h"

#include <cstdio>
#include <exception>
#include <stdexcept>
#include <string>

// Taken from hdcam

void
reporter(int is_error,
         const char* file,
         int line,
         const char* function,
         const char* msg)
{
    fprintf(is_error ? stderr : stdout,
            "%s%s(%d) - %s: %s\n",
            is_error ? "ERROR " : "",
            file,
            line,
            function,
            msg);
}

/// Helper for passing size static strings as function args.
/// For a function: `f(char*,size_t)` use `f(SIZED("hello"))`.
/// Expands to `f("hello",5)`.
#define SIZED(str) str, sizeof(str)

#define L (aq_logger)
#define LOG(...) L(0, __FILE__, __LINE__, __FUNCTION__, __VA_ARGS__)
#define ERR(...) L(1, __FILE__, __LINE__, __FUNCTION__, __VA_ARGS__)
#define EXPECT(e, ...)                                                         \
    do {                                                                       \
        if (!(e)) {                                                            \
            char buf[1 << 8] = { 0 };                                          \
            ERR(__VA_ARGS__);                                                  \
            snprintf(buf, sizeof(buf) - 1, __VA_ARGS__);                       \
            throw std::runtime_error(buf);                                     \
        }                                                                      \
    } while (0)
#define CHECK(e) EXPECT(e, "Expression evaluated as false: %s", #e)
#define DEVOK(e) CHECK(Device_Ok == (e))
#define OK(e) CHECK(AcquireStatus_Ok == (e))

int
main()
{

    auto runtime = acquire_init(reporter);
    try {

        auto dm = acquire_device_manager(runtime);
        CHECK(runtime);
        CHECK(dm);

        AcquireProperties props = {};
        OK(acquire_get_configuration(runtime, &props));

        DEVOK(device_manager_select(dm,
                                    DeviceKind_Camera,
                                    SIZED(".*sin.*") - 1,
                                    &props.video[0].camera.identifier));
        DEVOK(device_manager_select(dm,
                                    DeviceKind_Storage,
                                    SIZED("webm") - 1,
                                    &props.video[0].storage.identifier));

        storage_properties_init(
          &props.video[0].storage.settings,
          0,
          SIZED("/Users/joshua.martinez/Desktop/Code/Aquire/"
                "acquire-driver-webm/" // TODO: don't use absolute file path
                                       // here
                "build/out.webm"),
          0,
          0,
          { 0 });

        OK(acquire_configure(runtime, &props));

        AcquirePropertyMetadata metadata = { 0 };
        OK(acquire_get_configuration_metadata(runtime, &metadata));

        props.video[0].camera.settings.binning = 1;
        props.video[0].camera.settings.pixel_type =
          SampleType_u8; // TODO: Create a test for u16
        props.video[0].camera.settings.shape = { .x = 1920, .y = 1080 };
        props.video[0].camera.settings.exposure_time_us = 1e4;
        props.video[0].max_frame_count = 10;

        OK(acquire_configure(runtime, &props));

        CHECK(props.video[0]
                .camera.settings.input_triggers.acquisition_start.enable == 0);
        CHECK(
          props.video[0].camera.settings.input_triggers.frame_start.enable ==
          0);
        CHECK(props.video[0].camera.settings.input_triggers.exposure.enable ==
              0);

        const auto next = [](VideoFrame* cur) -> VideoFrame* {
            return (VideoFrame*)(((uint8_t*)cur) + cur->bytes_of_frame);
        };

        const auto consumed_bytes = [](const VideoFrame* const cur,
                                       const VideoFrame* const end) -> size_t {
            return (uint8_t*)end - (uint8_t*)cur;
        };

        struct clock clock
        {};
        // expected time to acquire frames + 100%
        static double time_limit_ms =
          (props.video[0].max_frame_count) * 1000.0 *
          6.0; // played around with this time limit, there are some instances where the factor needs longer, 6 seems to be the most consistent
        clock_init(&clock);
        clock_shift_ms(&clock, time_limit_ms);
        OK(acquire_start(runtime));
        {
            uint64_t nframes = 0;
            while (nframes < props.video[0].max_frame_count) {
                struct clock throttle
                {};
                clock_init(&throttle);
                // This time limit check makes the test brittle, doesn't always finish in time
                EXPECT(clock_cmp_now(&clock) < 0,
                       "Timeout at %f ms",
                       clock_toc_ms(&clock) + time_limit_ms);
                VideoFrame *beg, *end, *cur;
                OK(acquire_map_read(runtime, 0, &beg, &end));
                for (cur = beg; cur < end; cur = next(cur)) {
                    LOG("stream %d counting frame w id %d", 0, cur->frame_id);
                    CHECK(cur->shape.dims.width ==
                          props.video[0].camera.settings.shape.x);
                    CHECK(cur->shape.dims.height ==
                          props.video[0].camera.settings.shape.y);
                    ++nframes;
                }
                {
                    uint32_t n = (uint32_t)consumed_bytes(beg, end);
                    OK(acquire_unmap_read(runtime, 0, n));
                    if (n)
                        LOG("stream %d consumed bytes %d", 0, n);
                }
                clock_sleep_ms(&throttle, 100.0f);

                LOG("stream %d nframes %d. remaining time %f s",
                    0,
                    nframes,
                    -1e-3 * clock_toc_ms(&clock));
            }

            CHECK(nframes == props.video[0].max_frame_count);
        }

        OK(acquire_stop(runtime));
        OK(acquire_shutdown(runtime));

        // Check if the file exists and is not empty
        FILE* f = fopen(
          "/Users/joshua.martinez/Desktop/Code/Aquire/acquire-driver-webm/"
          "build/out.webm",
          "r");
        CHECK(f);
        fseek(f, 0, SEEK_END);
        CHECK(ftell(f) > 0); // Ensure the file is not empty

        // Print file size
        LOG("Webm file size: %ld bytes", ftell(f));

        return 0;
    } catch (const std::exception& e) {
        ERR("Exception: %s", e.what());
    } catch (...) {
        ERR("Exception: (unknown)");
    }
    if (runtime)
        acquire_shutdown(runtime);
    return 1;
}