#include "IrNecTransmitter.h"

#include "driver/rmt.h"

#include "NecFormat.h"

namespace {

constexpr rmt_channel_t RMT_CHANNEL = RMT_CHANNEL_0;

// RMT clock:
// APB 80 MHz / 80 = 1 MHz
// → 1 tick = 1 us
constexpr uint8_t RMT_CLK_DIV = 80;

constexpr uint8_t CARRIER_DUTY = 33;

// leader 1 + data 32 + final mark 1
constexpr size_t FRAME_ITEMS = 1 + nec::FRAME_BITS + 1;

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

    rmt_item32_t items[FRAME_ITEMS] = {};

    // --------------------------------------------------------
    // Leader
    // --------------------------------------------------------

    items[0].level0 = 1;
    items[0].duration0 = nec::LEADER_MARK_US;

    items[0].level1 = 0;
    items[0].duration1 = nec::LEADER_SPACE_US;


    // --------------------------------------------------------
    // 32-bit data
    // --------------------------------------------------------

    // LSB first (NecFormat.h 参照)
    for (int bit = 0; bit < nec::FRAME_BITS; ++bit) {

        bool value =
            (rawNec >> bit) & 0x01;

        items[bit + 1].level0 = 1;
        items[bit + 1].duration0 = nec::BIT_MARK_US;

        items[bit + 1].level1 = 0;

        if (value) {
            items[bit + 1].duration1 = nec::ONE_SPACE_US;
        }
        else {
            items[bit + 1].duration1 = nec::ZERO_SPACE_US;
        }
    }


    // --------------------------------------------------------
    // Final mark
    // --------------------------------------------------------

    items[FRAME_ITEMS - 1].level0 = 1;
    items[FRAME_ITEMS - 1].duration0 = nec::BIT_MARK_US;

    items[FRAME_ITEMS - 1].level1 = 0;
    items[FRAME_ITEMS - 1].duration1 = 0;


    // --------------------------------------------------------
    // Start transmission
    //
    // wait_tx_done = false
    //
    // → RMT hardware continues transmission
    // → CPU immediately returns
    //
    // 1フレームは約 50〜90ms (leader 13.5ms + 32bit)。前の送信中に
    // 呼ぶと rmt_write_items は前回の完了を待ってから書き込む。
    // --------------------------------------------------------

    esp_err_t result = rmt_write_items(
        RMT_CHANNEL,
        items,
        FRAME_ITEMS,
        false
    );

    if (result != ESP_OK) {
        Serial.printf(
            "IR TX: rmt_write_items failed: %d\n",
            result
        );

        return false;
    }

    return true;
}

bool IrNecTransmitter::send(uint8_t address, uint8_t command)
{
    return send(nec::encode(address, command));
}
