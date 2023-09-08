#include "device/kit/driver.h"
#include "device/kit/camera.h"
#include "device/kit/storage.h"
#include "logger.h"

#include <stdlib.h>
#include <string.h>

#define containerof(P, T, F) ((T*)(((char*)(P)) - offsetof(T, F)))

#define L aq_logger
#define LOG(...) L(0, __FILE__, __LINE__, __FUNCTION__, __VA_ARGS__)
#define LOGE(...) L(1, __FILE__, __LINE__, __FUNCTION__, __VA_ARGS__)
#define EXPECT(e, ...)                                                         \
    do {                                                                       \
        if (!(e)) {                                                            \
            LOGE(__VA_ARGS__);                                                 \
            goto Error;                                                        \
        }                                                                      \
    } while (0)
#define CHECK(e) EXPECT(e, "Expression evaluated as false:\n\t%s", #e)

struct WebmDriver
{
    struct Driver driver;
};

static unsigned
webm_device_count(struct Driver* driver)
{
    return 1;
}

static enum DeviceStatusCode
webm_device_describe(const struct Driver* driver,
                     struct DeviceIdentifier* identifier,
                     uint64_t i)
{
    *identifier = (struct DeviceIdentifier){
        .name = "webm",
        .kind = DeviceKind_Storage,
        .device_id = 0,
    };
    return Device_Ok;
Error:
    return Device_Err;
}

// Have to do extern c in webm.cpp when I cross the divide into this file, b/c c++ and c are different
extern struct Storage*
make_webm_storage();

extern enum DeviceStatusCode
close_webm_storage(struct Storage*);

static enum DeviceStatusCode
webm_device_open(struct Driver* driver, uint64_t device_id, struct Device** out)
{
    EXPECT(out, "Invalid parameter. out was NULL.");

    // TODO: Check, don't do anything with the device_id? only one so it doesn't matter?

    struct Storage* ctx = make_webm_storage(); // init the storage device, Change to set?
    EXPECT(ctx, "Failed to make Webm Storage");
    *out = &ctx->device;

    return Device_Ok;
Error:
    return Device_Err;
}

static enum DeviceStatusCode
webm_device_close(struct Driver* driver, struct Device* in)
{
    EXPECT(in, "Invalid parameter. out was NULL.");
    struct Storage* ctx = containerof(in, struct Storage, device);
    return close_webm_storage(ctx);
    Error:
    return Device_Err;
}

static enum DeviceStatusCode
webm_shutdown(struct Driver* driver)
{
    if (driver) {
        free(driver);
    }
    return Device_Ok;
}

acquire_export struct Driver*
acquire_driver_init_v0(void (*reporter)(int is_error,
                                        const char* file,
                                        int line,
                                        const char* function,
                                        const char* msg))
{
    struct WebmDriver* self;
    logger_set_reporter(reporter);
    CHECK(self = (struct WebmDriver*)malloc(sizeof(*self)));
    *self = (struct WebmDriver){
        .driver = { .device_count = webm_device_count,
                    .describe = webm_device_describe,
                    .open = webm_device_open,
                    .close = webm_device_close,
                    .shutdown = webm_shutdown },
    };

    return &self->driver;
Error:
    return 0;
}
