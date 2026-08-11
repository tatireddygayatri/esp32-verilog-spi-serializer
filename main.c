/**
 * @file main.c
 * @brief High-Performance SPI Master Serializer Controller for ESP32
 * @note Optimized for Wokwi Simulation & Production Hardware
 */

#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_err.h"

static const char *TAG = "SERIALIZER_MASTER";

/* --- HARDWARE INTERFACE CONFIGURATION --- */
#define SPI_HOST_ID          SPI2_HOST  /* Using HSPI/SPI2 Periph on ESP32 */
#define PIN_NUM_MISO         25         /* Serial data returning from chip (optional) */
#define PIN_NUM_MOSI         23         /* Serial Data Input to Verilog Serializer */
#define PIN_NUM_CLK          19         /* Serial Shift Clock (SCLK) */
#define PIN_NUM_CS           22         /* Chip Select (Active Low) */
#define PIN_NUM_LOAD         21         /* Hardware Pulse Pin to commit Parallel Register Data */

/* --- SERIALIZER PROTOCOL SETTINGS --- */
#define SPI_CLOCK_SPEED_HZ   (10 * 1000 * 1000) // 10 MHz Operation
#define SERIALIZER_BIT_WIDTH 32                 // Word size of data serialization

/* --- GLOBAL HANDLES --- */
static spi_device_handle_t spi_device_handle;

/**
 * @brief Initializes Dedicated Control Pins and SPI Peripheral Bus
 */
esp_err_t serializer_hardware_init(void) {
    esp_err_t ret;

    // 1. Configure Hardware Pulse GPIO Line (LOAD)
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << PIN_NUM_LOAD),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_ENABLE, // Stay grounded until explicitly pulsed
        .intr_type = GPIO_INTR_DISABLE
    };
    ret = gpio_config(&io_conf);
    if (ret != ESP_OK) return ret;
    gpio_set_level(PIN_NUM_LOAD, 0);

    // 2. Define SPI Bus Configurations
    spi_bus_config_t bus_cfg = {
        .miso_io_num = PIN_NUM_MISO,
        .mosi_io_num = PIN_NUM_MOSI,
        .sclk_io_num = PIN_NUM_CLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 32
    };

    // 3. Define Specific SPI Interface Parameters
    spi_device_interface_config_t dev_cfg = {
        .clock_speed_hz = SPI_CLOCK_SPEED_HZ,
        .mode = 0,                             // CPOL=0, CPHA=0 (Standard rising edge capture)
        .spics_io_num = PIN_NUM_CS,            // Managed automatically by hardware driver
        .queue_size = 7,                       // Transaction queue capacity
        .flags = SPI_DEVICE_NO_DUMMY,
        .pre_cb = NULL,                        // Optional callbacks omitted for performance
        .post_cb = NULL
    };

    // 4. Initialize Core Bus & Attach Device
    ret = spi_bus_initialize(SPI_HOST_ID, &bus_cfg, SPI_DMA_CH_AUTO);
    if (ret != ESP_OK) return ret;

    ret = spi_bus_add_device(SPI_HOST_ID, &dev_cfg, &spi_device_handle);
    return ret;
}

/**
 * @brief Transmits a multi-bit word out to the serializer register and asserts a fast load pulse.
 * @param data Data payload to serialize.
 */
esp_err_t serializer_send_word(uint32_t data) {
    // Allocate 32-bit DMA compliant transfer block
    DMA_ATTR static uint32_t tx_buffer;
    
    // Ensure correct network byte order alignment (MSB First format over SPI)
    tx_buffer = __builtin_bswap32(data); 

    spi_transaction_t trans = {
        .flags = 0,
        .length = SERIALIZER_BIT_WIDTH, 
        .rxlength = 0,
        .tx_buffer = &tx_buffer,
        .rx_buffer = NULL
    };

    // Synchronous Polling Transaction for sub-microsecond driver overhead 
    esp_err_t ret = spi_device_polling_transmit(spi_device_handle, &trans);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "SPI transmission failed!");
        return ret;
    }

    // Generate physical hardware latch pulse for Verilog parallel layout update
    gpio_set_level(PIN_NUM_LOAD, 1);
    esp_rom_delay_us(1); 
    gpio_set_level(PIN_NUM_LOAD, 0);

    return ESP_OK;
}

void app_main(void) {
    ESP_LOGI(TAG, "Initializing Master Controller Architecture...");
    
    if (serializer_hardware_init() != ESP_OK) {
        ESP_LOGE(TAG, "Hardware Peripherals Setup Error!");
        return;
    }
    
    ESP_LOGI(TAG, "Initialization Successful. Entering Active Serialization Loop.");

    uint32_t sample_frame = 0xABCDE123;

    while (1) {
        ESP_LOGI(TAG, "Transmitting Frame Update: 0x%08X", (unsigned int)sample_frame);
        
        if (serializer_send_word(sample_frame) == ESP_OK) {
            // Modify payload incrementally to observe serialization dynamically 
            sample_frame++; 
        }

        // Rest cycle execution limits (1Hz update cycle for cleaner emulation tracking)
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
