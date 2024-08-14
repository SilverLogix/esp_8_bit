#include "esp_system.h"
#include "esp_int_wdt.h"
#include "esp_spiffs.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"
#include "soc/efuse_reg.h"

#include "config.h"
#include "src/emu.h"
#include "src/video_out.h"

// Project: esp_8_bit
// Description: NES emulator for ESP32 with TFT display and Bluetooth support
// Configuration: Edit src/config.h for emulator setup
// Filesystem: Requires folders named for each emulator (e.g., 'nofrendo'). 
//             Use 'ESP32 Sketch Data Upload' to copy prepared data to ESP32

// Globals
Emu* _emu = nullptr;       // Emulator instance running on core 0
uint32_t _frame_time = 0;
uint32_t _drawn = 1;
bool _inited = false;

// Function Declarations
void emu_init();
void emu_loop();
void emu_task(void* arg);
esp_err_t mount_filesystem();
void setup();
void perf();
void loop();

// Initialize the emulator
void emu_init()
{
    std::string folder = "/" + _emu->name;
    gui_start(_emu, folder.c_str());
    _drawn = _frame_counter;
}

// Main emulator loop
void emu_loop()
{
    video_sync(); // Wait for blanking before drawing to avoid tearing

    uint32_t start_time = xthal_get_ccount();
    gui_update();
    _frame_time = xthal_get_ccount() - start_time;
    _lines = _emu->video_buffer();
    _drawn++;
}

// Task running the emulator on the communication core
void emu_task(void* arg)
{
    printf("emu_task %s running on core %d at %dHz\n", _emu->name.c_str(), xPortGetCoreID(), rtc_clk_cpu_freq_value(rtc_clk_cpu_freq_get()));
    emu_init();
    for (;;) {
        emu_loop();
    }
}

// Mount the filesystem
esp_err_t mount_filesystem()
{
#ifdef USE_SD_CARD
    // Use SD card for file storage, formatted as FAT with 8.3 filenames
    vTaskDelay(300 / portTICK_RATE_MS); // Delay to allow SD card to power up

    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    sdspi_slot_config_t slot_config = SDSPI_SLOT_CONFIG_DEFAULT();
    slot_config.gpio_miso = (gpio_num_t)CONFIG_SD_MISO;
    slot_config.gpio_mosi = (gpio_num_t)CONFIG_SD_MOSI;
    slot_config.gpio_sck = (gpio_num_t)CONFIG_SD_SCK;
    slot_config.gpio_cs = (gpio_num_t)CONFIG_SD_CS;
    slot_config.dma_channel = 2;

    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files = 2
    };

    sdmmc_card_t *card;
    esp_err_t err = esp_vfs_fat_sdmmc_mount("", &host, &slot_config, &mount_config, &card);

    if (err != ESP_OK) {
        printf("Failed to mount SD card\n");
    } else {
        sdmmc_card_print_info(stdout, card); // Print card properties
    }

#else
    // Use ESP SPIFFS
    printf("\n\n\nesp_8_bit\n\nMounting SPIFFS (may take ~15 seconds if formatting for the first time)...\n");
    uint32_t start_time = millis();
    esp_vfs_spiffs_conf_t conf = {
        .base_path = "",
        .partition_label = NULL,
        .max_files = 5,
        .format_if_mount_failed = true // Force format if mounting fails
    };
    esp_err_t err = esp_vfs_spiffs_register(&conf);
    if (err != ESP_OK) {
        printf("Failed to mount or format filesystem: %d. Use 'ESP32 Sketch Data Upload' from 'Tools' menu\n", err);
    }
    printf("... mounted in %d ms\n", millis() - start_time);
#endif

    return err;
}

// Setup function
void setup()
{
    // Check silicon version for APLL issue
    int silicon_version = (REG_READ(EFUSE_BLK0_RDATA3_REG) >> 15) & 1;
    if (silicon_version == 0) {
        printf("Warning: This revision of the chip has an issue with the APLL and will not work properly!\n");
    }
    
    rtc_clk_cpu_freq_set(RTC_CPU_FREQ_240M); // Set CPU frequency
    mount_filesystem();                      // Mount the filesystem
    _emu = NewNofrendo();                   // Create the emulator
    hid_init("emu32");                      // Initialize Bluetooth HID on core 1

#ifdef SINGLE_CORE
    emu_init();
    video_init(_emu->cc_width, _emu->composite_palette()); // Initialize video
#else
    xTaskCreatePinnedToCore(emu_task, "emu_task", 5 * 1024, NULL, 0, NULL, 0); // Start emulator task on core 0
#endif
}

// Performance measurement
void perf()
#ifdef PERF
{
    static int next_draw = 0;
    if (_drawn < next_draw) return;

    float elapsed_us = 120 * 1000000 / 60;
    next_draw = _drawn + 120;

    printf("frame_time:%d drawn:%d displayed:%d blit_ticks:%d->%d, isr time:%2.2f%%\n",
        _frame_time / 240, _drawn, _frame_counter, _blit_ticks_min, _blit_ticks_max, (_isr_us * 100) / elapsed_us);

    _blit_ticks_min = UINT32_MAX;
    _blit_ticks_max = 0;
    _isr_us = 0;
}
#else
{};
#endif

// Main loop
void loop()
{
#ifdef SINGLE_CORE
    emu_loop();
#else
    // Initialize video after emulator has started
    if (!_inited && _lines) {
        printf("video_init\n");
        video_init(_emu->cc_width, _emu->composite_palette()); // Start video processing
        _inited = true;
    } else {
        vTaskDelay(1 / portTICK_RATE_MS); // Use vTaskDelay while waiting for initialization
    }
#endif

    hid_update(); // Update Bluetooth EDR/HID stack
    perf();       // Dump performance stats
}
