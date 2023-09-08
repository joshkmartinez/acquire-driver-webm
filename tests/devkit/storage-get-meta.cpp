/// @file
/// @brief Check that all storage devices implement the get_meta function.
/// Also, since none of the basic storage devices support chunking or
/// multiscale, check that this is reflected in the metadata.

#include "platform.h"
#include "logger.h"
#include "device/kit/driver.h"
#include "device/hal/driver.h"
#include "device/hal/storage.h"
#include "device/props/storage.h"

#include <cstdio>

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

typedef struct Driver* (*init_func_t)(void (*reporter)(int is_error,
                                                       const char* file,
                                                       int line,
                                                       const char* function,
                                                       const char* msg));

int
main()
{
    logger_set_reporter(reporter);
    lib lib{};
    CHECK(lib_open_by_name(&lib, "acquire-driver-webm"));
    {
        auto init = (init_func_t)lib_load(&lib, "acquire_driver_init_v0");
        auto driver = init(reporter);
        CHECK(driver);
        const auto n = driver->device_count(driver);
        for (uint32_t i = 0; i < n; ++i) {
            DeviceIdentifier id{};
            CHECK(driver->describe(driver, &id, i) == Device_Ok);

            if (id.kind == DeviceKind_Storage) {
                struct Device* device = nullptr;
                struct Storage* storage = nullptr;
                struct StoragePropertyMetadata metadata = { 0 };

                CHECK(Device_Ok == driver_open_device(driver, i, &device));
                storage = containerof(device, struct Storage, device);

                CHECK(Device_Ok == storage_get_meta(storage, &metadata));
                CHECK(0 == metadata.chunking.supported);
                CHECK(0 == metadata.multiscale.supported);

                CHECK(Device_Ok == driver_close_device(device));
            }
        }
    }
    lib_close(&lib);
    return 0;
Error:
    lib_close(&lib);
    return 1;
}
