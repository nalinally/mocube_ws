#include "Protocol.h"

namespace protocol {

const String& Packet::at(int index) const
{
    static const String EMPTY;

    if (index < 0 || index >= count) {
        return EMPTY;
    }
    return fields[index];
}

void parse(const String& text, Packet& out)
{
    for (int i = 0; i < Packet::MAX_FIELDS; ++i) {
        out.fields[i] = "";
    }

    int index = 0;
    const int length = text.length();

    for (int i = 0; i < length; ++i) {
        const char c = text.charAt(i);

        if (c == ',') {
            ++index;
        } else if (index < Packet::MAX_FIELDS) {
            out.fields[index] += c;
        }
    }

    out.count = min(index + 1, Packet::MAX_FIELDS);
}

String makeRegister(const String& mac)
{
    return String(command::REGISTER) + "," + mac;
}

String makeHeartbeat(const String& mac)
{
    return String(command::HEARTBEAT) + "," + mac;
}

String makeNeighbor(const String& mac, uint8_t channel, uint8_t address)
{
    return String(command::NEIGHBOR) + "," + mac + "," +
           String(channel) + "," + String(address);
}

}  // namespace protocol
