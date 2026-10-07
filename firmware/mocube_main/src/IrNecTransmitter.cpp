#include "IrNecTransmitter.h"

#include "driver/rmt.h"

namespace {

constexpr rmt_channel_t RMT_CHANNEL = RMT_CHANNEL_0;

// RMT clock:
// APB 80 MHz / 80 = 1 MHz
// → 1 tick = 1 us
constexpr uint8_t RMT_CLK_DIV = 80;

constexpr uint8_t CARRIER_DUTY = 33;

} // namespace


IrNecTransmitter::IrNecTransmitter(uint8_t pin)
    : _pin(pin)
{
}


void IrNecTransmitter::begin(uint32_t carrierHz)
{
    _carrierHz = carrierHz;

    rmt_config_t config = {};

    config.rmt_mode = RMT_MODE_TX;
    config.channel = RMT_CHANNEL;
    config.gpio_num = static_cast<gpio_num_t>(_pin);

    config.clk_div = RMT_CLK_DIV;

    config.mem_block_num = 1;

    config.tx_config.loop_en = false;

    config.tx_config.carrier_en = true;
    config.tx_config.carrier_freq_hz = _carrierHz;
    config.tx_config.carrier_duty_percent = CARRIER_DUTY;
    config.tx_config.carrier_level = RMT_CARRIER_LEVEL_HIGH;

    config.tx_config.idle_level = RMT_IDLE_LEVEL_LOW;
    config.tx_config.idle_output_en = true;

    esp_err_t result = rmt_config(&config);

    if (result != ESP_OK) {
        Serial.printf(
            "IR TX: rmt_config failed: %d\n",
            result
        );

        return;
    }

    result = rmt_driver_install(
        RMT_CHANNEL,
        0,
        0
    );

    if (result != ESP_OK) {
        Serial.printf(
            "IR TX: rmt_driver_install failed: %d\n",
            result
        );

        return;
    }

    _ready = true;

    Serial.printf(
        "IR TX ready: GPIO=%u, carrier=%lu Hz\n",
        _pin,
        static_cast<unsigned long>(_carrierHz)
    );
}


bool IrNecTransmitter::send(uint32_t rawNec)
{
    if (!_ready) {
        return false;
    }

    // NEC:
    //
    // Leader:
    //   9000 us mark
    //   4500 us space
    //
    // Data:
    //   562 us mark
    //   562 us space = 0
    //   562 us mark
    //   1687 us space = 1
    //
    // Final:
    //   562 us mark
    //

    rmt_item32_t items[34] = {};

    // --------------------------------------------------------
    // Leader
    // --------------------------------------------------------

    items[0].level0 = 1;
    items[0].duration0 = 9000;

    items[0].level1 = 0;
    items[0].duration1 = 4500;


    // --------------------------------------------------------
    // 32-bit data
    // --------------------------------------------------------

    for (int bit = 0; bit < 32; ++bit) {

        bool value =
            (rawNec >> (31 - bit)) & 0x01;

        items[bit + 1].level0 = 1;
        items[bit + 1].duration0 = 562;

        items[bit + 1].level1 = 0;

        if (value) {
            items[bit + 1].duration1 = 1687;
        }
        else {
            items[bit + 1].duration1 = 562;
        }
    }


    // --------------------------------------------------------
    // Final mark
    // --------------------------------------------------------

    items[33].level0 = 1;
    items[33].duration0 = 562;

    items[33].level1 = 0;
    items[33].duration1 = 0;


    // --------------------------------------------------------
    // Start transmission
    //
    // wait_tx_done = false
    //
    // → RMT hardware continues transmission
    // → CPU immediately returns
    // --------------------------------------------------------

    esp_err_t result = rmt_write_items(
        RMT_CHANNEL,
        items,
        34,
        false
    );

    if (result != ESP_OK) {
        Serial.printf(
            "IR TX: rmt_write_items failed: %d\n",
            result
        );

        return false;
    }

    // Serial.printf(
    //     "IR TX: rmt_write_items successed: 0x%08lX\n",
    //     rawNec
    // );

    return true;
}

bool IrNecTransmitter::send(uint8_t id, uint8_t cmd) {
    uint32_t rawNec = (uint32_t)((~cmd << 24 & 0xFF000000) | (cmd << 16 & 0x00FF0000) | (~id << 8 & 0x0000FF00) | (id & 0x000000FF));
    // Serial.printf("~cmd: 0x%08lX, cmd: 0x%08lX, ~id: 0x%08lX, id: 0x%08lX, rawNec: 0x%08lX", ~cmd, cmd, ~id, id, rawNec);
    return send(rawNec);
}