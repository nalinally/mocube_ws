#include "NeighborDetector.h"

#include "Config.h"

NeighborDetector::NeighborDetector(PcLink& link)
    : _link(link),
      _receiver(config::IR_RX_PINS),
      _transmitter(config::IR_TX_PIN)
{
}

void NeighborDetector::begin()
{
    _receiver.begin(
        [this](uint8_t channel, const IrNecReceiver::Message& message) {
            onMessage(channel, message);
        });
    _transmitter.begin(config::IR_CARRIER_HZ);

    Serial.println("IR receiver/transmitter ready");
}

void NeighborDetector::updateReceiver()
{
    _receiver.update();
}

void NeighborDetector::updateBeacon(unsigned long now)
{
    if (now - _lastBeacon < config::IR_BEACON_INTERVAL_MS) {
        return;
    }

    _lastBeacon = now;
    _transmitter.send(
        static_cast<uint8_t>(_link.id()),
        config::IR_BEACON_COMMAND);
}

void NeighborDetector::onMessage(
    uint8_t channel,
    const IrNecReceiver::Message& message)
{
    Serial.printf(
        "IR RX%d: ADDR=0x%02X CMD=0x%02X DATA=0x%08lX\n",
        channel + 1,
        message.address,
        message.command,
        static_cast<unsigned long>(message.raw)
    );

    if (message.command == config::IR_BEACON_COMMAND) {
        _link.broadcast(
            protocol::makeNeighbor(_link.mac(), channel, message.address));
    }
}

void NeighborDetector::handleCommand(const protocol::Packet& packet)
{
    if (packet.at(1) != "send" || packet.count < 3) {
        return;
    }

    const uint32_t raw =
        strtoul(packet.at(2).c_str(), nullptr, 0);

    if (_transmitter.send(raw)) {
        Serial.printf(
            "IR TX: 0x%08lX\n",
            static_cast<unsigned long>(raw)
        );
    } else {
        Serial.println("IR TX: failed");
    }
}
